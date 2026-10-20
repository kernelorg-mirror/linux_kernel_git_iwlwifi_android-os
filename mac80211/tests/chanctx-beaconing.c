// SPDX-License-Identifier: GPL-2.0-only
/*
 * KUnit tests for regulatory-strict channel context sharing
 *
 * Copyright (C) 2026 Intel Corporation
 */
#include <kunit/test.h>
#include "../ieee80211_i.h"

#if LINUX_VERSION_IS_LESS(6,13,0)
MODULE_IMPORT_NS(EXPORTED_FOR_KUNIT_TESTING);
#else
MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");
#endif

#define CHAN5G(_freq) { \
	.band = NL80211_BAND_5GHZ, \
	.center_freq = (_freq), \
	.hw_value = (_freq), \
}

static const struct ieee80211_channel channels_5ghz[] = {
	/* UNII-1 */
	CHAN5G(5180), /* 36 */
	CHAN5G(5200), /* 40 */
	CHAN5G(5220), /* 44 */
	CHAN5G(5240), /* 48 */
	/* UNII-2A */
	CHAN5G(5260), /* 52 */
	CHAN5G(5280), /* 56 */
	CHAN5G(5300), /* 60 */
	CHAN5G(5320), /* 64 */
};

#define NO_IR_CONC (IEEE80211_CHAN_NO_IR | IEEE80211_CHAN_IR_CONCURRENT)
#define DFS_CONC (IEEE80211_CHAN_RADAR | IEEE80211_CHAN_DFS_CONCURRENT)

struct chanctx_beaconing_user {
	/* NL80211_IFTYPE_UNSPECIFIED for no user */
	enum nl80211_iftype iftype;
	u32 control_freq, center_freq1;
	enum nl80211_chan_width width;
};

#define USER_STA_160 {					\
	.iftype = NL80211_IFTYPE_STATION,		\
	.control_freq = 5180,				\
	.center_freq1 = 5250,				\
	.width = NL80211_CHAN_WIDTH_160,		\
}

#define USER_GO_80 {					\
	.iftype = NL80211_IFTYPE_P2P_GO,		\
	.control_freq = 5180,				\
	.center_freq1 = 5210,				\
	.width = NL80211_CHAN_WIDTH_80,			\
}

#define USER_GO_160 {					\
	.iftype = NL80211_IFTYPE_P2P_GO,		\
	.control_freq = 5180,				\
	.center_freq1 = 5250,				\
	.width = NL80211_CHAN_WIDTH_160,		\
}

#define USER_CLI_80 {					\
	.iftype = NL80211_IFTYPE_P2P_CLIENT,		\
	.control_freq = 5180,				\
	.center_freq1 = 5210,				\
	.width = NL80211_CHAN_WIDTH_80,			\
}

static const struct chanctx_beaconing_case {
	const char *desc;
	u32 unii1_flags, unii2a_flags;
	bool strict;
	/* user already assigned to the context */
	struct chanctx_beaconing_user assigned;
	/* the assigned user is reserving another context */
	bool assigned_reserving;
	/* user that wants to use the context */
	struct chanctx_beaconing_user new;
	bool reservation;
	bool expected;
} chanctx_beaconing_cases[] = {
	{
		.desc = "flag not set",
		.unii1_flags = NO_IR_CONC,
		.unii2a_flags = IEEE80211_CHAN_NO_IR,
		.assigned = USER_GO_80,
		.new = USER_STA_160,
		.expected = true,
	},
	{
		.desc = "STA joins GO, permissive secondary 80 MHz",
		.unii1_flags = NO_IR_CONC,
		.strict = true,
		.assigned = USER_GO_80,
		.new = USER_STA_160,
		/* we decided this case was too rare/complex, so no sharing */
		.expected = false,
	},
	{
		.desc = "STA joins GO, NO_IR secondary 80 MHz",
		.unii1_flags = NO_IR_CONC,
		.unii2a_flags = IEEE80211_CHAN_NO_IR,
		.strict = true,
		.assigned = USER_GO_80,
		.new = USER_STA_160,
		.expected = false,
	},
	{
		.desc = "STA joins GO, IR_CONCURRENT secondary 80 MHz in other UNII band",
		.unii1_flags = NO_IR_CONC,
		.unii2a_flags = NO_IR_CONC,
		.strict = true,
		.assigned = USER_GO_80,
		.new = USER_STA_160,
		.expected = false,
	},
	{
		.desc = "STA joins GO, DFS_CONCURRENT secondary 80 MHz",
		.unii1_flags = NO_IR_CONC,
		.unii2a_flags = DFS_CONC,
		.strict = true,
		.assigned = USER_GO_80,
		.new = USER_STA_160,
		/* a connecting STA isn't known to cfg80211, so no sharing */
		.expected = false,
	},
	{
		.desc = "GO joins STA, NO_IR secondary 80 MHz",
		.unii1_flags = NO_IR_CONC,
		.unii2a_flags = IEEE80211_CHAN_NO_IR,
		.strict = true,
		.assigned = USER_STA_160,
		.new = USER_GO_80,
		.expected = false,
	},
	{
		.desc = "GO joins STA, identical chandef",
		.unii1_flags = NO_IR_CONC,
		.unii2a_flags = IEEE80211_CHAN_NO_IR,
		.strict = true,
		.assigned = USER_STA_160,
		.new = USER_GO_160,
		.expected = true,
	},
	{
		.desc = "P2P client joins STA, NO_IR secondary 80 MHz",
		.unii1_flags = NO_IR_CONC,
		.unii2a_flags = IEEE80211_CHAN_NO_IR,
		.strict = true,
		.assigned = USER_STA_160,
		.new = USER_CLI_80,
		.expected = true,
	},
	{
		.desc = "GO without STA, widened by another GO",
		.unii1_flags = NO_IR_CONC,
		.strict = true,
		.assigned = USER_GO_80,
		.new = USER_GO_160,
		.expected = false,
	},
	{
		.desc = "STA joins GO reserving another context",
		.unii1_flags = NO_IR_CONC,
		.unii2a_flags = IEEE80211_CHAN_NO_IR,
		.strict = true,
		.assigned = USER_GO_80,
		.assigned_reserving = true,
		.new = USER_STA_160,
		.reservation = true,
		.expected = true,
	},
	{
		.desc = "STA joins GO reserving another context, not a reservation",
		.unii1_flags = NO_IR_CONC,
		.unii2a_flags = IEEE80211_CHAN_NO_IR,
		.strict = true,
		.assigned = USER_GO_80,
		.assigned_reserving = true,
		.new = USER_STA_160,
		.expected = false,
	},
	{
		.desc = "GO context widened without STA, DFS_CONCURRENT secondary",
		.unii1_flags = NO_IR_CONC,
		.unii2a_flags = DFS_CONC,
		.strict = true,
		.assigned = USER_GO_80,
		/* no new user, check only the combined chandef */
		.new = {
			.control_freq = 5180,
			.center_freq1 = 5250,
			.width = NL80211_CHAN_WIDTH_160,
		},
		/* no concurrent BSS, so DFS_CONCURRENT doesn't help */
		.expected = false,
	},
};

KUNIT_ARRAY_PARAM_DESC(chanctx_beaconing, chanctx_beaconing_cases, desc)

struct chanctx_beaconing_test_ctx {
	struct ieee80211_supported_band band_5ghz;
	struct ieee80211_channel channels[ARRAY_SIZE(channels_5ghz)];
	struct ieee80211_chanctx ctx, other_ctx;
};

static void chanctx_beaconing_tx(struct ieee80211_hw *hw,
				 struct ieee80211_tx_control *control,
				 struct sk_buff *skb)
{
}

static int chanctx_beaconing_start(struct ieee80211_hw *hw)
{
	return 0;
}

static void chanctx_beaconing_stop(struct ieee80211_hw *hw, bool suspend)
{
}

static int chanctx_beaconing_add_interface(struct ieee80211_hw *hw,
					   struct ieee80211_vif *vif)
{
	return 0;
}

static void chanctx_beaconing_remove_interface(struct ieee80211_hw *hw,
					       struct ieee80211_vif *vif)
{
}

static int chanctx_beaconing_config(struct ieee80211_hw *hw, int radio_idx,
				    u32 changed)
{
	return 0;
}

static void chanctx_beaconing_configure_filter(struct ieee80211_hw *hw,
					       unsigned int changed_flags,
					       unsigned int *total_flags,
					       u64 multicast)
{
}

static void chanctx_beaconing_wake_tx_queue(struct ieee80211_hw *hw,
					    struct ieee80211_txq *txq)
{
}

static const struct ieee80211_ops chanctx_beaconing_ops = {
	.tx = chanctx_beaconing_tx,
	.start = chanctx_beaconing_start,
	.stop = chanctx_beaconing_stop,
	.add_interface = chanctx_beaconing_add_interface,
	.remove_interface = chanctx_beaconing_remove_interface,
	.config = chanctx_beaconing_config,
	.configure_filter = chanctx_beaconing_configure_filter,
	.wake_tx_queue = chanctx_beaconing_wake_tx_queue,
	.add_chanctx = ieee80211_emulate_add_chanctx,
	.remove_chanctx = ieee80211_emulate_remove_chanctx,
	.change_chanctx = ieee80211_emulate_change_chanctx,
};

static void chanctx_beaconing_free_hw(void *data)
{
	ieee80211_free_hw(data);
}

static void chanctx_beaconing_remove_sdata(void *data)
{
	struct ieee80211_sub_if_data *sdata = data;

	list_del(&sdata->list);
}

static void chanctx_beaconing_chandef(struct kunit *test, struct wiphy *wiphy,
				      const struct chanctx_beaconing_user *user,
				      struct cfg80211_chan_def *chandef)
{
	*chandef = (struct cfg80211_chan_def){
		.chan = ieee80211_get_channel(wiphy, user->control_freq),
		.center_freq1 = user->center_freq1,
		.width = user->width,
	};
	KUNIT_ASSERT_NOT_NULL(test, chandef->chan);
	KUNIT_ASSERT_TRUE(test, cfg80211_chandef_valid(chandef));
}

static struct ieee80211_sub_if_data *
chanctx_beaconing_add_sdata(struct kunit *test, struct ieee80211_local *local,
			    const struct chanctx_beaconing_user *user)
{
	struct ieee80211_sub_if_data *sdata;

	sdata = kunit_kzalloc(test, sizeof(*sdata), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, sdata);

	sdata->local = local;
	sdata->wdev.wiphy = local->hw.wiphy;
	sdata->wdev.iftype = user->iftype;

	switch (user->iftype) {
	case NL80211_IFTYPE_P2P_GO:
		sdata->vif.type = NL80211_IFTYPE_AP;
		sdata->vif.p2p = true;
		break;
	case NL80211_IFTYPE_P2P_CLIENT:
		sdata->vif.type = NL80211_IFTYPE_STATION;
		sdata->vif.p2p = true;
		break;
	default:
		sdata->vif.type = user->iftype;
		break;
	}

	sdata->deflink.sdata = sdata;
	sdata->deflink.conf = &sdata->vif.bss_conf;
	RCU_INIT_POINTER(sdata->link[0], &sdata->deflink);
	chanctx_beaconing_chandef(test, local->hw.wiphy, user,
				  &sdata->deflink.conf->chanreq.oper);

	set_bit(SDATA_STATE_RUNNING, &sdata->state);
	list_add_tail(&sdata->list, &local->interfaces);
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test,
						  chanctx_beaconing_remove_sdata,
						  sdata));

	return sdata;
}

static void test_chanctx_beaconing(struct kunit *test)
{
	const struct chanctx_beaconing_case *params = test->param_value;
	struct ieee80211_sub_if_data *assigned, *new;
	struct chanctx_beaconing_test_ctx *t;
	struct ieee80211_supported_band *sband;
	struct cfg80211_chan_def combined;
	const struct cfg80211_chan_def *compat;
	struct ieee80211_beacon_check check = {
		.combined = &combined,
		.reservation = params->reservation,
	};
	struct ieee80211_local *local;
	struct ieee80211_hw *hw;
	struct wiphy *wiphy;
	bool ret;

	t = kunit_kzalloc(test, sizeof(*t), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, t);

	hw = ieee80211_alloc_hw_nm(0, &chanctx_beaconing_ops, NULL);
	KUNIT_ASSERT_NOT_NULL(test, hw);
	KUNIT_ASSERT_EQ(test, 0,
			kunit_add_action_or_reset(test,
						  chanctx_beaconing_free_hw,
						  hw));
	local = hw_to_local(hw);
	wiphy = hw->wiphy;

	memcpy(t->channels, channels_5ghz, sizeof(channels_5ghz));
	for (int i = 0; i < ARRAY_SIZE(t->channels); i++) {
		struct ieee80211_channel *chan = &t->channels[i];

		if (chan->center_freq <= 5250)
			chan->flags = params->unii1_flags;
		else
			chan->flags = params->unii2a_flags;
	}

	sband = &t->band_5ghz;
	sband->band = NL80211_BAND_5GHZ;
	sband->channels = t->channels;
	sband->n_channels = ARRAY_SIZE(t->channels);
	sband->ht_cap.ht_supported = true;
	sband->ht_cap.cap = IEEE80211_HT_CAP_SUP_WIDTH_20_40;
	sband->vht_cap.vht_supported = true;
	sband->vht_cap.cap = IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ;
	wiphy->bands[NL80211_BAND_5GHZ] = sband;

	wiphy->regulatory_flags |= REGULATORY_ENABLE_RELAX_NO_IR;
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_DFS_CONCURRENT);

	if (params->strict)
		ieee80211_hw_set(hw, NEED_FULL_CHANDEF_RELAX);

	assigned = chanctx_beaconing_add_sdata(test, local, &params->assigned);
	rcu_assign_pointer(assigned->deflink.conf->chanctx_conf,
			   &t->ctx.conf);
	if (params->assigned_reserving)
		assigned->deflink.reserved_chanctx = &t->other_ctx;

	if (params->new.iftype != NL80211_IFTYPE_UNSPECIFIED) {
		new = chanctx_beaconing_add_sdata(test, local, &params->new);
		check.sdata = new;
		check.link = &new->deflink;
		check.chandef = &new->deflink.conf->chanreq.oper;
		compat = cfg80211_chandef_compatible(check.chandef,
						     &assigned->deflink.conf->chanreq.oper);
		KUNIT_ASSERT_NOT_NULL(test, compat);
		combined = *compat;
	} else {
		chanctx_beaconing_chandef(test, wiphy, &params->new, &combined);
	}

	t->ctx.conf.def = assigned->deflink.conf->chanreq.oper;
	check.ctx = &t->ctx;

	wiphy_lock(wiphy);
	ret = ieee80211_chanctx_beaconing_ok(local, &check);
	wiphy_unlock(wiphy);

	KUNIT_EXPECT_EQ(test, ret, params->expected);
}

static struct kunit_case chanctx_beaconing_test_cases[] = {
	KUNIT_CASE_PARAM(test_chanctx_beaconing, chanctx_beaconing_gen_params),
	{}
};

static struct kunit_suite chanctx_beaconing = {
	.name = "mac80211-chanctx-beaconing",
	.test_cases = chanctx_beaconing_test_cases,
};

kunit_test_suite(chanctx_beaconing);
