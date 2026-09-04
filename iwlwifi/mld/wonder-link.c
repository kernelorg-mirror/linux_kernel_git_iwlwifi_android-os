// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2026 Intel Corporation
 */

#include <linux/etherdevice.h>
#include <net/cfg80211.h>

#include "fw/api/commands.h"
#include "fw/api/context.h"
#include "fw/api/mac-cfg.h"
#include "fw/api/rs.h"
#include "hcmd.h"
#include "link.h"
#include "mld.h"
#include "wonder-link.h"

static int
iwl_mld_wonder_link_fill_rates(struct iwl_mld *mld,
			       struct iwl_link_config_cmd *cmd,
			       const struct wondertap_init_params *params)
{
	struct ieee80211_channel *chan =
		ieee80211_get_channel(mld->wiphy, params->channel.freq);
	u32 ofdm = 0;
	u32 cck = 0;

	if (WARN_ON(!chan))
		return -EINVAL;

	/* Keep mandatory OFDM basic rates enabled. */
	ofdm |= IWL_RATE_BIT_MSK(6) >> IWL_FIRST_OFDM_RATE;
	ofdm |= IWL_RATE_BIT_MSK(12) >> IWL_FIRST_OFDM_RATE;
	ofdm |= IWL_RATE_BIT_MSK(24) >> IWL_FIRST_OFDM_RATE;

	if (chan->band == NL80211_BAND_2GHZ) {
		/* Allow all legacy CCK basics in 2.4 GHz. */
		cck |= IWL_RATE_BIT_MSK(1) >> IWL_FIRST_CCK_RATE;
		cck |= IWL_RATE_BIT_MSK(2) >> IWL_FIRST_CCK_RATE;
		cck |= IWL_RATE_BIT_MSK(5) >> IWL_FIRST_CCK_RATE;
		cck |= IWL_RATE_BIT_MSK(11) >> IWL_FIRST_CCK_RATE;
	}

	cmd->cck_rates = cpu_to_le32(cck);
	cmd->ofdm_rates = cpu_to_le32(ofdm);

	return 0;
}

static int iwl_mld_wonder_set_link_active(struct iwl_mld *mld,
					  struct iwl_mld_wonder_ctx *wonder_ctx,
					  bool active)
{
	struct iwl_link_config_cmd cmd = {
		.link_id = cpu_to_le32(wonder_ctx->link_id),
		.mac_id = cpu_to_le32(wonder_ctx->mac_id),
		.phy_id = cpu_to_le32(wonder_ctx->phy_id),
		.active = cpu_to_le32(active ? 1 : 0),
		.modify_mask = cpu_to_le32(LINK_CONTEXT_MODIFY_ACTIVE),
	};

	lockdep_assert_wiphy(mld->wiphy);

	if (WARN_ON(active && !wonder_ctx->netdev))
		return -EINVAL;

	/* Always carried in MODIFY: the FW zeroes both on a partial update. */
	if (wonder_ctx->netdev)
		ether_addr_copy(cmd.local_link_addr,
				wonder_ctx->netdev->dev_addr);
	ether_addr_copy(cmd.ibss_bssid_addr, wonder_ctx->bssid_filter);

	return iwl_mld_send_link_cmd(mld, &cmd, FW_CTXT_ACTION_MODIFY);
}

/**
 * iwl_mld_wonder_free_link - deactivate, free and remove a LINK context
 * @mld: the MLD instance
 * @wonder_ctx: the wonder context containing the LINK ID to remove
 */
void iwl_mld_wonder_free_link(struct iwl_mld *mld,
			      struct iwl_mld_wonder_ctx *wonder_ctx)
{
	struct iwl_link_config_cmd cmd = {
		.link_id = cpu_to_le32(wonder_ctx->link_id),
		.phy_id = cpu_to_le32(FW_CTXT_ID_INVALID),
	};

	lockdep_assert_wiphy(mld->wiphy);

	if (WARN_ON(wonder_ctx->link_id == IWL_MLD_INVALID_FW_ID))
		return;

	iwl_mld_wonder_set_link_active(mld, wonder_ctx, false);

	iwl_mld_send_link_cmd(mld, &cmd, FW_CTXT_ACTION_REMOVE);
	RCU_INIT_POINTER(mld->fw_id_to_bss_conf[wonder_ctx->link_id], NULL);
	wonder_ctx->link_id = IWL_MLD_INVALID_FW_ID;
}

static bool iwl_mld_wonder_calib_done_fn(struct iwl_notif_wait_data *notif_wait,
					 struct iwl_rx_packet *pkt, void *data)
{
	const struct iwl_chan_hop_calib_done_notif *notif = (void *)pkt->data;
	unsigned int pkt_len = iwl_rx_packet_payload_len(pkt);
	struct iwl_mld_wonder_ctx *wonder_ctx = data;
	struct iwl_mld *mld = wonder_ctx->mld;
	u32 notif_link_id;

	if (IWL_FW_CHECK(mld, pkt_len < sizeof(*notif),
			 "short CALIB_DONE_NTF (%u)\n", pkt_len))
		return false;

	notif_link_id = le32_to_cpu(notif->link_id);
	if (IWL_FW_CHECK(mld, notif_link_id != wonder_ctx->link_id,
			 "wonder: CALIB_DONE_NTF link_id %u != expected %u\n",
			 notif_link_id, wonder_ctx->link_id))
		return false;

	return true;
}

/**
 * iwl_mld_wonder_allocate_link - allocate and activate a LINK context
 * @mld: the MLD instance
 * @wonder_ctx: the wonder context to store the LINK ID
 * @params: initialization parameters with address/rate/channel data
 *
 * Return: 0 on success, negative error code on failure.
 */
int
iwl_mld_wonder_allocate_link(struct iwl_mld *mld,
			     struct iwl_mld_wonder_ctx *wonder_ctx,
			     const struct wondertap_init_params *params)
{
	static const u16 calib_done_notif[] = {
		WIDE_ID(MAC_CONF_GROUP, CHANNEL_HOP_CALIB_DONE_NOTIF)
	};
	struct iwl_link_config_cmd cmd = {
		.phy_id = cpu_to_le32(wonder_ctx->phy_id),
		.mac_id = cpu_to_le32(wonder_ctx->mac_id),
	};
	struct iwl_notification_wait calib_wait;
	u8 link_id;
	int ret;

	lockdep_assert_wiphy(mld->wiphy);

	if (WARN_ON(wonder_ctx->link_id != IWL_MLD_INVALID_FW_ID))
		return -EINVAL;

	if (WARN_ON(wonder_ctx->mac_id == IWL_MLD_INVALID_FW_ID ||
		    wonder_ctx->phy_id == IWL_MLD_INVALID_FW_ID))
		return -EINVAL;

	ret = iwl_mld_allocate_link_fw_id(mld, &link_id, ERR_PTR(-ENODEV));
	if (ret)
		return ret;

	cmd.link_id = cpu_to_le32(link_id);

	ether_addr_copy(cmd.local_link_addr, params->mac_addr);
	ether_addr_copy(cmd.ibss_bssid_addr, params->bssid);

	ret = iwl_mld_wonder_link_fill_rates(mld, &cmd, params);
	if (ret)
		goto err_free_id;

	cmd.protection_flags = cpu_to_le32(LINK_PROT_FLG_HT_PROT |
					    LINK_PROT_FLG_FAT_PROT);
	cmd.qos_flags = cpu_to_le32(MAC_QOS_FLG_TGN);
	cmd.cck_short_preamble = cpu_to_le32(1);
	cmd.short_slot = cpu_to_le32(1);
	/* Required for rates, protection and QoS fields, including on ADD. */
	cmd.modify_mask = cpu_to_le32(LINK_CONTEXT_MODIFY_RATES_INFO |
				      LINK_CONTEXT_MODIFY_PROTECT_FLAGS |
				      LINK_CONTEXT_MODIFY_QOS_PARAMS);

	ret = iwl_mld_send_link_cmd(mld, &cmd, FW_CTXT_ACTION_ADD);
	if (ret)
		goto err_free_id;

	wonder_ctx->link_id = link_id;

	/*
	 * Register before activating: FW sends CALIB_DONE_NTF once LMAC
	 * calibration completes, and CHANNEL_HOPPING_CMD is only safe after.
	 */
	iwl_init_notification_wait(&mld->notif_wait, &calib_wait,
				   calib_done_notif,
				   ARRAY_SIZE(calib_done_notif),
				   iwl_mld_wonder_calib_done_fn, wonder_ctx);

	ret = iwl_mld_wonder_set_link_active(mld, wonder_ctx, true);
	if (ret) {
		iwl_remove_notification(&mld->notif_wait, &calib_wait);
		iwl_mld_wonder_free_link(mld, wonder_ctx);
		return ret;
	}

	/*
	 * TODO: fail init on timeout once vLab testing is done. Currently
	 * non-fatal because the notif never arrives in vLab (PHY never
	 * reaches full calibration there); also re-evaluate the timeout
	 * value below.
	 */
	ret = iwl_wait_notification(&mld->notif_wait, &calib_wait, 5 * HZ);
	if (ret)
		IWL_WARN(mld, "wonder: CHAN_HOP CALIB_DONE_NTF timed out\n");
	else
		IWL_INFO(mld, "wonder: CHAN_HOP CALIB_DONE_NTF received\n");

	return 0;

err_free_id:
	RCU_INIT_POINTER(mld->fw_id_to_bss_conf[link_id], NULL);
	return ret;
}
