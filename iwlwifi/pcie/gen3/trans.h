// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2025 Intel Corporation
 */

#include "iwl-trans.h"

/**
 * struct iwl_pcie_gen3 - Generation 3 PCIe transport specific data
 *
 * @trans: pointer to the generic transport
 */
struct iwl_pcie_gen3 {
	struct iwl_trans *trans;
};

int iwl_pci_gen3_probe(struct pci_dev *pdev,
		       const struct pci_device_id *ent,
		       const struct iwl_mac_cfg *mac_cfg);

static inline struct iwl_pcie_gen3 *
IWL_GET_PCIE_GEN3(struct iwl_trans *trans)
{
	return (void *)trans->trans_specific;
}
