// SPDX-License-Identifier: GPL-2.0-only
/*
 * NAN mode implementation
 * Copyright(c) 2025-2026 Intel Corporation
 */
#include <net/mac80211.h>

#include "ieee80211_i.h"
#include "driver-ops.h"
#include "sta_info.h"

static void
ieee80211_nan_init_channel(struct ieee80211_nan_channel *nan_channel,
			   struct cfg80211_nan_channel *cfg_nan_channel)
{
	memset(nan_channel, 0, sizeof(*nan_channel));

	nan_channel->chanreq.oper = cfg_nan_channel->chandef;
	memcpy(nan_channel->channel_entry, cfg_nan_channel->channel_entry,
	       sizeof(nan_channel->channel_entry));
	nan_channel->needed_rx_chains = cfg_nan_channel->rx_nss;
}

static void
ieee80211_nan_update_channel(struct ieee80211_local *local,
			     struct ieee80211_nan_channel *nan_channel,
			     struct cfg80211_nan_channel *cfg_nan_channel)
{
	struct ieee80211_chanctx_conf *conf;

	if (WARN_ON(!cfg80211_chandef_identical(&nan_channel->chanreq.oper,
						&cfg_nan_channel->chandef)))
		return;

	if (WARN_ON(memcmp(nan_channel->channel_entry,
			   cfg_nan_channel->channel_entry,
			   sizeof(nan_channel->channel_entry))))
		return;

	if (nan_channel->needed_rx_chains == cfg_nan_channel->rx_nss)
		return;

	nan_channel->needed_rx_chains = cfg_nan_channel->rx_nss;

	conf = nan_channel->chanctx_conf;
	if (!conf)
		return;

	ieee80211_recalc_smps_chanctx(local, container_of(conf,
							  struct ieee80211_chanctx,
							  conf));
}

static int
ieee80211_nan_use_chanctx(struct ieee80211_sub_if_data *sdata,
			  struct ieee80211_nan_channel *nan_channel,
			  bool assign_on_failure)
{
	struct ieee80211_chanctx *ctx;
	bool reused_ctx;

	if (!nan_channel->chanreq.oper.chan)
		return -EINVAL;

	if (ieee80211_check_combinations(sdata, &nan_channel->chanreq.oper,
					 IEEE80211_CHANCTX_SHARED, 0, -1))
		return -EBUSY;

	ctx = ieee80211_find_or_create_chanctx(sdata, &nan_channel->chanreq,
					       IEEE80211_CHANCTX_SHARED,
					       assign_on_failure,
					       &reused_ctx);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	nan_channel->chanctx_conf = &ctx->conf;

	/*
	 * In case an existing channel context is being used, we marked it as
	 * will_be_used, now that it is assigned - clear this indication
	 */
	if (reused_ctx) {
		WARN_ON(!ctx->will_be_used);
		ctx->will_be_used = false;
	}
	ieee80211_recalc_chanctx_min_def(sdata->local, ctx);
	ieee80211_recalc_smps_chanctx(sdata->local, ctx);

	return 0;
}

static void
ieee80211_nan_update_peer_channels(struct ieee80211_sub_if_data *sdata,
				   struct ieee80211_chanctx_conf *removed_conf)
{
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;

	lockdep_assert_wiphy(local->hw.wiphy);

	list_for_each_entry(sta, &local->sta_list, list) {
		struct ieee80211_nan_peer_sched *peer_sched;
		int write_idx = 0;
		bool updated = false;

		if (sta->sdata != sdata)
			continue;

		peer_sched = sta->sta.nan_sched;
		if (!peer_sched)
			continue;

		/* NULL out map slots for channels being removed */
		for (int i = 0; i < peer_sched->n_channels; i++) {
			if (peer_sched->channels[i].chanctx_conf != removed_conf)
				continue;

			for (int m = 0; m < CFG80211_NAN_MAX_PEER_MAPS; m++) {
				struct ieee80211_nan_peer_map *map =
					&peer_sched->maps[m];

				if (map->map_id == CFG80211_NAN_INVALID_MAP_ID)
					continue;

				for (int s = 0; s < ARRAY_SIZE(map->slots); s++)
					if (map->slots[s] == &peer_sched->channels[i])
						map->slots[s] = NULL;
			}
		}

		/* Compact channels array, removing those with removed_conf */
		for (int i = 0; i < peer_sched->n_channels; i++) {
			if (peer_sched->channels[i].chanctx_conf == removed_conf) {
				updated = true;
				continue;
			}

			if (write_idx != i) {
				/* Update map pointers before moving */
				for (int m = 0; m < CFG80211_NAN_MAX_PEER_MAPS; m++) {
					struct ieee80211_nan_peer_map *map =
						&peer_sched->maps[m];

					if (map->map_id == CFG80211_NAN_INVALID_MAP_ID)
						continue;

					for (int s = 0; s < ARRAY_SIZE(map->slots); s++)
						if (map->slots[s] == &peer_sched->channels[i])
							map->slots[s] = &peer_sched->channels[write_idx];
				}

				peer_sched->channels[write_idx] = peer_sched->channels[i];
			}
			write_idx++;
		}

		/* Clear any remaining entries at the end */
		for (int i = write_idx; i < peer_sched->n_channels; i++)
			memset(&peer_sched->channels[i], 0, sizeof(peer_sched->channels[i]));

		peer_sched->n_channels = write_idx;

		if (updated)
			drv_nan_peer_sched_changed(local, sdata, sta);
	}
}

static void
ieee80211_nan_remove_channel(struct ieee80211_sub_if_data *sdata,
			     struct ieee80211_nan_channel *nan_channel)
{
	struct ieee80211_chanctx_conf *conf;
	struct ieee80211_chanctx *ctx;

	if (WARN_ON(!nan_channel))
		return;

	lockdep_assert_wiphy(sdata->local->hw.wiphy);

	if (!nan_channel->chanreq.oper.chan)
		return;

	for (int slot = 0; slot < ARRAY_SIZE(sdata->vif.cfg.nan_schedule); slot++)
		if (sdata->vif.cfg.nan_schedule[slot] == nan_channel)
			sdata->vif.cfg.nan_schedule[slot] = NULL;

	conf = nan_channel->chanctx_conf;

	/* If any peer nan schedule uses this chanctx, update them */
	if (conf)
		ieee80211_nan_update_peer_channels(sdata, conf);

	memset(nan_channel, 0, sizeof(*nan_channel));

	/* Update the driver before (possibly) releasing the channel context */
	drv_vif_cfg_changed(sdata->local, sdata, BSS_CHANGED_NAN_LOCAL_SCHED);

	/* Channel might not have a chanctx if it was ULWed */
	if (!conf)
		return;

	ctx = container_of(conf, struct ieee80211_chanctx, conf);

	if (ieee80211_chanctx_num_assigned(sdata->local, ctx) > 0) {
		ieee80211_recalc_chanctx_chantype(sdata->local, ctx);
		ieee80211_recalc_smps_chanctx(sdata->local, ctx);
		ieee80211_recalc_chanctx_min_def(sdata->local, ctx);
	}

	if (ieee80211_chanctx_refcount(sdata->local, ctx) == 0)
		ieee80211_free_chanctx(sdata->local, ctx, false);
}

static void
ieee80211_nan_update_all_ndi_carriers(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata;

	lockdep_assert_wiphy(local->hw.wiphy);

	/* Iterate all interfaces and update carrier for NDI interfaces */
	list_for_each_entry(sdata, &local->interfaces, list) {
		if (!ieee80211_sdata_running(sdata) ||
		    sdata->vif.type != NL80211_IFTYPE_NAN_DATA)
			continue;

		ieee80211_nan_update_ndi_carrier(sdata);
	}
}

static struct ieee80211_nan_channel *
ieee80211_nan_find_free_channel(struct ieee80211_vif_cfg *vif_cfg)
{
	for (int i = 0; i < ARRAY_SIZE(vif_cfg->nan_channels); i++) {
		if (!vif_cfg->nan_channels[i].chanreq.oper.chan)
			return &vif_cfg->nan_channels[i];
	}

	return NULL;
}

int ieee80211_nan_set_local_sched(struct ieee80211_sub_if_data *sdata,
				  struct cfg80211_nan_local_sched *sched)
{
	struct ieee80211_nan_channel backup_channels[IEEE80211_NAN_MAX_CHANNELS];
	struct ieee80211_nan_channel *backup_schedule[CFG80211_NAN_SCHED_NUM_TIME_SLOTS];
	struct ieee80211_nan_channel *sched_idx_to_chan[IEEE80211_NAN_MAX_CHANNELS] = {};
	DECLARE_BITMAP(removed_channels, IEEE80211_NAN_MAX_CHANNELS) = {};
	struct ieee80211_vif_cfg *vif_cfg = &sdata->vif.cfg;
	int ret;

	if (sched->n_channels > IEEE80211_NAN_MAX_CHANNELS)
		return -EOPNOTSUPP;

	memcpy(backup_schedule, vif_cfg->nan_schedule, sizeof(backup_schedule));
	memcpy(backup_channels, vif_cfg->nan_channels, sizeof(backup_channels));

	/*
	 * Remove channels that are no longer in the new schedule to free up
	 * resources before adding new channels.
	 * Create a mapping from sched index to vif_cfg channel
	 */
	for (int i = 0; i < ARRAY_SIZE(vif_cfg->nan_channels); i++) {
		bool still_needed = false;

		if (!vif_cfg->nan_channels[i].chanreq.oper.chan)
			continue;

		for (int j = 0; j < sched->n_channels; j++) {
			if (cfg80211_chandef_identical(&vif_cfg->nan_channels[i].chanreq.oper,
						       &sched->nan_channels[j].chandef)) {
				sched_idx_to_chan[j] =
					&vif_cfg->nan_channels[i];
				still_needed = true;
				break;
			}
		}

		if (!still_needed) {
			__set_bit(i, removed_channels);
			ieee80211_nan_remove_channel(sdata, &vif_cfg->nan_channels[i]);
		}
	}

	for (int i = 0; i < sched->n_channels; i++) {
		struct ieee80211_nan_channel *chan = sched_idx_to_chan[i];

		if (chan) {
			ieee80211_nan_update_channel(sdata->local, chan,
						     &sched->nan_channels[i]);
		} else {
			chan = ieee80211_nan_find_free_channel(vif_cfg);
			if (WARN_ON(!chan)) {
				ret = -EINVAL;
				goto err;
			}

			sched_idx_to_chan[i] = chan;
			ieee80211_nan_init_channel(chan,
						   &sched->nan_channels[i]);

			ret = ieee80211_nan_use_chanctx(sdata, chan, false);
			if (ret) {
				memset(chan, 0, sizeof(*chan));
				goto err;
			}
		}
	}

	for (int s = 0; s < ARRAY_SIZE(vif_cfg->nan_schedule); s++) {
		if (sched->schedule[s] < ARRAY_SIZE(sched_idx_to_chan))
			vif_cfg->nan_schedule[s] =
				sched_idx_to_chan[sched->schedule[s]];
		else
			vif_cfg->nan_schedule[s] = NULL;
	}

	drv_vif_cfg_changed(sdata->local, sdata, BSS_CHANGED_NAN_LOCAL_SCHED);

	ieee80211_nan_update_all_ndi_carriers(sdata->local);

	return 0;
err:
	/* Remove newly added channels */
	for (int i = 0; i < ARRAY_SIZE(vif_cfg->nan_channels); i++) {
		struct cfg80211_chan_def *chan_def = &vif_cfg->nan_channels[i].chanreq.oper;

		if (!chan_def->chan)
			continue;

		if (!cfg80211_chandef_identical(&backup_channels[i].chanreq.oper,
						chan_def))
			ieee80211_nan_remove_channel(sdata,
						     &vif_cfg->nan_channels[i]);
	}

	/* Re-add all backed up channels */
	for (int i = 0; i < ARRAY_SIZE(backup_channels); i++) {
		struct ieee80211_nan_channel *chan = &vif_cfg->nan_channels[i];

		*chan = backup_channels[i];

		if (!chan->chanctx_conf)
			continue;

		if (test_bit(i, removed_channels)) {
			/* Clear the stale chanctx pointer */
			chan->chanctx_conf = NULL;
			/*
			 * We removed the newly added channels so we don't lack
			 * resources. So the only reason that this would fail
			 * is a FW error which we ignore. Therefore, this
			 * should never fail.
			 */
			WARN_ON(ieee80211_nan_use_chanctx(sdata, chan, true));
		} else {
			struct ieee80211_chanctx_conf *conf = chan->chanctx_conf;

			/* FIXME: detect no-op? */
			/* Channel was not removed but may have been updated */
			ieee80211_recalc_smps_chanctx(sdata->local,
						     container_of(conf,
								  struct ieee80211_chanctx,
								  conf));
		}
	}

	memcpy(vif_cfg->nan_schedule, backup_schedule, sizeof(backup_schedule));

	drv_vif_cfg_changed(sdata->local, sdata, BSS_CHANGED_NAN_LOCAL_SCHED);
	ieee80211_nan_update_all_ndi_carriers(sdata->local);
	return ret;
}

void ieee80211_nan_free_peer_sched(struct ieee80211_nan_peer_sched *sched)
{
	if (!sched)
		return;

	kfree(sched->init_ulw);
	kfree(sched);
}

static int
ieee80211_nan_init_peer_channel(struct ieee80211_sub_if_data *sdata,
				const struct sta_info *sta,
				const struct cfg80211_nan_channel *cfg_chan,
				struct ieee80211_nan_channel *new_chan)
{
	/* Find compatible local channel */
	for (int j = 0; j < ARRAY_SIZE(sdata->vif.cfg.nan_channels); j++) {
		struct ieee80211_nan_channel *local_chan = &sdata->vif.cfg.nan_channels[j];
		const struct cfg80211_chan_def *compat;

		if (!local_chan->chanreq.oper.chan)
			continue;

		compat = cfg80211_chandef_compatible(&local_chan->chanreq.oper,
						     &cfg_chan->chandef);
		if (!compat)
			continue;

		/* compat is the wider chandef, and we want the narrower one */
		new_chan->chanreq.oper = compat == &local_chan->chanreq.oper ?
					 cfg_chan->chandef : local_chan->chanreq.oper;
		new_chan->needed_rx_chains = min(local_chan->needed_rx_chains,
						 cfg_chan->rx_nss);
		new_chan->chanctx_conf = local_chan->chanctx_conf;

		break;
	}

	/*
	 * nl80211 already validated that each peer channel is compatible
	 * with at least one local channel, so this should never happen.
	 */
	if (WARN_ON(!new_chan->chanreq.oper.chan))
		return -EINVAL;

	memcpy(new_chan->channel_entry, cfg_chan->channel_entry,
	       sizeof(new_chan->channel_entry));

	return 0;
}

static void
ieee80211_nan_init_peer_map(struct ieee80211_nan_peer_sched *peer_sched,
			    const struct cfg80211_nan_peer_map *cfg_map,
			    struct ieee80211_nan_peer_map *new_map)
{
	new_map->map_id = cfg_map->map_id;

	if (new_map->map_id == CFG80211_NAN_INVALID_MAP_ID)
		return;

	/* Set up the slots array */
	for (int slot = 0; slot < ARRAY_SIZE(new_map->slots); slot++) {
		u8 chan_idx = cfg_map->schedule[slot];

		if (chan_idx < peer_sched->n_channels)
			new_map->slots[slot] = &peer_sched->channels[chan_idx];
	}
}

/*
 * Check if the local schedule and a peer schedule have at least one common
 * slot - a slot where both schedules are active on compatible channels.
 */
static bool
ieee80211_nan_has_common_slots(struct ieee80211_sub_if_data *sdata,
			       struct ieee80211_nan_peer_sched *peer_sched)
{
	for (int slot = 0; slot < CFG80211_NAN_SCHED_NUM_TIME_SLOTS; slot++) {
		struct ieee80211_nan_channel *local_chan =
			sdata->vif.cfg.nan_schedule[slot];

		if (!local_chan || !local_chan->chanctx_conf)
			continue;

		/* Check all peer maps for this slot */
		for (int m = 0; m < CFG80211_NAN_MAX_PEER_MAPS; m++) {
			struct ieee80211_nan_peer_map *map = &peer_sched->maps[m];
			struct ieee80211_nan_channel *peer_chan;

			if (map->map_id == CFG80211_NAN_INVALID_MAP_ID)
				continue;

			peer_chan = map->slots[slot];
			if (!peer_chan)
				continue;

			if (local_chan->chanctx_conf == peer_chan->chanctx_conf)
				return true;
		}
	}

	return false;
}

void ieee80211_nan_update_ndi_carrier(struct ieee80211_sub_if_data *ndi_sdata)
{
	struct ieee80211_local *local = ndi_sdata->local;
	struct ieee80211_sub_if_data *nmi_sdata;
	struct sta_info *sta;

	lockdep_assert_wiphy(local->hw.wiphy);

	if (WARN_ON(ndi_sdata->vif.type != NL80211_IFTYPE_NAN_DATA ||
		    !ndi_sdata->dev) || !ieee80211_sdata_running(ndi_sdata))
		return;

	nmi_sdata = wiphy_dereference(local->hw.wiphy, ndi_sdata->u.nan_data.nmi);
	if (WARN_ON(!nmi_sdata))
		return;

	list_for_each_entry(sta, &local->sta_list, list) {
		struct ieee80211_sta *nmi_sta;

		if (sta->sdata != ndi_sdata ||
		    !test_sta_flag(sta, WLAN_STA_AUTHORIZED))
			continue;

		nmi_sta = wiphy_dereference(local->hw.wiphy, sta->sta.nmi);
		if (WARN_ON(!nmi_sta) || !nmi_sta->nan_sched)
			continue;

		if (ieee80211_nan_has_common_slots(nmi_sdata, nmi_sta->nan_sched)) {
			netif_carrier_on(ndi_sdata->dev);
			return;
		}
	}

	netif_carrier_off(ndi_sdata->dev);
}

static void
ieee80211_nan_update_peer_ndis_carrier(struct ieee80211_local *local,
				       struct sta_info *nmi_sta)
{
	struct sta_info *sta;

	lockdep_assert_wiphy(local->hw.wiphy);

	list_for_each_entry(sta, &local->sta_list, list) {
		if (rcu_access_pointer(sta->sta.nmi) == &nmi_sta->sta)
			ieee80211_nan_update_ndi_carrier(sta->sdata);
	}
}

int ieee80211_nan_set_peer_sched(struct ieee80211_sub_if_data *sdata,
				 struct cfg80211_nan_peer_sched *sched)
{
	struct ieee80211_nan_peer_sched *new_sched, *old_sched, *to_free;
	struct sta_info *sta;
	int ret;

	lockdep_assert_wiphy(sdata->local->hw.wiphy);

	if (!sdata->u.nan.started)
		return -EINVAL;

	sta = sta_info_get(sdata, sched->peer_addr);
	if (!sta)
		return -ENOENT;

	new_sched = kzalloc(struct_size(new_sched, channels, sched->n_channels),
			    GFP_KERNEL);
	if (!new_sched)
		return -ENOMEM;

	to_free = new_sched;

	new_sched->seq_id = sched->seq_id;
	new_sched->committed_dw = sched->committed_dw;
	new_sched->max_chan_switch = sched->max_chan_switch;
	new_sched->n_channels = sched->n_channels;

	if (sched->ulw_size && sched->init_ulw) {
		new_sched->init_ulw = kmemdup(sched->init_ulw, sched->ulw_size,
					      GFP_KERNEL);
		if (!new_sched->init_ulw) {
			ret = -ENOMEM;
			goto out;
		}
		new_sched->ulw_size = sched->ulw_size;
	}

	for (int i = 0; i < sched->n_channels; i++) {
		ret = ieee80211_nan_init_peer_channel(sdata, sta,
						      &sched->nan_channels[i],
						      &new_sched->channels[i]);
		if (ret)
			goto out;
	}

	for (int m = 0; m < ARRAY_SIZE(sched->maps); m++)
		ieee80211_nan_init_peer_map(new_sched, &sched->maps[m],
					    &new_sched->maps[m]);

	/* Install the new schedule before calling the driver */
	old_sched = sta->sta.nan_sched;
	sta->sta.nan_sched = new_sched;

	ret = drv_nan_peer_sched_changed(sdata->local, sdata, sta);
	if (ret) {
		/* Revert to old schedule */
		sta->sta.nan_sched = old_sched;
		goto out;
	}

	ieee80211_nan_update_peer_ndis_carrier(sdata->local, sta);

	/* Success - free old schedule */
	to_free = old_sched;
	ret = 0;

out:
	ieee80211_nan_free_peer_sched(to_free);
	return ret;
}
