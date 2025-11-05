/*
 * Copyright(c) 2015 - 2017 Intel Deutschland GmbH
 * Copyright (C) 2018, 2020, 2022-2024 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include "mac80211-exp.h"

#include <linux/export.h>
#include <linux/if_vlan.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <net/ip.h>
#include <linux/unaligned.h>
#include <linux/device.h>
#include <net/cfg80211.h>
#include "mac80211/ieee80211_i.h"
#include "mac80211/driver-ops.h"

enum hrtimer_restart wiphy_hrtimer_work_timer(struct hrtimer *t)
{
	struct wiphy_hrtimer_work *hrwork =
		container_of(t, struct wiphy_hrtimer_work, timer);

	wiphy_work_queue(hrwork->wiphy, &hrwork->work);

	return HRTIMER_NORESTART;
}
EXPORT_SYMBOL_GPL(wiphy_hrtimer_work_timer);

void wiphy_hrtimer_work_queue(struct wiphy *wiphy,
			      struct wiphy_hrtimer_work *hrwork,
			      ktime_t delay)
{
	if (!delay) {
		hrtimer_cancel(&hrwork->timer);
		wiphy_work_queue(wiphy, &hrwork->work);
		return;
	}

	hrwork->wiphy = wiphy;
	hrtimer_start_range_ns(&hrwork->timer, delay,
			       1000 * NSEC_PER_USEC, HRTIMER_MODE_REL);
}
EXPORT_SYMBOL_GPL(wiphy_hrtimer_work_queue);

void wiphy_hrtimer_work_cancel(struct wiphy *wiphy,
			       struct wiphy_hrtimer_work *hrwork)
{
	lockdep_assert_held(&wiphy->mtx);

	hrtimer_cancel(&hrwork->timer);
	wiphy_work_cancel(wiphy, &hrwork->work);
}
EXPORT_SYMBOL_GPL(wiphy_hrtimer_work_cancel);

void wiphy_hrtimer_work_flush(struct wiphy *wiphy,
			      struct wiphy_hrtimer_work *hrwork)
{
	lockdep_assert_held(&wiphy->mtx);

	hrtimer_cancel(&hrwork->timer);
	wiphy_work_flush(wiphy, &hrwork->work);
}
EXPORT_SYMBOL_GPL(wiphy_hrtimer_work_flush);

bool wiphy_hrtimer_work_pending(struct wiphy *wiphy,
				struct wiphy_hrtimer_work *hrwork)
{
	return hrtimer_is_queued(&hrwork->timer);
}
EXPORT_SYMBOL_GPL(wiphy_hrtimer_work_pending);
