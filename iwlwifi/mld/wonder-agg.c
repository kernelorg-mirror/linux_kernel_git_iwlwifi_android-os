// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2026 Intel Corporation
 */

/*
 * In-driver RX Block Ack session management for the wonder station.
 *
 * mac80211 does not deliver WLAN_CATEGORY_BACK action frames to monitor
 * VIFs. Frames matching the wonder BSSID filter are intercepted in
 * iwl_mld_wonder_rx_frame() (RX softirq) and queued in
 * wonder_ctx->agg_pending. A wiphy_work handler dequeues and processes
 * them under the wiphy mutex so synchronous FW commands can be sent.
 *
 * ADDBA responses are built as raw 802.11 management frames and
 * transmitted via iwl_mld_wonder_tx(), the same path used for data.
 */

#include <linux/bitfield.h>
#include <linux/ieee80211.h>
#include <linux/skbuff.h>

#include "fw/api/datapath.h"
#include "hcmd.h"
#include "mld.h"
#include "wonder.h"
#include "wonder-agg.h"
#include "wonder-sta.h"
#include "wonder-tx.h"

/* Maximum RX BA window size accepted from a station. */
#define WONDER_MAX_RX_BA_BUF_SIZE	IEEE80211_MAX_AMPDU_BUF_EHT
/* Default window when the station sends buf_size == 0. */
#define WONDER_DEFAULT_RX_BA_BUF_SIZE	64

/* ------------------------------------------------------------------ */
/* FW BAID allocation                                                  */
/* ------------------------------------------------------------------ */

/**
 * iwl_mld_wonder_baid_alloc() - allocate an RX BAID in firmware
 * @mld: the MLD instance
 * @sta_mask: firmware station ID bitmask for the station
 * @tid: TID of the BA session
 * @ssn: starting sequence number
 * @buf_size: BA window size
 *
 * Sends RX_BAID_ALLOCATION_CONFIG_CMD (ADD).
 * Must be called with the wiphy mutex held.
 *
 * Return: allocated BAID (>= 0) on success, negative errno on failure.
 */
int iwl_mld_wonder_baid_alloc(struct iwl_mld *mld, u32 sta_mask,
			      int tid, u16 ssn, u16 buf_size)
{
	struct iwl_rx_baid_cfg_cmd cmd = {
		.action            = cpu_to_le32(IWL_RX_BAID_ACTION_ADD),
		.alloc.sta_id_mask = cpu_to_le32(sta_mask),
		.alloc.tid         = tid,
		.alloc.ssn         = cpu_to_le16(ssn),
		.alloc.win_size    = cpu_to_le16(buf_size),
	};
	struct iwl_host_cmd hcmd = {
		.id      = WIDE_ID(DATA_PATH_GROUP,
				   RX_BAID_ALLOCATION_CONFIG_CMD),
		.flags   = CMD_WANT_SKB,
		.len[0]  = sizeof(cmd),
		.data[0] = &cmd,
	};
	struct iwl_rx_baid_cfg_resp *resp;
	struct iwl_rx_packet *pkt;
	u32 resp_len;
	int ret, baid;

	BUILD_BUG_ON(sizeof(*resp) != sizeof(baid));
	lockdep_assert_wiphy(mld->wiphy);

	if (mld->num_rx_ba_sessions >= IWL_MAX_BAID) {
		IWL_WARN(mld, "wonder-agg: max RX BA sessions reached\n");
		return -ENOSPC;
	}

	ret = iwl_mld_send_cmd(mld, &hcmd);
	if (ret)
		return ret;

	pkt = hcmd.resp_pkt;
	resp_len = iwl_rx_packet_payload_len(pkt);

	if (IWL_FW_CHECK(mld, resp_len != sizeof(*resp),
			 "wonder-agg: BAID alloc resp bad len %u\n",
			 resp_len)) {
		ret = -EIO;
		goto out;
	}

	resp = (void *)pkt->data;
	baid = le32_to_cpu(resp->baid);

	if (IWL_FW_CHECK(mld, baid < 0 ||
			 baid >= (int)ARRAY_SIZE(mld->fw_id_to_ba),
			 "wonder-agg: FW returned invalid BAID %d\n", baid)) {
		ret = -EINVAL;
		goto out;
	}

	mld->num_rx_ba_sessions++;
	IWL_INFO(mld,
		 "wonder-agg: RX BA started sta_mask=0x%x tid=%d baid=%d\n",
		 sta_mask, tid, baid);
	ret = baid;
out:
	iwl_free_resp(&hcmd);
	return ret;
}

/**
 * iwl_mld_wonder_baid_free() - release an RX BAID in firmware
 * @mld: the MLD instance
 * @sta_mask: firmware station ID bitmask for the station
 * @tid: TID of the BA session to remove
 *
 * Sends RX_BAID_ALLOCATION_CONFIG_CMD (REMOVE). Skips the FW command
 * during a hardware restart; only updates the session counter.
 * Must be called with the wiphy mutex held.
 *
 * Return: 0 on success, negative errno on failure.
 */
int iwl_mld_wonder_baid_free(struct iwl_mld *mld, u32 sta_mask, int tid)
{
	struct iwl_rx_baid_cfg_cmd cmd = {
		.action              = cpu_to_le32(IWL_RX_BAID_ACTION_REMOVE),
		.remove.sta_id_mask  = cpu_to_le32(sta_mask),
		.remove.tid          = cpu_to_le32(tid),
	};
	u32 cmd_id = WIDE_ID(DATA_PATH_GROUP, RX_BAID_ALLOCATION_CONFIG_CMD);
	int ret = 0;

	lockdep_assert_wiphy(mld->wiphy);

	if (!mld->fw_status.in_hw_restart) {
		ret = iwl_mld_send_cmd_pdu(mld, cmd_id, &cmd);
		if (ret)
			return ret;
	}

	if (!WARN_ON(mld->num_rx_ba_sessions == 0))
		mld->num_rx_ba_sessions--;

	IWL_INFO(mld, "wonder-agg: RX BA stopped sta_mask=0x%x tid=%d\n",
		 sta_mask, tid);
	return 0;
}

/* ------------------------------------------------------------------ */
/* ADDBA response frame builder                                        */
/* ------------------------------------------------------------------ */

static struct sk_buff *
wonder_build_addba_resp(const u8 *da, const u8 *sa, const u8 *bssid,
			u8 dialog_token, u8 tid, u16 buf_size,
			u16 timeout, u16 status)
{
	struct ieee80211_mgmt *resp;
	struct sk_buff *skb;
	size_t len;
	u16 capab;

	len = offsetof(struct ieee80211_mgmt, u.action.addba_resp) +
	      sizeof(resp->u.action.addba_resp);

	skb = dev_alloc_skb(len);
	if (!skb)
		return NULL;

	resp = skb_put_zero(skb, len);
	resp->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_ACTION);
	ether_addr_copy(resp->da, da);
	ether_addr_copy(resp->sa, sa);
	ether_addr_copy(resp->bssid, bssid);

	resp->u.action.category    = WLAN_CATEGORY_BACK;
	resp->u.action.action_code = WLAN_ACTION_ADDBA_RESP;

	resp->u.action.addba_resp.dialog_token = dialog_token;
	resp->u.action.addba_resp.status       = cpu_to_le16(status);

	capab = u16_encode_bits(1, IEEE80211_ADDBA_PARAM_POLICY_MASK)  |
		u16_encode_bits(tid, IEEE80211_ADDBA_PARAM_TID_MASK)   |
		u16_encode_bits(buf_size,
				IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK);
	resp->u.action.addba_resp.capab   = cpu_to_le16(capab);
	resp->u.action.addba_resp.timeout = cpu_to_le16(timeout);

	return skb;
}

/* ------------------------------------------------------------------ */
/* BACK action frame handlers (run under wiphy lock from work item)   */
/* ------------------------------------------------------------------ */

static void
wonder_handle_addba_req(struct iwl_mld_wonder_ctx *wonder_ctx,
			struct sk_buff *skb)
{
	struct iwl_mld *mld = wonder_ctx->mld;
	struct ieee80211_mgmt *req = (void *)skb->data;
	struct iwl_mld_wonder_sta *sta;
	struct iwl_mld_wonder_rx_ba *session;
	struct sk_buff *resp_skb;
	const struct wonder_txd txd = {
		.is_unicast = true,
		.frame_type = IEEE80211_FTYPE_MGMT,
		.tid        = 0,
	};
	u16 capab, tid, buf_size, timeout, ssn;
	u8 dialog_token;
	u32 sta_mask;
	int baid;
	u16 status = WLAN_STATUS_REQUEST_DECLINED;

	lockdep_assert_wiphy(mld->wiphy);

	if (skb->len < offsetof(struct ieee80211_mgmt,
				u.action.addba_req.variable)) {
		IWL_WARN(mld, "wonder-agg: ADDBA req truncated (len=%u)\n",
			 skb->len);
		return;
	}

	dialog_token = req->u.action.addba_req.dialog_token;
	capab    = le16_to_cpu(req->u.action.addba_req.capab);
	timeout  = le16_to_cpu(req->u.action.addba_req.timeout);
	ssn      = le16_to_cpu(req->u.action.addba_req.start_seq_num) >> 4;
	tid      = u16_get_bits(capab, IEEE80211_ADDBA_PARAM_TID_MASK);
	buf_size = u16_get_bits(capab, IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK);

	IWL_INFO(mld,
		 "wonder-agg: ADDBA req from %pM tid=%u ssn=%u buf=%u\n",
		 req->sa, tid, ssn, buf_size);

	if (tid >= IEEE80211_NUM_TIDS) {
		IWL_WARN(mld, "wonder-agg: ADDBA bad tid=%u, declining\n",
			 tid);
		goto send_resp;
	}

	if (!mld->fw_status.running) {
		IWL_WARN(mld,
			 "wonder-agg: FW not running, declining ADDBA tid=%u\n",
			 tid);
		goto send_resp;
	}

	sta = iwl_mld_wonder_find_sta(wonder_ctx, req->sa);
	if (!sta) {
		IWL_WARN(mld,
			 "wonder-agg: unknown sta %pM, declining ADDBA tid=%u\n",
			 req->sa, tid);
		goto send_resp;
	}

	if (buf_size == 0)
		buf_size = WONDER_DEFAULT_RX_BA_BUF_SIZE;
	else if (buf_size > WONDER_MAX_RX_BA_BUF_SIZE)
		buf_size = WONDER_MAX_RX_BA_BUF_SIZE;

	sta_mask = BIT(sta->sta_id);
	session  = &sta->rx_ba[tid];

	if (session->active) {
		IWL_DEBUG_HT(mld,
			     "wonder-agg: re-ADDBA tid=%u, stop baid=%u first\n",
			     tid, session->baid);
		iwl_mld_wonder_baid_free(mld, sta_mask, tid);
		session->active = false;
	}

	baid = iwl_mld_wonder_baid_alloc(mld, sta_mask, tid, ssn, buf_size);
	if (baid < 0) {
		IWL_WARN(mld,
			 "wonder-agg: BAID alloc failed tid=%u err=%d\n",
			 tid, baid);
		goto send_resp;
	}

	session->active       = true;
	session->baid         = (u8)baid;
	session->dialog_token = dialog_token;
	session->ssn          = ssn;
	session->buf_size     = buf_size;
	status                = WLAN_STATUS_SUCCESS;

	IWL_DEBUG_HT(mld,
		     "wonder-agg: ADDBA tid=%u baid=%u sta_mask=0x%x accepted\n",
		     tid, (u8)baid, sta_mask);

send_resp:
	if (WARN_ON(!wonder_ctx->netdev))
		return;

	resp_skb = wonder_build_addba_resp(req->sa,
					   wonder_ctx->netdev->dev_addr,
					   req->bssid,
					   dialog_token, tid,
					   buf_size, timeout, status);
	if (!resp_skb) {
		IWL_ERR(mld, "wonder-agg: failed to alloc ADDBA resp skb\n");
		return;
	}

	IWL_INFO(mld,
		 "wonder-agg: sending ADDBA resp to %pM tid=%u status=%u\n",
		 req->sa, tid, status);

	if (iwl_mld_wonder_tx(wonder_ctx, resp_skb, &txd)) {
		IWL_WARN(mld, "wonder-agg: ADDBA resp TX failed tid=%u\n",
			 tid);
		dev_kfree_skb(resp_skb);
	}
}

static void
wonder_handle_delba(struct iwl_mld_wonder_ctx *wonder_ctx,
		    struct sk_buff *skb)
{
	struct iwl_mld *mld = wonder_ctx->mld;
	struct ieee80211_mgmt *req = (void *)skb->data;
	struct iwl_mld_wonder_sta *sta;
	struct iwl_mld_wonder_rx_ba *session;
	u16 params, tid;
	u32 sta_mask;

	lockdep_assert_wiphy(mld->wiphy);

	if (skb->len < offsetof(struct ieee80211_mgmt, u.action.delba) +
			(int)sizeof(req->u.action.delba)) {
		IWL_WARN(mld, "wonder-agg: DELBA truncated (len=%u)\n",
			 skb->len);
		return;
	}

	params = le16_to_cpu(req->u.action.delba.params);
	tid    = u16_get_bits(params, IEEE80211_DELBA_PARAM_TID_MASK);

	IWL_INFO(mld, "wonder-agg: DELBA from %pM tid=%u\n", req->sa, tid);

	if (tid >= IEEE80211_NUM_TIDS) {
		IWL_WARN(mld, "wonder-agg: DELBA bad tid=%u\n", tid);
		return;
	}

	sta = iwl_mld_wonder_find_sta(wonder_ctx, req->sa);
	if (!sta) {
		IWL_DEBUG_HT(mld,
			     "wonder-agg: DELBA unknown sta %pM tid=%u\n",
			     req->sa, tid);
		return;
	}

	session = &sta->rx_ba[tid];
	if (!session->active) {
		IWL_DEBUG_HT(mld,
			     "wonder-agg: DELBA inactive tid=%u, ignoring\n",
			     tid);
		return;
	}

	if (!mld->fw_status.running ||
	    sta->sta_id == IWL_INVALID_STA) {
		IWL_DEBUG_HT(mld,
			     "wonder-agg: DELBA skip FW cmd tid=%u\n", tid);
		goto clear;
	}

	sta_mask = BIT(sta->sta_id);
	iwl_mld_wonder_baid_free(mld, sta_mask, tid);

clear:
	IWL_DEBUG_HT(mld, "wonder-agg: DELBA done tid=%u baid=%u\n",
		     tid, session->baid);
	session->active = false;
}

/* ------------------------------------------------------------------ */
/* wiphy_work handler -- dequeues and processes pending BACK frames    */
/* ------------------------------------------------------------------ */

static void iwl_mld_wonder_agg_work(struct wiphy *wiphy,
				    struct wiphy_work *work)
{
	struct iwl_mld_wonder_ctx *wonder_ctx =
		container_of(work, struct iwl_mld_wonder_ctx, agg_work);
	struct ieee80211_mgmt *mgmt;
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&wonder_ctx->agg_pending))) {
		mgmt = (void *)skb->data;

		IWL_DEBUG_HT(wonder_ctx->mld,
			     "wonder-agg: work: action=%u from %pM\n",
			     mgmt->u.action.action_code, mgmt->sa);

		switch (mgmt->u.action.action_code) {
		case WLAN_ACTION_ADDBA_REQ:
			wonder_handle_addba_req(wonder_ctx, skb);
			break;
		case WLAN_ACTION_DELBA:
			wonder_handle_delba(wonder_ctx, skb);
			break;
		default:
			IWL_WARN(wonder_ctx->mld,
				 "wonder-agg: unexpected action=%u\n",
				 mgmt->u.action.action_code);
			break;
		}

		dev_kfree_skb(skb);
	}
}

/* ------------------------------------------------------------------ */
/* Called from wonder-rx.c (RX softirq) to queue a BACK action frame  */
/* ------------------------------------------------------------------ */

void iwl_mld_wonder_rx_queue_back_action(struct iwl_mld_wonder_ctx *wonder_ctx,
					 struct sk_buff *skb)
{
	skb_queue_tail(&wonder_ctx->agg_pending, skb);
	wiphy_work_queue(wonder_ctx->mld->wiphy, &wonder_ctx->agg_work);
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

void iwl_mld_wonder_agg_init(struct iwl_mld_wonder_ctx *wonder_ctx,
			     struct iwl_mld *mld)
{
	skb_queue_head_init(&wonder_ctx->agg_pending);
	wiphy_work_init(&wonder_ctx->agg_work, iwl_mld_wonder_agg_work);
	IWL_DEBUG_HT(mld, "wonder-agg: initialised\n");
}

void iwl_mld_wonder_agg_stop_sta(struct iwl_mld *mld,
				 struct iwl_mld_wonder_sta *sta)
{
	u32 sta_mask;
	int tid;

	lockdep_assert_wiphy(mld->wiphy);

	if (sta->sta_id == IWL_INVALID_STA)
		return;

	sta_mask = BIT(sta->sta_id);
	for (tid = 0; tid < IEEE80211_NUM_TIDS; tid++) {
		if (!sta->rx_ba[tid].active)
			continue;
		IWL_DEBUG_HT(mld,
			     "wonder-agg: teardown tid=%d baid=%u\n",
			     tid, sta->rx_ba[tid].baid);
		iwl_mld_wonder_baid_free(mld, sta_mask, tid);
		sta->rx_ba[tid].active = false;
	}
}

void iwl_mld_wonder_agg_stop_all(struct iwl_mld_wonder_ctx *wonder_ctx)
{
	struct iwl_mld *mld = wonder_ctx->mld;
	int i;

	lockdep_assert_wiphy(mld->wiphy);

	for (i = 0; i < IWL_MLD_WONDER_MAX_STAS; i++) {
		struct iwl_mld_wonder_sta *sta = &wonder_ctx->stas[i];

		iwl_mld_wonder_agg_stop_sta(mld, sta);
	}
}
