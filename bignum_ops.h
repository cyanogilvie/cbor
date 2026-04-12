/*
 * bignum_ops.h -- Thin interface for libtommath operations compiled
 * separately from Tcl headers to avoid type conflicts.
 *
 * Requires: mp_int must be defined before including this header
 *           (via either <tcl.h>+<tclTomMath.h> or <tommath.h>).
 */

#ifndef BIGNUM_OPS_H
#define BIGNUM_OPS_H

#include <stdint.h>
#include <stddef.h>

int bignum_from_uint64(mp_int *mp, uint64_t val);
int bignum_from_nint64(mp_int *mp, uint64_t val);  /* result: -1 - val */
int bignum_from_ubin(mp_int *mp, const uint8_t *bytes, size_t len);
int bignum_from_nbin(mp_int *mp, const uint8_t *bytes, size_t len);  /* result: -1 - n */
int bignum_to_ubin(const mp_int *mp, uint8_t *buf, size_t maxlen, size_t *written);
size_t bignum_ubin_size(const mp_int *mp);
int bignum_cmp(const mp_int *a, const mp_int *b);
void bignum_clear(mp_int *mp);

#endif /* BIGNUM_OPS_H */
