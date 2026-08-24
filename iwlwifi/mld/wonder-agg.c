// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2026 Intel Corporation
 */

/*
 * RX Block Ack session handling for wonder stations. mac80211 does not
 * handle BACK action frames on monitor VIFs, so ADDBA/DELBA are handled here.
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

#define WONDER_MAX_RX_BA_BUF_SIZE	IEEE80211_MAX_AMPDU_BUF_EHT
#define WONDER_DEFAULT_RX_BA_BUF_SIZE	64
/* One ADDBA + one DELBA pending per possible wonder peer. */
#define WONDER_MAX_AGG_PENDING		(2 * IWL_MLD_WONDER_MAX_STAS)

/**
 * struct iwl_mld_wonder_rx_ba - RX Block Ack session data
 * @baid: FW-assigned BAID
 * @dialog_token: dialog token of the ADDBA request that set up the session
 * @buf_size: negotiated BA window size
 */
struct iwl_mld_wonder_rx_ba {
	u8 baid;
	u8 dialog_token;
	u16 buf_size;
};

/**
 * iwl_mld_wonder_add_baid() - allocate an RX BAID in firmware
 * @mld: the MLD instance
 * @sta_mask: firmware station ID bitmask for the station
 * @tid: TID of the BA session
 * @ssn: starting sequence number
 * @buf_size: BA window size
 *
 * Sends RX_BAID_ALLOCATION_CONFIG_CMD (ADD).
 *
 * Return: allocated BAID (>= 0) on success, negative errno on failure.
 */
static int iwl_mld_wonder_add_baid(struct iwl_mld *mld, u32 sta_mask,
				   int tid, u16 ssn, u16 buf_size)
{
	struct iwl_rx_baid_cfg_cmd cmd = {
		.action = cpu_to_le32(IWL_RX_BAID_ACTION_ADD),
		.alloc.sta_id_mask = cpu_to_le32(sta_mask),
		.alloc.tid = tid,
		.alloc.ssn = cpu_to_le16(ssn),
		.alloc.win_size = cpu_to_le16(buf_size),
	};
	struct iwl_host_cmd hcmd = {
		.id = WIDE_ID(DATA_PATH_GROUP, RX_BAID_ALLOCATION_CONFIG_CMD),
		.flags = CMD_WANT_SKB,
		.len[0] = sizeof(cmd),
		.data[0] = &cmd,
	};
	struct iwl_rx_baid_cfg_resp *resp;
	struct iwl_rx_packet *pkt;
	u32 resp_len, baid;
	int ret;

	BUILD_BUG_ON(sizeof(*resp) != sizeof(baid));
	lockdep_assert_wiphy(mld->wiphy);

	if (mld->num_rx_ba_sessions >= IWL_MAX_BAID) {
		IWL_DEBUG_HT(mld, "wonder-agg: max RX BA sessions reached\n");
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
		ret = -EINVAL;
		goto out;
	}

	resp = (void *)pkt->data;
	baid = le32_to_cpu(resp->baid);

	if (IWL_FW_CHECK(mld, baid >= ARRAY_SIZE(mld->fw_id_to_ba),
			 "wonder-agg: FW returned invalid BAID %u\n", baid)) {
		ret = -EINVAL;
		goto out;
	}

	mld->num_rx_ba_sessions++;
	IWL_DEBUG_HT(mld,
		     "wonder-agg: RX BA started sta_mask=0x%x tid=%d baid=%u\n",
		     sta_mask, tid, baid);
	ret = baid;
out:
	iwl_free_resp(&hcmd);
	return ret;
}

/* Release the FW BAID and free the session, if any */
static void iwl_mld_wonder_stop_rx_ba(struct iwl_mld *mld,
				      struct iwl_mld_wonder_sta *sta, int tid)
{
	struct iwl_rx_baid_cfg_cmd cmd = {
		.action = cpu_to_le32(IWL_RX_BAID_ACTION_REMOVE),
		.remove.sta_id_mask = cpu_to_le32(BIT(sta->sta_id)),
		.remove.tid = cpu_to_le32(tid),
	};
	u32 cmd_id = WIDE_ID(DATA_PATH_GROUP, RX_BAID_ALLOCATION_CONFIG_CMD);
	struct iwl_mld_wonder_rx_ba *session = sta->rx_ba[tid];

	lockdep_assert_wiphy(mld->wiphy);

	if (!session)
		return;

	iwl_mld_send_cmd_pdu(mld, cmd_id, &cmd);

	if (!WARN_ON(mld->num_rx_ba_sessions == 0))
		mld->num_rx_ba_sessions--;

	IWL_DEBUG_HT(mld,
		     "wonder-agg: RX BA stopped sta_id=%u tid=%d baid=%u\n",
		     sta->sta_id, tid, session->baid);
	sta->rx_ba[tid] = NULL;
	kfree(session);
}

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

	resp->u.action.category = WLAN_CATEGORY_BACK;
	resp->u.action.action_code = WLAN_ACTION_ADDBA_RESP;

	resp->u.action.addba_resp.dialog_token = dialog_token;
	resp->u.action.addba_resp.status = cpu_to_le16(status);

	capab = u16_encode_bits(1, IEEE80211_ADDBA_PARAM_POLICY_MASK) |
		u16_encode_bits(tid, IEEE80211_ADDBA_PARAM_TID_MASK) |
		u16_encode_bits(buf_size,
				IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK);
	resp->u.action.addba_resp.capab = cpu_to_le16(capab);
	resp->u.action.addba_resp.timeout = cpu_to_le16(timeout);

	return skb;
}

static void
wonder_handle_addba_req(struct iwl_mld_wonder_ctx *wonder_ctx,
			struct sk_buff *skb)
{
	const struct wonder_txd txd = {
		.is_unicast = true,
		.frame_type = IEEE80211_FTYPE_MGMT,
		.tid = 0,
	};
	u16 status = WLAN_STATUS_REQUEST_DECLINED;
	u16 capab, tid, buf_size, timeout, ssn;
	struct iwl_mld *mld = wonder_ctx->mld;
	struct iwl_mld_wonder_rx_ba *session;
	struct iwl_mld_wonder_sta *sta;
	struct ieee80211_mgmt *req;
	struct sk_buff *resp_skb;
	u8 dialog_token;
	u32 sta_mask;
	int baid;

	lockdep_assert_wiphy(mld->wiphy);

	if (!pskb_may_pull(skb, IEEE80211_MIN_ACTION_SIZE(addba_req))) {
		IWL_DEBUG_HT(mld, "wonder-agg: ADDBA req truncated (len=%u)\n",
			     skb->len);
		return;
	}
	req = (void *)skb->data;

	dialog_token = req->u.action.addba_req.dialog_token;
	capab = le16_to_cpu(req->u.action.addba_req.capab);
	timeout = le16_to_cpu(req->u.action.addba_req.timeout);
	ssn = le16_to_cpu(req->u.action.addba_req.start_seq_num) >> 4;
	tid = u16_get_bits(capab, IEEE80211_ADDBA_PARAM_TID_MASK);
	buf_size = u16_get_bits(capab, IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK);

	IWL_DEBUG_HT(mld,
		     "wonder-agg: ADDBA req from %pM tid=%u ssn=%u buf=%u timeout=%u\n",
		     req->sa, tid, ssn, buf_size, timeout);

	if (tid >= IWL_MAX_TID_COUNT) {
		IWL_DEBUG_HT(mld, "wonder-agg: ADDBA bad tid=%u, declining\n",
			     tid);
		goto send_resp;
	}

	sta = iwl_mld_wonder_find_sta(wonder_ctx, req->sa);
	if (!sta) {
		IWL_DEBUG_HT(mld,
			     "wonder-agg: unknown sta %pM, declining ADDBA tid=%u\n",
			     req->sa, tid);
		goto send_resp;
	}

	if (buf_size == 0)
		buf_size = WONDER_DEFAULT_RX_BA_BUF_SIZE;
	else if (buf_size > WONDER_MAX_RX_BA_BUF_SIZE)
		buf_size = WONDER_MAX_RX_BA_BUF_SIZE;

	sta_mask = BIT(sta->sta_id);
	session = sta->rx_ba[tid];

	/*
	 * Same token: retransmitted or updated ADDBA, keep the session.
	 * TODO: the BA inactivity timeout is not enforced, so a timeout
	 * update needs no action here; add a session timer if peers use it.
	 */
	if (session && session->dialog_token == dialog_token) {
		IWL_DEBUG_HT(mld, "wonder-agg: updated ADDBA tid=%u\n", tid);
		status = WLAN_STATUS_SUCCESS;
		goto send_resp;
	}

	iwl_mld_wonder_stop_rx_ba(mld, sta, tid);

	session = kzalloc_obj(*session);
	if (!session)
		goto send_resp;

	baid = iwl_mld_wonder_add_baid(mld, sta_mask, tid, ssn, buf_size);
	if (baid < 0) {
		IWL_DEBUG_HT(mld,
			     "wonder-agg: BAID alloc failed tid=%u err=%d\n",
			     tid, baid);
		kfree(session);
		goto send_resp;
	}

	session->baid = (u8)baid;
	session->dialog_token = dialog_token;
	session->buf_size = buf_size;
	sta->rx_ba[tid] = session;
	status = WLAN_STATUS_SUCCESS;

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

	IWL_DEBUG_HT(mld,
		     "wonder-agg: sending ADDBA resp to %pM tid=%u status=%u\n",
		     req->sa, tid, status);

	local_bh_disable();
	if (iwl_mld_wonder_tx(wonder_ctx, resp_skb, &txd)) {
		IWL_DEBUG_HT(mld, "wonder-agg: ADDBA resp TX failed tid=%u\n",
			     tid);
		dev_kfree_skb(resp_skb);
	}
	local_bh_enable();
}

static void
wonder_handle_delba(struct iwl_mld_wonder_ctx *wonder_ctx,
		    struct sk_buff *skb)
{
	struct iwl_mld *mld = wonder_ctx->mld;
	struct iwl_mld_wonder_sta *sta;
	struct ieee80211_mgmt *req;
	u16 params, tid;

	lockdep_assert_wiphy(mld->wiphy);

	if (!pskb_may_pull(skb, IEEE80211_MIN_ACTION_SIZE(delba))) {
		IWL_DEBUG_HT(mld, "wonder-agg: DELBA truncated (len=%u)\n",
			     skb->len);
		return;
	}
	req = (void *)skb->data;

	params = le16_to_cpu(req->u.action.delba.params);
	tid = u16_get_bits(params, IEEE80211_DELBA_PARAM_TID_MASK);

	IWL_DEBUG_HT(mld, "wonder-agg: DELBA from %pM tid=%u\n", req->sa, tid);

	if (tid >= IWL_MAX_TID_COUNT) {
		IWL_DEBUG_HT(mld, "wonder-agg: DELBA bad tid=%u\n", tid);
		return;
	}

	sta = iwl_mld_wonder_find_sta(wonder_ctx, req->sa);
	if (!sta) {
		IWL_DEBUG_HT(mld,
			     "wonder-agg: DELBA unknown sta %pM tid=%u\n",
			     req->sa, tid);
		return;
	}

	iwl_mld_wonder_stop_rx_ba(mld, sta, tid);
}

/* Runs under the mld wiphy lock, which every wondertap op also takes. */
void iwl_mld_wonder_agg_work(struct wiphy *wiphy, struct wiphy_work *work)
{
	struct iwl_mld_wonder_ctx *wonder_ctx =
		container_of(work, struct iwl_mld_wonder_ctx, agg_work);
	struct ieee80211_mgmt *mgmt;
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&wonder_ctx->agg_pending))) {
		mgmt = (void *)skb->data;

		switch (mgmt->u.action.action_code) {
		case WLAN_ACTION_ADDBA_REQ:
			wonder_handle_addba_req(wonder_ctx, skb);
			break;
		case WLAN_ACTION_DELBA:
			wonder_handle_delba(wonder_ctx, skb);
			break;
		default:
			IWL_DEBUG_HT(wonder_ctx->mld,
				     "wonder-agg: unexpected action=%u\n",
				     mgmt->u.action.action_code);
			break;
		}

		dev_kfree_skb(skb);
	}
}

void iwl_mld_wonder_rx_queue_back_action(struct iwl_mld_wonder_ctx *wonder_ctx,
					 struct sk_buff *skb)
{
	if (skb_queue_len(&wonder_ctx->agg_pending) >= WONDER_MAX_AGG_PENDING) {
		IWL_DEBUG_HT(wonder_ctx->mld,
			     "wonder-agg: agg_pending full, dropping action frame\n");
		dev_kfree_skb(skb);
		return;
	}

	skb_queue_tail(&wonder_ctx->agg_pending, skb);
	wiphy_work_queue(wonder_ctx->mld->wiphy, &wonder_ctx->agg_work);
}

void iwl_mld_wonder_agg_stop(struct iwl_mld *mld,
			     struct iwl_mld_wonder_sta *sta)
{
	lockdep_assert_wiphy(mld->wiphy);

	for (int tid = 0; tid < IWL_MAX_TID_COUNT; tid++)
		iwl_mld_wonder_stop_rx_ba(mld, sta, tid);
}
