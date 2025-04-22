// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2025 Intel Corporation
 */

#include <linux/pci.h>

#include "fw/api/tx.h"
#include "trans.h"

static int
iwl_construct_pcie_gen3(struct pci_dev *pdev,
			struct iwl_trans *iwl_trans)
{
	struct iwl_pcie_gen3 *trans_pcie, **priv;

	/* TODO: pci_assign_resource PM bug */
	trans_pcie = IWL_GET_PCIE_GEN3(iwl_trans);

	trans_pcie->hw_base = pcim_iomap(pdev, 0, 0);
	if (!trans_pcie->hw_base) {
		dev_err(&pdev->dev, "Could not ioremap PCI BAR 0.\n");
		return -ENODEV;
	}

	/* TODO: disable interrupts */
	/* TODO: assign num_rx_bufs */

	trans_pcie->napi_dev =
		alloc_netdev_dummy(sizeof(struct iwl_pcie_gen3 *));
	if (!trans_pcie->napi_dev)
		return -ENODEV;

	/* The private struct in netdev is a pointer to struct iwl_pcie_gen3 */
	priv = netdev_priv(trans_pcie->napi_dev);
	*priv = trans_pcie;

	trans_pcie->pci_dev = pdev;

	return 0;
}

static void
iwl_pcie_gen3_free(struct iwl_trans *iwl_trans)
{
	struct iwl_pcie_gen3 *trans_pcie = IWL_GET_PCIE_GEN3(iwl_trans);

	free_netdev(trans_pcie->napi_dev);
	iwl_trans_free(iwl_trans);
}

int iwl_pci_gen3_probe(struct pci_dev *pdev,
		       const struct pci_device_id *ent,
		       const struct iwl_mac_cfg *mac_cfg)
{
	struct iwl_trans *iwl_trans;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	pci_set_master(pdev);

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		/* both attempts failed: */
		if (ret) {
			dev_err(&pdev->dev, "No suitable DMA available\n");
			return ret;
		}
	}

	ret = pcim_request_all_regions(pdev, DRV_NAME);
	if (ret) {
		dev_err(&pdev->dev, "Requesting all PCI BARs failed.\n");
		return ret;
	}

	/* TODO: make sure this is still needed. task=cfg */
	pci_write_config_byte(pdev, PCI_CFG_RETRY_TIMEOUT, 0x00);

	iwl_trans = iwl_trans_alloc(sizeof(struct iwl_pcie_gen3),
				    &pdev->dev, mac_cfg);
	if (!iwl_trans)
		return -EINVAL;

	ret = iwl_construct_pcie_gen3(pdev, iwl_trans);
	if (ret)
		goto out_free_trans;

	/* TODO: Handle info */
	/* TODO: assign and allocate txqs parameters (tfd, cmd, tso, bc) */
	/* TODO: max_skb_frags to iwl_trans */
	/* TODO: init rx */
	/* TODO: unset debug_rfkill */
	/* TODO: read hw_rev and step */
	/* TODO: set interrupt capa */
	/* TODO: alloc invalid tx cmd */
	/* TODO: init msix handler */
	/* TODO: debugfs state and fw continuous recording data */
	/* TODO: check_product_reset status and mode */
	/* TODO: handle pcie info struct */
	/* TODO: get_crf_id: prepare_card_hw,finish_nic_init,grab_nic_access */
	/* TODO: find_dev_info - iwl_dev_info */
	/* TODO: link status for discrete case */
	/* TODO: check_me_status */
	/* TODO: pcie_dbgfs_register */
	/* TODO: iwl_pcie_prepare_card_hw */

	iwl_dbg_tlv_init(iwl_trans);

	ret = iwl_trans_init(iwl_trans, sizeof(struct iwl_tx_cmd), 128);
	if (ret)
		goto out_free_trans;

	pci_set_drvdata(pdev, iwl_trans);

	iwl_trans->drv = iwl_drv_start(iwl_trans);

	if (IS_ERR(iwl_trans->drv)) {
		ret = PTR_ERR(iwl_trans->drv);
		goto out_free_trans;
	}

	return 0;

out_free_trans:
	iwl_pcie_gen3_free(iwl_trans);
	return ret;
}
