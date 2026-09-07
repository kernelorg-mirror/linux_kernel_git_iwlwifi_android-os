// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2017 Intel Deutschland GmbH
 * Copyright (C) 2019-2021, 2024-2026 Intel Corporation
 */
#include "iwl-drv.h"
#include "runtime.h"
#include "dbg.h"
#include "debugfs.h"

#include "fw/api/system.h"
#include "fw/api/commands.h"

void iwl_fw_runtime_init(struct iwl_fw_runtime *fwrt, struct iwl_trans *trans,
			const struct iwl_fw *fw,
			const struct iwl_fw_runtime_ops *ops, void *ops_ctx,
			const struct iwl_dump_sanitize_ops *sanitize_ops,
			void *sanitize_ctx,
			struct dentry *dbgfs_dir)
{
	int i;

	memset(fwrt, 0, sizeof(*fwrt));
	fwrt->wrds_table_revision = IWL_BIOS_REVISION_UNSET;
	fwrt->ewrd_table_revision = IWL_BIOS_REVISION_UNSET;
	fwrt->trans = trans;
	fwrt->fw = fw;
	fwrt->dev = trans->dev;
	fwrt->dump.conf = FW_DBG_INVALID;
	fwrt->ops = ops;
	fwrt->sanitize_ops = sanitize_ops;
	fwrt->sanitize_ctx = sanitize_ctx;
	fwrt->ops_ctx = ops_ctx;
	for (i = 0; i < IWL_FW_RUNTIME_DUMP_WK_NUM; i++) {
		fwrt->dump.wks[i].idx = i;
		INIT_DELAYED_WORK(&fwrt->dump.wks[i].wk, iwl_fw_error_dump_wk);
	}
	iwl_fwrt_dbgfs_register(fwrt, dbgfs_dir);
}
IWL_EXPORT_SYMBOL(iwl_fw_runtime_init);

/* Assumes the appropriate lock is held by the caller */
void iwl_fw_runtime_suspend(struct iwl_fw_runtime *fwrt)
{
	iwl_fw_suspend_timestamp(fwrt);
	iwl_dbg_tlv_time_point_sync(fwrt, IWL_FW_INI_TIME_POINT_HOST_D3_START,
				    NULL);
}
IWL_EXPORT_SYMBOL(iwl_fw_runtime_suspend);

void iwl_fw_runtime_resume(struct iwl_fw_runtime *fwrt)
{
	iwl_dbg_tlv_time_point(fwrt, IWL_FW_INI_TIME_POINT_HOST_D3_END, NULL);
	iwl_fw_resume_timestamp(fwrt);
}
IWL_EXPORT_SYMBOL(iwl_fw_runtime_resume);

/* set device type and latency */
int iwl_set_soc_latency(struct iwl_fw_runtime *fwrt)
{
	struct iwl_soc_configuration_cmd cmd = {};
	struct iwl_host_cmd hcmd = {
		.id = WIDE_ID(SYSTEM_GROUP, SOC_CONFIGURATION_CMD),
		.data[0] = &cmd,
		.len[0] = sizeof(cmd),
	};
	int ret;

	/*
	 * In VER_1 of this command, the discrete value is considered
	 * an integer; In VER_2, it's a bitmask.  Since we have only 2
	 * values in VER_1, this is backwards-compatible with VER_2,
	 * as long as we don't set any other bits.
	 */
	if (!fwrt->trans->mac_cfg->integrated)
		cmd.flags = cpu_to_le32(SOC_CONFIG_CMD_FLAGS_DISCRETE);

	BUILD_BUG_ON(IWL_CFG_TRANS_LTR_DELAY_NONE !=
		     SOC_FLAGS_LTR_APPLY_DELAY_NONE);
	BUILD_BUG_ON(IWL_CFG_TRANS_LTR_DELAY_200US !=
		     SOC_FLAGS_LTR_APPLY_DELAY_200);
	BUILD_BUG_ON(IWL_CFG_TRANS_LTR_DELAY_2500US !=
		     SOC_FLAGS_LTR_APPLY_DELAY_2500);
	BUILD_BUG_ON(IWL_CFG_TRANS_LTR_DELAY_1820US !=
		     SOC_FLAGS_LTR_APPLY_DELAY_1820);

	if (fwrt->trans->mac_cfg->ltr_delay != IWL_CFG_TRANS_LTR_DELAY_NONE &&
	    !WARN_ON(!fwrt->trans->mac_cfg->integrated))
		cmd.flags |= le32_encode_bits(fwrt->trans->mac_cfg->ltr_delay,
					      SOC_FLAGS_LTR_APPLY_DELAY_MASK);

	if (iwl_fw_lookup_cmd_ver(fwrt->fw, SCAN_REQ_UMAC,
				  IWL_FW_CMD_VER_UNKNOWN) >= 2 &&
	    fwrt->trans->mac_cfg->low_latency_xtal)
		cmd.flags |= cpu_to_le32(SOC_CONFIG_CMD_FLAGS_LOW_LATENCY);

	cmd.latency = cpu_to_le32(fwrt->trans->mac_cfg->xtal_latency);

	ret = iwl_trans_send_cmd(fwrt->trans, &hcmd);
	if (ret)
		IWL_ERR(fwrt, "Failed to set soc latency: %d\n", ret);
	return ret;
}
IWL_EXPORT_SYMBOL(iwl_set_soc_latency);
