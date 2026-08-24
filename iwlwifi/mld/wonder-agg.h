/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2026 Intel Corporation
 */
#ifndef __iwl_mld_wonder_agg_h__
#define __iwl_mld_wonder_agg_h__

struct iwl_mld;
struct iwl_mld_wonder_ctx;
struct iwl_mld_wonder_sta;
struct sk_buff;

/**
 * struct iwl_mld_wonder_rx_ba - per-TID RX Block Ack session state
 * @active: session is live in firmware
 * @baid: firmware BAID index assigned by RX_BAID_ALLOCATION_CONFIG_CMD
 * @dialog_token: dialog token echoed from the station's ADDBA request
 * @ssn: starting sequence number from the ADDBA request
 * @buf_size: negotiated BA window size
 */
struct iwl_mld_wonder_rx_ba {
	bool	active;
	u8	baid;
	u8	dialog_token;
	u16	ssn;
	u16	buf_size;
};

void iwl_mld_wonder_agg_init(struct iwl_mld_wonder_ctx *wonder_ctx,
			     struct iwl_mld *mld);
void iwl_mld_wonder_agg_stop_sta(struct iwl_mld *mld,
				 struct iwl_mld_wonder_sta *sta);
void iwl_mld_wonder_agg_stop_all(struct iwl_mld_wonder_ctx *wonder_ctx);

int iwl_mld_wonder_baid_alloc(struct iwl_mld *mld, u32 sta_mask,
			      int tid, u16 ssn, u16 buf_size);
int iwl_mld_wonder_baid_free(struct iwl_mld *mld, u32 sta_mask, int tid);

void iwl_mld_wonder_rx_queue_back_action(struct iwl_mld_wonder_ctx *wonder_ctx,
					 struct sk_buff *skb);

#endif /* __iwl_mld_wonder_agg_h__ */
