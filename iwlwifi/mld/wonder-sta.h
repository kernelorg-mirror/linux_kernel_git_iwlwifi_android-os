/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2026 Intel Corporation
 */
#ifndef __iwl_mld_wonder_sta_h__
#define __iwl_mld_wonder_sta_h__

#include <wondertap.h>

#include "mld.h"

struct iwl_mld_wonder_ctx;

/* Maximum number of concurrent stations wonder can maintain. */
#define IWL_MLD_WONDER_MAX_STAS 10

/**
 * struct iwl_mld_wonder_sta - one station slot
 * @addr: the station's MAC address, all-zero when the slot is free
 * @sta_id: firmware station id, or IWL_INVALID_STA when the slot is free
 * @queue_id: the station's single TX queue, used for all TIDs.
 *	IWL_MLD_INVALID_QUEUE when not yet allocated
 */
struct iwl_mld_wonder_sta {
	u8 addr[ETH_ALEN];
	u8 sta_id;
	u32 queue_id;
};

int
iwl_mld_wonder_alloc_bcast_mgmt_sta(struct iwl_mld *mld,
				    struct iwl_mld_wonder_ctx *wonder_ctx,
				    const struct wondertap_init_params *params);
int
iwl_mld_wonder_alloc_mcast_bcast_data_sta(struct iwl_mld *mld,
					  struct iwl_mld_wonder_ctx *wonder_ctx,
					  const struct wondertap_init_params *params);
void
iwl_mld_wonder_free_mcast_bcast_stas(struct iwl_mld *mld,
				     struct iwl_mld_wonder_ctx *wonder_ctx);

int iwl_mld_wonder_alloc_sta(struct iwl_mld *mld,
			     struct iwl_mld_wonder_ctx *wonder_ctx,
			     const u8 *addr);
void iwl_mld_wonder_free_sta(struct iwl_mld *mld,
			     struct iwl_mld_wonder_ctx *wonder_ctx,
			     const u8 *addr);
void iwl_mld_wonder_free_all_ucast_stas(struct iwl_mld *mld,
					struct iwl_mld_wonder_ctx *wonder_ctx);

#endif /* __iwl_mld_wonder_sta_h__ */
