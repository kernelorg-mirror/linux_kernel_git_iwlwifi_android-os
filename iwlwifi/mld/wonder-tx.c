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

#include "fw/api/rs.h"
#include "fw/api/tx.h"
#include "mld.h"
#include "wonder.h"
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
