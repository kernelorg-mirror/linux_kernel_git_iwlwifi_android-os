/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2026 Intel Corporation
 */
#ifndef __iwl_mld_wonder_agg_h__
#define __iwl_mld_wonder_agg_h__

#include <linux/types.h>

struct iwl_mld;
struct iwl_mld_wonder_ctx;
struct iwl_mld_wonder_rx_ba;
struct iwl_mld_wonder_sta;
struct sk_buff;
struct wiphy;
struct wiphy_work;
struct iwl_rx_mpdu_desc;

void iwl_mld_wonder_agg_work(struct wiphy *wiphy, struct wiphy_work *work);
void iwl_mld_wonder_agg_stop(struct iwl_mld *mld,
			     struct iwl_mld_wonder_sta *sta);

void iwl_mld_wonder_rx_queue_back_action(struct iwl_mld_wonder_ctx *wonder_ctx,
					 struct sk_buff *skb);

/**
 * enum iwl_mld_wonder_reorder_result - outcome of iwl_mld_wonder_reorder()
 * @IWL_MLD_WONDER_REORDER_PASS: not buffered, caller should deliver the skb
 * @IWL_MLD_WONDER_REORDER_BUFFERED: skb stored, caller must not free/deliver
 * @IWL_MLD_WONDER_REORDER_DROP: duplicate/stale skb, caller must free it
 */
enum iwl_mld_wonder_reorder_result {
	IWL_MLD_WONDER_REORDER_PASS,
	IWL_MLD_WONDER_REORDER_BUFFERED,
	IWL_MLD_WONDER_REORDER_DROP,
};

#ifdef CPTCFG_IWLMLD_WONDER

enum iwl_mld_wonder_reorder_result
iwl_mld_wonder_reorder(struct iwl_mld_wonder_ctx *wonder_ctx, int queue,
		       struct sk_buff *skb,
		       const struct iwl_rx_mpdu_desc *mpdu_desc);

bool iwl_mld_wonder_release_frames(struct iwl_mld *mld, u8 baid, u16 nssn,
				   int queue);

bool iwl_mld_wonder_del_ba(struct iwl_mld *mld, int queue, u8 baid);

#endif /* CPTCFG_IWLMLD_WONDER */

#endif /* __iwl_mld_wonder_agg_h__ */
