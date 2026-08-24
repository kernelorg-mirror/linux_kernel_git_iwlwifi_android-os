/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2026 Intel Corporation
 */
#ifndef __iwl_mld_wonder_agg_h__
#define __iwl_mld_wonder_agg_h__

#include <linux/types.h>

#include "iwl-trans.h"

struct iwl_mld;
struct iwl_mld_wonder_ctx;
struct iwl_mld_wonder_sta;
struct sk_buff;
struct napi_struct;
struct iwl_rx_mpdu_desc;

/**
 * struct iwl_mld_wonder_reorder_buf - per-queue RX reorder window state
 * @head_sn: next sequence number expected to be released
 * @num_stored: number of MPDUs currently buffered for this queue
 * @valid: true once at least one frame has been seen for this queue
 */
struct iwl_mld_wonder_reorder_buf {
	u16 head_sn;
	u16 num_stored;
	bool valid;
} ____cacheline_aligned_in_smp;

/**
 * struct iwl_mld_wonder_reorder_entry - one reorder window slot
 * @frames: skbs sharing this sequence number (more than one for A-MSDU)
 */
struct iwl_mld_wonder_reorder_entry {
	struct sk_buff_head frames;
};

/**
 * struct iwl_mld_wonder_rx_ba - per-TID RX Block Ack session state
 * @active: session is live in firmware
 * @baid: firmware BAID index assigned by RX_BAID_ALLOCATION_CONFIG_CMD
 * @dialog_token: dialog token echoed from the station's ADDBA request
 * @ssn: starting sequence number from the ADDBA request
 * @buf_size: negotiated BA window size
 * @reorder_buf: per RX-queue reorder window state
 * @entries: reorder window slots (num_rxqs * buf_size), allocated on
 *	ADDBA and freed on DELBA/teardown
 */
struct iwl_mld_wonder_rx_ba {
	bool	active;
	u8	baid;
	u8	dialog_token;
	u16	ssn;
	u16	buf_size;
	struct iwl_mld_wonder_reorder_buf reorder_buf[IWL_MAX_RX_HW_QUEUES];
	struct iwl_mld_wonder_reorder_entry *entries;
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

bool iwl_mld_wonder_owns_baid(struct iwl_mld *mld, u8 baid);

void iwl_mld_wonder_release_frames(struct iwl_mld *mld,
				   struct napi_struct *napi,
				   u8 baid, u16 nssn, int queue);

#endif /* CPTCFG_IWLMLD_WONDER */

#endif /* __iwl_mld_wonder_agg_h__ */
