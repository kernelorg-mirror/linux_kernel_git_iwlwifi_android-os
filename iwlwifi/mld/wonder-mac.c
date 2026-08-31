// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2026 Intel Corporation
 */

#include <linux/etherdevice.h>

#include "fw/api/commands.h"
#include "fw/api/context.h"
#include "fw/api/mac-cfg.h"
#include "mld.h"
#include "hcmd.h"
#include "wonder-mac.h"

IWL_MLD_ALLOC_FN_STATIC(vif, vif)

/**
 * iwl_mld_wonder_check_fw_version_supported - check MAC_CONFIG_CMD version
 * @mld: the MLD instance
 *
 * FW_MAC_TYPE_CHAN_HOP is only meaningful to firmware that supports
 * MAC_CONFIG_CMD version 4 or later; older firmware doesn't recognize the
 * mac_type value at all, so wonder must not register against it.
 *
 * Return: true if the running firmware's MAC_CONFIG_CMD version is at
 *	least 4, false otherwise.
 */
bool iwl_mld_wonder_check_fw_version_supported(struct iwl_mld *mld)
{
	u16 cmd_id = WIDE_ID(MAC_CONF_GROUP, MAC_CONFIG_CMD);

	return iwl_fw_lookup_cmd_ver(mld->fw, cmd_id, 0) >= 4;
}

static void
iwl_mld_wonder_mac_fill_gen(struct iwl_mac_config_cmd *mac_cmd,
			    const struct wondertap_init_params *params)
{
	switch (params->tx_rate.preamble) {
	case WONDERTAP_RATE_PREAMBLE_EHT:
		mac_cmd->wifi_gen.eht_support = true;
		fallthrough;
	case WONDERTAP_RATE_PREAMBLE_HE:
		mac_cmd->wifi_gen.he_support = true;
		break;
	default:
		break;
	}
}

/**
 * iwl_mld_wonder_allocate_mac_ctx - allocate a channel-hopping MAC context
 * @mld: the MLD instance
 * @wonder_ctx: the wonder context to store the MAC ID
 * @params: initialization parameters containing the MAC address
 *
 * Return: 0 on success, negative error code on failure.
 */
int iwl_mld_wonder_allocate_mac_ctx(struct iwl_mld *mld,
				    struct iwl_mld_wonder_ctx *wonder_ctx,
				    const struct wondertap_init_params *params)
{
	struct iwl_mac_config_cmd mac_cmd = {
		.action = cpu_to_le32(FW_CTXT_ACTION_ADD),
		.mac_type = cpu_to_le32(FW_MAC_TYPE_CHAN_HOP),
		.filter_flags = cpu_to_le32(MAC_CFG_FILTER_ACCEPT_GRP       |
					    MAC_CFG_FILTER_ACCEPT_BEACON     |
					    MAC_CFG_FILTER_ACCEPT_PROBE_REQ),
	};
	u8 mac_id;
	int ret;

	lockdep_assert_wiphy(mld->wiphy);

	if (WARN_ON(wonder_ctx->mac_id != IWL_MLD_INVALID_FW_ID))
		return -EINVAL;

	ret = iwl_mld_allocate_vif_fw_id(mld, &mac_id, ERR_PTR(-ENODEV));
	if (ret) {
		IWL_ERR(mld, "Failed to allocate MAC context fw id: %d\n", ret);
		return ret;
	}

	mac_cmd.id_and_color = cpu_to_le32(mac_id);
	ether_addr_copy(mac_cmd.local_mld_addr, params->mac_addr);
	iwl_mld_wonder_mac_fill_gen(&mac_cmd, params);

	ret = iwl_mld_send_cmd_pdu(mld, WIDE_ID(MAC_CONF_GROUP, MAC_CONFIG_CMD),
				   &mac_cmd);
	if (ret) {
		IWL_ERR(mld, "Failed to send MAC_CONFIG_CMD: %d\n", ret);
		RCU_INIT_POINTER(mld->fw_id_to_vif[mac_id], NULL);
		return ret;
	}

	wonder_ctx->mac_id = mac_id;
	return 0;
}

/**
 * iwl_mld_wonder_free_mac_ctx - free and remove a MAC context
 * @mld: the MLD instance
 * @wonder_ctx: the wonder context containing the MAC ID to remove
 */
void iwl_mld_wonder_free_mac_ctx(struct iwl_mld *mld,
				 struct iwl_mld_wonder_ctx *wonder_ctx)
{
	struct iwl_mac_config_cmd mac_cmd = {
		.id_and_color = cpu_to_le32(wonder_ctx->mac_id),
		.action = cpu_to_le32(FW_CTXT_ACTION_REMOVE),
		.mac_type = cpu_to_le32(FW_MAC_TYPE_CHAN_HOP),
	};

	lockdep_assert_wiphy(mld->wiphy);

	if (WARN_ON(wonder_ctx->mac_id == IWL_MLD_INVALID_FW_ID))
		return;

	iwl_mld_send_cmd_pdu(mld, WIDE_ID(MAC_CONF_GROUP, MAC_CONFIG_CMD),
			     &mac_cmd);

	RCU_INIT_POINTER(mld->fw_id_to_vif[wonder_ctx->mac_id], NULL);
	wonder_ctx->mac_id = IWL_MLD_INVALID_FW_ID;
}
