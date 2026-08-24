// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2026 Intel Corporation
 */

/**
 * DOC: Wonder TX data path
 *
 * Translates wondertap rate parameters to the firmware rate word and submits
 * frames to the transport layer via the bcast/mcast/sta TX queues.
 *
 * Frames arrive from wonder.ko via the wondertap0 ndo_start_xmit callback.
 * The radiotap header is stripped by iwl_mld_wonder_netdev_xmit() before
 * calling iwl_mld_wonder_tx(); the per-frame metadata that used to sit in
 * skb headroom is copied out by the caller before that strip and passed in
 * as @txd. The 802.11 header is read in place at skb->data, not pulled off:
 * the transport layer expects it to still be present in the skb.
 *
 * TX completion is handled by the existing iwl_mld_handle_tx_resp_notif()
 * and iwl_mld_tx_reclaim_txq() paths. Wonder frames are identified by a
 * non-NULL info->driver_data[0] and are freed with dev_kfree_skb_any()
 * instead of ieee80211_tx_status_skb().
 */
#include <linux/etherdevice.h>
#include <linux/ieee80211.h>
#include <linux/skbuff.h>
#include <wondertap.h>

#include "fw/api/commands.h"
#include "fw/api/rs.h"
#include "fw/api/tx.h"
#include "hcmd.h"
#include "mld.h"
#include "wonder.h"
#include "wonder-phy.h"
#include "wonder-sta.h"
#include "wonder-tx.h"

/**
 * iwl_mld_wonder_rate_to_fw - translate wondertap rate params to FW rate word
 * @mld: MLD instance (used for antenna selection)
 * @r: rate parameters from the last set_fixed_tx_rate() call
 *
 * Produces a rate_n_flags value in v2/v3 format as described in fw/api/rs.h.
 * Falls back to 6 Mbps OFDM when the preamble type is unknown.
 *
 * Return: 32-bit FW rate word (host byte order, NOT cpu_to_le32'd yet).
 */
static u32
iwl_mld_wonder_rate_to_fw(struct iwl_mld *mld,
			  const struct wondertap_fixed_tx_rate_params *r)
{
	u32 rate = BIT(mld->mgmt_tx_ant) << RATE_MCS_ANT_POS;
	u8 gi_val;

	/* BW -- common to all modulation types */
	switch (r->bw) {
	case WONDERTAP_RATE_BW_40:
		rate |= RATE_MCS_CHAN_WIDTH_40;
		break;
	case WONDERTAP_RATE_BW_80:
		rate |= RATE_MCS_CHAN_WIDTH_80;
		break;
	case WONDERTAP_RATE_BW_160:
		rate |= RATE_MCS_CHAN_WIDTH_160;
		break;
	case WONDERTAP_RATE_BW_320:
		rate |= RATE_MCS_CHAN_WIDTH_320;
		break;
	default: /* WONDERTAP_RATE_BW_20 / NONE */
		rate |= RATE_MCS_CHAN_WIDTH_20;
		break;
	}

	switch (r->preamble) {
	case WONDERTAP_RATE_PREAMBLE_LEGACY:
		rate |= RATE_MCS_MOD_TYPE_LEGACY_OFDM;
		rate |= r->mcs & RATE_LEGACY_RATE_MSK;
		break;

	case WONDERTAP_RATE_PREAMBLE_HT:
		rate |= RATE_MCS_MOD_TYPE_HT;
		/* wondertap mcs for HT folds NSS into the value (0-7, 8-15,
		 * 16-23, 24-31 for NSS 1-4); only the low 3 bits are the
		 * actual per-stream code, NSS comes from r->nss below.
		 */
		rate |= u32_encode_bits(r->mcs & RATE_HT_MCS_CODE_MSK,
					RATE_MCS_CODE_MSK);
		/* RATE_MCS_NSS_MSK encodes NSS-1; nss=0 is invalid per spec */
		rate |= u32_encode_bits(r->nss > 0 ? r->nss - 1 : 0,
					RATE_MCS_NSS_MSK);
		if (r->gi == WONDERTAP_RATE_GI_SHORT)
			rate |= RATE_MCS_SGI_MSK;
		break;

	case WONDERTAP_RATE_PREAMBLE_VHT:
		rate |= RATE_MCS_MOD_TYPE_VHT;
		rate |= u32_encode_bits(r->mcs, RATE_MCS_CODE_MSK);
		rate |= u32_encode_bits(r->nss > 0 ? r->nss - 1 : 0,
					RATE_MCS_NSS_MSK);
		if (r->gi == WONDERTAP_RATE_GI_SHORT)
			rate |= RATE_MCS_SGI_MSK;
		break;

	case WONDERTAP_RATE_PREAMBLE_HE:
		rate |= RATE_MCS_MOD_TYPE_HE;
		rate |= u32_encode_bits(r->mcs, RATE_MCS_CODE_MSK);
		rate |= u32_encode_bits(r->nss > 0 ? r->nss - 1 : 0,
					RATE_MCS_NSS_MSK);
		/* HE SU GI+LTF field (bits 22:20):
		 * 0 = 1xLTF+0.8us  1 = 2xLTF+0.8us
		 * 2 = 2xLTF+1.6us  3 = 4xLTF+3.2us
		 */
		switch (r->gi) {
		case WONDERTAP_RATE_GI_0_8_US:
			gi_val = 1;
			break;
		case WONDERTAP_RATE_GI_1_6_US:
			gi_val = 2;
			break;
		case WONDERTAP_RATE_GI_3_2_US:
			gi_val = 3;
			break;
		default:
			gi_val = 0;
			break;
		}
		rate |= u32_encode_bits(gi_val, RATE_MCS_HE_GI_LTF_MSK);
		break;

	case WONDERTAP_RATE_PREAMBLE_EHT:
		rate |= RATE_MCS_MOD_TYPE_EHT;
		rate |= u32_encode_bits(r->mcs, RATE_MCS_CODE_MSK);
		rate |= u32_encode_bits(r->nss > 0 ? r->nss - 1 : 0,
					RATE_MCS_NSS_MSK);
		/* EHT MU GI+LTF field (bits 22:20):
		 * 0 = 2xLTF+0.8us  1 = 2xLTF+1.6us
		 * 2 = 4xLTF+0.8us  3 = 4xLTF+3.2us
		 */
		switch (r->gi) {
		case WONDERTAP_RATE_GI_1_6_US:
			gi_val = 1;
			break;
		case WONDERTAP_RATE_GI_0_8_US:
			gi_val = 2;
			break;
		case WONDERTAP_RATE_GI_3_2_US:
			gi_val = 3;
			break;
		default:
			gi_val = 0;
			break;
		}
		rate |= u32_encode_bits(gi_val, RATE_MCS_HE_GI_LTF_MSK);
		break;

	default:
		/* Unknown preamble -- fall back to 6 Mbps OFDM at 20 MHz */
		rate &= ~RATE_MCS_CHAN_WIDTH_MSK;
		rate |= RATE_MCS_MOD_TYPE_LEGACY_OFDM;
		break;
	}

	return rate;
}

/**
 * iwl_mld_wonder_tx - submit a wonder frame to the FW TX path
 * @wonder_ctx: the wonder context
 * @skb: 802.11 frame (radiotap already stripped)
 * @txd: per-frame metadata from wonder.ko (tid, is_unicast, frame_type)
 *
 * Unicast frames are routed to the destination station's single TX queue
 * (@wonder_ctx->stas[].queue_id), found by matching the RA against each
 * station's address, regardless of the frame's TID. Everything else
 * (broadcast/multicast, or unicast to an unknown station) goes out via
 * @wonder_ctx->bcast_mgmt_sta (management) or
 * @wonder_ctx->mcast_bcast_data_sta (data).
 *
 * Return: 0 on success, negative errno on failure.
 */
int iwl_mld_wonder_tx(struct iwl_mld_wonder_ctx *wonder_ctx,
		      struct sk_buff *skb,
		      const struct wonder_txd *txd)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct wondertap_fixed_tx_rate_params fixed_rate = {};
	struct iwl_mld_wonder_sta *sta = NULL;
	struct iwl_mld *mld = wonder_ctx->mld;
	struct iwl_device_tx_cmd *dev_tx_cmd;
	struct iwl_tx_cmd *tx_cmd;
	struct ieee80211_hdr *hdr;
	bool rate_adapt;
	u32 offload;
	u16 mh_len;
	u32 queue;
	u32 rate;
	int ret;

	/* Read the header in place -- iwl_trans_tx()'s transport layer
	 * expects it to still be present in skb->data.
	 */
	if (WARN_ON_ONCE(skb->len < sizeof(struct ieee80211_hdr_3addr)))
		return -EINVAL;
	hdr = (void *)skb->data;

	if (txd->is_unicast)
		sta = iwl_mld_wonder_find_sta(wonder_ctx, hdr->addr1);

	dev_tx_cmd = iwl_trans_alloc_tx_cmd(mld->trans);
	if (unlikely(!dev_tx_cmd)) {
		IWL_DEBUG_TX(mld, "wonder TX: failed to allocate tx_cmd\n");
		return -ENOMEM;
	}

	dev_tx_cmd->hdr.cmd = TX_CMD;

	tx_cmd = (void *)dev_tx_cmd->payload;
	memset(tx_cmd, 0, sizeof(*tx_cmd));

	mh_len = ieee80211_hdrlen(hdr->frame_control);

	/* Copy the 802.11 MAC header into the TX command without removing it
	 * from @skb: the transport layer re-reads it at skb->data and expects
	 * tx_cmd->len below to cover the whole frame including the header.
	 */
	if (WARN_ON_ONCE(skb->len < mh_len)) {
		iwl_trans_free_tx_cmd(mld->trans, dev_tx_cmd);
		return -EINVAL;
	}
	memcpy(tx_cmd->hdr, skb->data, mh_len);

	tx_cmd->len = cpu_to_le16((u16)skb->len);

	/* Set MH size; set pad bit for non-4B-aligned headers (e.g. QoS
	 * Data).
	 */
	offload = (mh_len / 2) << TX_CMD_OFFLD_MH_SIZE;
	if (mh_len % 4)
		offload |= BIT(TX_CMD_OFFLD_PAD);
	tx_cmd->offload_assist = cpu_to_le32(offload);

	/* Management frames: fixed 6 Mbps OFDM, unicast to the station's
	 * queue when the station is known, bcast_mgmt_sta otherwise.
	 * Legacy OFDM bits[3:0] = 0 -> 6 Mbps (see fw/api/rs.h).
	 */
	if (txd->frame_type == IEEE80211_FTYPE_MGMT) {
		rate = BIT(mld->mgmt_tx_ant) << RATE_MCS_ANT_POS |
		       RATE_MCS_MOD_TYPE_LEGACY_OFDM |
		       RATE_MCS_CHAN_WIDTH_20;
		tx_cmd->flags = cpu_to_le16(IWL_TX_FLAGS_CMD_RATE |
					    IWL_TX_FLAGS_ENCRYPT_DIS);
		tx_cmd->rate_n_flags = cpu_to_le32(rate);
		queue = sta ? sta->queue_id :
			      wonder_ctx->bcast_mgmt_sta.queue_id;
		goto submit;
	}

	/* Unicast data to a known station: its single queue, regardless of
	 * TID. Everything else (broadcast/multicast, or unicast to an
	 * unknown station) uses the MCAST station.
	 */
	queue = sta ? sta->queue_id : wonder_ctx->mcast_bcast_data_sta.queue_id;

	/* Disable FW crypto -- wonder.ko handles its own encryption.
	 * rate_adaptation_enable is only ever written by init(), before TX
	 * is possible, so it needs no lock. fixed_tx_rate can also change
	 * at runtime via set_fixed_tx_rate(), so that one is snapshotted
	 * under config_lock.
	 */
	rate_adapt = wonder_ctx->rate_adaptation_enable;
	if (!rate_adapt) {
		spin_lock(&wonder_ctx->config_lock);
		fixed_rate = wonder_ctx->fixed_tx_rate;
		spin_unlock(&wonder_ctx->config_lock);
	}

	if (!rate_adapt) {
		tx_cmd->flags = cpu_to_le16(IWL_TX_FLAGS_CMD_RATE |
					    IWL_TX_FLAGS_ENCRYPT_DIS);
		rate = iwl_mld_wonder_rate_to_fw(mld, &fixed_rate);
	} else {
		/* Rate adaptation mode: let the firmware select the rate. */
		tx_cmd->flags = cpu_to_le16(IWL_TX_FLAGS_ENCRYPT_DIS);
		rate = 0;
	}
	tx_cmd->rate_n_flags = cpu_to_le32(rate);

submit:
	/*
	 * Store a non-NULL marker in driver_data[0] so the TX completion
	 * handlers can distinguish wonder frames from mac80211 frames and
	 * avoid calling ieee80211_tx_status_skb() on them.
	 */
	memset(info, 0, sizeof(*info));
	info->driver_data[0] = wonder_ctx;
	info->driver_data[1] = dev_tx_cmd;

	if (WARN_ON_ONCE(queue == IWL_MLD_INVALID_QUEUE)) {
		iwl_trans_free_tx_cmd(mld->trans, dev_tx_cmd);
		return -ENOENT;
	}

	IWL_DEBUG_TX(mld, "wonder TX: fc=0x%04x len=%d rate=0x%08x queue=%u\n",
		     le16_to_cpu(hdr->frame_control), skb->len, rate, queue);

	ret = iwl_trans_tx(mld->trans, skb, dev_tx_cmd, queue);
	if (ret) {
		IWL_DEBUG_TX(mld, "wonder TX: iwl_trans_tx failed: %d\n", ret);
		iwl_trans_free_tx_cmd(mld->trans, dev_tx_cmd);
		return -EIO;
	}

	return 0;
}

static int iwl_mld_wonder_vht_mcs_bitmap(u8 mcs, u8 max_ch_width)
{
	int supp;

	switch (mcs) {
	case IEEE80211_VHT_MCS_SUPPORT_0_7:
		supp = BIT(IWL_TLC_MNG_HT_RATE_MCS7 + 1) - 1;
		break;
	case IEEE80211_VHT_MCS_SUPPORT_0_8:
		supp = BIT(IWL_TLC_MNG_HT_RATE_MCS8 + 1) - 1;
		break;
	case IEEE80211_VHT_MCS_SUPPORT_0_9:
		supp = BIT(IWL_TLC_MNG_HT_RATE_MCS9 + 1) - 1;
		break;
	case IEEE80211_VHT_MCS_NOT_SUPPORTED:
	default:
		return 0;
	}

	/* VHT MCS9 isn't valid at 20MHz for any NSS */
	if (max_ch_width == IWL_TLC_MNG_CH_WIDTH_20MHZ)
		supp &= ~BIT(IWL_TLC_MNG_HT_RATE_MCS9);

	return supp;
}

static __le32 iwl_mld_wonder_vht_rate_word(u8 mcs, u8 max_ch_width)
{
	return cpu_to_le32(iwl_mld_wonder_vht_mcs_bitmap(mcs, max_ch_width));
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

static bool
iwl_mld_wonder_he_capa_valid(const struct wondertap_station_info *info)
{
	return info->he_capa_len >= sizeof(info->he_capa.mac_cap_info) + 3;
}

/* wondertap gives peer caps as raw bitmasks; derive max FW channel width. */
static u8
iwl_mld_wonder_fw_bw_from_sta_bw(const struct wondertap_station_info *info,
				 enum nl80211_band band)
{
	u32 vht_chan_width = le32_to_cpu(info->vht_capa.vht_cap_info) &
			     IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK;
	bool has_ht = info->capability_mask & BIT(WONDERTAP_STATION_CAP_HT);
	bool has_vht = info->capability_mask & BIT(WONDERTAP_STATION_CAP_VHT);
	bool has_he = (info->capability_mask & BIT(WONDERTAP_STATION_CAP_HE)) &&
		      iwl_mld_wonder_he_capa_valid(info);
	bool is_2ghz = band == NL80211_BAND_2GHZ;

	if (has_he && !is_2ghz &&
	    (info->he_capa.phy_cap_info[0] &
	     IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G))
		return IWL_TLC_MNG_CH_WIDTH_160MHZ;

	if (has_vht &&
	    vht_chan_width == IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ)
		return IWL_TLC_MNG_CH_WIDTH_160MHZ;

	if (has_vht ||
	    (has_he && !is_2ghz &&
	     (info->he_capa.phy_cap_info[0] &
	      IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G)))
		return IWL_TLC_MNG_CH_WIDTH_80MHZ;

	if ((has_ht && (le16_to_cpu(info->ht_capa.cap_info) &
			IEEE80211_HT_CAP_SUP_WIDTH_20_40)) ||
	    (has_he && is_2ghz &&
	     (info->he_capa.phy_cap_info[0] &
	      IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G)))
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

/* HE stations use HE MCS rates; SGI bits don't apply when HE mode is used. */
static u8
iwl_mld_wonder_get_fw_sgi(const struct wondertap_station_info *info,
			  u8 max_ch_width, u8 mode)
{
	u8 sgi_chwidths = 0;

	if (mode == IWL_TLC_MNG_MODE_HE)
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
			     const struct wondertap_station_info *info,
			     u8 max_nss)
{
	bool has_ht = info->capability_mask & BIT(WONDERTAP_STATION_CAP_HT);
	bool has_vht = info->capability_mask & BIT(WONDERTAP_STATION_CAP_VHT);
	bool has_he = (info->capability_mask & BIT(WONDERTAP_STATION_CAP_HE)) &&
		      iwl_mld_wonder_he_capa_valid(info);
	u32 vht_cap = le32_to_cpu(info->vht_capa.vht_cap_info);
	u16 ht_cap = le16_to_cpu(info->ht_capa.cap_info);
	u8 he_phy_cap1 = info->he_capa.phy_cap_info[1];
	u8 he_phy_cap2 = info->he_capa.phy_cap_info[2];
	bool sta_supports_stbc, sta_supports_ldpc;
	u16 flags = 0;

	sta_supports_stbc =
		(has_he && (he_phy_cap2 &
			    IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ)) ||
		(has_vht && (vht_cap & IEEE80211_VHT_CAP_RXSTBC_MASK)) ||
		(has_ht && (ht_cap & IEEE80211_HT_CAP_RX_STBC));

	if (mld->cfg->ht_params.stbc && max_nss >= 2 && sta_supports_stbc)
		flags |= IWL_TLC_MNG_CFG_FLAGS_STBC_MSK;

	sta_supports_ldpc =
		(has_he && (he_phy_cap1 &
			    IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD)) ||
		(has_vht && (vht_cap & IEEE80211_VHT_CAP_RXLDPC)) ||
		(has_ht && (ht_cap & IEEE80211_HT_CAP_LDPC_CODING));

	if (mld->cfg->ht_params.ldpc && sta_supports_ldpc)
		flags |= IWL_TLC_MNG_CFG_FLAGS_LDPC_MSK;

	return cpu_to_le16(flags);
}

#define WONDER_NON_HT_RATES_OFDM \
	(IWL_RATE_BIT_MSK(6) | IWL_RATE_BIT_MSK(9) | IWL_RATE_BIT_MSK(12) | \
	 IWL_RATE_BIT_MSK(18) | IWL_RATE_BIT_MSK(24) | IWL_RATE_BIT_MSK(36) | \
	 IWL_RATE_BIT_MSK(48) | IWL_RATE_BIT_MSK(54))
#define WONDER_NON_HT_RATES_CCK \
	(IWL_RATE_BIT_MSK(1) | IWL_RATE_BIT_MSK(2) | IWL_RATE_BIT_MSK(5) | \
	 IWL_RATE_BIT_MSK(11))

static u16 iwl_mld_wonder_get_non_ht_rates(enum nl80211_band band)
{
	if (band == NL80211_BAND_2GHZ)
		return WONDER_NON_HT_RATES_CCK | WONDER_NON_HT_RATES_OFDM;

	return WONDER_NON_HT_RATES_OFDM;
}

/**
 * struct iwl_mld_wonder_tlc_limits - resolved TLC config limits for a station
 * @max_ch_width: final channel width (link width clamped by the RA mask)
 * @max_nss: final NSS ceiling (radio capability clamped by the RA mask)
 * @max_mcs: RA mask MCS ceiling, or U8_MAX when RA is disabled/unset
 * @max_preamble: RA mask preamble ceiling, or the highest preamble by default
 * @mode: resolved &enum iwl_tlc_mng_cfg_mode for this station
 */
struct iwl_mld_wonder_tlc_limits {
	u8 max_ch_width;
	u8 max_nss;
	u8 max_mcs;
	enum wondertap_rate_preamble max_preamble;
	u8 mode;
};

static struct iwl_mld_wonder_tlc_limits
iwl_mld_wonder_resolve_tlc_limits(struct iwl_mld_wonder_ctx *wonder_ctx,
				  const struct wondertap_station_info *info,
				  bool has_he, bool has_vht, bool has_ht)
{
	const struct wondertap_tx_rate_mask_params *mask =
		&wonder_ctx->tx_rate_mask;
	enum nl80211_chan_width chan_width = wonder_ctx->phy_chan_width;
	bool ra_enabled = wonder_ctx->rate_adaptation_enable;
	u8 link_bw = iwl_mld_wonder_tlc_bw_from_chan_width(chan_width);
	u8 sta_bw;
	struct iwl_mld_wonder_tlc_limits limits = {
		.max_nss = wonder_ctx->capabilities.bits.nss,
		.max_mcs = U8_MAX,
		.max_preamble = WONDERTAP_RATE_PREAMBLE_EHT,
	};

	sta_bw = iwl_mld_wonder_fw_bw_from_sta_bw(info, wonder_ctx->phy_band);
	limits.max_ch_width = min_t(u8, sta_bw, link_bw);

	/* max_bw == NONE means "unbounded"; leave max_ch_width as-is */
	if (ra_enabled && mask->max_bw != WONDERTAP_RATE_BW_NONE) {
		enum nl80211_chan_width mask_width =
			iwl_mld_wonder_bw_to_nl80211(mask->max_bw);
		u8 mask_bw = iwl_mld_wonder_tlc_bw_from_chan_width(mask_width);

		limits.max_ch_width = min_t(u8, limits.max_ch_width, mask_bw);
	}

	if (ra_enabled) {
		/* max_nss == 0 is documented as invalid; ignore it */
		if (mask->max_nss)
			limits.max_nss = min_t(u8, limits.max_nss,
					       mask->max_nss);
		limits.max_preamble = mask->max_preamble;
		limits.max_mcs = mask->max_mcs;
	}

	if (has_he && limits.max_preamble >= WONDERTAP_RATE_PREAMBLE_HE)
		limits.mode = IWL_TLC_MNG_MODE_HE;
	else if (has_vht && limits.max_preamble >= WONDERTAP_RATE_PREAMBLE_VHT)
		limits.mode = IWL_TLC_MNG_MODE_VHT;
	else if (has_ht && limits.max_preamble >= WONDERTAP_RATE_PREAMBLE_HT)
		limits.mode = IWL_TLC_MNG_MODE_HT;
	else
		limits.mode = IWL_TLC_MNG_MODE_NON_HT;

	return limits;
}

static void
iwl_mld_wonder_fill_ht_rates(struct iwl_tlc_config_cmd *cmd,
			     const struct wondertap_station_info *info,
			     const struct iwl_mld_wonder_tlc_limits *limits)
{
	u16 vht_rx_map;

	switch (limits->mode) {
	case IWL_TLC_MNG_MODE_HE:
		/* wondertap has no peer HE MCS/NSS map to derive from */
		cmd->ht_rates[IWL_TLC_NSS_1][IWL_TLC_MCS_PER_BW_80] =
			cpu_to_le32(BIT(IWL_TLC_MNG_HT_RATE_MCS7 + 1) - 1);

		if (limits->max_nss >= 2)
			cmd->ht_rates[IWL_TLC_NSS_2][IWL_TLC_MCS_PER_BW_80] =
				cpu_to_le32(BIT(IWL_TLC_MNG_HT_RATE_MCS7 + 1) -
					   1);
		break;
	case IWL_TLC_MNG_MODE_VHT:
		vht_rx_map = le16_to_cpu(info->vht_capa.supp_mcs.rx_mcs_map);

		cmd->ht_rates[IWL_TLC_NSS_1][IWL_TLC_MCS_PER_BW_80] =
			iwl_mld_wonder_vht_rate_word(vht_rx_map & 0x3,
						     limits->max_ch_width);

		/* only fill NSS_2 rates if our radio has a second chain */
		if (limits->max_nss >= 2) {
			u8 nss2_mcs = (vht_rx_map >> 2) & 0x3;
			u8 max_ch_width = limits->max_ch_width;

			cmd->ht_rates[IWL_TLC_NSS_2][IWL_TLC_MCS_PER_BW_80] =
				iwl_mld_wonder_vht_rate_word(nss2_mcs,
							     max_ch_width);
		}
		break;
	case IWL_TLC_MNG_MODE_HT:
		cmd->ht_rates[IWL_TLC_NSS_1][IWL_TLC_MCS_PER_BW_80] =
			cpu_to_le32(info->ht_capa.mcs.rx_mask[0]);

		if (limits->max_nss >= 2)
			cmd->ht_rates[IWL_TLC_NSS_2][IWL_TLC_MCS_PER_BW_80] =
				cpu_to_le32(info->ht_capa.mcs.rx_mask[1]);
		break;
	default:
		break;
	}
}

static void
iwl_mld_wonder_apply_max_mcs(struct iwl_tlc_config_cmd *cmd, u8 max_mcs,
			     u16 non_ht_rates)
{
	if (max_mcs == U8_MAX)
		return;

	if (cmd->mode == IWL_TLC_MNG_MODE_NON_HT) {
		u16 legacy_mask = 0;
		int n_set = 0;

		/* max_mcs is a legacy rate index, not a bit position */
		for (int bit = 0; bit < 16 && n_set <= max_mcs; bit++) {
			if (non_ht_rates & BIT(bit)) {
				legacy_mask |= BIT(bit);
				n_set++;
			}
		}
		cmd->non_ht_rates &= cpu_to_le16(legacy_mask);
	} else if (cmd->mode == IWL_TLC_MNG_MODE_HT) {
		/*
		 * wondertap's HT max_mcs is an absolute index (0-31)
		 * across streams; ht_rates[] is a per-NSS 0-7 bitmap,
		 * so split it into a per-stream ceiling instead of
		 * masking every NSS row alike.
		 */
		u8 max_mcs_nss = max_mcs / 8;
		u8 mcs_in_nss = max_mcs % 8;

		if (max_mcs_nss < ARRAY_SIZE(cmd->ht_rates)) {
			__le32 full_msk =
				cpu_to_le32(BIT(IWL_TLC_MNG_HT_RATE_MCS7 + 1) -
					   1);
			__le32 partial_msk =
				cpu_to_le32(BIT(mcs_in_nss + 1) - 1);
			int n_bw = ARRAY_SIZE(cmd->ht_rates[0]);

			for (int nss = 0; nss < ARRAY_SIZE(cmd->ht_rates);
			     nss++) {
				__le32 nss_msk;

				if (nss < max_mcs_nss)
					nss_msk = full_msk;
				else if (nss == max_mcs_nss)
					nss_msk = partial_msk;
				else
					nss_msk = 0;

				for (int bw = 0; bw < n_bw; bw++)
					cmd->ht_rates[nss][bw] &= nss_msk;
			}
		}
	} else {
		u8 mcs_ceil =
			min_t(u8, max_mcs, IWL_TLC_MNG_HT_RATE_MAX);
		__le32 mcs_msk = cpu_to_le32(BIT(mcs_ceil + 1) - 1);
		int n_bw = ARRAY_SIZE(cmd->ht_rates[0]);

		for (int nss = 0; nss < ARRAY_SIZE(cmd->ht_rates); nss++)
			for (int bw = 0; bw < n_bw; bw++)
				cmd->ht_rates[nss][bw] &= mcs_msk;
	}
}

int
iwl_mld_wonder_send_tlc_cfg(struct iwl_mld *mld,
			    struct iwl_mld_wonder_ctx *wonder_ctx,
			    const struct iwl_mld_wonder_sta *sta,
			    const struct wondertap_station_info *info)
{
	bool has_he = (info->capability_mask & BIT(WONDERTAP_STATION_CAP_HE)) &&
		      iwl_mld_wonder_he_capa_valid(info);
	bool has_vht = info->capability_mask & BIT(WONDERTAP_STATION_CAP_VHT);
	bool has_ht = info->capability_mask & BIT(WONDERTAP_STATION_CAP_HT);
	enum nl80211_chan_width chan_width = wonder_ctx->phy_chan_width;
	u16 non_ht = iwl_mld_wonder_get_non_ht_rates(wonder_ctx->phy_band);
	u32 cmd_id = WIDE_ID(DATA_PATH_GROUP, TLC_MNG_CONFIG_CMD);
	struct iwl_mld_wonder_tlc_limits limits;
	struct iwl_tlc_config_cmd cmd;
	u8 link_bw;
	u8 cmd_ver;

	lockdep_assert_wiphy(mld->wiphy);

	if (WARN_ON(sta->sta_id == IWL_INVALID_STA ||
		    wonder_ctx->phy_id == IWL_MLD_INVALID_FW_ID))
		return -EINVAL;

	cmd_ver = iwl_fw_lookup_cmd_ver(mld->fw, cmd_id, 0);
	if (cmd_ver != 6) {
		IWL_ERR(mld, "Unsupported TLC config cmd version %d\n",
			cmd_ver);
		return -EINVAL;
	}

	limits = iwl_mld_wonder_resolve_tlc_limits(wonder_ctx, info, has_he,
						   has_vht, has_ht);
	/* pre-RA-mask link width, kept only for the debug log below */
	link_bw = iwl_mld_wonder_tlc_bw_from_chan_width(chan_width);

	cmd = (struct iwl_tlc_config_cmd){
		.max_ch_width = limits.max_ch_width,
		.mode = limits.mode,
		.chains = iwl_mld_wonder_get_fw_chains(mld),
		.sgi_ch_width_supp =
			iwl_mld_wonder_get_fw_sgi(info, limits.max_ch_width,
						  limits.mode),
		.non_ht_rates = cpu_to_le16(non_ht),
	};

	cmd.sta_mask = cpu_to_le32(BIT(sta->sta_id));
	cmd.phy_id = cpu_to_le32(wonder_ctx->phy_id);
	cmd.flags = iwl_mld_wonder_get_tlc_flags(mld, info, limits.max_nss);

	iwl_mld_wonder_fill_ht_rates(&cmd, info, &limits);
	iwl_mld_wonder_apply_max_mcs(&cmd, limits.max_mcs, non_ht);

	IWL_DEBUG_RATE(mld,
		       "wonder: TLC cfg sta=%u mode=%u cap=0x%x phy=%u max_ch_width=%u link_bw=%u max_preamble=%u sgi=0x%x chains=0x%x flags=0x%x nss1[80]=0x%x nss1[160]=0x%x nss2[80]=0x%x nss2[160]=0x%x\n",
		       sta->sta_id, cmd.mode,
		       info->capability_mask, wonder_ctx->phy_id,
		       cmd.max_ch_width, link_bw, limits.max_preamble,
		       cmd.sgi_ch_width_supp,
		       cmd.chains, le16_to_cpu(cmd.flags),
		       le32_to_cpu(cmd.ht_rates[IWL_TLC_NSS_1][IWL_TLC_MCS_PER_BW_80]),
		       le32_to_cpu(cmd.ht_rates[IWL_TLC_NSS_1][IWL_TLC_MCS_PER_BW_160]),
		       le32_to_cpu(cmd.ht_rates[IWL_TLC_NSS_2][IWL_TLC_MCS_PER_BW_80]),
		       le32_to_cpu(cmd.ht_rates[IWL_TLC_NSS_2][IWL_TLC_MCS_PER_BW_160]));

	return iwl_mld_send_cmd_pdu(mld, cmd_id, &cmd);
}
