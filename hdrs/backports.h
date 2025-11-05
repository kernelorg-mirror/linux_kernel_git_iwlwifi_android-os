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
