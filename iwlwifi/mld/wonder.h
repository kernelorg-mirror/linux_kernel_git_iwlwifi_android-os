/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2026 Intel Corporation
 */
#ifndef __iwl_mld_wonder_h__
#define __iwl_mld_wonder_h__

#include <linux/netdevice.h>
#include <wondertap.h>

#include "mld.h"
#include "sta.h"
#include "wonder-sta.h"

/*
 * wonder.ko has its own wiphy, whose mutex shares the same lockdep class as
 * mld->wiphy->mtx (all wiphy mutexes are init'd from the same call site).
 * init()/deinit() are called from wonder.ko with its own wiphy mutex held,
 * so locking mld->wiphy->mtx here is always a nested acquisition -- never
 * the reverse -- hence SINGLE_DEPTH_NESTING rather than a plain guard(wiphy).
 */
DEFINE_GUARD(nested_wiphy, struct wiphy *,
	     mutex_lock_nested(&_T->mtx, SINGLE_DEPTH_NESTING),
	     mutex_unlock(&_T->mtx))

/**
 * struct iwl_mld_wonder_ctx - singleton context for the wonder feature
 * @wonder_dev: the wondertap auxiliary device published on the auxiliary bus
 * @mld: back-pointer to the MLD instance that registered
 * @registered: bitmap used as a flag; bit 0 set means the adev is registered
 * @phy_id: firmware PHY context ID, or IWL_MLD_INVALID_FW_ID when not allocated
 * @mac_id: firmware MAC context ID, or IWL_MLD_INVALID_FW_ID when not allocated
 * @link_id: firmware LINK context ID, or IWL_MLD_INVALID_FW_ID when not
 *	allocated
 * @bcast_mgmt_sta: station used for bcast management frames
 * @mcast_bcast_data_sta: station used for broadcast and multicast data frames
 * @stas: station slots, managed via the set_station_info vtable op
 * @bssid_filter: the BSSID latched at init() time
 * @netdev: the wondertap0 netdev, created/destroyed with register/unregister
 */
struct iwl_mld_wonder_ctx {
	struct wondertap_aux_dev *wonder_dev;
	struct iwl_mld *mld;
	unsigned long registered;
	u8 phy_id;
	u8 mac_id;
	u8 link_id;
	struct iwl_mld_int_sta bcast_mgmt_sta;
	struct iwl_mld_int_sta mcast_bcast_data_sta;
	struct iwl_mld_wonder_sta stas[IWL_MLD_WONDER_MAX_STAS];
	u8 bssid_filter[ETH_ALEN];
	struct net_device *netdev;
};

int iwl_mld_wonder_register(struct iwl_mld *mld);
void iwl_mld_wonder_unregister(struct iwl_mld *mld);

#endif /* __iwl_mld_wonder_h__ */
