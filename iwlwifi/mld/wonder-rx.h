/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2026 Intel Corporation
 */
#ifndef __iwl_mld_wonder_rx_h__
#define __iwl_mld_wonder_rx_h__

struct iwl_mld;
struct sk_buff;
struct ieee80211_rx_status;
struct iwl_rx_mpdu_desc;
struct iwl_mld_wonder_ctx;

/**
 * iwl_mld_wonder_rx_frame - attempt to deliver an RX frame to wonder.ko
 * @mld: the MLD instance
 * @skb: the received 802.11 MPDU (no radiotap header yet; skb->data points
 *	to the 802.11 MAC header)
 * @rx_status: the mac80211 rx_status already populated in the skb CB
 * @queue: the RX queue the frame arrived on
 * @mpdu_desc: the firmware MPDU descriptor for the frame
 *
 * Delivers the frame to wondertap0 (with a radiotap header prepended) when
 * wonder is up and the frame's BSSID (addr3) matches the configured filter
 * and its destination is broadcast/multicast or the DUT's own address.
 * Data frames may be buffered for in-driver A-MPDU reordering first.
 *
 * Return: true if the frame was consumed (caller must not pass it to mld's
 *	mac80211), false otherwise.
 */
bool iwl_mld_wonder_rx_frame(struct iwl_mld *mld,
			     struct sk_buff *skb,
			     struct ieee80211_rx_status *rx_status,
			     int queue,
			     const struct iwl_rx_mpdu_desc *mpdu_desc);

/* Radiotap-wrap and deliver skb to wondertap0. */
void iwl_mld_wonder_deliver_skb(struct iwl_mld_wonder_ctx *wonder_ctx,
				struct sk_buff *skb);
#endif /* __iwl_mld_wonder_rx_h__ */
