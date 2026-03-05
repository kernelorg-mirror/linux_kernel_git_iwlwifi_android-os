/*
 * ChromeOS backport definitions
 * Copyright (C) 2015-2017 Intel Deutschland GmbH
 * Copyright (C) 2018-2024 Intel Corporation
 */

/* backport wiphy_ext_feature_set/_isset
 *
 * To do so, define our own versions thereof that check for a negative
 * feature index and in that case ignore it entirely. That allows us to
 * define the ones that the cfg80211 version doesn't support to -1.
 */
static inline void iwl7000_wiphy_ext_feature_set(struct wiphy *wiphy, int ftidx)
{
	if (ftidx < 0)
		return;
	wiphy_ext_feature_set(wiphy, ftidx);
}

static inline bool iwl7000_wiphy_ext_feature_isset(struct wiphy *wiphy,
						   int ftidx)
{
	if (ftidx < 0)
		return false;
	return wiphy_ext_feature_isset(wiphy, ftidx);
}
#define wiphy_ext_feature_set iwl7000_wiphy_ext_feature_set
#define wiphy_ext_feature_isset iwl7000_wiphy_ext_feature_isset

static inline enum ieee80211_ap_reg_power
cfg80211_6ghz_power_type(u8 control, u32 client_flags)
{
	switch (u8_get_bits(control, IEEE80211_HE_6GHZ_OPER_CTRL_REG_INFO)) {
	case IEEE80211_6GHZ_CTRL_REG_LPI_AP:
	case IEEE80211_6GHZ_CTRL_REG_INDOOR_LPI_AP:
	case IEEE80211_6GHZ_CTRL_REG_INDOOR_SP_AP_OLD:
		return IEEE80211_REG_LPI_AP;
	case IEEE80211_6GHZ_CTRL_REG_SP_AP:
		return IEEE80211_REG_SP_AP;
	case IEEE80211_6GHZ_CTRL_REG_VLP_AP:
		return IEEE80211_REG_VLP_AP;
	case IEEE80211_6GHZ_CTRL_REG_INDOOR_SP_AP:
		if (client_flags & IEEE80211_CHAN_NO_6GHZ_AFC_CLIENT)
			return IEEE80211_REG_LPI_AP;
		return IEEE80211_REG_SP_AP;
	default:
		return IEEE80211_REG_UNSET_AP;
	}
}

#define WIPHY_NAN_FLAGS_USERSPACE_DE BIT(1)

#define IEEE80211_CHAN_NO_UHR 0
#define NL80211_RRF_NO_UHR 0
#define ASSOC_REQ_DISABLE_UHR 0

struct ieee80211_sta_uhr_cap {
	bool has_uhr;
	struct ieee80211_uhr_capa_mac mac;
	struct ieee80211_uhr_capa_phy phy;
};

static inline const struct ieee80211_sta_uhr_cap *
ieee80211_get_uhr_iftype_cap(const struct ieee80211_supported_band *sband,
			     enum nl80211_iftype iftype)
{
	return NULL;
}

#define CFG80211_NAN_SCHED_NUM_TIME_SLOTS 32
#define CFG80211_NAN_MAX_PEER_MAPS 2
#define CFG80211_NAN_INVALID_MAP_ID 0xff
