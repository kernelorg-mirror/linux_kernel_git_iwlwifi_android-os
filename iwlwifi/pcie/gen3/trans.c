// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2025 Intel Corporation
 */

#include <linux/pci.h>

#include "fw/api/tx.h"
#include "trans.h"

static int
iwl_construct_pcie_gen3(struct pci_dev *pdev,
			struct iwl_trans *iwl_trans,
			u8 __iomem *hw_base)
{
	struct iwl_pcie_gen3 *trans_pcie, **priv;

	trans_pcie = IWL_GET_PCIE_GEN3(iwl_trans);

	trans_pcie->hw_base = hw_base;

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

	spin_lock_init(&trans_pcie->reg_lock);

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
		       const struct iwl_mac_cfg *mac_cfg, u8 __iomem *hw_base,
		       u32 hw_rev)
{
	struct iwl_trans *iwl_trans;
	unsigned int txcmd_size = sizeof(struct iwl_tx_cmd);
	unsigned int txcmd_align = 128;
	int ret;

	txcmd_size += sizeof(struct iwl_cmd_header);
	txcmd_size += 36; /* biggest possible 802.11 header */

	/* Ensure device TX cmd cannot reach/cross a page boundary */
	if (WARN_ON(txcmd_size >= txcmd_align))
		return -EINVAL;

	iwl_trans = iwl_trans_alloc(sizeof(struct iwl_pcie_gen3),
				    &pdev->dev, mac_cfg, txcmd_size, txcmd_align);
	if (!iwl_trans)
		return -EINVAL;

	ret = iwl_construct_pcie_gen3(pdev, iwl_trans, hw_base);
	if (ret)
		goto out_free_trans;

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		/* both attempts failed: */
		if (ret) {
			dev_err(&pdev->dev, "No suitable DMA available\n");
			goto out_free_trans;
		}
	}

	/* TODO: make sure this is still needed. task=cfg */
	pci_write_config_byte(pdev, PCI_CFG_RETRY_TIMEOUT, 0x00);

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

int iwl_pcie_gen3_start_hw(struct iwl_trans *trans)
{
	/* TODO: sw_reset. */

	/* TODO: apm init. */

	/* TODO: init msix. */

	/* TODO: Check if rfkill is needed here (task=rf_kill). */

	return 0;
}

bool iwl_trans_pcie_gen3_grab_nic_access(struct iwl_trans *trans)
{
	int ret;
	struct iwl_pcie_gen3 *trans_pcie = IWL_GET_PCIE_GEN3(trans);

	if (test_bit(STATUS_TRANS_DEAD, &trans->status))
		return false;

	spin_lock(&trans_pcie->reg_lock);

	/* this bit wakes up the NIC */
	iwl_trans_set_bit(trans, CSR_GP_CNTRL,
			  CSR_GP_CNTRL_REG_FLAG_BZ_MAC_ACCESS_REQ);
	udelay(2);

	ret = iwl_poll_bits(trans, CSR_GP_CNTRL,
			    CSR_GP_CNTRL_REG_FLAG_MAC_STATUS,
			    15000);
	if (unlikely(ret < 0)) {
		u32 cntrl = iwl_read32(trans, CSR_GP_CNTRL);

		WARN_ONCE(1,
			  "Timeout waiting for hardware access (CSR_GP_CNTRL 0x%08x)\n",
			  cntrl);

		iwl_trans_pcie_dump_regs(trans, trans_pcie->pci_dev);

		if (iwlwifi_mod_params.remove_when_gone && cntrl == ~0U) {
			/* TODO: task=reset */
		} else {
			iwl_write32(trans, CSR_RESET,
				    CSR_RESET_REG_FLAG_FORCE_NMI);
		}

		spin_unlock(&trans_pcie->reg_lock);
		return false;
	}
	/*
	 * Fool sparse by faking we release the lock - sparse will
	 * track nic_access anyway.
	 */
	__release(&trans_pcie->reg_lock);
	return true;
}

void __releases(nic_access)
iwl_trans_pcie_gen3_release_nic_access(struct iwl_trans *trans)
{
	struct iwl_pcie_gen3 *trans_pcie = IWL_GET_PCIE_GEN3(trans);

	lockdep_assert_held(&trans_pcie->reg_lock);

	/*
	 * Fool sparse by faking we acquiring the lock - sparse will
	 * track nic_access anyway.
	 */
	__acquire(&trans_pcie->reg_lock);

	iwl_trans_clear_bit(trans, CSR_GP_CNTRL,
			    CSR_GP_CNTRL_REG_FLAG_BZ_MAC_ACCESS_REQ);

	__release(nic_access);
	spin_unlock(&trans_pcie->reg_lock);
}

int iwl_trans_pcie_gen3_read_mem(struct iwl_trans *trans, u32 addr,
				 void *buf, int dwords)
{
#define IWL_MAX_HW_ERRS 5
	unsigned int num_consec_hw_errors = 0;
	int offs = 0;
	u32 *vals = buf;

	/* TODO: add support for Vlab (task=vlab) */

	while (offs < dwords) {
		/* limit the time we spin here under lock to 1/2s */
		unsigned long end = jiffies + HZ / 2;
		bool resched = false;

		if (iwl_trans_grab_nic_access(trans)) {
			iwl_write32(trans, HBUS_TARG_MEM_RADDR,
				    addr + 4 * offs);

			while (offs < dwords) {
				vals[offs] = iwl_read32(trans,
							HBUS_TARG_MEM_RDAT);

				if (iwl_trans_is_hw_error_value(vals[offs]))
					num_consec_hw_errors++;
				else
					num_consec_hw_errors = 0;

				if (num_consec_hw_errors >= IWL_MAX_HW_ERRS) {
					iwl_trans_release_nic_access(trans);
					return -EIO;
				}

				offs++;

				if (time_after(jiffies, end)) {
					resched = true;
					break;
				}
			}
			iwl_trans_release_nic_access(trans);

			if (resched)
				cond_resched();
		} else {
			return -EBUSY;
		}
	}

	return 0;
}
