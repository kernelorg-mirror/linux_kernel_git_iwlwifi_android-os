// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2025 Intel Corporation
 */

#include <linux/pci.h>

#include "trans.h"

int iwl_pci_gen3_probe(struct pci_dev *pdev,
		       const struct pci_device_id *ent,
		       const struct iwl_mac_cfg *mac_cfg)
{
	IWL_ERR_DEV(&pdev->dev, "NOT IMPLEMENTED YET: %s\n", __func__);

	/* TODO: pci_assign_resource PM bug */
	/* TODO: pcim_enable_device */
	/* TODO: allocate transport */
	/* TODO: Initialize the wait queue for commands */
	/* TODO: assign txqs.tfd, bc_tbl_size, bc_pool parameters */
	/* TODO: assign dma mask, tso_hdr_page */
	/* TODO: Init NAPI */
	/* TODO: Init locks and waitq */
	/* TODO: Init rx allocator work */
	/* TODO: pci_set_master */
	/* TODO: dma_set_mask_and_coherent */
	/* TODO: pcim_request_all_regions */
	/* TODO: hw_base = pcim_iomap */
	/* TODO: PCI_CFG_RETRY_TIMEOUT disable */
	/* TODO: iwl_disable_interrupts */
	/* TODO: read hw_rev and step */
	/* TODO: iwl_pcie_set_interrupt_capa */
	/* TODO: init sx_waitq */
	/* TODO: iwl_pcie_alloc_invalid_tx_cmd */
	/* TODO: iwl_pcie_init_msix_handler */
	/* TODO: iwl_dbg_tlv_init */
	/* TODO: check_product_reset status and mode */
	/* TODO: handle pcie info struct */
	/* TODO: get_crf_id: prepare_card_hw,finish_nic_init,grab_nic_access */
	/* TODO: find_dev_info - iwl_dev_info */
	/* TODO: link status for discrete case */
	/* TODO: iwl_trans_init */
	/* TODO: pci_set_drvdata */
	/* TODO: check_me_status */
	/* TODO: iwl_pcie_prepare_card_hw again? */
	/* TODO: drv_start */
	/* TODO: pcie_dbgfs_register */

	return 0;
}
