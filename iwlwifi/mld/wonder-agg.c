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
#include <net/mac80211.h>

#include "fw/api/datapath.h"
#include "agg.h"
#include "hcmd.h"
#include "iwl-trans.h"
#include "mld.h"
#include "wonder.h"
#include "wonder-agg.h"
#include "wonder-rx.h"
#include "wonder-sta.h"
#include "wonder-tx.h"

#define WONDER_MAX_RX_BA_BUF_SIZE	IEEE80211_MAX_AMPDU_BUF_EHT
#define WONDER_DEFAULT_RX_BA_BUF_SIZE	64
/* One ADDBA + one DELBA pending per possible wonder peer. */
#define WONDER_MAX_AGG_PENDING		(2 * IWL_MLD_WONDER_MAX_STAS)

/**
 * struct iwl_mld_wonder_reorder_buf - per-queue RX reorder window state
 * @head_sn: next sequence number expected to be released
 * @num_stored: number of MPDUs buffered
 * @valid: a frame has been seen on this queue
 */
struct iwl_mld_wonder_reorder_buf {
	u16 head_sn;
	u16 num_stored;
	bool valid;
} ____cacheline_aligned_in_smp;

/**
 * struct iwl_mld_wonder_reorder_entry - one reorder window slot
 * @frames: skbs sharing this sequence number (more than one for A-MSDU)
 */
struct iwl_mld_wonder_reorder_entry {
	struct sk_buff_head frames;
};

/**
 * struct iwl_mld_wonder_rx_ba - RX Block Ack session data
 * @rcu_head: RCU head for freeing this data
 * @baid: FW-assigned BAID
 * @dialog_token: dialog token of the ADDBA request that set up the session
 * @buf_size: negotiated BA window size
 * @reorder_buf: per RX-queue reorder window state
 * @entries: reorder window slots, num_rxqs * @buf_size
 */
struct iwl_mld_wonder_rx_ba {
	struct rcu_head rcu_head;
	u8 baid;
	u8 dialog_token;
	u16 buf_size;
	struct iwl_mld_wonder_reorder_buf reorder_buf[IWL_MAX_RX_HW_QUEUES];
	struct iwl_mld_wonder_reorder_entry entries[];
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

static struct iwl_mld_wonder_rx_ba *
iwl_mld_wonder_reorder_alloc(struct iwl_mld *mld, u16 buf_size, u16 ssn)
{
	int num_rxqs = mld->trans->info.num_rxqs;
	struct iwl_mld_wonder_rx_ba *session;

	session = kzalloc_flex(*session, entries, num_rxqs * buf_size);
	if (!session)
		return NULL;

	session->buf_size = buf_size;

	for (int i = 0; i < num_rxqs; i++) {
		session->reorder_buf[i].head_sn = ssn;

		for (int j = 0; j < buf_size; j++) {
			struct sk_buff_head *frames =
				&session->entries[i * buf_size + j].frames;

			__skb_queue_head_init(frames);
		}
	}

	return session;
}

static void iwl_mld_wonder_reorder_free(struct iwl_mld *mld,
					struct iwl_mld_wonder_rx_ba *session)
{
	int num_rxqs = mld->trans->info.num_rxqs;

	for (int i = 0; i < num_rxqs; i++) {
		struct iwl_mld_wonder_reorder_buf *buf =
			&session->reorder_buf[i];

		WARN_ON(buf->num_stored);

		for (int j = 0; j < session->buf_size; j++) {
			int idx = i * session->buf_size + j;

			__skb_queue_purge(&session->entries[idx].frames);
		}
	}

	kfree_rcu(session, rcu_head);
}

static void iwl_mld_wonder_reorder_teardown(struct iwl_mld *mld,
					    struct iwl_mld_wonder_sta *sta,
					    int tid)
{
	struct iwl_mld_wonder_rx_ba *session =
		wiphy_dereference(mld->wiphy, sta->rx_ba[tid]);
	struct iwl_mld_delba_data data = { .baid = session->baid };

	iwl_mld_sync_rx_queues(mld, IWL_MLD_RXQ_NOTIF_DEL_BA,
			       &data, sizeof(data));
	RCU_INIT_POINTER(sta->rx_ba[tid], NULL);
	iwl_mld_wonder_reorder_free(mld, session);
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
	struct iwl_mld_wonder_rx_ba *session =
		wiphy_dereference(mld->wiphy, sta->rx_ba[tid]);

	lockdep_assert_wiphy(mld->wiphy);

	if (!session)
		return;

	iwl_mld_send_cmd_pdu(mld, cmd_id, &cmd);

	if (!WARN_ON(mld->num_rx_ba_sessions == 0))
		mld->num_rx_ba_sessions--;

	IWL_DEBUG_HT(mld,
		     "wonder-agg: RX BA stopped sta_id=%u tid=%d baid=%u\n",
		     sta->sta_id, tid, session->baid);
	iwl_mld_wonder_reorder_teardown(mld, sta, tid);
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
	session = wiphy_dereference(mld->wiphy, sta->rx_ba[tid]);

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

	session = iwl_mld_wonder_reorder_alloc(mld, buf_size, ssn);
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
	rcu_assign_pointer(sta->rx_ba[tid], session);
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

/*
 * RX reorder, mirrors iwl_mld_reorder() in agg.c. Each per-queue buffer is
 * only touched from that queue's RX softirq.
 */

static void wonder_release_frames(struct iwl_mld_wonder_ctx *wonder_ctx,
				  struct iwl_mld_wonder_rx_ba *session,
				  int queue, u16 nssn)
{
	struct iwl_mld_wonder_reorder_buf *buf = &session->reorder_buf[queue];
	u16 ssn = buf->head_sn;

	while (ieee80211_sn_less(ssn, nssn)) {
		struct sk_buff_head *list =
			&session->entries[queue * session->buf_size +
					  ssn % session->buf_size].frames;
		struct sk_buff *skb;

		ssn = ieee80211_sn_inc(ssn);

		while ((skb = __skb_dequeue(list))) {
			iwl_mld_wonder_deliver_skb(wonder_ctx, skb);
			buf->num_stored--;
		}
	}
	buf->head_sn = nssn;
}

enum iwl_mld_wonder_reorder_result
iwl_mld_wonder_reorder(struct iwl_mld_wonder_ctx *wonder_ctx, int queue,
		       struct sk_buff *skb,
		       const struct iwl_rx_mpdu_desc *mpdu_desc)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	u32 reorder = le32_to_cpu(mpdu_desc->reorder_data);
	bool amsdu, last_subframe, is_old_sn, is_dup;
	struct iwl_mld_wonder_reorder_buf *buf;
	struct iwl_mld *mld = wonder_ctx->mld;
	struct iwl_mld_wonder_rx_ba *session;
	struct iwl_mld_wonder_sta *sta;
	u16 nssn, sn;
	u8 baid, tid;
	int index;

	baid = u32_get_bits(reorder, IWL_RX_MPDU_REORDER_BAID_MASK);

	if (baid == IWL_RX_REORDER_DATA_INVALID_BAID)
		return IWL_MLD_WONDER_REORDER_PASS;

	if (!ieee80211_is_data_qos(hdr->frame_control) ||
	    is_multicast_ether_addr(hdr->addr1))
		return IWL_MLD_WONDER_REORDER_PASS;

	tid = ieee80211_get_tid(hdr);
	if (tid >= IWL_MAX_TID_COUNT)
		return IWL_MLD_WONDER_REORDER_PASS;

	sta = iwl_mld_wonder_find_sta(wonder_ctx, hdr->addr2);
	if (!sta)
		return IWL_MLD_WONDER_REORDER_PASS;

	session = rcu_dereference(sta->rx_ba[tid]);
	if (!session || session->baid != baid) {
		IWL_DEBUG_HT(mld,
			     "wonder-agg: valid BAID %u but no session, bypass (tid=%u)\n",
			     baid, tid);
		return IWL_MLD_WONDER_REORDER_PASS;
	}

	buf = &session->reorder_buf[queue];

	is_old_sn = !!(reorder & IWL_RX_MPDU_REORDER_BA_OLD_SN);
	if (!buf->valid && is_old_sn)
		return IWL_MLD_WONDER_REORDER_PASS;
	buf->valid = true;

	is_dup = !!(mpdu_desc->status &
		   cpu_to_le32(IWL_RX_MPDU_STATUS_DUPLICATE));
	if (is_dup || is_old_sn) {
		IWL_DEBUG_DROP(mld,
			       "wonder-agg: drop dup=%d old_sn=%d tid=%u baid=%u\n",
			       is_dup, is_old_sn, tid, baid);
		return IWL_MLD_WONDER_REORDER_DROP;
	}

	sn = u32_get_bits(reorder, IWL_RX_MPDU_REORDER_SN_MASK);
	nssn = u32_get_bits(reorder, IWL_RX_MPDU_REORDER_NSSN_MASK);
	amsdu = mpdu_desc->mac_flags2 & IWL_RX_MPDU_MFLG2_AMSDU;
	last_subframe = mpdu_desc->amsdu_info &
		IWL_RX_MPDU_AMSDU_LAST_SUBFRAME;

	if (!buf->num_stored && ieee80211_sn_less(sn, nssn)) {
		if (!amsdu || last_subframe)
			buf->head_sn = nssn;
		return IWL_MLD_WONDER_REORDER_PASS;
	}

	if (!buf->num_stored && sn == buf->head_sn) {
		if (!amsdu || last_subframe)
			buf->head_sn = ieee80211_sn_inc(buf->head_sn);
		return IWL_MLD_WONDER_REORDER_PASS;
	}

	index = sn % session->buf_size;
	__skb_queue_tail(&session->entries[queue * session->buf_size +
					   index].frames, skb);
	buf->num_stored++;

	if (!amsdu || last_subframe)
		wonder_release_frames(wonder_ctx, session, queue, nssn);
	else if (buf->num_stored == 1)
		buf->head_sn = nssn;

	return IWL_MLD_WONDER_REORDER_BUFFERED;
}

static struct iwl_mld_wonder_rx_ba *
iwl_mld_wonder_find_rx_ba(struct iwl_mld_wonder_ctx *wonder_ctx, u8 baid)
{
	for (int i = 0; i < IWL_MLD_WONDER_MAX_STAS; i++) {
		struct iwl_mld_wonder_sta *sta = &wonder_ctx->stas[i];

		if (sta->sta_id == IWL_INVALID_STA)
			continue;

		for (int tid = 0; tid < IWL_MAX_TID_COUNT; tid++) {
			struct iwl_mld_wonder_rx_ba *session =
				rcu_dereference(sta->rx_ba[tid]);

			if (session && session->baid == baid)
				return session;
		}
	}
	return NULL;
}

/* Return: true if @baid belongs to a wonder RX BA session */
bool iwl_mld_wonder_release_frames(struct iwl_mld *mld, u8 baid, u16 nssn,
				   int queue)
{
	struct iwl_mld_wonder_ctx *wonder_ctx = &iwl_mld_wonder_ctx;
	struct iwl_mld_wonder_rx_ba *session;

	if (wonder_ctx->mld != mld)
		return false;

	guard(rcu)();

	session = iwl_mld_wonder_find_rx_ba(wonder_ctx, baid);
	if (!session)
		return false;

	IWL_DEBUG_HT(mld, "wonder-agg: release notif baid=%u nssn=%u\n",
		     baid, nssn);
	wonder_release_frames(wonder_ctx, session, queue, nssn);

	return true;
}

/* Return: true if @baid belongs to a wonder RX BA session */
bool iwl_mld_wonder_del_ba(struct iwl_mld *mld, int queue, u8 baid)
{
	struct iwl_mld_wonder_ctx *wonder_ctx = &iwl_mld_wonder_ctx;
	struct iwl_mld_wonder_rx_ba *session;
	u16 nssn;

	if (wonder_ctx->mld != mld)
		return false;

	guard(rcu)();

	session = iwl_mld_wonder_find_rx_ba(wonder_ctx, baid);
	if (!session)
		return false;

	nssn = ieee80211_sn_add(session->reorder_buf[queue].head_sn,
				session->buf_size);
	wonder_release_frames(wonder_ctx, session, queue, nssn);

	return true;
}
