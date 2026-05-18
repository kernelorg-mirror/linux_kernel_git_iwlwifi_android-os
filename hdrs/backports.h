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


#include <crypto/aes.h>
#include <linux/string.h>
#include <crypto/utils.h>

int iwl7000_aes_expandkey(struct crypto_aes_ctx *ctx, const u8 *in_key,
			  unsigned int key_len);
void iwl7000_aes_encrypt(const struct crypto_aes_ctx *ctx, u8 *out,
			 const u8 *in);

/*
 * Compat: provide struct aes_enckey wrapping the old struct crypto_aes_ctx.
 */
struct aes_enckey {
	struct crypto_aes_ctx ctx;
};

static inline int aes_prepareenckey(struct aes_enckey *key,
				    const u8 *in_key, size_t key_len)
{
	return iwl7000_aes_expandkey(&key->ctx, in_key, key_len);
}

/**
 * struct aes_cmac_key - Prepared key for AES-CMAC
 */
struct aes_cmac_key {
	struct aes_enckey aes;
	union {
		u8 b[AES_BLOCK_SIZE];
		__be64 w[2];
	} k_final[2];
};

/**
 * struct aes_cmac_ctx - Context for computing an AES-CMAC value
 */
struct aes_cmac_ctx {
	const struct aes_cmac_key *key;
	size_t partial_len;
	u8 h[AES_BLOCK_SIZE];
};

static inline void _bp_aes_enc(const struct aes_enckey *key,
			       u8 out[AES_BLOCK_SIZE],
			       const u8 in[AES_BLOCK_SIZE])
{
	iwl7000_aes_encrypt(&key->ctx, out, in);
}

static inline int aes_cmac_preparekey(struct aes_cmac_key *key,
				      const u8 *in_key, size_t key_len)
{
	u64 hi, lo, mask;
	int err;
	int i;

	err = aes_prepareenckey(&key->aes, in_key, key_len);
	if (err)
		return err;

	memset(key->k_final[0].b, 0, AES_BLOCK_SIZE);
	_bp_aes_enc(&key->aes, key->k_final[0].b, key->k_final[0].b);
	hi = be64_to_cpu(key->k_final[0].w[0]);
	lo = be64_to_cpu(key->k_final[0].w[1]);
	for (i = 0; i < 2; i++) {
		mask = ((s64)hi >> 63) & 0x87;
		hi = (hi << 1) ^ (lo >> 63);
		lo = (lo << 1) ^ mask;
		key->k_final[i].w[0] = cpu_to_be64(hi);
		key->k_final[i].w[1] = cpu_to_be64(lo);
	}
	return 0;
}

static inline void aes_cmac_init(struct aes_cmac_ctx *ctx,
				 const struct aes_cmac_key *key)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->key = key;
}

static inline void aes_cmac_update(struct aes_cmac_ctx *ctx,
				   const u8 *data, size_t data_len)
{
	size_t nblocks;

	if (ctx->partial_len) {
		size_t l = min(data_len, AES_BLOCK_SIZE - ctx->partial_len);

		crypto_xor(&ctx->h[ctx->partial_len], data, l);
		data += l;
		data_len -= l;
		ctx->partial_len += l;
		if (data_len == 0)
			return;
		_bp_aes_enc(&ctx->key->aes, ctx->h, ctx->h);
	}

	nblocks = data_len / AES_BLOCK_SIZE;
	data_len %= AES_BLOCK_SIZE;

	if (nblocks == 0) {
		crypto_xor(ctx->h, data, data_len);
		ctx->partial_len = data_len;
	} else if (data_len != 0) {
		while (nblocks-- > 0) {
			crypto_xor(ctx->h, data, AES_BLOCK_SIZE);
			data += AES_BLOCK_SIZE;
			_bp_aes_enc(&ctx->key->aes, ctx->h, ctx->h);
		}
		crypto_xor(ctx->h, data, data_len);
		ctx->partial_len = data_len;
	} else {
		while (nblocks-- > 1) {
			crypto_xor(ctx->h, data, AES_BLOCK_SIZE);
			data += AES_BLOCK_SIZE;
			_bp_aes_enc(&ctx->key->aes, ctx->h, ctx->h);
		}
		crypto_xor(ctx->h, data, AES_BLOCK_SIZE);
		ctx->partial_len = AES_BLOCK_SIZE;
	}
}

static inline void aes_cmac_final(struct aes_cmac_ctx *ctx,
				  u8 out[AES_BLOCK_SIZE])
{
	if (ctx->partial_len == AES_BLOCK_SIZE) {
		crypto_xor(ctx->h, ctx->key->k_final[0].b, AES_BLOCK_SIZE);
	} else {
		ctx->h[ctx->partial_len] ^= 0x80;
		crypto_xor(ctx->h, ctx->key->k_final[1].b, AES_BLOCK_SIZE);
	}
	_bp_aes_enc(&ctx->key->aes, out, ctx->h);
	memzero_explicit(ctx, sizeof(*ctx));
}

static inline void aes_cmac(const struct aes_cmac_key *key, const u8 *data,
			    size_t data_len, u8 out[AES_BLOCK_SIZE])
{
	struct aes_cmac_ctx ctx;

	aes_cmac_init(&ctx, key);
	aes_cmac_update(&ctx, data, data_len);
	aes_cmac_final(&ctx, out);
}

#define system_dfl_wq system_wq
#define system_percpu_wq system_wq

#define NL80211_EXT_FEATURE_ASSOC_FRAME_ENCRYPTION -1
#define NL80211_EXT_FEATURE_EPPKE -1
#define NL80211_EXT_FEATURE_IEEE8021X_AUTH -1

/*
 * 2-arg strscpy_pad compat: newer kernels infer size from sizeof(dest),
 * but this kernel requires the explicit 3-arg form.
 */
static inline ssize_t __bp_strscpy_pad(char *dst, const char *src, size_t cnt)
{
	return strscpy_pad(dst, src, cnt);
}
#undef strscpy_pad
#define __bp_strscpy_pad3(dst, src, cnt) __bp_strscpy_pad(dst, src, cnt)
#define __bp_strscpy_pad2(dst, src) __bp_strscpy_pad(dst, src, sizeof(dst))
#define __bp_strscpy_pad_pick(dst, src, cnt, fn, ...) fn
#define strscpy_pad(dst, ...) \
	__bp_strscpy_pad_pick(dst, ##__VA_ARGS__, __bp_strscpy_pad3, __bp_strscpy_pad2)(dst, __VA_ARGS__)
