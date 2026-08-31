// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2026 Intel Corporation
 */

/**
 * DOC: Wonder WiFi virtual device integration for the MLD driver.
 *
 * This file is compiled only when CPTCFG_IWLMLD_WONDER is selected.
 *
 * iwlmld acts as the device *publisher*: it exposes a wondertap_aux_dev on
 * the auxiliary bus so that wonder_driver (the consumer) will be probed on
 * it and can use iwlwifi as the underlying radio.
 *
 * The auxiliary-device match name is derived from the publisher module name
 * and adev.name, so this driver (built into iwlmld.ko) publishes
 * "iwlmld.wondertap.<id>", matching wonder_driver's "iwlmld.wondertap"
 * id_table entry.
 */
#include <linux/auxiliary_bus.h>
#include <linux/netdevice.h>
#include <wondertap.h>

#include "fw/api/commands.h"
#include "fw/api/mac-cfg.h"
#include "hcmd.h"
#include "mld.h"
#include "wonder-mac.h"
#include "wonder-link.h"
#include "wonder-sta.h"
#include "wonder.h"
#include "wonder-phy.h"

static const struct net_device_ops iwl_mld_wonder_netdev_ops = {
};

static void iwl_mld_wonder_netdev_setup(struct net_device *dev)
{
	/* 0 = ARPHRD_UNSPEC; ARPHRD_ETHER is prohibited here */
	dev->type = 0;
	dev->hard_header_len = 0;
	dev->addr_len = ETH_ALEN;
	dev->netdev_ops = &iwl_mld_wonder_netdev_ops;
}

static int iwl_mld_wonder_netdev_create(struct iwl_mld_wonder_ctx *wonder_ctx)
{
	struct iwl_mld *mld = wonder_ctx->mld;
	struct net_device *dev;
	int ret;

	if (WARN_ON(wonder_ctx->netdev))
		return -EINVAL;

	dev = alloc_netdev(0, "wondertap0", NET_NAME_PREDICTABLE,
			   iwl_mld_wonder_netdev_setup);
	if (!dev) {
		IWL_ERR(mld, "Failed to allocate wondertap0 netdev\n");
		return -ENOMEM;
	}

	SET_NETDEV_DEV(dev, mld->trans->dev);

	ret = register_netdev(dev);
	if (ret) {
		IWL_ERR(mld, "Failed to register wondertap0 netdev: %d\n", ret);
		free_netdev(dev);
		return ret;
	}

	wonder_ctx->netdev = dev;
	return 0;
}

static void iwl_mld_wonder_netdev_destroy(struct iwl_mld_wonder_ctx *wonder_ctx)
{
	if (WARN_ON(!wonder_ctx->netdev))
		return;

	unregister_netdev(wonder_ctx->netdev);
	free_netdev(wonder_ctx->netdev);
	wonder_ctx->netdev = NULL;
}

static struct iwl_mld_wonder_ctx iwl_mld_wonder_ctx = {
	.phy_id = IWL_MLD_INVALID_FW_ID,
	.mac_id = IWL_MLD_INVALID_FW_ID,
	.link_id = IWL_MLD_INVALID_FW_ID,
	.bcast_mgmt_sta = {
		.sta_id = IWL_INVALID_STA,
		.queue_id = IWL_MLD_INVALID_QUEUE,
	},
	.mcast_bcast_data_sta = {
		.sta_id = IWL_INVALID_STA,
		.queue_id = IWL_MLD_INVALID_QUEUE,
	},
	.stas = {
		[0 ... IWL_MLD_WONDER_MAX_STAS - 1] = {
			.sta_id = IWL_INVALID_STA,
			.queue_id = IWL_MLD_INVALID_QUEUE,
		},
	},
};

static int iwl_mld_wondertap_get_capabilities(void *handle,
					      struct wondertap_capability *cap)
{
	memset(cap, 0, sizeof(*cap));

	cap->bits.channel_hopping = 1;

	return 0;
}

static int iwl_mld_wondertap_init(void **handle,
				  const struct wondertap_init_params *params)
{
	struct iwl_mld_wonder_ctx *wonder_ctx = &iwl_mld_wonder_ctx;
	struct iwl_mld *mld = wonder_ctx->mld;
	int ret;

	if (WARN_ON(!params))
		return -EINVAL;

	if (WARN_ON(!mld))
		return -ENODEV;

	/* wonder.ko calls this with its own (separate) wiphy mutex held. */
	guard(nested_wiphy)(mld->wiphy);

	if (wonder_ctx->netdev)
		eth_hw_addr_set(wonder_ctx->netdev, params->mac_addr);

	if (WARN_ON(!mld->fw_status.running))
		return -ENODEV;

	if (WARN_ON(wonder_ctx->phy_id != IWL_MLD_INVALID_FW_ID))
		return -EINVAL;

	ether_addr_copy(wonder_ctx->bssid_filter, params->bssid);

	ret = iwl_mld_wonder_allocate_phy_ctx(mld, wonder_ctx, params);
	if (ret)
		return ret;

	ret = iwl_mld_wonder_allocate_mac_ctx(mld, wonder_ctx, params);
	if (ret)
		goto free_phy;

	ret = iwl_mld_wonder_allocate_link(mld, wonder_ctx, params);
	if (ret)
		goto free_mac;

	ret = iwl_mld_wonder_alloc_bcast_mgmt_sta(mld, wonder_ctx, params);
	if (ret)
		goto free_link;

	ret = iwl_mld_wonder_alloc_mcast_bcast_data_sta(mld, wonder_ctx,
							params);
	if (ret)
		goto free_stas;

	*handle = wonder_ctx;
	return 0;

free_stas:
	iwl_mld_wonder_free_mcast_bcast_stas(mld, wonder_ctx);
free_link:
	iwl_mld_wonder_free_link(mld, wonder_ctx);
free_mac:
	iwl_mld_wonder_free_mac_ctx(mld, wonder_ctx);
free_phy:
	iwl_mld_wonder_free_phy_ctx(mld, wonder_ctx);
	return ret;
}

static void
iwl_mld_wondertap_deinit(void *handle,
			 const struct wondertap_deinit_params *params)
{
	struct iwl_mld_wonder_ctx *wonder_ctx = handle;
	struct iwl_mld *mld = wonder_ctx->mld;

	if (WARN_ON(!mld))
		return;

	/* See iwl_mld_wondertap_init() for why this is nested. */
	guard(nested_wiphy)(mld->wiphy);

	WARN_ON(!mld->fw_status.running);

	iwl_mld_wonder_free_all_ucast_stas(mld, wonder_ctx);
	iwl_mld_wonder_free_mcast_bcast_stas(mld, wonder_ctx);

	if (wonder_ctx->link_id != IWL_MLD_INVALID_FW_ID)
		iwl_mld_wonder_free_link(mld, wonder_ctx);

	if (wonder_ctx->mac_id != IWL_MLD_INVALID_FW_ID)
		iwl_mld_wonder_free_mac_ctx(mld, wonder_ctx);

	if (wonder_ctx->phy_id != IWL_MLD_INVALID_FW_ID)
		iwl_mld_wonder_free_phy_ctx(mld, wonder_ctx);
}

/* GP2 free-running timer, read directly from the periphery register. */
static int iwl_mld_wondertap_get_mac_tsf(void *handle, u32 *mac_tsf)
{
	struct iwl_mld_wonder_ctx *wonder_ctx = handle;
	struct iwl_mld *mld = wonder_ctx->mld;

	if (WARN_ON(!mld))
		return -ENODEV;

	guard(nested_wiphy)(mld->wiphy);

	if (WARN_ON(!mld->fw_status.running))
		return -EIO;

	*mac_tsf = iwl_read_prph(mld->trans,
				 mld->trans->mac_cfg->base->gp2_reg_addr);
	if (*mac_tsf == 0x5a5a5a5a)
		return -EIO;

	return 0;
}

#define IWL_WONDER_MAX_CHANNEL_LIST_LEN 32
#define IWL_WONDER_MAX_DWELL_TIME_TU 64
#define IWL_WONDER_MIN_DWELL_TIME_TU 8

/**
 * iwl_mld_wondertap_channel_schedule_request - send channel hopping cmd to FW
 * @handle: opaque vendor driver instance handle
 * @request: channel schedule request parameters from wonder
 *
 * Per-slot freq/bandwidth in @request are not part of CHANNEL_HOPPING_CMD
 * and are ignored here; only each slot's role (wonder vs. STA-managed)
 * is sent to FW.
 *
 * Return: 0 on success, negative error code on failure.
 */
static int
iwl_mld_wondertap_channel_schedule_request(void *handle,
					   const struct channel_schedule_request
					   *request)
{
	struct iwl_mld_wonder_ctx *wonder_ctx = handle;
	struct iwl_channel_hopping_cmd cmd = {};
	struct iwl_mld *mld = wonder_ctx->mld;
	u32 bitmap = 0;
	int i;

	if (WARN_ON(!mld || !request))
		return -EINVAL;

	guard(nested_wiphy)(mld->wiphy);

	if (WARN_ON(!mld->fw_status.running))
		return -EIO;

	if (WARN_ON(wonder_ctx->link_id == IWL_MLD_INVALID_FW_ID))
		return -EINVAL;

	/* Feature requirement: wonder.ko always sends a power-of-2 length. */
	if (request->channel_list_len > IWL_WONDER_MAX_CHANNEL_LIST_LEN ||
	    !is_power_of_2(request->channel_list_len))
		return -EINVAL;

	if (!request->channel_list)
		return -EINVAL;

	if (request->next_channel_index >= request->channel_list_len)
		return -EINVAL;

	if (request->dwell_time_tu < IWL_WONDER_MIN_DWELL_TIME_TU ||
	    request->dwell_time_tu > IWL_WONDER_MAX_DWELL_TIME_TU)
		return -EINVAL;

	/* Each bit represents a slot; set it for wonder (NOP) slots only. */
	for (i = 0; i < request->channel_list_len; i++)
		if (request->channel_list[i].role == WONDERTAP_ROLE_NOP)
			bitmap |= BIT(i);

	cmd.link_id = cpu_to_le32(wonder_ctx->link_id);
	cmd.channel_list_length = cpu_to_le32(request->channel_list_len);
	cmd.next_index = cpu_to_le32(request->next_channel_index);
	cmd.dwell_time = cpu_to_le32(request->dwell_time_tu);
	cmd.channel_hopping_bitmap = cpu_to_le32(bitmap);
	cmd.target_switch_time = cpu_to_le32(request->target_switch_time_tsf);

	return iwl_mld_send_cmd_pdu(mld, WIDE_ID(MAC_CONF_GROUP,
						 CHANNEL_HOPPING_CMD), &cmd);
}

/**
 * iwl_mld_wondertap_set_station_info - add, update or remove a station
 * @handle: opaque vendor driver instance handle
 * @action: WONDERTAP_STATION_STATE_NEW/UPDATE/DEL
 * @info: station parameters; @info->mac identifies the station
 *
 * Return: 0 on success, negative error code on failure.
 */
static int
iwl_mld_wondertap_set_station_info(void *handle,
				   const enum wondertap_station_action action,
				   struct wondertap_station_info *info)
{
	struct iwl_mld_wonder_ctx *wonder_ctx = handle;
	struct iwl_mld *mld;
	int ret;

	if (WARN_ON(!wonder_ctx || !info))
		return -EINVAL;

	mld = wonder_ctx->mld;
	if (WARN_ON(!mld))
		return -ENODEV;

	guard(nested_wiphy)(mld->wiphy);

	if (WARN_ON(!mld->fw_status.running))
		return -EIO;

	switch (action) {
	case WONDERTAP_STATION_STATE_NEW:
		ret = iwl_mld_wonder_alloc_sta(mld, wonder_ctx,
					       info->mac);
		if (ret)
			IWL_ERR(mld, "wonder: failed to add sta %pM: %d\n",
				info->mac, ret);
		return ret;
	case WONDERTAP_STATION_STATE_UPDATE:
		/* Capability update -- nothing to do for basic data path. */
		return 0;
	case WONDERTAP_STATION_STATE_DEL:
		iwl_mld_wonder_free_sta(mld, wonder_ctx, info->mac);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static const struct wondertap_ops iwl_mld_wondertap_ops = {
	.init = iwl_mld_wondertap_init,
	.deinit = iwl_mld_wondertap_deinit,
	.get_capabilities = iwl_mld_wondertap_get_capabilities,
	.get_mac_tsf = iwl_mld_wondertap_get_mac_tsf,
	.channel_schedule_request = iwl_mld_wondertap_channel_schedule_request,
	.set_station_info = iwl_mld_wondertap_set_station_info,
};

static void iwl_mld_wonder_adev_release(struct device *dev)
{
	struct auxiliary_device *aux = to_auxiliary_dev(dev);

	kfree(container_of(aux, struct wondertap_aux_dev, adev));
}

/*
 * Reset wonder_ctx's own registration state. Called from
 * iwl_mld_wonder_register()'s failure path and from
 * iwl_mld_wonder_unregister(), so a NIC can register again later.
 */
static void iwl_mld_wonder_ctx_reset(struct iwl_mld_wonder_ctx *wonder_ctx)
{
	wonder_ctx->wonder_dev = NULL;
	wonder_ctx->mld = NULL;
	wonder_ctx->registered = 0;
}

/**
 * iwl_mld_wonder_register - publish the wondertap auxiliary device
 * @mld: the MLD instance calling this function
 *
 * When more than one NIC is present this function is called once per
 * instance, but the device is only created on the first call; subsequent
 * calls just return 0.
 *
 * Return: 0 on success, negative error code on failure.
 */
int iwl_mld_wonder_register(struct iwl_mld *mld)
{
	struct iwl_mld_wonder_ctx *wonder_ctx = &iwl_mld_wonder_ctx;
	struct wondertap_aux_dev *wonder_dev;
	int ret;

	if (test_and_set_bit(0, &wonder_ctx->registered)) {
		IWL_DEBUG_INFO(mld,
			       "Wonder already registered by another instance\n");
		return 0;
	}

	if (!iwl_mld_wonder_check_fw_version_supported(mld)) {
		IWL_ERR(mld, "Wonder: unsupported firmware version\n");
		ret = -EOPNOTSUPP;
		goto err;
	}

	wonder_dev = kzalloc(sizeof(*wonder_dev), GFP_KERNEL);
	if (!wonder_dev) {
		IWL_ERR(mld, "Failed to allocate wondertap aux device\n");
		ret = -ENOMEM;
		goto err;
	}

	wonder_dev->adev.name = "wondertap";
	wonder_dev->adev.id = 0;
	wonder_dev->adev.dev.parent = mld->trans->dev;
	wonder_dev->adev.dev.release = iwl_mld_wonder_adev_release;
	wonder_dev->ver = WONDER_VERSION_3_6_2;
	wonder_dev->wonder_ops = &iwl_mld_wondertap_ops;

	wonder_ctx->wonder_dev = wonder_dev;
	wonder_ctx->mld = mld;

	ret = iwl_mld_wonder_netdev_create(wonder_ctx);
	if (ret) {
		IWL_ERR(mld, "Failed to create wondertap0 netdev: %d\n", ret);
		kfree(wonder_dev);
		goto err;
	}

	ret = auxiliary_device_init(&wonder_dev->adev);
	if (ret) {
		IWL_ERR(mld, "Failed to init wondertap aux device: %d\n", ret);
		kfree(wonder_dev);
		goto err_free_ndev;
	}

	ret = auxiliary_device_add(&wonder_dev->adev);
	if (ret) {
		IWL_ERR(mld, "Failed to add wondertap aux device: %d\n", ret);
		goto err_free_adev;
	}

	return 0;

err_free_adev:
	auxiliary_device_uninit(&wonder_dev->adev);
err_free_ndev:
	iwl_mld_wonder_netdev_destroy(wonder_ctx);
err:
	iwl_mld_wonder_ctx_reset(wonder_ctx);
	return ret;
}

/**
 * iwl_mld_wonder_unregister - remove the wondertap auxiliary device
 * @mld: the MLD instance calling this function
 *
 * Only the MLD instance that registered may unregister; other instances
 * (and repeated calls) are no-ops.
 */
void iwl_mld_wonder_unregister(struct iwl_mld *mld)
{
	struct iwl_mld_wonder_ctx *wonder_ctx = &iwl_mld_wonder_ctx;

	if (mld != wonder_ctx->mld)
		return;

	if (!test_and_clear_bit(0, &wonder_ctx->registered))
		return;

	/* delete drops wonder.ko's ref on wondertap0 before we destroy it */
	auxiliary_device_delete(&wonder_ctx->wonder_dev->adev);
	iwl_mld_wonder_netdev_destroy(wonder_ctx);
	auxiliary_device_uninit(&wonder_ctx->wonder_dev->adev);

	iwl_mld_wonder_ctx_reset(wonder_ctx);
}
