// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2025 Intel Corporation
 */

#include <linux/pci.h>

#include "interrupts.h"
#include "trans.h"

int iwl_pcie_alloc_msix_irqs(struct pci_dev *pdev,
			     struct iwl_trans *iwl_trans,
			     struct iwl_trans_info *info)
{
	struct iwl_pcie_gen3 *trans_pcie = IWL_GET_PCIE_GEN3(iwl_trans);
	int max_irqs, num_irqs;

	/* TODO: enable msi in case msix is disabled or fails, task=msi */

	if (iwlwifi_mod_params.disable_msix)
		return -EOPNOTSUPP;

	/* We want to allocate an rxq for each CPU plus a default queue (rxq 0).
	 * Every rxq should have a matching irq. Additionally, we reserve
	 * another irq for non rx interrupts.
	 */
	max_irqs = min_t(u32, num_online_cpus() + 2, IWL_MAX_RX_HW_QUEUES);
	for (int i = 0; i < max_irqs; i++)
		trans_pcie->msix.entries[i].entry = i;

	num_irqs = pci_enable_msix_range(pdev, trans_pcie->msix.entries,
					 MSIX_MIN_INTERRUPT_VECTORS,
					 max_irqs);
	if (num_irqs < 0) {
		IWL_DEBUG_ISR(iwl_trans,
			      "Failed to enable msi-x mode (ret %d).\n",
			      num_irqs);
		return -ENODEV;
	}

	IWL_DEBUG_ISR(iwl_trans,
		      "MSI-X enabled. %d irqs were allocated\n",
		      num_irqs);

	/* In case the OS provides fewer irqs than requested, different
	 * causes will share the default rxq's irq as follows:
	 * One irq less: non rx causes shared with default rxq.
	 * Two irqs less: non rx causes shared with default rxq and RSS #1.
	 * More than two irq: we will use fewer RSS queues.
	 */
	trans_pcie->msix.shared_irq_mask = IWL_GEN3_SHARED_IRQ_DEFAULT_RXQ;
	if (num_irqs == max_irqs) {
		info->num_rxqs = num_irqs - 1;
	} else if (num_irqs == max_irqs - 1) {
		info->num_rxqs = num_irqs;
		trans_pcie->msix.shared_irq_mask |= IWL_GEN3_SHARED_IRQ_NON_RX;
	} else {
		info->num_rxqs = num_irqs + 1;
		trans_pcie->msix.shared_irq_mask |= IWL_GEN3_SHARED_IRQ_NON_RX |
			IWL_GEN3_SHARED_IRQ_FIRST_RXQ;
	}

	/* If at least one of the irqs is shared, non rx irq is shared
	 * with the default rxq.
	 */
	trans_pcie->msix.non_rx_irq = (num_irqs == max_irqs) ? num_irqs - 1 :
		DEFAULT_RXQ;

	WARN_ON(info->num_rxqs > IWL_MAX_RX_HW_QUEUES);

	IWL_DEBUG_ISR(iwl_trans,
		      "MSI-X enabled. rx queues=%d, shared_irq_mask=0x%x\n",
		      info->num_rxqs, trans_pcie->msix.shared_irq_mask);

	trans_pcie->msix.is_enabled = true;
	trans_pcie->msix.alloc_irqs = num_irqs;
	return 0;
}
