/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2026 Intel Corporation
 */
#ifndef __iwl_mld_wonder_tx_h__
#define __iwl_mld_wonder_tx_h__

#include <wondertap.h>

#include "wonder.h"

int iwl_mld_wonder_tx(struct iwl_mld_wonder_ctx *wonder_ctx,
		      struct sk_buff *skb,
		      const struct wonder_txd *txd);

#endif /* __iwl_mld_wonder_tx_h__ */
