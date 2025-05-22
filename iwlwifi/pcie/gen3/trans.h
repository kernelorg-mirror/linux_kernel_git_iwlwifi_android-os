// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2025 Intel Corporation
 */
#ifndef __iwl_trans_pcie_gen3_h__
#define __iwl_trans_pcie_gen3_h__

#include "iwl-trans.h"
#include "pcie/utils.h"

/**
 * struct iwl_pcie_gen3 - Generation 3 PCIe transport specific data
 *
 * @trans: pointer to the generic transport
 * @napi_dev: netdev for NAPI registration
 * @pci_dev: basic pci-network driver stuff
 * @reg_lock: protect hw register access
 * @hw_base: PCI hardware address
 */
struct iwl_pcie_gen3 {
	struct iwl_trans *trans;

	struct net_device *napi_dev;
	/* PCI bus related data */
	struct pci_dev *pci_dev;
	spinlock_t reg_lock;

	u8 __iomem *hw_base;
};

int iwl_pci_gen3_probe(struct pci_dev *pdev,
		       const struct pci_device_id *ent,
		       const struct iwl_mac_cfg *mac_cfg, u8 __iomem *hw_base,
		       u32 hw_rev);

int iwl_pcie_gen3_start_hw(struct iwl_trans *trans);

static inline struct iwl_pcie_gen3 *
IWL_GET_PCIE_GEN3(struct iwl_trans *trans)
{
	return (void *)trans->trans_specific;
}

/* PCI registers */
#define PCI_CFG_RETRY_TIMEOUT	0x041

static inline void
iwl_trans_pcie_gen3_set_bits_mask(struct iwl_trans *trans, u32 reg,
				  u32 mask, u32 value)
{
	struct iwl_pcie_gen3 *trans_pcie = IWL_GET_PCIE_GEN3(trans);

	spin_lock(&trans_pcie->reg_lock);
	_iwl_trans_set_bits_mask(trans, reg, mask, value);
	spin_unlock(&trans_pcie->reg_lock);
}

#endif /* __iwl_trans_pcie_gen3_h__ */
