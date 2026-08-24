/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2026 Intel Corporation
 */
#ifndef __iwl_mld_wonder_agg_h__
#define __iwl_mld_wonder_agg_h__

struct iwl_mld;
struct iwl_mld_wonder_ctx;
struct iwl_mld_wonder_rx_ba;
struct iwl_mld_wonder_sta;
struct sk_buff;
struct wiphy;
struct wiphy_work;

void iwl_mld_wonder_agg_work(struct wiphy *wiphy, struct wiphy_work *work);
void iwl_mld_wonder_agg_stop(struct iwl_mld *mld,
			     struct iwl_mld_wonder_sta *sta);

void iwl_mld_wonder_rx_queue_back_action(struct iwl_mld_wonder_ctx *wonder_ctx,
					 struct sk_buff *skb);

#endif /* __iwl_mld_wonder_agg_h__ */
