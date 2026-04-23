/*
 * bignum_ops.c -- libtommath operations in a tcl.h-free compilation unit.
 *
 * This file includes <tommath.h> directly (from our private libtommath
 * subproject) and must NOT include <tcl.h> or any header that pulls it in.
 * The MP_32BIT / MP_64BIT flag is set by the build system to match the
 * target Tcl's mp_digit width.
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <defer.h>

#include <tommath.h>
#include <limits.h>
#include <bignum_ops.h>

int bignum_from_uint64(mp_int *mp, uint64_t val)
{
	int		rc;
	const int	digits = (sizeof(uint64_t)*CHAR_BIT + MP_DIGIT_BIT-1) / MP_DIGIT_BIT;

	if (MP_OKAY != (rc = mp_init_size(mp, digits))) return rc;
	mp->sign = MP_ZPOS;
	mp->used = digits;
	for (int i=0; i<digits; i++)
		mp->dp[i] = (mp_digit)((val >> (i * MP_DIGIT_BIT)) & MP_MASK);
	mp_clamp(mp);
	return MP_OKAY;
}

int bignum_from_nint64(mp_int *mp, uint64_t val)
{
	int rc;

	if (MP_OKAY != (rc = bignum_from_uint64(mp, val)))	return rc;
	defer { if (rc != MP_OKAY) mp_clear(mp); }
	if (MP_OKAY != (rc = mp_add_d(mp, 1, mp)		))	return rc;
	if (MP_OKAY != (rc = mp_neg(mp, mp)				))	return rc;
	return MP_OKAY;
}

int bignum_from_ubin(mp_int *mp, const uint8_t *bytes, size_t len)
{
	int rc;

	if (MP_OKAY != (rc = mp_init(mp)					))	return rc;
	defer { if (rc != MP_OKAY) mp_clear(mp); }
	if (MP_OKAY != (rc = mp_from_ubin(mp, bytes, len)	))	return rc;
	return MP_OKAY;
}

int bignum_from_nbin(mp_int *mp, const uint8_t *bytes, size_t len)
{
	int rc;

	if (MP_OKAY != (rc = bignum_from_ubin(mp, bytes, len)	))	return rc;
	defer { if (rc != MP_OKAY) mp_clear(mp); }
	if (MP_OKAY != (rc = mp_add_d(mp, 1, mp)				))	return rc;
	if (MP_OKAY != (rc = mp_neg(mp, mp)						))	return rc;
	return MP_OKAY;
}

int bignum_to_ubin(const mp_int *mp, uint8_t *buf, size_t maxlen, size_t *written)
{
	return mp_to_ubin(mp, buf, maxlen, written);
}

size_t bignum_ubin_size(const mp_int *mp)
{
	return mp_ubin_size(mp);
}

int bignum_cmp(const mp_int *a, const mp_int *b)
{
	return mp_cmp(a, b);
}

void bignum_clear(mp_int *mp)
{
	mp_clear(mp);
}
