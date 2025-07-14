// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2025 Intel Corporation
 */

#include "mld.h"
#include "iface.h"
#include "fw/api/mac-cfg.h"

#define IWL_NAN_DISOVERY_BEACON_INTERNVAL_TU 512
#define IWL_NAN_RSSI_CLOSE 55
#define IWL_NAN_RSSI_MIDDLE 70

bool iwl_mld_nan_supported(struct iwl_mld *mld)
{
	return false;
}

int iwl_mld_start_nan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		      struct cfg80211_nan_conf *conf)
{
	return 0;
}

int iwl_mld_stop_nan(struct ieee80211_hw *hw,
		     struct ieee80211_vif *vif)
{
	return 0;
}

void iwl_mld_handle_nan_cluster_notif(struct iwl_mld *mld,
				      struct iwl_rx_packet *pkt)
{
}

bool iwl_mld_cancel_nan_cluster_notif(struct iwl_mld *mld,
				      struct iwl_rx_packet *pkt,
				      u32 obj_id)
{
	return true;
}

bool iwl_mld_cancel_nan_dw_end_notif(struct iwl_mld *mld,
				     struct iwl_rx_packet *pkt,
				     u32 obj_id)
{
	return true;
}

void iwl_mld_handle_nan_dw_end_notif(struct iwl_mld *mld,
				     struct iwl_rx_packet *pkt)
{
}
