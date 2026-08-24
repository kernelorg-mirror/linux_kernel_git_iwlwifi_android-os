// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2026 Intel Corporation
 */

#include <net/cfg80211.h>

#include "fw/api/commands.h"
#include "fw/api/context.h"
#include "fw/api/phy-ctxt.h"
#include "mld.h"
#include "hcmd.h"
#include "phy.h"
#include "wonder-phy.h"

static u8 iwl_mld_wonder_width_to_fw(enum nl80211_chan_width width)
{
	switch (width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
	case NL80211_CHAN_WIDTH_20:
		return IWL_PHY_CHANNEL_MODE20;
	case NL80211_CHAN_WIDTH_40:
		return IWL_PHY_CHANNEL_MODE40;
	case NL80211_CHAN_WIDTH_80:
		return IWL_PHY_CHANNEL_MODE80;
	case NL80211_CHAN_WIDTH_160:
		return IWL_PHY_CHANNEL_MODE160;
	case NL80211_CHAN_WIDTH_320:
		return IWL_PHY_CHANNEL_MODE320;
	default:
		WARN(1, "Invalid channel width=%u", width);
		return IWL_PHY_CHANNEL_MODE20;
	}
}

enum nl80211_chan_width
iwl_mld_wonder_bw_to_nl80211(enum wondertap_rate_bw bw)
{
	switch (bw) {
	case WONDERTAP_RATE_BW_20:
		return NL80211_CHAN_WIDTH_20;
	case WONDERTAP_RATE_BW_40:
		return NL80211_CHAN_WIDTH_40;
	case WONDERTAP_RATE_BW_80:
		return NL80211_CHAN_WIDTH_80;
	case WONDERTAP_RATE_BW_160:
		return NL80211_CHAN_WIDTH_160;
	default:
		return NL80211_CHAN_WIDTH_20;
	}
}

/* JP channel 44 at 20 MHz is exempted from the no-IR rule. */
static bool
iwl_mld_wonder_no_ir_allowed(const struct ieee80211_channel *chan,
			     const char *alpha2,
			     enum nl80211_chan_width width)
{
	return width == NL80211_CHAN_WIDTH_20 &&
	       alpha2[0] == 'J' && alpha2[1] == 'P' &&
	       chan->band == NL80211_BAND_5GHZ &&
	       chan->hw_value == 44;
}

static int
iwl_mld_wonder_build_chandef(struct iwl_mld *mld,
			     struct ieee80211_channel *chan,
			     const char *alpha2,
			     const struct wondertap_init_params *params,
			     struct cfg80211_chan_def *chandef)
{
	u32 control_freq = params->channel.freq;
	enum nl80211_chan_width width;
	u32 offset = 0;
	int ret = 0;

	switch (params->channel.bandwidth) {
	case WONDERTAP_RATE_BW_20:
	case WONDERTAP_RATE_BW_40:
	case WONDERTAP_RATE_BW_80:
		break;
	default:
		ret = -EOPNOTSUPP;
		goto out;
	}

	width = iwl_mld_wonder_bw_to_nl80211(params->channel.bandwidth);

	if (chan->band == NL80211_BAND_6GHZ) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	memset(chandef, 0, sizeof(*chandef));
	chandef->chan = chan;
	chandef->width = width;

	if (width == NL80211_CHAN_WIDTH_20) {
		chandef->center_freq1 = control_freq;
		goto check_usable;
	}

	/* HT40 is valid on 2.4 GHz, but wondertap gives us no secondary
	 * channel offset (HT40+/-), and unlike 5 GHz this band isn't on
	 * a 20 MHz-aligned grid we could infer it from -- reject for now.
	 */
	if (chan->band == NL80211_BAND_2GHZ) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	switch (chan->band) {
	case NL80211_BAND_5GHZ:
		if (control_freq >= 5745)
			offset = control_freq - 5745;
		else
			offset = control_freq - 5180;
		break;
	default:
		ret = -EOPNOTSUPP;
		goto out;
	}
	offset /= 20;

	switch (width) {
	case NL80211_CHAN_WIDTH_40:
		chandef->center_freq1 = control_freq + 10 - (offset & 1) * 20;
		break;
	case NL80211_CHAN_WIDTH_80:
		chandef->center_freq1 = control_freq + 30 - (offset & 3) * 20;
		break;
	default:
		ret = -EOPNOTSUPP;
		goto out;
	}

check_usable:
	if (!cfg80211_chandef_valid(chandef) ||
	    !cfg80211_chandef_usable(mld->wiphy, chandef,
				     IEEE80211_CHAN_DISABLED)) {
		ret = -EINVAL;
		goto out;
	}

	if (!iwl_mld_wonder_no_ir_allowed(chan, alpha2, width) &&
	    !cfg80211_chandef_usable(mld->wiphy, chandef,
				     IEEE80211_CHAN_NO_IR))
		ret = -EINVAL;

out:
	if (ret)
		IWL_ERR(mld,
			"Wonder: can't use ch %u (freq %u, bw %u) in %.2s: %d\n",
			chan->hw_value, params->channel.freq,
			params->channel.bandwidth, alpha2, ret);
	return ret;
}

/**
 * iwl_mld_wonder_allocate_phy_ctx - allocate and configure a PHY context
 * @mld: the MLD instance
 * @wonder_ctx: the wonder context to store the PHY ID
 * @params: initialization parameters containing channel frequency
 *
 * Allocates a firmware PHY context ID, configures it with the channel info,
 * and sends the PHY_CONTEXT_CMD to the firmware.
 * Must be called with wiphy lock held.
 *
 * Return: 0 on success, negative error code on failure.
 */
int iwl_mld_wonder_allocate_phy_ctx(struct iwl_mld *mld,
				    struct iwl_mld_wonder_ctx *wonder_ctx,
				    const struct wondertap_init_params *params)
{
	struct iwl_phy_context_cmd phy_cmd = {
		.action = cpu_to_le32(FW_CTXT_ACTION_ADD),
	};
	const struct ieee80211_regdomain *regd;
	struct cfg80211_chan_def chandef;
	struct ieee80211_channel *chan;
	int fw_id;
	int ret;

	lockdep_assert_wiphy(mld->wiphy);

	if (WARN_ON(wonder_ctx->phy_id != IWL_MLD_INVALID_FW_ID))
		return -EINVAL;

	/* The regdomain must already be resolved; wonder doesn't set it. */
	regd = wiphy_dereference(mld->wiphy, mld->wiphy->regd);
	if (WARN_ON(!regd))
		return -EINVAL;

	chan = ieee80211_get_channel(mld->wiphy, params->channel.freq);
	if (!chan || chan->flags & IEEE80211_CHAN_DISABLED) {
		IWL_ERR(mld, "Wonder: invalid or disabled channel freq=%u\n",
			params->channel.freq);
		return -EINVAL;
	}

	ret = iwl_mld_wonder_build_chandef(mld, chan, regd->alpha2, params,
					   &chandef);
	if (ret)
		return ret;

	fw_id = iwl_mld_allocate_fw_phy_id(mld);
	if (fw_id < 0) {
		IWL_ERR(mld, "Wonder: failed to allocate a PHY id: %d\n",
			fw_id);
		return fw_id;
	}

	phy_cmd.id_and_color = cpu_to_le32(fw_id);
	phy_cmd.ci.channel = cpu_to_le32(chan->hw_value);
	phy_cmd.ci.band = iwl_mld_nl80211_band_to_fw(chan->band);
	phy_cmd.ci.width = iwl_mld_wonder_width_to_fw(chandef.width);

	phy_cmd.ci.ctrl_pos = iwl_mld_get_fw_ctrl_pos(&chandef);
	phy_cmd.secondary_ctrl_chnl_loc =
		phy_cmd.ci.ctrl_pos ^ IWL_PHY_CTRL_POS_ABOVE;

	ret = iwl_mld_send_cmd_pdu(mld, PHY_CONTEXT_CMD, &phy_cmd);
	if (ret) {
		IWL_ERR(mld, "Failed to send PHY_CONTEXT_CMD: %d\n", ret);
		mld->used_phy_ids &= ~BIT(fw_id);
		return ret;
	}

	wonder_ctx->phy_id = fw_id;
	wonder_ctx->phy_chan_width = chandef.width;
	wonder_ctx->phy_band = chan->band;
	return 0;
}

/**
 * iwl_mld_wonder_free_phy_ctx - free and remove a PHY context
 * @mld: the MLD instance
 * @wonder_ctx: the wonder context containing the PHY ID to remove
 *
 * Sends a PHY_CONTEXT_CMD with FW_CTXT_ACTION_REMOVE to the firmware and
 * clears the allocated PHY ID.
 * Must be called with wiphy lock held.
 */
void iwl_mld_wonder_free_phy_ctx(struct iwl_mld *mld,
				 struct iwl_mld_wonder_ctx *wonder_ctx)
{
	struct iwl_phy_context_cmd phy_cmd = {
		.id_and_color = cpu_to_le32(wonder_ctx->phy_id),
		.action = cpu_to_le32(FW_CTXT_ACTION_REMOVE),
	};

	lockdep_assert_wiphy(mld->wiphy);

	if (WARN_ON(wonder_ctx->phy_id == IWL_MLD_INVALID_FW_ID))
		return;

	if (mld->fw_status.running)
		iwl_mld_send_cmd_pdu(mld, PHY_CONTEXT_CMD, &phy_cmd);

	mld->used_phy_ids &= ~BIT(wonder_ctx->phy_id);
	wonder_ctx->phy_id = IWL_MLD_INVALID_FW_ID;
}
