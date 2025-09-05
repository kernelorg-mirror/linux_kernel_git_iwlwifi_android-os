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
hrtimer_setup(struct hrtimer *timer,
              enum hrtimer_restart (*function)(struct hrtimer *),
              clockid_t clock_id, enum hrtimer_mode mode)
{
	hrtimer_init(timer, clock_id, mode);
	timer->function = function;
}


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


static inline bool
backport_cfg80211_rx_spurious_frame(struct net_device *dev, const u8 *addr,
        int link_id, gfp_t gfp)
{
        return cfg80211_rx_spurious_frame(dev, addr, gfp);
}
#define cfg80211_rx_spurious_frame LINUX_BACKPORT(cfg80211_rx_spurious_frame)


static inline bool
backport_cfg80211_rx_unexpected_4addr_frame(struct net_device *dev, const u8 *addr,
        int link_id, gfp_t gfp)
{
        return cfg80211_rx_unexpected_4addr_frame(dev, addr, gfp);
}
#define cfg80211_rx_unexpected_4addr_frame LINUX_BACKPORT(cfg80211_rx_unexpected_4addr_frame)

#ifndef secs_to_jiffies
#define secs_to_jiffies(_secs) (unsigned long)((_secs) * HZ)
#endif

#include <linux/hrtimer.h>

struct wiphy_hrtimer_work {
	struct wiphy_work work;
	struct wiphy *wiphy;
	struct hrtimer timer;
};

enum hrtimer_restart wiphy_hrtimer_work_timer(struct hrtimer *t);

static inline void wiphy_hrtimer_work_init(struct wiphy_hrtimer_work *hrwork,
					   wiphy_work_func_t func)
{
	hrtimer_setup(&hrwork->timer, wiphy_hrtimer_work_timer,
		      CLOCK_BOOTTIME, HRTIMER_MODE_REL);
	wiphy_work_init(&hrwork->work, func);
}

/**
 * wiphy_hrtimer_work_queue - queue hrtimer work for the wiphy
 * @wiphy: the wiphy to queue for
 * @hrwork: the high resolution timer worker
 * @delay: the delay given as a ktime_t
 *
 * Please refer to wiphy_delayed_work_queue(). The difference is that
 * the hrtimer work uses a high resolution timer for scheduling. This
 * may be needed if timeouts might be scheduled further in the future
 * and the accuracy of the normal timer is not sufficient.
 *
 * Expect a delay of a few milliseconds as the timer is scheduled
 * with some slack and some more time may pass between queueing the
 * work and its start.
 */
void wiphy_hrtimer_work_queue(struct wiphy *wiphy,
			      struct wiphy_hrtimer_work *hrwork,
			      ktime_t delay);

/**
 * wiphy_hrtimer_work_cancel - cancel previously queued hrtimer work
 * @wiphy: the wiphy, for debug purposes
 * @hrwork: the hrtimer work to cancel
 *
 * Cancel the work *without* waiting for it, this assumes being
 * called under the wiphy mutex acquired by wiphy_lock().
 */
void wiphy_hrtimer_work_cancel(struct wiphy *wiphy,
			       struct wiphy_hrtimer_work *hrtimer);

/**
 * wiphy_hrtimer_work_flush - flush previously queued hrtimer work
 * @wiphy: the wiphy, for debug purposes
 * @hrwork: the hrtimer work to flush
 *
 * Flush the work (i.e. run it if pending). This must be called
 * under the wiphy mutex acquired by wiphy_lock().
 */
void wiphy_hrtimer_work_flush(struct wiphy *wiphy,
			      struct wiphy_hrtimer_work *hrwork);

/**
 * wiphy_hrtimer_work_pending - Find out whether a wiphy hrtimer
 * work item is currently pending.
 *
 * @wiphy: the wiphy, for debug purposes
 * @hrwork: the hrtimer work in question
 *
 * Return: true if timer is pending, false otherwise
 *
 * Please refer to the wiphy_delayed_work_pending() documentation as
 * this is the equivalent function for hrtimer based delayed work
 * items.
 */
bool wiphy_hrtimer_work_pending(struct wiphy *wiphy,
				struct wiphy_hrtimer_work *hrwork);
