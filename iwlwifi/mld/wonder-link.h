/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2026 Intel Corporation
 */
#ifndef __iwl_mld_wonder_link_h__
#define __iwl_mld_wonder_link_h__

#include "mld.h"
#include "wonder.h"

int
iwl_mld_wonder_allocate_link(struct iwl_mld *mld,
			     struct iwl_mld_wonder_ctx *wonder_ctx,
			     const struct wondertap_init_params *params);
void iwl_mld_wonder_free_link(struct iwl_mld *mld,
			      struct iwl_mld_wonder_ctx *wonder_ctx);
#endif /* __iwl_mld_wonder_link_h__ */
