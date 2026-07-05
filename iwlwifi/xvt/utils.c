// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2005-2014, 2018-2021, 2025-2026 Intel Corporation
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#include "iwl-debug.h"
#include "iwl-trans.h"

#include "fw-api.h"
#include "xvt.h"
#include "fw/dbg.h"

int iwl_xvt_send_cmd(struct iwl_xvt *xvt, struct iwl_host_cmd *cmd)
{
	/*
	 * Synchronous commands from this op-mode must hold
	 * the mutex, this ensures we don't try to send two
	 * (or more) synchronous commands at a time.
	 */
	if (!(cmd->flags & CMD_ASYNC))
		lockdep_assert_held(&xvt->mutex);

	return iwl_trans_send_cmd(xvt->trans, cmd);
}

int iwl_xvt_send_cmd_pdu(struct iwl_xvt *xvt, u32 id,
			 u32 flags, u16 len, const void *data)
{
	struct iwl_host_cmd cmd = {
		.id = id,
		.len = { len, },
		.data = { data, },
		.flags = flags,
	};

	return iwl_xvt_send_cmd(xvt, &cmd);
}

struct iwl_xvt_txq_alloc_data {
	u32 flags;
	u32 sta_mask;
	u8 tid;
};

static int iwl_xvt_txq_alloc_cmd(struct iwl_op_mode *op_mode,
				 const struct iwl_txq_alloc_info *info,
				 struct iwl_txq_alloc_result *result,
				 void *data)
{
	const struct iwl_xvt_txq_alloc_data *params = data;
	struct iwl_xvt *xvt = IWL_OP_MODE_GET_XVT(op_mode);
	u8 cmd_ver = iwl_fw_lookup_cmd_ver(xvt->fw,
					   WIDE_ID(DATA_PATH_GROUP,
						   SCD_QUEUE_CONFIG_CMD), 0);
	union {
		struct iwl_tx_queue_cfg_cmd old;
		struct iwl_scd_queue_cfg_cmd new;
	} cmd = {};
	struct iwl_host_cmd hcmd = {
		.flags = CMD_WANT_SKB,
	};
	struct iwl_tx_queue_cfg_rsp *rsp;
	int ret;

	if (cmd_ver == 0) {
		if (hweight32(params->sta_mask) != 1)
			return -EINVAL;

		cmd.old.tfdq_addr = cpu_to_le64(info->tfdq_dram_addr);
		cmd.old.byte_cnt_addr = cpu_to_le64(info->bc_dram_addr);
		cmd.old.cb_size = cpu_to_le32(info->cb_size);
		cmd.old.flags = cpu_to_le16(params->flags |
					    TX_QUEUE_CFG_ENABLE_QUEUE);
		cmd.old.tid = params->tid;
		cmd.old.sta_id = ffs(params->sta_mask) - 1;

		hcmd.id = SCD_QUEUE_CFG;
		hcmd.len[0] = sizeof(cmd.old);
		hcmd.data[0] = &cmd.old;
	} else if (cmd_ver == 3 || cmd_ver == 4) {
		cmd.new.operation = cpu_to_le32(IWL_SCD_QUEUE_ADD);
		cmd.new.u.add.tfdq_dram_addr =
			cpu_to_le64(info->tfdq_dram_addr);
		cmd.new.u.add.bc_dram_addr = cpu_to_le64(info->bc_dram_addr);
		cmd.new.u.add.cb_size = cpu_to_le32(info->cb_size);
		cmd.new.u.add.flags = cpu_to_le32(params->flags);
		cmd.new.u.add.sta_mask = cpu_to_le32(params->sta_mask);
		cmd.new.u.add.tid = params->tid;

		hcmd.id = WIDE_ID(DATA_PATH_GROUP, SCD_QUEUE_CONFIG_CMD);
		hcmd.len[0] = sizeof(cmd.new);
		hcmd.data[0] = &cmd.new;
	} else {
		return -EOPNOTSUPP;
	}

	ret = iwl_xvt_send_cmd(xvt, &hcmd);
	if (ret)
		return ret;

	if (WARN_ON(iwl_rx_packet_payload_len(hcmd.resp_pkt) != sizeof(*rsp))) {
		ret = -EINVAL;
		goto out;
	}

	rsp = (void *)hcmd.resp_pkt->data;
	result->qid = le16_to_cpu(rsp->queue_number);
	result->wr_ptr = le16_to_cpu(rsp->write_pointer);

out:
	iwl_free_resp(&hcmd);
	return ret;
}

int iwl_xvt_alloc_fw_txq(struct iwl_xvt *xvt, u32 flags, u32 sta_mask,
			 u8 tid, int size, unsigned int timeout)
{
	struct iwl_xvt_txq_alloc_data data = {
		.flags = flags,
		.sta_mask = sta_mask,
		.tid = tid,
	};

	return iwl_trans_txq_alloc(xvt->trans, size, timeout,
				   iwl_xvt_txq_alloc_cmd, &data);
}

void iwl_xvt_get_nic_error_log_v2(struct iwl_xvt *xvt,
				  struct iwl_error_event_table_v2 *table)
{
	struct iwl_trans *trans = xvt->trans;
	u32 base = xvt->trans->dbg.lmac_error_event_table[0];
	/* TODO: support CDB */

	if (xvt->fwrt.cur_fw_img == IWL_UCODE_INIT) {
		if (!base)
			base = xvt->fw->init_errlog_ptr;
	} else {
		if (!base)
			base = xvt->fw->inst_errlog_ptr;
	}

	iwl_trans_read_mem_bytes(trans, base, table, sizeof(*table));
}

void iwl_xvt_dump_nic_error_log_v2(struct iwl_xvt *xvt,
				   struct iwl_error_event_table_v2 *table)
{
	IWL_ERR(xvt, "0x%08X | %-28s\n", table->error_id,
		iwl_fw_lookup_assert_desc(table->error_id));
	IWL_ERR(xvt, "0x%08X | trm_hw_status0\n", table->trm_hw_status0);
	IWL_ERR(xvt, "0x%08X | trm_hw_status1\n", table->trm_hw_status1);
	IWL_ERR(xvt, "0x%08X | branchlink2\n", table->blink2);
	IWL_ERR(xvt, "0x%08X | interruptlink1\n", table->ilink1);
	IWL_ERR(xvt, "0x%08X | interruptlink2\n", table->ilink2);
	IWL_ERR(xvt, "0x%08X | data1\n", table->data1);
	IWL_ERR(xvt, "0x%08X | data2\n", table->data2);
	IWL_ERR(xvt, "0x%08X | data3\n", table->data3);
	IWL_ERR(xvt, "0x%08X | beacon time\n", table->bcon_time);
	IWL_ERR(xvt, "0x%08X | tsf low\n", table->tsf_low);
	IWL_ERR(xvt, "0x%08X | tsf hi\n", table->tsf_hi);
	IWL_ERR(xvt, "0x%08X | time gp1\n", table->gp1);
	IWL_ERR(xvt, "0x%08X | time gp2\n", table->gp2);
	IWL_ERR(xvt, "0x%08X | uCode revision type\n", table->fw_rev_type);
	IWL_ERR(xvt, "0x%08X | uCode version major\n", table->major);
	IWL_ERR(xvt, "0x%08X | uCode version minor\n", table->minor);
	IWL_ERR(xvt, "0x%08X | hw version\n", table->hw_ver);
	IWL_ERR(xvt, "0x%08X | board version\n", table->brd_ver);
	IWL_ERR(xvt, "0x%08X | hcmd\n", table->hcmd);
	IWL_ERR(xvt, "0x%08X | isr0\n", table->isr0);
	IWL_ERR(xvt, "0x%08X | isr1\n", table->isr1);
	IWL_ERR(xvt, "0x%08X | isr2\n", table->isr2);
	IWL_ERR(xvt, "0x%08X | isr3\n", table->isr3);
	IWL_ERR(xvt, "0x%08X | isr4\n", table->isr4);
	IWL_ERR(xvt, "0x%08X | last cmd Id\n", table->last_cmd_id);
	IWL_ERR(xvt, "0x%08X | wait_event\n", table->wait_event);
	IWL_ERR(xvt, "0x%08X | l2p_control\n", table->l2p_control);
	IWL_ERR(xvt, "0x%08X | l2p_duration\n", table->l2p_duration);
	IWL_ERR(xvt, "0x%08X | l2p_mhvalid\n", table->l2p_mhvalid);
	IWL_ERR(xvt, "0x%08X | l2p_addr_match\n", table->l2p_addr_match);
	IWL_ERR(xvt, "0x%08X | lmpm_pmg_sel\n", table->lmpm_pmg_sel);
	IWL_ERR(xvt, "0x%08X | timestamp\n", table->u_timestamp);
	IWL_ERR(xvt, "0x%08X | flow_handler\n", table->flow_handler);
}

void iwl_xvt_get_umac_error_log(struct iwl_xvt *xvt,
				struct iwl_umac_error_event_table *table)
{
	struct iwl_trans *trans = xvt->trans;
	u32 base = xvt->trans->dbg.umac_error_event_table;

	iwl_trans_read_mem_bytes(trans, base, table, sizeof(*table));
}

void iwl_xvt_dump_umac_error_log(struct iwl_xvt *xvt,
				 struct iwl_umac_error_event_table *table)
{
	IWL_ERR(xvt, "0x%08X | %s\n", table->error_id,
		iwl_fw_lookup_assert_desc(table->error_id));
	IWL_ERR(xvt, "0x%08X | umac branchlink1\n", table->blink1);
	IWL_ERR(xvt, "0x%08X | umac branchlink2\n", table->blink2);
	IWL_ERR(xvt, "0x%08X | umac interruptlink1\n", table->ilink1);
	IWL_ERR(xvt, "0x%08X | umac interruptlink2\n", table->ilink2);
	IWL_ERR(xvt, "0x%08X | umac data1\n", table->data1);
	IWL_ERR(xvt, "0x%08X | umac data2\n", table->data2);
	IWL_ERR(xvt, "0x%08X | umac data3\n", table->data3);
	IWL_ERR(xvt, "0x%08X | umac major\n", table->umac_major);
	IWL_ERR(xvt, "0x%08X | umac minor\n", table->umac_minor);
	IWL_ERR(xvt, "0x%08X | frame pointer\n", table->frame_pointer);
	IWL_ERR(xvt, "0x%08X | stack pointer\n", table->stack_pointer);
	IWL_ERR(xvt, "0x%08X | last host cmd\n", table->cmd_header);
	IWL_ERR(xvt, "0x%08X | isr status reg\n", table->nic_isr_pref);
}
