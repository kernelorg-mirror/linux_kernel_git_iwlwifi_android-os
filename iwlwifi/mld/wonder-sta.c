// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2026 Intel Corporation
 */

#include <linux/etherdevice.h>

#include "fw/api/mac-cfg.h"
#include "hcmd.h"
#include "mld.h"
#include "sta.h"
#include "tx.h"
#include "wonder.h"
#include "wonder-sta.h"

IWL_MLD_ALLOC_FN_STATIC(link_sta, link_sta)

static void iwl_mld_wonder_rm_sta_from_fw(struct iwl_mld *mld, u8 fw_sta_id)
{
	struct iwl_remove_sta_cmd cmd = {
		.sta_id = cpu_to_le32(fw_sta_id),
	};
	int ret;

	ret = iwl_mld_send_cmd_pdu(mld, WIDE_ID(MAC_CONF_GROUP, STA_REMOVE_CMD),
				   &cmd);
	if (ret)
		IWL_ERR(mld, "Failed to remove station. Id=%d\n", fw_sta_id);
}

static int iwl_mld_wonder_send_sta_cmd(struct iwl_mld *mld,
				       struct iwl_sta_cfg_cmd *cmd)
{
	int cmd_id = WIDE_ID(MAC_CONF_GROUP, STA_CONFIG_CMD);
	int cmd_ver = iwl_fw_lookup_cmd_ver(mld->fw, cmd_id, 0);

	if (WARN_ON(cmd_ver < 3))
		return -EINVAL;

	return iwl_mld_send_cmd_pdu(mld, cmd_id, cmd, sizeof(*cmd));
}

static int
iwl_mld_wonder_set_sta_to_fw(struct iwl_mld *mld, u32 sta_id,
			     enum iwl_fw_sta_type sta_type,
			     u32 link_mask, const u8 *addr)
{
	struct iwl_sta_cfg_cmd cmd = {
		.sta_id = cpu_to_le32(sta_id),
		.link_mask = cpu_to_le32(link_mask),
		.station_type = cpu_to_le32(sta_type),
	};

	memcpy(cmd.peer_mld_address, addr, ETH_ALEN);
	memcpy(cmd.peer_link_address, addr, ETH_ALEN);

	return iwl_mld_wonder_send_sta_cmd(mld, &cmd);
}

static int iwl_mld_wonder_alloc_txq(struct iwl_mld *mld,
				    struct iwl_mld_int_sta *internal_sta,
				    u8 tid)
{
	u32 sta_mask = BIT(internal_sta->sta_id);
	int queue, size;

	size = max_t(u32, IWL_MGMT_QUEUE_SIZE,
		     mld->trans->mac_cfg->base->min_txq_size);

	queue = iwl_trans_txq_alloc(mld->trans, 0, sta_mask, tid, size,
				    IWL_WATCHDOG_DISABLED);
	if (queue >= 0)
		IWL_DEBUG_TX_QUEUES(mld,
				    "Enabling TXQ #%d for sta mask 0x%x tid %d\n",
				    queue, sta_mask, tid);
	return queue;
}

static int iwl_mld_wonder_add_sta(struct iwl_mld *mld,
				  struct iwl_mld_int_sta *internal_sta,
				  enum iwl_fw_sta_type sta_type,
				  u32 link_mask, const u8 *addr, u8 tid)
{
	int ret, queue_id;

	ret = iwl_mld_allocate_link_sta_fw_id(mld, &internal_sta->sta_id,
					      ERR_PTR(-EINVAL));
	if (ret)
		return ret;

	internal_sta->sta_type = sta_type;

	ret = iwl_mld_wonder_set_sta_to_fw(mld, internal_sta->sta_id,
					   internal_sta->sta_type, link_mask,
					   addr);
	if (ret)
		goto err;

	queue_id = iwl_mld_wonder_alloc_txq(mld, internal_sta, tid);
	if (queue_id < 0) {
		iwl_mld_wonder_rm_sta_from_fw(mld, internal_sta->sta_id);
		ret = queue_id;
		goto err;
	}

	internal_sta->queue_id = queue_id;

	return 0;
err:
	iwl_mld_free_internal_sta(mld, internal_sta);
	return ret;
}

static void iwl_mld_wonder_remove_sta(struct iwl_mld *mld,
				      struct iwl_mld_int_sta *internal_sta,
				      u8 tid)
{
	if (WARN_ON_ONCE(internal_sta->sta_id == IWL_INVALID_STA))
		return;

	iwl_mld_flush_link_sta_txqs(mld, internal_sta->sta_id);

	if (!WARN_ON(internal_sta->queue_id == IWL_MLD_INVALID_QUEUE))
		iwl_mld_free_txq(mld, BIT(internal_sta->sta_id), tid,
				 internal_sta->queue_id);

	iwl_mld_wonder_rm_sta_from_fw(mld, internal_sta->sta_id);

	iwl_mld_free_internal_sta(mld, internal_sta);
}

/**
 * iwl_mld_wonder_alloc_bcast_mgmt_sta - add the management station
 * @mld: the MLD instance
 * @wonder_ctx: the wonder context; must have a valid @link_id
 * @params: initialization parameters; @bssid is used as the station address
 *
 * Allocates a STATION_TYPE_BCAST_MGMT station on IWL_MGMT_TID for
 * management frames (beacons, probe responses etc.).
 * Must be called with wiphy lock held.
 *
 * Return: 0 on success, negative error code on failure.
 */
int
iwl_mld_wonder_alloc_bcast_mgmt_sta(struct iwl_mld *mld,
				    struct iwl_mld_wonder_ctx *wonder_ctx,
				    const struct wondertap_init_params *params)
{
	lockdep_assert_wiphy(mld->wiphy);

	if (WARN_ON(wonder_ctx->link_id == IWL_MLD_INVALID_FW_ID))
		return -EINVAL;

	return iwl_mld_wonder_add_sta(mld, &wonder_ctx->bcast_mgmt_sta,
				      STATION_TYPE_BCAST_MGMT,
				      BIT(wonder_ctx->link_id),
				      params->bssid, IWL_MGMT_TID);
}

/**
 * iwl_mld_wonder_alloc_mcast_bcast_data_sta - add the data station
 * @mld: the MLD instance
 * @wonder_ctx: the wonder context; must have a valid @link_id
 * @params: initialization parameters; @bssid is used as the station address
 *
 * Allocates a STATION_TYPE_MCAST station on TID 0 for broadcast and
 * multicast data frames.
 * Must be called after iwl_mld_wonder_alloc_bcast_mgmt_sta().
 * Must be called with wiphy lock held.
 *
 * Return: 0 on success, negative error code on failure.
 */
int
iwl_mld_wonder_alloc_mcast_bcast_data_sta(struct iwl_mld *mld,
					  struct iwl_mld_wonder_ctx *wonder_ctx,
					  const struct wondertap_init_params *params)
{
	lockdep_assert_wiphy(mld->wiphy);

	if (WARN_ON(wonder_ctx->link_id == IWL_MLD_INVALID_FW_ID))
		return -EINVAL;

	return iwl_mld_wonder_add_sta(mld, &wonder_ctx->mcast_bcast_data_sta,
				      STATION_TYPE_MCAST,
				      BIT(wonder_ctx->link_id),
				      params->bssid, 0);
}

/**
 * iwl_mld_wonder_free_mcast_bcast_stas - remove the bcast and mcast stations
 * @mld: the MLD instance
 * @wonder_ctx: the wonder context containing the stations to remove
 *
 * Must be called with wiphy lock held.
 */
void
iwl_mld_wonder_free_mcast_bcast_stas(struct iwl_mld *mld,
				     struct iwl_mld_wonder_ctx *wonder_ctx)
{
	lockdep_assert_wiphy(mld->wiphy);

	if (wonder_ctx->mcast_bcast_data_sta.sta_id != IWL_INVALID_STA)
		iwl_mld_wonder_remove_sta(mld,
					  &wonder_ctx->mcast_bcast_data_sta,
					  0);

	if (wonder_ctx->bcast_mgmt_sta.sta_id != IWL_INVALID_STA)
		iwl_mld_wonder_remove_sta(mld, &wonder_ctx->bcast_mgmt_sta,
					  IWL_MGMT_TID);
}

/*
 * Find the sta slot matching @addr, or NULL. Called without the wiphy lock
 * from the TX fast path.
 *
 * TODO: evaluate whether this can race with a concurrent sta add/remove.
 */
struct iwl_mld_wonder_sta *
iwl_mld_wonder_find_sta(struct iwl_mld_wonder_ctx *wonder_ctx, const u8 *addr)
{
	for (int i = 0; i < ARRAY_SIZE(wonder_ctx->stas); i++) {
		struct iwl_mld_wonder_sta *sta = &wonder_ctx->stas[i];

		if (sta->sta_id != IWL_INVALID_STA &&
		    ether_addr_equal(sta->addr, addr))
			return sta;
	}

	return NULL;
}

static struct iwl_mld_wonder_sta *
iwl_mld_wonder_find_free_sta(struct iwl_mld_wonder_ctx *wonder_ctx)
{
	for (int i = 0; i < ARRAY_SIZE(wonder_ctx->stas); i++) {
		struct iwl_mld_wonder_sta *sta = &wonder_ctx->stas[i];

		if (sta->sta_id == IWL_INVALID_STA)
			return sta;
	}

	return NULL;
}

/* All traffic for a station, regardless of TID, goes through this one
 * queue -- allocated once at station-add time.
 */
static int iwl_mld_wonder_alloc_sta_queue(struct iwl_mld *mld,
					  struct iwl_mld_wonder_sta *sta)
{
	u32 sta_mask = BIT(sta->sta_id);
	u32 size;
	int queue;

	if (WARN_ON(sta->queue_id != IWL_MLD_INVALID_QUEUE))
		return -EINVAL;

	size = max_t(u32, IWL_DEFAULT_QUEUE_SIZE,
		     mld->trans->mac_cfg->base->min_txq_size);

	queue = iwl_trans_txq_alloc(mld->trans, 0, sta_mask, 0, size,
				    IWL_WATCHDOG_DISABLED);
	if (queue < 0) {
		IWL_ERR(mld, "wonder: failed to alloc sta TXQ: %d\n", queue);
		return queue;
	}

	IWL_DEBUG_TX_QUEUES(mld, "wonder: sta TXQ #%d sta_mask=0x%x\n",
			    queue, sta_mask);
	sta->queue_id = queue;
	return 0;
}

static void iwl_mld_wonder_free_sta_queue(struct iwl_mld *mld, u8 sta_id,
					  struct iwl_mld_wonder_sta *sta)
{
	if (WARN_ON(sta->queue_id == IWL_MLD_INVALID_QUEUE))
		return;

	iwl_mld_free_txq(mld, BIT(sta_id), 0, sta->queue_id);
	sta->queue_id = IWL_MLD_INVALID_QUEUE;
}

static void iwl_mld_wonder_release_sta(struct iwl_mld *mld,
				       struct iwl_mld_wonder_sta *sta)
{
	u8 sta_id = sta->sta_id;

	sta->sta_id = IWL_INVALID_STA;
	eth_zero_addr(sta->addr);

	iwl_mld_flush_link_sta_txqs(mld, sta_id);
	iwl_mld_wonder_free_sta_queue(mld, sta_id, sta);
	iwl_mld_wonder_rm_sta_from_fw(mld, sta_id);
	RCU_INIT_POINTER(mld->fw_id_to_link_sta[sta_id], NULL);
}

/**
 * iwl_mld_wonder_alloc_sta - add a station
 * @mld: the MLD instance
 * @wonder_ctx: the wonder context; must have a valid @link_id
 * @addr: the station's MAC address
 *
 * Allocates a STATION_TYPE_PEER station and its single TX queue, used
 * for all TIDs. Must be called with wiphy lock held.
 *
 * Return: 0 on success, -EALREADY if @addr is already a known station,
 *	-ENOSPC if IWL_MLD_WONDER_MAX_STAS are already in use, other
 *	negative error code on failure.
 */
int iwl_mld_wonder_alloc_sta(struct iwl_mld *mld,
			     struct iwl_mld_wonder_ctx *wonder_ctx,
			     const u8 *addr)
{
	struct iwl_mld_wonder_sta *sta;
	int ret;

	lockdep_assert_wiphy(mld->wiphy);

	if (WARN_ON(wonder_ctx->link_id == IWL_MLD_INVALID_FW_ID))
		return -EINVAL;

	/* A zero addr would collide with the free-slot marker below. */
	if (WARN_ON(!is_valid_ether_addr(addr)))
		return -EINVAL;

	if (iwl_mld_wonder_find_sta(wonder_ctx, addr))
		return -EALREADY;

	sta = iwl_mld_wonder_find_free_sta(wonder_ctx);
	if (!sta)
		return -ENOSPC;

	ret = iwl_mld_allocate_link_sta_fw_id(mld, &sta->sta_id,
					      ERR_PTR(-EINVAL));
	if (ret)
		return ret;

	ret = iwl_mld_wonder_set_sta_to_fw(mld, sta->sta_id, STATION_TYPE_PEER,
					   BIT(wonder_ctx->link_id), addr);
	if (ret)
		goto free_fw_id;

	ret = iwl_mld_wonder_alloc_sta_queue(mld, sta);
	if (ret)
		goto remove_fw_sta;

	ether_addr_copy(sta->addr, addr);
	return 0;

remove_fw_sta:
	iwl_mld_wonder_rm_sta_from_fw(mld, sta->sta_id);
free_fw_id:
	RCU_INIT_POINTER(mld->fw_id_to_link_sta[sta->sta_id], NULL);
	sta->sta_id = IWL_INVALID_STA;
	return ret;
}

/**
 * iwl_mld_wonder_free_sta - remove a station
 * @mld: the MLD instance
 * @wonder_ctx: the wonder context containing the station to remove
 * @addr: the station's MAC address
 *
 * Must be called with wiphy lock held.
 */
void iwl_mld_wonder_free_sta(struct iwl_mld *mld,
			     struct iwl_mld_wonder_ctx *wonder_ctx,
			     const u8 *addr)
{
	struct iwl_mld_wonder_sta *sta;

	lockdep_assert_wiphy(mld->wiphy);

	sta = iwl_mld_wonder_find_sta(wonder_ctx, addr);
	if (WARN_ON(!sta))
		return;

	iwl_mld_wonder_release_sta(mld, sta);
}

/**
 * iwl_mld_wonder_free_all_ucast_stas - remove all stations
 * @mld: the MLD instance
 * @wonder_ctx: the wonder context containing the stations to remove
 *
 * Must be called with wiphy lock held.
 */
void iwl_mld_wonder_free_all_ucast_stas(struct iwl_mld *mld,
					struct iwl_mld_wonder_ctx *wonder_ctx)
{
	lockdep_assert_wiphy(mld->wiphy);

	for (int i = 0; i < ARRAY_SIZE(wonder_ctx->stas); i++) {
		struct iwl_mld_wonder_sta *sta = &wonder_ctx->stas[i];

		if (sta->sta_id != IWL_INVALID_STA)
			iwl_mld_wonder_release_sta(mld, sta);
	}
}
