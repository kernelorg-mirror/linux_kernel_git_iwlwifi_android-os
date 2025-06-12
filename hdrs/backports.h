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


static inline void
cfg80211_epcs_changed(struct net_device *netdev, bool enabled)
{
}

DEFINE_GUARD(wiphy, struct wiphy *,
        mutex_lock(&_T->mtx),
        mutex_unlock(&_T->mtx))

static inline int __printf(2, 3) debugfs_change_name(struct dentry *dentry, const char *fmt, ...)
{
	const char *new_name;
	struct dentry *parent;
	va_list ap;

	va_start(ap, fmt);
	new_name = kvasprintf_const(GFP_KERNEL, fmt, ap);
	va_end(ap);
	if (!new_name)
		return -ENOMEM;

	parent = dentry->d_parent;

	debugfs_rename(parent, dentry, parent, new_name);

	kfree_const(new_name);
	/* We never checked the succession of debugfs_rename anyway */
	return 0;
}

#define NL80211_RRF_ALLOW_20MHZ_ACTIVITY    BIT(25)

static inline int cfg80211_chandef_get_width(const struct cfg80211_chan_def *c)
{
	return nl80211_chan_width_to_mhz(c->width);
}

#ifndef MAC_ADDR_STR_LEN
#define MAC_ADDR_STR_LEN (3 * ETH_ALEN - 1)
#endif

#ifndef timer_container_of
#define timer_container_of from_timer
#endif

static inline bool
sk_requests_wifi_status(struct sock *sk)
{
	return sk && sk_fullsock(sk) && sock_flag(sk, SOCK_WIFI_STATUS);
}
