// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2025-2026 Intel Corporation
 */

#include "mld.h"
#include "iface.h"
#include "mlo.h"
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

int iwl_mld_nan_change_config(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct cfg80211_nan_conf *conf,
			      u32 changes)
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

void iwl_mld_nan_vif_cfg_changed(struct iwl_mld *mld,
				 struct ieee80211_vif *vif,
				 u64 changes)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	bool has_sched = memchr_inv(vif->cfg.nan_schedule, 0,
				    sizeof(vif->cfg.nan_schedule));

	lockdep_assert_wiphy(mld->wiphy);

	if (!(changes & BSS_CHANGED_NAN_LOCAL_SCHED))
		return;

	if (has_sched && !mld_vif->nan.mac_added) {
		if (iwl_mld_add_nan_vif(mld, vif))
			IWL_ERR(mld, "Failed to add NAN vif\n");
	} else if (!has_sched && mld_vif->nan.mac_added) {
		iwl_mld_rm_vif(mld, vif);
	}
}
