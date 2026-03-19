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

#ifndef __flex_counter
#define __flex_counter(FAM)	((void *)NULL)
#endif

#ifndef struct_size_t
#define struct_size_t(type, member, count)					\
	struct_size((type *)NULL, member, count)
#endif

#define typeof_flex_counter(FAM)				\
	typeof(_Generic(__flex_counter(FAM),			\
			void *: (size_t)0,			\
			default: *__flex_counter(FAM)))

#define overflows_flex_counter_type(TYPE, FAM, COUNT)		\
	(overflows_type(COUNT, typeof_flex_counter(((TYPE *)NULL)->FAM)))

#define __set_flex_counter(FAM, COUNT)				\
({								\
	*_Generic(__flex_counter(FAM),				\
		  void *:  &(size_t){ 0 },			\
		  default: __flex_counter(FAM)) = (COUNT);	\
})

#define __default_gfp(a,...) a
#define default_gfp(...) __default_gfp(__VA_ARGS__ __VA_OPT__(,) GFP_KERNEL)

#include <linux/bug.h>

#define __alloc_objs(KMALLOC, GFP, TYPE, COUNT)				\
({									\
	const size_t __obj_size = size_mul(sizeof(TYPE), COUNT);	\
	(TYPE *)KMALLOC(__obj_size, GFP);				\
})

#define __alloc_flex(KMALLOC, GFP, TYPE, FAM, COUNT)			\
({									\
	const size_t __count = (COUNT);					\
	const size_t __obj_size = struct_size_t(TYPE, FAM, __count);	\
	TYPE *__obj_ptr = KMALLOC(__obj_size, GFP);			\
	if (__obj_ptr)							\
		__set_flex_counter(__obj_ptr->FAM, __count);		\
	__obj_ptr;							\
})

#define kmalloc_obj(VAR_OR_TYPE, ...) \
	__alloc_objs(kmalloc, default_gfp(__VA_ARGS__), typeof(VAR_OR_TYPE), 1)

#define kmalloc_objs(VAR_OR_TYPE, COUNT, ...) \
	__alloc_objs(kmalloc, default_gfp(__VA_ARGS__), typeof(VAR_OR_TYPE), COUNT)

#define kmalloc_flex(VAR_OR_TYPE, FAM, COUNT, ...) \
	__alloc_flex(kmalloc, default_gfp(__VA_ARGS__), typeof(VAR_OR_TYPE), FAM, COUNT)

#define kzalloc_obj(P, ...) \
	__alloc_objs(kzalloc, default_gfp(__VA_ARGS__), typeof(P), 1)
#define kzalloc_objs(P, COUNT, ...) \
	__alloc_objs(kzalloc, default_gfp(__VA_ARGS__), typeof(P), COUNT)
#define kzalloc_flex(P, FAM, COUNT, ...)		\
	__alloc_flex(kzalloc, default_gfp(__VA_ARGS__), typeof(P), FAM, COUNT)

#define kvmalloc_obj(P, ...) \
	__alloc_objs(kvmalloc, default_gfp(__VA_ARGS__), typeof(P), 1)
#define kvmalloc_objs(P, COUNT, ...) \
	__alloc_objs(kvmalloc, default_gfp(__VA_ARGS__), typeof(P), COUNT)
#define kvmalloc_flex(P, FAM, COUNT, ...) \
	__alloc_flex(kvmalloc, default_gfp(__VA_ARGS__), typeof(P), FAM, COUNT)

#define kvzalloc_obj(P, ...) \
	__alloc_objs(kvzalloc, default_gfp(__VA_ARGS__), typeof(P), 1)
#define kvzalloc_objs(P, COUNT, ...) \
	__alloc_objs(kvzalloc, default_gfp(__VA_ARGS__), typeof(P), COUNT)
#define kvzalloc_flex(P, FAM, COUNT, ...) \
	__alloc_flex(kvzalloc, default_gfp(__VA_ARGS__), typeof(P), FAM, COUNT)

#define IEEE80211_CHAN_NO_UHR 0
#define NL80211_RRF_NO_UHR 0
#define ASSOC_REQ_DISABLE_UHR 0

struct ieee80211_sta_uhr_cap {
	bool has_uhr;
	struct ieee80211_uhr_cap_mac mac;
	struct ieee80211_uhr_cap_phy phy;
};

static inline const struct ieee80211_sta_uhr_cap *
ieee80211_get_uhr_iftype_cap(const struct ieee80211_supported_band *sband,
			     enum nl80211_iftype iftype)
{
	return NULL;
}


#define WIPHY_NAN_FLAGS_USERSPACE_DE BIT(1)
#define CFG80211_NAN_SCHED_NUM_TIME_SLOTS 32
#define CFG80211_NAN_MAX_PEER_MAPS 2
#define CFG80211_NAN_INVALID_MAP_ID 0xff
