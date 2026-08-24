// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2026 Intel Corporation
 */

/**
 * DOC: Wonder WiFi virtual device integration for the MLD driver.
 *
 * This file is compiled only when CPTCFG_IWLMLD_WONDER is selected.
 *
 * iwlmld acts as the device *publisher*: it exposes a wondertap_aux_dev on
 * the auxiliary bus so that wonder_driver (the consumer) will be probed on
 * it and can use iwlwifi as the underlying radio.
 *
 * The auxiliary-device match name is derived from the publisher module name
 * and adev.name, so this driver (built into iwlmld.ko) publishes
 * "iwlmld.wondertap.<id>", matching wonder_driver's "iwlmld.wondertap"
 * id_table entry.
 */
#include <linux/auxiliary_bus.h>
#include <linux/ieee80211.h>
#include <linux/netdevice.h>
#include <wondertap.h>

#include "fw/api/commands.h"
#include "fw/api/mac-cfg.h"
#include "hcmd.h"
#include "mcc.h"
#include "mld.h"
#include "wonder-mac.h"
#include "wonder-link.h"
#include "wonder-sta.h"
#include "wonder.h"
#include "wonder-phy.h"
#include "wonder-tx.h"
#include "wonder-agg.h"

/* Not static: referenced directly from wonder-rx.c. */
struct iwl_mld_wonder_ctx iwl_mld_wonder_ctx = {
	.phy_id = IWL_MLD_INVALID_FW_ID,
	.mac_id = IWL_MLD_INVALID_FW_ID,
	.link_id = IWL_MLD_INVALID_FW_ID,
	.bcast_mgmt_sta = {
		.sta_id = IWL_INVALID_STA,
		.queue_id = IWL_MLD_INVALID_QUEUE,
	},
	.mcast_bcast_data_sta = {
		.sta_id = IWL_INVALID_STA,
		.queue_id = IWL_MLD_INVALID_QUEUE,
	},
	.stas = {
		[0 ... IWL_MLD_WONDER_MAX_STAS - 1] = {
			.sta_id = IWL_INVALID_STA,
			.queue_id = IWL_MLD_INVALID_QUEUE,
		},
	},
	.config_lock = __SPIN_LOCK_UNLOCKED(iwl_mld_wonder_ctx.config_lock),
};

static netdev_tx_t
iwl_mld_wonder_netdev_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct iwl_mld_wonder_ctx *wonder_ctx = &iwl_mld_wonder_ctx;
	u16 rt_hdr_len = sizeof(struct ieee80211_radiotap_header);
	struct wonder_txd txd;
	u16 rt_len;

	if (!wonder_ctx->mld) {
		netdev_dbg(dev, "wonder xmit drop: not ready\n");
		goto drop;
	}

	/* Validate the fixed radiotap header without consuming it: txd sits
	 * in headroom just before it, and it_len must still be read from
	 * skb->data below.
	 */
	if (skb->len < rt_hdr_len) {
		netdev_dbg(dev, "wonder xmit drop: radiotap header too short\n");
		goto drop;
	}

	/*
	 * wonder_txd sits in headroom, immediately before skb->data. Copy it
	 * out now: any pull below, or inside iwl_mld_wonder_tx(), advances
	 * skb->data and invalidates a pointer into headroom.
	 */
	if (skb_headroom(skb) < sizeof(txd)) {
		netdev_dbg(dev, "wonder xmit drop: not enough headroom for txd\n");
		goto drop;
	}
	txd = *(const struct wonder_txd *)(skb->data - sizeof(txd));

	rt_len = ieee80211_get_radiotap_len(skb->data);
	if (rt_len < rt_hdr_len || !skb_pull(skb, rt_len)) {
		netdev_dbg(dev, "wonder xmit drop: radiotap len %u invalid\n",
			   rt_len);
		goto drop;
	}

	if (iwl_mld_wonder_tx(wonder_ctx, skb, &txd)) {
		netdev_dbg(dev, "wonder xmit drop: iwl_mld_wonder_tx failed\n");
		goto drop;
	}

	return NETDEV_TX_OK;

drop:
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static const struct net_device_ops iwl_mld_wonder_netdev_ops = {
	.ndo_start_xmit = iwl_mld_wonder_netdev_xmit,
};

static void iwl_mld_wonder_netdev_setup(struct net_device *dev)
{
	/* 0 = ARPHRD_UNSPEC; ARPHRD_ETHER is prohibited here */
	dev->type = 0;
	dev->hard_header_len = 0;
	dev->addr_len = ETH_ALEN;
	dev->netdev_ops = &iwl_mld_wonder_netdev_ops;
}

static int iwl_mld_wonder_netdev_create(struct iwl_mld_wonder_ctx *wonder_ctx)
{
	struct iwl_mld *mld = wonder_ctx->mld;
	struct net_device *dev;
	int ret;

	if (WARN_ON(wonder_ctx->netdev))
		return -EINVAL;

	dev = alloc_netdev(0, "wondertap0", NET_NAME_PREDICTABLE,
			   iwl_mld_wonder_netdev_setup);
	if (!dev) {
		IWL_ERR(mld, "Failed to allocate wondertap0 netdev\n");
		return -ENOMEM;
	}

	SET_NETDEV_DEV(dev, mld->trans->dev);

	ret = register_netdev(dev);
	if (ret) {
		IWL_ERR(mld, "Failed to register wondertap0 netdev: %d\n", ret);
		free_netdev(dev);
		return ret;
	}

	WRITE_ONCE(wonder_ctx->netdev, dev);
	return 0;
}

static void iwl_mld_wonder_netdev_destroy(struct iwl_mld_wonder_ctx *wonder_ctx)
{
	struct net_device *netdev = wonder_ctx->netdev;

	if (WARN_ON(!netdev))
		return;

	/* Close the window for iwl_mld_wonder_rx_frame() (NAPI context, no
	 * lock) to dereference a netdev we are about to free.
	 */
	WRITE_ONCE(wonder_ctx->netdev, NULL);
	synchronize_net();

	unregister_netdev(netdev);
	free_netdev(netdev);
}

static int iwl_mld_wondertap_get_capabilities(void *handle,
					      struct wondertap_capability *cap)
{
	*cap = iwl_mld_wonder_ctx.capabilities;

	return 0;
}

static void
iwl_mld_wonder_set_capabilities(struct iwl_mld *mld,
				struct iwl_mld_wonder_ctx *wonder_ctx)
{
	struct wondertap_capability *cap = &wonder_ctx->capabilities;

	memset(cap, 0, sizeof(*cap));
	cap->bits.channel_hopping = 1;
	cap->bits.rate_adaptation = 1;
	cap->bits.ampdu_aggregation = 1;
	cap->bits.dynamic_fixed_tx_rate = 1;
	cap->bits.nss = hweight8(iwl_mld_get_valid_tx_ant(mld));
}

/* Legacy OFDM only exists at 20 MHz; wonder.ko must never combine it with a
 * wider channel.
 */
static struct wondertap_fixed_tx_rate_params
iwl_mld_wonder_validate_tx_rate(const struct wondertap_fixed_tx_rate_params
				*params)
{
	struct wondertap_fixed_tx_rate_params rate = *params;

	if (WARN_ON_ONCE(rate.preamble == WONDERTAP_RATE_PREAMBLE_LEGACY &&
			 rate.bw != WONDERTAP_RATE_BW_20 &&
			 rate.bw != WONDERTAP_RATE_BW_NONE))
		rate.bw = WONDERTAP_RATE_BW_20;

	return rate;
}

static int iwl_mld_wondertap_init(void **handle,
				  const struct wondertap_init_params *params)
{
	struct iwl_mld_wonder_ctx *wonder_ctx = &iwl_mld_wonder_ctx;
	struct iwl_mld *mld = wonder_ctx->mld;
	int ret;

	if (WARN_ON(!params))
		return -EINVAL;

	if (WARN_ON(!mld))
		return -ENODEV;

	/* wonder.ko calls this with its own (separate) wiphy mutex held. */
	guard(nested_wiphy)(mld->wiphy);

	if (wonder_ctx->netdev)
		eth_hw_addr_set(wonder_ctx->netdev, params->mac_addr);

	if (WARN_ON(!mld->fw_status.running))
		return -ENODEV;

	/* No concurrent TX reader yet: that requires bcast_mgmt_sta.queue_id,
	 * set further down in this same function.
	 */
	wonder_ctx->fixed_tx_rate =
		iwl_mld_wonder_validate_tx_rate(&params->tx_rate);
	wonder_ctx->rate_adaptation_enable = params->rate_adaptation_enable;
	/* wonder.ko only populates tx_rate_mask when RA is enabled. */
	if (params->rate_adaptation_enable)
		wonder_ctx->tx_rate_mask = params->tx_rate_mask;

	{
		struct ieee80211_regdomain *regd;

		regd = iwl_mld_get_regdomain(mld, params->country_code,
					     MCC_SOURCE_MCC_API, NULL);
		if (IS_ERR_OR_NULL(regd))
			return -EIO;
		regulatory_set_wiphy_regd(mld->wiphy, regd);
		kfree(regd);
	}

	if (WARN_ON(wonder_ctx->phy_id != IWL_MLD_INVALID_FW_ID))
		return -EINVAL;

	ether_addr_copy(wonder_ctx->bssid_filter, params->bssid);

	ret = iwl_mld_wonder_allocate_phy_ctx(mld, wonder_ctx, params);
	if (ret)
		return ret;

	ret = iwl_mld_wonder_allocate_mac_ctx(mld, wonder_ctx, params);
	if (ret)
		goto free_phy;

	ret = iwl_mld_wonder_allocate_link(mld, wonder_ctx, params);
	if (ret)
		goto free_mac;

	ret = iwl_mld_wonder_alloc_bcast_mgmt_sta(mld, wonder_ctx, params);
	if (ret)
		goto free_link;

	ret = iwl_mld_wonder_alloc_mcast_bcast_data_sta(mld, wonder_ctx,
							params);
	if (ret)
		goto free_stas;

	*handle = wonder_ctx;
	return 0;

free_stas:
	iwl_mld_wonder_free_mcast_bcast_stas(mld, wonder_ctx);
free_link:
	iwl_mld_wonder_free_link(mld, wonder_ctx);
free_mac:
	iwl_mld_wonder_free_mac_ctx(mld, wonder_ctx);
free_phy:
	iwl_mld_wonder_free_phy_ctx(mld, wonder_ctx);
	return ret;
}

static void
iwl_mld_wondertap_deinit(void *handle,
			 const struct wondertap_deinit_params *params)
{
	struct iwl_mld_wonder_ctx *wonder_ctx = handle;
	struct iwl_mld *mld = wonder_ctx->mld;

	if (WARN_ON(!mld))
		return;

	/* See iwl_mld_wondertap_init() for why this is nested. */
	guard(nested_wiphy)(mld->wiphy);

	/* wiphy_work_cancel() requires wiphy->mtx, held above. */
	wiphy_work_cancel(mld->wiphy, &wonder_ctx->agg_work);
	skb_queue_purge(&wonder_ctx->agg_pending);

	WARN_ON(!mld->fw_status.running);

	iwl_mld_wonder_agg_stop_all(wonder_ctx);
	iwl_mld_wonder_free_all_ucast_stas(mld, wonder_ctx);
	iwl_mld_wonder_free_mcast_bcast_stas(mld, wonder_ctx);

	if (wonder_ctx->link_id != IWL_MLD_INVALID_FW_ID)
		iwl_mld_wonder_free_link(mld, wonder_ctx);

	if (wonder_ctx->mac_id != IWL_MLD_INVALID_FW_ID)
		iwl_mld_wonder_free_mac_ctx(mld, wonder_ctx);

	if (wonder_ctx->phy_id != IWL_MLD_INVALID_FW_ID)
		iwl_mld_wonder_free_phy_ctx(mld, wonder_ctx);
}

/* GP2 free-running timer, read directly from the periphery register. */
static int iwl_mld_wondertap_get_mac_tsf(void *handle, u32 *mac_tsf)
{
	struct iwl_mld_wonder_ctx *wonder_ctx = handle;
	struct iwl_mld *mld = wonder_ctx->mld;

	if (WARN_ON(!mld))
		return -ENODEV;

	guard(nested_wiphy)(mld->wiphy);

	if (WARN_ON(!mld->fw_status.running))
		return -EIO;

	*mac_tsf = iwl_read_prph(mld->trans,
				 mld->trans->mac_cfg->base->gp2_reg_addr);
	if (*mac_tsf == 0x5a5a5a5a)
		return -EIO;

	return 0;
}

#define IWL_WONDER_MAX_CHANNEL_LIST_LEN 32
#define IWL_WONDER_MAX_DWELL_TIME_TU 64
#define IWL_WONDER_MIN_DWELL_TIME_TU 8

/**
 * iwl_mld_wondertap_channel_schedule_request - send channel hopping cmd to FW
 * @handle: opaque vendor driver instance handle
 * @request: channel schedule request parameters from wonder
 *
 * Per-slot freq/bandwidth in @request are not part of CHANNEL_HOPPING_CMD
 * and are ignored here; only each slot's role (wonder vs. STA-managed)
 * is sent to FW.
 *
 * Return: 0 on success, negative error code on failure.
 */
static int
iwl_mld_wondertap_channel_schedule_request(void *handle,
					   const struct channel_schedule_request
					   *request)
{
	struct iwl_mld_wonder_ctx *wonder_ctx = handle;
	struct iwl_channel_hopping_cmd cmd = {};
	struct iwl_mld *mld = wonder_ctx->mld;
	u32 bitmap = 0;
	int i;

	if (WARN_ON(!mld || !request))
		return -EINVAL;

	guard(nested_wiphy)(mld->wiphy);

	if (WARN_ON(!mld->fw_status.running))
		return -EIO;

	if (WARN_ON(wonder_ctx->link_id == IWL_MLD_INVALID_FW_ID))
		return -EINVAL;

	/* Feature requirement: wonder.ko always sends a power-of-2 length. */
	if (request->channel_list_len > IWL_WONDER_MAX_CHANNEL_LIST_LEN ||
	    !is_power_of_2(request->channel_list_len))
		return -EINVAL;

	if (!request->channel_list)
		return -EINVAL;

	if (request->next_channel_index >= request->channel_list_len)
		return -EINVAL;

	if (request->dwell_time_tu < IWL_WONDER_MIN_DWELL_TIME_TU ||
	    request->dwell_time_tu > IWL_WONDER_MAX_DWELL_TIME_TU)
		return -EINVAL;

	/* Each bit represents a slot; set it for wonder (NOP) slots only. */
	for (i = 0; i < request->channel_list_len; i++)
		if (request->channel_list[i].role == WONDERTAP_ROLE_NOP)
			bitmap |= BIT(i);

	cmd.link_id = cpu_to_le32(wonder_ctx->link_id);
	cmd.channel_list_length = cpu_to_le32(request->channel_list_len);
	cmd.next_index = cpu_to_le32(request->next_channel_index);
	cmd.dwell_time = cpu_to_le32(request->dwell_time_tu);
	cmd.channel_hopping_bitmap = cpu_to_le32(bitmap);
	cmd.target_switch_time = cpu_to_le32(request->target_switch_time_tsf);

	return iwl_mld_send_cmd_pdu(mld, WIDE_ID(MAC_CONF_GROUP,
						 CHANNEL_HOPPING_CMD), &cmd);
}

static int iwl_mld_wonder_vht_mcs_to_bitmap(u8 mcs)
{
	switch (mcs) {
	case IEEE80211_VHT_MCS_SUPPORT_0_7:
		return BIT(IWL_TLC_MNG_HT_RATE_MCS7 + 1) - 1;
	case IEEE80211_VHT_MCS_SUPPORT_0_8:
		return BIT(IWL_TLC_MNG_HT_RATE_MCS8 + 1) - 1;
	case IEEE80211_VHT_MCS_SUPPORT_0_9:
		return BIT(IWL_TLC_MNG_HT_RATE_MCS9 + 1) - 1;
	case IEEE80211_VHT_MCS_NOT_SUPPORTED:
	default:
		return 0;
	}
}

static u8
iwl_mld_wonder_get_fw_chains(struct iwl_mld *mld)
{
	u8 chains = iwl_mld_get_valid_tx_ant(mld);
	u8 fw_chains = 0;

	if (chains & ANT_A)
		fw_chains |= IWL_TLC_MNG_CHAIN_A_MSK;
	if (chains & ANT_B)
		fw_chains |= IWL_TLC_MNG_CHAIN_B_MSK;

	return fw_chains;
}

/* wondertap gives station caps as raw bitmasks; derive max FW channel width. */
static u8
iwl_mld_wonder_fw_bw_from_sta_bw(const struct wondertap_station_info *info)
{
	u32 vht_chan_width = le32_to_cpu(info->vht_capa.vht_cap_info) &
			     IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK;
	bool has_ht = info->capability_mask & BIT(WONDERTAP_STATION_CAP_HT);
	bool has_vht = info->capability_mask & BIT(WONDERTAP_STATION_CAP_VHT);
	bool has_he = info->capability_mask & BIT(WONDERTAP_STATION_CAP_HE);

	if (has_he &&
	    (info->he_capa.phy_cap_info[0] &
	     IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G))
		return IWL_TLC_MNG_CH_WIDTH_160MHZ;

	if (has_vht &&
	    (vht_chan_width == IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ ||
	     vht_chan_width == IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ))
		return IWL_TLC_MNG_CH_WIDTH_160MHZ;

	if (has_vht ||
	    (has_he &&
	     (info->he_capa.phy_cap_info[0] &
	      IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G)))
		return IWL_TLC_MNG_CH_WIDTH_80MHZ;

	if (has_ht && (le16_to_cpu(info->ht_capa.cap_info) &
		       IEEE80211_HT_CAP_SUP_WIDTH_20_40))
		return IWL_TLC_MNG_CH_WIDTH_40MHZ;

	return IWL_TLC_MNG_CH_WIDTH_20MHZ;
}

static u8
iwl_mld_wonder_tlc_bw_from_chan_width(enum nl80211_chan_width width)
{
	switch (width) {
	case NL80211_CHAN_WIDTH_160:
		return IWL_TLC_MNG_CH_WIDTH_160MHZ;
	case NL80211_CHAN_WIDTH_80:
		return IWL_TLC_MNG_CH_WIDTH_80MHZ;
	case NL80211_CHAN_WIDTH_40:
		return IWL_TLC_MNG_CH_WIDTH_40MHZ;
	default:
		return IWL_TLC_MNG_CH_WIDTH_20MHZ;
	}
}

/* HE stations use HE MCS rates; SGI bits don't apply to them. */
static u8
iwl_mld_wonder_get_fw_sgi(const struct wondertap_station_info *info,
			  u8 max_ch_width)
{
	bool has_he = info->capability_mask & BIT(WONDERTAP_STATION_CAP_HE);
	u8 sgi_chwidths = 0;

	if (has_he)
		return 0;

	if (info->capability_mask & BIT(WONDERTAP_STATION_CAP_HT)) {
		u16 ht_cap = le16_to_cpu(info->ht_capa.cap_info);

		if (ht_cap & IEEE80211_HT_CAP_SGI_20)
			sgi_chwidths |= BIT(IWL_TLC_MNG_CH_WIDTH_20MHZ);
		if (max_ch_width >= IWL_TLC_MNG_CH_WIDTH_40MHZ &&
		    (ht_cap & IEEE80211_HT_CAP_SGI_40))
			sgi_chwidths |= BIT(IWL_TLC_MNG_CH_WIDTH_40MHZ);
	}

	if (info->capability_mask & BIT(WONDERTAP_STATION_CAP_VHT)) {
		u32 vht_cap = le32_to_cpu(info->vht_capa.vht_cap_info);

		if (max_ch_width >= IWL_TLC_MNG_CH_WIDTH_80MHZ &&
		    (vht_cap & IEEE80211_VHT_CAP_SHORT_GI_80))
			sgi_chwidths |= BIT(IWL_TLC_MNG_CH_WIDTH_80MHZ);
		if (max_ch_width >= IWL_TLC_MNG_CH_WIDTH_160MHZ &&
		    (vht_cap & IEEE80211_VHT_CAP_SHORT_GI_160))
			sgi_chwidths |= BIT(IWL_TLC_MNG_CH_WIDTH_160MHZ);
	}

	return sgi_chwidths;
}

static __le16
iwl_mld_wonder_get_tlc_flags(struct iwl_mld *mld,
			     const struct iwl_mld_wonder_ctx *wonder_ctx,
			     const struct wondertap_station_info *info)
{
	bool has_ht = info->capability_mask & BIT(WONDERTAP_STATION_CAP_HT);
	bool has_vht = info->capability_mask & BIT(WONDERTAP_STATION_CAP_VHT);
	bool has_he = info->capability_mask & BIT(WONDERTAP_STATION_CAP_HE);
	u32 vht_cap = le32_to_cpu(info->vht_capa.vht_cap_info);
	u16 ht_cap = le16_to_cpu(info->ht_capa.cap_info);
	u16 flags = 0;

	if (mld->cfg->ht_params.stbc &&
	    wonder_ctx->capabilities.bits.nss >= 2) {
		if (has_he &&
		    (info->he_capa.phy_cap_info[2] &
		     IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ))
			flags |= IWL_TLC_MNG_CFG_FLAGS_STBC_MSK;
		else if (has_vht && (vht_cap & IEEE80211_VHT_CAP_RXSTBC_MASK))
			flags |= IWL_TLC_MNG_CFG_FLAGS_STBC_MSK;
		else if (has_ht && (ht_cap & IEEE80211_HT_CAP_RX_STBC))
			flags |= IWL_TLC_MNG_CFG_FLAGS_STBC_MSK;
	}

	if (mld->cfg->ht_params.ldpc &&
	    ((has_ht && (ht_cap & IEEE80211_HT_CAP_LDPC_CODING)) ||
	     (has_vht && (vht_cap & IEEE80211_VHT_CAP_RXLDPC))))
		flags |= IWL_TLC_MNG_CFG_FLAGS_LDPC_MSK;

	if (has_he &&
	    (info->he_capa.phy_cap_info[1] &
	     IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD))
		flags |= IWL_TLC_MNG_CFG_FLAGS_LDPC_MSK;

	return cpu_to_le16(flags);
}

static int
iwl_mld_wonder_send_tlc_cfg(struct iwl_mld *mld,
			    struct iwl_mld_wonder_ctx *wonder_ctx,
			    const struct iwl_mld_wonder_sta *sta,
			    const struct wondertap_station_info *info)
{
	u8 link_bw = iwl_mld_wonder_tlc_bw_from_chan_width(wonder_ctx->phy_chan_width);
	u8 max_ch_width = min_t(u8, iwl_mld_wonder_fw_bw_from_sta_bw(info), link_bw);
	struct iwl_tlc_config_cmd cmd = {
		.max_ch_width = max_ch_width,
		.mode = IWL_TLC_MNG_MODE_NON_HT,
		.chains = iwl_mld_wonder_get_fw_chains(mld),
		.sgi_ch_width_supp = iwl_mld_wonder_get_fw_sgi(info, max_ch_width),
		.non_ht_rates = cpu_to_le16(0x00ff),
	};
	enum wondertap_rate_preamble max_preamble = wonder_ctx->rate_adaptation_enable ?
		wonder_ctx->tx_rate_mask.max_preamble : WONDERTAP_RATE_PREAMBLE_EHT;
	bool has_vht = info->capability_mask & BIT(WONDERTAP_STATION_CAP_VHT);
	bool has_he = info->capability_mask & BIT(WONDERTAP_STATION_CAP_HE);
	bool has_ht = info->capability_mask & BIT(WONDERTAP_STATION_CAP_HT);
	u32 cmd_id = WIDE_ID(DATA_PATH_GROUP, TLC_MNG_CONFIG_CMD);
	const u8 max_nss = wonder_ctx->capabilities.bits.nss;
	u16 vht_rx_map;

	lockdep_assert_wiphy(mld->wiphy);

	if (WARN_ON(sta->sta_id == IWL_INVALID_STA ||
		    wonder_ctx->phy_id == IWL_MLD_INVALID_FW_ID))
		return -EINVAL;

	cmd.sta_mask = cpu_to_le32(BIT(sta->sta_id));
	cmd.phy_id = cpu_to_le32(wonder_ctx->phy_id);
	cmd.flags = iwl_mld_wonder_get_tlc_flags(mld, wonder_ctx, info);

	if (has_he && max_preamble >= WONDERTAP_RATE_PREAMBLE_HE) {
		cmd.mode = IWL_TLC_MNG_MODE_HE;
		cmd.ht_rates[IWL_TLC_NSS_1][IWL_TLC_MCS_PER_BW_80] =
			cpu_to_le32(BIT(IWL_TLC_MNG_HT_RATE_MCS7 + 1) - 1);
	} else if (has_vht && max_preamble >= WONDERTAP_RATE_PREAMBLE_VHT) {
		cmd.mode = IWL_TLC_MNG_MODE_VHT;
		vht_rx_map = le16_to_cpu(info->vht_capa.supp_mcs.rx_mcs_map);

		cmd.ht_rates[IWL_TLC_NSS_1][IWL_TLC_MCS_PER_BW_80] =
			cpu_to_le32(iwl_mld_wonder_vht_mcs_to_bitmap(vht_rx_map & 0x3));

		/* only fill NSS_2 rates if our radio has a second chain */
		if (max_nss >= 2)
			cmd.ht_rates[IWL_TLC_NSS_2][IWL_TLC_MCS_PER_BW_80] =
				cpu_to_le32(iwl_mld_wonder_vht_mcs_to_bitmap((vht_rx_map >> 2) & 0x3));
	} else if (has_ht && max_preamble >= WONDERTAP_RATE_PREAMBLE_HT) {
		cmd.mode = IWL_TLC_MNG_MODE_HT;
		cmd.ht_rates[IWL_TLC_NSS_1][IWL_TLC_MCS_PER_BW_80] =
			cpu_to_le32(info->ht_capa.mcs.rx_mask[0]);

		if (max_nss >= 2)
			cmd.ht_rates[IWL_TLC_NSS_2][IWL_TLC_MCS_PER_BW_80] =
				cpu_to_le32(info->ht_capa.mcs.rx_mask[1]);
	}

	IWL_DEBUG_RATE(mld,
		       "wonder: TLC cfg sta=%u mode=%u cap=0x%x phy=%u "
		       "max_ch_width=%u link_bw=%u max_preamble=%u "
		       "sgi=0x%x chains=0x%x flags=0x%x "
		       "nss1[80]=0x%x nss1[160]=0x%x "
		       "nss2[80]=0x%x nss2[160]=0x%x\n",
		       sta->sta_id, cmd.mode,
		       info->capability_mask, wonder_ctx->phy_id,
		       cmd.max_ch_width, link_bw, max_preamble, cmd.sgi_ch_width_supp,
		       cmd.chains, le16_to_cpu(cmd.flags),
		       le32_to_cpu(cmd.ht_rates[IWL_TLC_NSS_1][IWL_TLC_MCS_PER_BW_80]),
		       le32_to_cpu(cmd.ht_rates[IWL_TLC_NSS_1][IWL_TLC_MCS_PER_BW_160]),
		       le32_to_cpu(cmd.ht_rates[IWL_TLC_NSS_2][IWL_TLC_MCS_PER_BW_80]),
		       le32_to_cpu(cmd.ht_rates[IWL_TLC_NSS_2][IWL_TLC_MCS_PER_BW_160]));

	return iwl_mld_send_cmd_pdu(mld, cmd_id, &cmd);
}
/**
 * iwl_mld_wondertap_set_station_info - add, update or remove a station
 * @handle: opaque vendor driver instance handle
 * @action: WONDERTAP_STATION_STATE_NEW/UPDATE/DEL
 * @info: station parameters; @info->mac identifies the station
 *
 * Return: 0 on success, negative error code on failure.
 */
static int
iwl_mld_wondertap_set_station_info(void *handle,
				   const enum wondertap_station_action action,
				   struct wondertap_station_info *info)
{
	struct iwl_mld_wonder_ctx *wonder_ctx = handle;
	struct iwl_mld_wonder_sta *sta;
	struct iwl_mld *mld;
	int ret;

	if (WARN_ON(!wonder_ctx || !info))
		return -EINVAL;

	mld = wonder_ctx->mld;
	if (WARN_ON(!mld))
		return -ENODEV;

	guard(nested_wiphy)(mld->wiphy);

	if (WARN_ON(!mld->fw_status.running))
		return -EIO;

	switch (action) {
	case WONDERTAP_STATION_STATE_NEW:
		ret = iwl_mld_wonder_alloc_sta(mld, wonder_ctx,
					       info->mac);
		if (ret) {
			IWL_ERR(mld,
				"wonder: failed to add sta %pM: %d\n",
				info->mac, ret);
			return ret;
		}
		sta = iwl_mld_wonder_find_sta(wonder_ctx, info->mac);
		if (WARN_ON(!sta))
			return -EINVAL;
		ret = iwl_mld_wonder_send_tlc_cfg(mld, wonder_ctx, sta, info);
		if (ret) {
			IWL_ERR(mld,
				"wonder: TLC cfg failed for %pM: %d\n",
				info->mac, ret);
			iwl_mld_wonder_free_sta(mld, wonder_ctx, info->mac);
		}
		return ret;
	case WONDERTAP_STATION_STATE_UPDATE:
		sta = iwl_mld_wonder_find_sta(wonder_ctx, info->mac);
		if (!sta)
			return -ENOENT;
		ret = iwl_mld_wonder_send_tlc_cfg(mld, wonder_ctx, sta, info);
		if (ret)
			IWL_WARN(mld,
				 "wonder: TLC cfg update failed %pM: %d\n",
				 info->mac, ret);
		return ret;
	case WONDERTAP_STATION_STATE_DEL:
		iwl_mld_wonder_free_sta(mld, wonder_ctx, info->mac);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int
iwl_mld_wondertap_set_fixed_tx_rate(void *handle,
				    const struct wondertap_fixed_tx_rate_params
				    *params)
{
	struct iwl_mld_wonder_ctx *wonder_ctx = handle;
	struct wondertap_fixed_tx_rate_params rate =
		iwl_mld_wonder_validate_tx_rate(params);

	spin_lock_bh(&wonder_ctx->config_lock);
	wonder_ctx->fixed_tx_rate = rate;
	spin_unlock_bh(&wonder_ctx->config_lock);

	return 0;
}

static int
iwl_mld_wondertap_set_tx_rate_mask(void *handle,
				   const struct wondertap_tx_rate_mask_params
				   *params)
{
	struct iwl_mld_wonder_ctx *wonder_ctx = handle;
	struct iwl_mld *mld = wonder_ctx->mld;

	/* only read from iwl_mld_wonder_send_tlc_cfg(), under the wiphy lock */
	guard(nested_wiphy)(mld->wiphy);
	wonder_ctx->tx_rate_mask = *params;

	return 0;
}

static const struct wondertap_ops iwl_mld_wondertap_ops = {
	.init = iwl_mld_wondertap_init,
	.deinit = iwl_mld_wondertap_deinit,
	.get_capabilities = iwl_mld_wondertap_get_capabilities,
	.get_mac_tsf = iwl_mld_wondertap_get_mac_tsf,
	.channel_schedule_request = iwl_mld_wondertap_channel_schedule_request,
	.set_station_info = iwl_mld_wondertap_set_station_info,
	.set_fixed_tx_rate = iwl_mld_wondertap_set_fixed_tx_rate,
	.set_tx_rate_mask = iwl_mld_wondertap_set_tx_rate_mask,
};

static void iwl_mld_wonder_adev_release(struct device *dev)
{
	struct auxiliary_device *aux = to_auxiliary_dev(dev);

	kfree(container_of(aux, struct wondertap_aux_dev, adev));
}

/*
 * Reset wonder_ctx's own registration state. Called from
 * iwl_mld_wonder_register()'s failure path and from
 * iwl_mld_wonder_unregister(), so a NIC can register again later.
 */
static void iwl_mld_wonder_ctx_reset(struct iwl_mld_wonder_ctx *wonder_ctx)
{
	wonder_ctx->wonder_dev = NULL;
	wonder_ctx->mld = NULL;
	wonder_ctx->registered = 0;
}

/**
 * iwl_mld_wonder_register - publish the wondertap auxiliary device
 * @mld: the MLD instance calling this function
 *
 * When more than one NIC is present this function is called once per
 * instance, but the device is only created on the first call; subsequent
 * calls just return 0.
 *
 * Return: 0 on success, negative error code on failure.
 */
int iwl_mld_wonder_register(struct iwl_mld *mld)
{
	struct iwl_mld_wonder_ctx *wonder_ctx = &iwl_mld_wonder_ctx;
	struct wondertap_aux_dev *wonder_dev;
	int ret;

	if (test_and_set_bit(0, &wonder_ctx->registered)) {
		IWL_DEBUG_INFO(mld,
			       "Wonder already registered by another instance\n");
		return 0;
	}

	if (!iwl_mld_wonder_check_fw_version_supported(mld)) {
		IWL_ERR(mld, "Wonder: unsupported firmware version\n");
		ret = -EOPNOTSUPP;
		goto err;
	}

	iwl_mld_wonder_set_capabilities(mld, wonder_ctx);

	wonder_dev = kzalloc(sizeof(*wonder_dev), GFP_KERNEL);
	if (!wonder_dev) {
		IWL_ERR(mld, "Failed to allocate wondertap aux device\n");
		ret = -ENOMEM;
		goto err;
	}

	wonder_dev->adev.name = "wondertap";
	wonder_dev->adev.id = 0;
	wonder_dev->adev.dev.parent = mld->trans->dev;
	wonder_dev->adev.dev.release = iwl_mld_wonder_adev_release;
	wonder_dev->ver = WONDER_VERSION_3_6_2;
	wonder_dev->wonder_ops = &iwl_mld_wondertap_ops;

	wonder_ctx->wonder_dev = wonder_dev;
	wonder_ctx->mld = mld;

	iwl_mld_wonder_agg_init(wonder_ctx, mld);

	ret = iwl_mld_wonder_netdev_create(wonder_ctx);
	if (ret) {
		IWL_ERR(mld, "Failed to create wondertap0 netdev: %d\n", ret);
		kfree(wonder_dev);
		goto err;
	}

	ret = auxiliary_device_init(&wonder_dev->adev);
	if (ret) {
		IWL_ERR(mld, "Failed to init wondertap aux device: %d\n", ret);
		kfree(wonder_dev);
		goto err_free_ndev;
	}

	ret = auxiliary_device_add(&wonder_dev->adev);
	if (ret) {
		IWL_ERR(mld, "Failed to add wondertap aux device: %d\n", ret);
		goto err_free_adev;
	}

	return 0;

err_free_adev:
	auxiliary_device_uninit(&wonder_dev->adev);
err_free_ndev:
	iwl_mld_wonder_netdev_destroy(wonder_ctx);
err:
	iwl_mld_wonder_ctx_reset(wonder_ctx);
	return ret;
}

/**
 * iwl_mld_wonder_unregister - remove the wondertap auxiliary device
 * @mld: the MLD instance calling this function
 *
 * Only the MLD instance that registered may unregister; other instances
 * (and repeated calls) are no-ops.
 */
void iwl_mld_wonder_unregister(struct iwl_mld *mld)
{
	struct iwl_mld_wonder_ctx *wonder_ctx = &iwl_mld_wonder_ctx;

	if (mld != wonder_ctx->mld)
		return;

	if (!test_and_clear_bit(0, &wonder_ctx->registered))
		return;

	/* delete drops wonder.ko's ref on wondertap0 before we destroy it */
	auxiliary_device_delete(&wonder_ctx->wonder_dev->adev);
	iwl_mld_wonder_netdev_destroy(wonder_ctx);
	auxiliary_device_uninit(&wonder_ctx->wonder_dev->adev);

	iwl_mld_wonder_ctx_reset(wonder_ctx);
}
