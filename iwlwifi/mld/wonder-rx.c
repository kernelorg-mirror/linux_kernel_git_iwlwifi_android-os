// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2026 Intel Corporation
 */

/**
 * DOC: Wonder RX data path
 *
 * Intercepts received 802.11 frames from the firmware (in iwl_mld_rx_mpdu)
 * and delivers matching frames to wondertap0 so that wonder.ko can pass them
 * to its mac80211 instance as if they had been received over the air.
 *
 * A frame is forwarded when ALL of the following hold:
 *   1. wondertap0 exists, is up, and wonder has finished initializing.
 *   2. This is the mld instance that owns wonder (wonder_ctx->mld == mld) --
 *      on a multi-NIC system, wonder is only ever bound to one of them.
 *   3. FW attributed the frame to wonder's own link (mac_phy_band link_id).
 *   4. The BSSID in addr3 matches wonder_ctx->bssid_filter.
 *   5. The destination (addr1) is broadcast/multicast, OR unicast addressed
 *      to the DUT MAC (wondertap0 dev_addr).
 *
 * Matching frames get a radiotap header prepended before being delivered via
 * netif_receive_skb(). The caller must not pass a consumed frame to mld->hw's
 * mac80211 -- wondertap0 lives on wonder.ko's own mac80211 instance.
 */

#include <linux/bitfield.h>
#include <linux/etherdevice.h>
#include <linux/ieee80211.h>
#include <linux/skbuff.h>
#include <net/ieee80211_radiotap.h>
#include <net/mac80211.h>

#include "fw/api/rx.h"
#include "mld.h"
#include "wonder.h"
#include "wonder-agg.h"
#include "wonder-rx.h"

/* Fixed noise floor estimate placed in the DBM_ANTNOISE radiotap field. */
#define IWL_MLD_WONDER_NOISE_FLOOR_DBM	(-95)

/*
 * Layout follows radiotap alignment rules relative to the start of the
 * header: tsft (u64) at offset 8, chan_freq (u16) at offset 18, etc.
 */
struct iwl_mld_wonder_radiotap {
	u8	rt_version;
	u8	rt_pad;
	__le16	rt_len;
	__le32	rt_present;
	__le64	tsft;
	u8	flags;
	u8	pad;
	__le16	chan_freq;
	__le16	chan_flags;
	s8	signal;
	s8	noise;
	u8	antenna;
} __packed;

static void
iwl_mld_wonder_fill_radiotap(struct iwl_mld_wonder_radiotap *rtap,
			     struct ieee80211_rx_status *rx_status)
{
	/* Classic radiotap has no 6 GHz band bit; treat it like 5 GHz. */
	bool is_2ghz = rx_status->band == NL80211_BAND_2GHZ;
	bool is_cck = is_2ghz && rx_status->encoding == RX_ENC_LEGACY &&
		      rx_status->rate_idx < 4;
	u16 chan_flags = (is_2ghz ? IEEE80211_CHAN_2GHZ : IEEE80211_CHAN_5GHZ) |
			 (is_cck ? IEEE80211_CHAN_CCK : IEEE80211_CHAN_OFDM);

	memset(rtap, 0, sizeof(*rtap));
	rtap->rt_version = 0;
	rtap->rt_len     = cpu_to_le16(sizeof(*rtap));
	rtap->rt_present = cpu_to_le32(BIT(IEEE80211_RADIOTAP_TSFT)          |
				       BIT(IEEE80211_RADIOTAP_FLAGS)         |
				       BIT(IEEE80211_RADIOTAP_CHANNEL)       |
				       BIT(IEEE80211_RADIOTAP_DBM_ANTSIGNAL) |
				       BIT(IEEE80211_RADIOTAP_DBM_ANTNOISE)  |
				       BIT(IEEE80211_RADIOTAP_ANTENNA));
	/* GP2 device clock, matching get_mac_tsf()'s domain. */
	rtap->tsft       = cpu_to_le64(rx_status->device_timestamp);
	rtap->flags      = (rx_status->flag & (RX_FLAG_FAILED_FCS_CRC |
					       RX_FLAG_FAILED_PLCP_CRC)) ?
			    IEEE80211_RADIOTAP_F_BADFCS : 0;
	/* rtap->pad is 0 from the memset, keeps chan_freq 2-byte aligned */
	rtap->chan_freq  = cpu_to_le16(rx_status->freq);
	rtap->chan_flags = cpu_to_le16(chan_flags);
	rtap->signal     = rx_status->signal;
	rtap->noise      = IWL_MLD_WONDER_NOISE_FLOOR_DBM;
	rtap->antenna    = rx_status->chains ? __ffs(rx_status->chains) : 0;
}

/* ADDBA request and initiator DELBA set up/tear down our RX BA session */
static bool iwl_mld_wonder_is_rx_ba_action(struct sk_buff *skb)
{
	const struct ieee80211_mgmt *mgmt = (void *)skb->data;
	u16 params;

	switch (mgmt->u.action.action_code) {
	case WLAN_ACTION_ADDBA_REQ:
		return true;
	case WLAN_ACTION_DELBA:
		if (!pskb_may_pull(skb, IEEE80211_MIN_ACTION_SIZE(delba)))
			return false;
		mgmt = (void *)skb->data;
		params = le16_to_cpu(mgmt->u.action.delba.params);
		return params & IEEE80211_DELBA_PARAM_INITIATOR_MASK;
	default:
		return false;
	}
}

bool iwl_mld_wonder_rx_frame(struct iwl_mld *mld,
			     struct sk_buff *skb,
			     struct ieee80211_rx_status *rx_status,
			     const struct iwl_rx_mpdu_desc *mpdu_desc)
{
	struct iwl_mld_wonder_ctx *wonder_ctx = &iwl_mld_wonder_ctx;
	/* netdev can be freed concurrently, see netdev_destroy(). */
	struct net_device *netdev = READ_ONCE(wonder_ctx->netdev);
	struct ieee80211_hdr *hdr = (void *)skb->data;
	struct iwl_mld_wonder_radiotap *rtap;

	if (!wonder_ctx->mld || wonder_ctx->mld != mld || !netdev ||
	    !netif_running(netdev))
		return false;

	if (u8_get_bits(mpdu_desc->mac_phy_band,
			IWL_RX_MPDU_MAC_PHY_BAND_LINK_MASK) != wonder_ctx->link_id)
		return false;

	if (IWL_FW_CHECK(mld, ieee80211_is_ctl(hdr->frame_control),
			 "wonder-rx: control frame on wonder link\n"))
		goto drop;

	/* addr3 = BSSID for both data and management frames in IBSS. */
	if (IWL_FW_CHECK(mld, !ether_addr_equal(hdr->addr3,
						wonder_ctx->bssid_filter),
			 "wonder-rx: frame on wonder link with foreign BSSID %pM\n",
			 hdr->addr3))
		goto drop;

	if (IWL_FW_CHECK(mld, !is_multicast_ether_addr(hdr->addr1) &&
			 !ether_addr_equal(hdr->addr1, netdev->dev_addr),
			 "wonder-rx: frame on wonder link not addressed to us: %pM\n",
			 hdr->addr1))
		goto drop;

	/* Consume all BACK frames: RX BA is handled here, TX BA is in FW */
	if (ieee80211_is_action(hdr->frame_control)) {
		const struct ieee80211_mgmt *mgmt;
		unsigned int min_len = IEEE80211_MIN_ACTION_SIZE(action_code);

		if (!pskb_may_pull(skb, min_len)) {
			IWL_DEBUG_RX(mld,
				     "wonder-rx: action frame too short (len=%u)\n",
				     skb->len);
			goto drop;
		}

		hdr = (void *)skb->data;
		mgmt = (const void *)hdr;

		if (mgmt->u.action.category == WLAN_CATEGORY_BACK) {
			if (!iwl_mld_wonder_is_rx_ba_action(skb))
				goto drop;
			iwl_mld_wonder_rx_queue_back_action(wonder_ctx, skb);
			return true;
		}
	}

	if (skb_cow_head(skb, sizeof(*rtap))) {
		IWL_DEBUG_RX(mld, "wonder-rx: failed to expand skb head\n");
		goto drop;
	}

	rtap = skb_push(skb, sizeof(*rtap));
	iwl_mld_wonder_fill_radiotap(rtap, rx_status);
	skb_reset_mac_header(skb);

	IWL_DEBUG_RX(mld, "wonder-rx: delivering to wondertap0: BSSID %pM\n",
		     wonder_ctx->bssid_filter);

	skb->dev = netdev;
	skb->protocol = htons(ETH_P_802_2);
	netif_receive_skb(skb);

	return true; /* consumed */

drop:
	dev_kfree_skb(skb);
	return true; /* consumed (dropped) */
}
