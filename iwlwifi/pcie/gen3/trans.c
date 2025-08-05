// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2025 Intel Corporation
 */


#include "fw/api/tx.h"
#include "trans.h"
#include "interrupts.h"
#include "iwl-debug.h"
#include "iwl-io.h"

static int iwl_pcie_gen3_take_hw_ownership_semaphore(struct iwl_trans *trans)
{
	int err;

	IWL_DEBUG_INFO(trans,
		       "Trying to take NIC ownership semaphore from ME\n");

	iwl_set_bit(trans, CSR_HW_IF_CONFIG_REG,
		    CSR_HW_IF_CONFIG_REG_PCI_OWN_SET);

	/* Check if we succeed to take the ownership */
	err = iwl_poll_bits(trans, CSR_HW_IF_CONFIG_REG,
			    CSR_HW_IF_CONFIG_REG_PCI_OWN_SET,
			    50);

	IWL_DEBUG_INFO(trans, "Current NIC owner: %s\n", err ? "ME" : "Host");
	return err;
}

static int iwl_pcie_gen3_acquire_hw_ownership(struct iwl_trans *trans)
{
	int err;
	int overall_time = 0;
	/* Time values are specified in microseconds (us) */
	const int max_loop_time = 750000;
	const int max_overall_time = 66000000 + max_loop_time;

	/*
	 * According to the requirements, ME may own the NIC
	 * and block the driver from taking ownership.
	 * Setting WAKE_ME bit will trigger a request from ME to release the NIC
	 * There are 4 cases:
	 *
	 * 1. ME is not the owner: we get ownership immediately.
	 * 2. ME releases ownership willingly when it can.
	 * 3. ME is not responding: we wait for timeout to get the ownership.
	 * 4. ME is the owner and refuses to release ownership.
	 *
	 * It can take up to 65.5 seconds (timeout) to get a response from ME.
	 */

	while (overall_time < max_overall_time) {
		int loop_time = 0;

		IWL_DEBUG_INFO(trans, "Requesting ME to release ownership\n");

		iwl_set_bit(trans, CSR_HW_IF_CONFIG_REG,
			    CSR_HW_IF_CONFIG_REG_WAKE_ME);

		while (loop_time < max_loop_time) {
			usleep_range(200, 1000);
			loop_time += 200;

			err = iwl_pcie_gen3_take_hw_ownership_semaphore(trans);
			if (!err)
				return 0;
		}

		msleep(25);
		overall_time += 25 * 1000 + loop_time;
	}

	IWL_ERR(trans,
		"Failed to take ownership from ME, ME is the owner and refuses to release ownership.\n");

	return err;
}

int iwl_pci_gen3_probe(struct pci_dev *pdev,
		       const struct pci_device_id *ent,
		       const struct iwl_mac_cfg *mac_cfg, u8 __iomem *hw_base,
		       u32 hw_rev)
{
	WARN_ONCE(1, "%s NOT IMPLEMENTED\n", __func__);
	return -EINVAL;
}

int iwl_pcie_gen3_start_hw(struct iwl_trans *trans)
{
	IWL_ERR(trans, "%s NOT IMPLEMENTED\n", __func__);
	return -EINVAL;
}

int iwl_pcie_gen3_sw_reset(struct iwl_trans *trans, bool retake_ownership)
{
	/* Reset entire device - do controller reset (results in SHRD_HW_RST) */
	iwl_set_bit(trans, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_SW_RESET);
	usleep_range(10000 * CPTCFG_IWL_DELAY_FACTOR,
		     20000 * CPTCFG_IWL_DELAY_FACTOR);

	if (retake_ownership)
		return iwl_pcie_gen3_acquire_hw_ownership(trans);

	return 0;
}

int iwl_pcie_gen3_activate_nic(struct iwl_trans *trans)
{
	int err;

	/* Unknown W/A, leave it to avoid a risk */
	iwl_set_bit(trans, CSR_DBG_HPET_MEM_REG, CSR_DBG_HPET_MEM_REG_VAL);

	/* request MAC initialization */
	iwl_set_bit(trans, CSR_GP_CNTRL,
		    CSR_GP_CNTRL_REG_FLAG_MAC_INIT);

	/*
	 * Check the status, once it is set, we can access the MAC
	 * registers and perform operations that require MAC access,
	 * such as using iwl_write_prph() or accessing the uCode SRAM.
	 */
	err = iwl_poll_bits(trans, CSR_GP_CNTRL,
			    CSR_GP_CNTRL_REG_FLAG_MAC_STATUS,
			    25000 * CPTCFG_IWL_TIMEOUT_FACTOR);

	if (err) {
		IWL_DEBUG_INFO(trans, "Failed to initialize NIC\n");

		IWL_ERR(trans, "CSR_RESET = 0x%x\n",
			iwl_read32(trans, CSR_RESET));
	}

	return err;
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

void iwl_pcie_gen3_op_mode_enter(struct iwl_trans *trans)
{
	IWL_ERR(trans, "%s NOT IMPLEMENTED\n", __func__);
}

void iwl_pcie_gen3_op_mode_leave(struct iwl_trans *trans)
{
	IWL_ERR(trans, "%s NOT IMPLEMENTED\n", __func__);
}
