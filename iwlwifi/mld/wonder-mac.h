/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2026 Intel Corporation
 */
#ifndef __iwl_mld_wonder_mac_h__
#define __iwl_mld_wonder_mac_h__

#include "mld.h"
#include "wonder.h"

int iwl_mld_wonder_allocate_mac_ctx(struct iwl_mld *mld,
				    struct iwl_mld_wonder_ctx *wonder_ctx,
				    const struct wondertap_init_params *params);
void iwl_mld_wonder_free_mac_ctx(struct iwl_mld *mld,
				 struct iwl_mld_wonder_ctx *wonder_ctx);

bool iwl_mld_wonder_check_fw_version_supported(struct iwl_mld *mld);

#endif /* __iwl_mld_wonder_mac_h__ */
