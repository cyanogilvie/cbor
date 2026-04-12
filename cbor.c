#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <tcl.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "compat_endian.h"

#include <tclstuff.h>
#include <tclTomMath.h>
#include <bignum_ops.h>
#if HAVE_DEFER_POLYFILL
#include <defer.h>
#endif

#include "cbor_encode.h"

enum svalue_types {
	S_FALSE = 20,
	S_TRUE  = 21,
	S_NULL  = 22,
	S_UNDEF = 23
};

enum cbor_mt {
	M_UINT = 0,
	M_NINT = 1,
	M_BSTR = 2,
	M_UTF8 = 3,
	M_ARR  = 4,
	M_MAP  = 5,
	M_TAG  = 6,
	M_REAL = 7
};

struct interp_cx {
	Tcl_Obj*	cbor_true;
	Tcl_Obj*	cbor_false;
	Tcl_Obj*	cbor_null;
	Tcl_Obj*	cbor_undefined;
	Tcl_Obj*	tcl_true;
};

// Shims to connect libtommath's memory management to Tcl's allocator.
// Required because Tcl's FreeBignum frees mp_int digit arrays with Tcl_Free.
void* tommath_malloc(size_t size)                                {return ckalloc(size);}
void* tommath_realloc(void* mem, size_t oldsize, size_t newsize) {(void)oldsize; return ckrealloc(mem, newsize);}
void* tommath_calloc(size_t nmemb, size_t size)                  {void* m=ckalloc(nmemb*size); memset(m, 0, nmemb*size); return m;}
void  tommath_free(void* mem, size_t size)                       {(void)size; ckfree(mem);}

enum indexmode {
	IDX_ABS,
	IDX_ENDREL
};

#define CBOR_TRUNCATED() \
	do { \
		Tcl_SetErrorCode(interp, "CBOR", "TRUNCATED", NULL); \
		Tcl_SetObjResult(interp, Tcl_ObjPrintf("CBOR value truncated")); \
		return TCL_ERROR; \
	} while(0)

#define CBOR_INVALID(...) \
	do { \
		Tcl_SetErrorCode(interp, "CBOR", "INVALID", NULL); \
		Tcl_SetObjResult(interp, Tcl_ObjPrintf("CBOR syntax error: " __VA_ARGS__)); \
		return TCL_ERROR; \
	} while(0)

#define CBOR_TRAILING() \
	do { \
		Tcl_SetErrorCode(interp, "CBOR", "TRAILING", NULL); \
		Tcl_SetObjResult(interp, Tcl_ObjPrintf("Excess bytes after CBOR value")); \
		return TCL_ERROR; \
	} while(0)

#define TAKE(n) \
	do { \
		const size_t	nb = n; \
		if ((size_t)(e-p) < nb) CBOR_TRUNCATED(); \
		valPtr = p; \
		p += nb; \
	} while (0)


static int cbor_matches(Tcl_Interp* interp, const uint8_t** pPtr, const uint8_t* e, Tcl_Obj* pathElem, int* matchesPtr);

static float decode_float(const uint8_t* p) { //{{{
	float		val;
	uint32_t	uval = be32toh(*(uint32_t*)p);
	memcpy(&val, &uval, sizeof(float));
	return val;
}

//}}}
static double decode_double(const uint8_t* p) { //{{{
	double		val;
	uint64_t	uval = be64toh(*(uint64_t*)p);
	memcpy(&val, &uval, sizeof(double));
	return val;
}

//}}}

// Private API {{{
// From RFC8949 {{{
static int well_formed_indefinite(Tcl_Interp* interp, const uint8_t** pPtr, const uint8_t* e, int breakable, int* mtPtr, uint8_t mt);

static double decode_half(const uint8_t* halfp) { //{{{
	uint32_t	half = (halfp[0] << 8) + halfp[1];
	uint32_t	exp = (half >> 10) & 0x1f;
	uint32_t	mant = half & 0x3ff;
	double		val;

	if (exp == 0)		val = ldexp(mant, -24);
	else if (exp != 31)	val = ldexp(mant + 1024, exp - 25);
	else				val = mant == 0 ? INFINITY : NAN;

	return half & 0x8000 ? -val : val;
}

//}}}
static int take(Tcl_Interp* interp, const uint8_t** pPtr, const uint8_t* end, size_t len, const uint8_t** partPtr) //{{{
{
	const uint8_t*	p = *pPtr;

	if ((size_t)(end - p) < len) CBOR_TRUNCATED();

	*partPtr = p;
	*pPtr = p + len;

	return TCL_OK;
}

//}}}
static int well_formed(Tcl_Interp* interp, const uint8_t** pPtr, const uint8_t* e, int breakable, int* mtPtr) //{{{
{
	const uint8_t*	part = NULL;

	// process initial bytes
	TEST_OK(take(interp, pPtr, e, 1, &part));
	uint8_t		ib = *(uint8_t*)part;
	uint8_t		mt = ib >> 5;
	uint8_t		ai;
	uint64_t	val;

	val = ai = ib & 0x1f;

	switch (ai) {
		case 24: TEST_OK(take(interp, pPtr, e, 1, &part)); val =          *(uint8_t*)part;  break;
		case 25: TEST_OK(take(interp, pPtr, e, 2, &part)); val = be16toh(*(uint16_t*)part); break;
		case 26: TEST_OK(take(interp, pPtr, e, 4, &part)); val = be32toh(*(uint32_t*)part); break;
		case 27: TEST_OK(take(interp, pPtr, e, 8, &part)); val = be64toh(*(uint64_t*)part); break;
		case 28: case 29: case 30: CBOR_INVALID("reserved additional info value: %d", ai);
		case 31: return well_formed_indefinite(interp, pPtr, e, breakable, mtPtr, mt);
	}
	// process content
	switch (mt) {
		// case 0, 1, 7 do not have content; just use val
		case 2: case 3: TEST_OK(take(interp, pPtr, e, val, &part)); break; // bytes/UTF-8
		case 4: for (uint64_t i=0; i<val;   i++) TEST_OK(well_formed(interp, pPtr, e, 0, mtPtr)); break;
		case 5: for (uint64_t i=0; i<val*2; i++) TEST_OK(well_formed(interp, pPtr, e, 0, mtPtr)); break;
		case 6: TEST_OK(well_formed(interp, pPtr, e, 0, mtPtr)); break;     // 1 embedded data item
		case 7: if (ai == 24 && val < 32) CBOR_INVALID("bad simple value: %" PRIu64, val);
	}
	if (mtPtr) *mtPtr = mt; // definite-length data item

	return TCL_OK;
}

//}}}
static int well_formed_indefinite(Tcl_Interp* interp, const uint8_t** pPtr, const uint8_t* e, int breakable, int* mtPtr, uint8_t mt) //{{{
{
	uint8_t	it;
	int		res;

	switch (mt) {
		case 2:
		case 3:
			for (;;) {
				TEST_OK(well_formed(interp, pPtr, e, 1, &res));
				if (res == -1) break;
				it = res;
				// need definite-length chunk of the same type
				if (it != mt) CBOR_INVALID("indefinite-length chunk type: %d doesn't match parent: %d", it, mt);
			}
			break;

		case 4:
			for (;;) {
				TEST_OK(well_formed(interp, pPtr, e, 1, &res));
				if (res == -1) break;
			}
			break;

		case 5:
			for (;;) {
				TEST_OK(well_formed(interp, pPtr, e, 1, &res));
				if (res == -1) break;
				TEST_OK(well_formed(interp, pPtr, e, 0, &res));
			}
			break;

		case 7:
			if (breakable) {
				if (mtPtr) *mtPtr = -1;	// signal break out
				return TCL_OK;
			}
			CBOR_INVALID("break outside indefinite-length data item");

		default:
			CBOR_INVALID("indefinite-length data item with major type: %d", mt);
	}
	if (mtPtr) *mtPtr = 99;	// indefinite-length data item

	return TCL_OK;
}

//}}}
// From RFC8948 }}}

static int parse_index(Tcl_Interp* interp, Tcl_Obj* obj, enum indexmode* mode, ssize_t* ofs) //{{{
{
	const char*		str = Tcl_GetString(obj);
	char*			end;

	if (
		str[0] == 'e' &&
		str[1] == 'n' &&
		str[2] == 'd' &&
		str[3] == '-'
	) {
		const long long val = strtoll(str+4, &end, 10) * -1;
		if (end[0] != 0) THROW_ERROR("Invalid index");

		*mode = IDX_ENDREL;
		*ofs = val;
	} else {
		const long long val = strtoll(str, &end, 10);
		if (end[0] != 0) THROW_ERROR("Invalid index");
		*mode = IDX_ABS;
		*ofs = val;
	}

	return TCL_OK;
}

//}}}
static int utf8_codepoint(Tcl_Interp* interp, const uint8_t** p, const uint8_t* e, uint32_t* codepointPtr) //{{{
{
	const uint8_t*	c = *p;
	uint32_t		codepoint = 0;
	uint32_t		composed = 0;

part:
	if ((*c & 0x80) == 0) {
		if (c >= e) CBOR_TRUNCATED();
		codepoint = *c++;
	} else if ((*c & 0xE0) == 0xC0) { // Two byte encoding
		if (c+1 >= e) CBOR_TRUNCATED();
		codepoint  = (*c++ & 0x1F) << 6;
		codepoint |= (*c++ & 0x3F);
	} else if ((*c & 0xF0) == 0xE0) { // Three byte encoding
		if (c+2 >= e) CBOR_TRUNCATED();
		codepoint  = (*c++ & 0x0F) << 12;
		codepoint |= (*c++ & 0x3F) << 6;
		codepoint |= (*c++ & 0x3F);
	} else if ((*c & 0xF8) == 0xF0) { // Four byte encoding
		if (c+3 >= e) CBOR_TRUNCATED();
		codepoint  = (*c++ & 0x07) << 18;
		codepoint |= (*c++ & 0x3F) << 12;
		codepoint |= (*c++ & 0x3F) << 6;
		codepoint |= (*c++ & 0x3F);
	} else if ((*c & 0xFC) == 0xF8) { // Five byte encoding
		if (c+4 >= e) CBOR_TRUNCATED();
		codepoint  = (*c++ & 0x03) << 24;
		codepoint |= (*c++ & 0x3F) << 18;
		codepoint |= (*c++ & 0x3F) << 12;
		codepoint |= (*c++ & 0x3F) << 6;
		codepoint |= (*c++ & 0x3F);
	} else if ((*c & 0xFE) == 0xFC) { // Six byte encoding
		if (c+5 >= e) CBOR_TRUNCATED();
		codepoint  = (*c++ & 0x01) << 30;
		codepoint |= (*c++ & 0x3F) << 24;
		codepoint |= (*c++ & 0x3F) << 18;
		codepoint |= (*c++ & 0x3F) << 12;
		codepoint |= (*c++ & 0x3F) << 6;
		codepoint |= (*c++ & 0x3F);
	} else {	// Invalid encoding
		CBOR_INVALID("Invalid UTF-8 encoding");
	}

	if (composed) {
		if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) { // Low surrogate
			codepoint = composed + (codepoint - 0xDC00);
		} else {
			CBOR_INVALID("UTF-8 low surrogate missing");
		}
	} else {
		if (codepoint >= 0xD800 && codepoint <= 0xDBFF) { // High surrogate
			composed = 0x10000 + ((codepoint - 0xD800) << 16);
			goto part;
		} else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) { // Orphan low surrogate
			CBOR_INVALID("UTF-8 orphan low surrogate");
		}
	}

	*p = c;
	*codepointPtr = codepoint;

	return TCL_OK;
}

//}}}
static Tcl_Obj* new_tcl_uint64(uint64_t val) //{{{
{
	if (val <= INT64_MAX) {
		return Tcl_NewWideIntObj(val);
	} else {
		mp_int	n;
		if (bignum_from_uint64(&n, val) != 0) Tcl_Panic("bignum_from_uint64 failed");
		Tcl_Obj* res = Tcl_NewBignumObj(&n);
		bignum_clear(&n);
		return res;
	}
}

//}}}
static Tcl_Obj* new_tcl_nint64(uint64_t val) //{{{
{
	if (val <= (uint64_t)(-(INT64_MIN+1))) {
		return Tcl_NewWideIntObj(-1 - (int64_t)val);
	} else {
		mp_int	n;
		if (bignum_from_nint64(&n, val) != 0) Tcl_Panic("bignum_from_nint64 failed");
		Tcl_Obj* res = Tcl_NewBignumObj(&n);
		bignum_clear(&n);
		return res;
	}
}

//}}}
static int cbor_get_obj(Tcl_Interp* interp, const uint8_t** pPtr, const uint8_t* e, Tcl_Obj** resPtr, Tcl_Obj** tagsPtr) //{{{
{
	struct interp_cx*	l = Tcl_GetAssocData(interp, "cbor", NULL);

	const uint8_t*	p = *pPtr;	defer { *pPtr = p; };
	Tcl_Obj*		res = NULL;	defer { replace_tclobj(&res, NULL); };

	const uint8_t*	valPtr;

read_dataitem:
	TAKE(1);
	const uint8_t		ib = valPtr[0];
	const enum cbor_mt	mt = ib >> 5;
	const uint8_t		ai = ib & 0x1f;
	uint64_t			val = ai;

	switch (ai) {
		case 24: TAKE(1); val = *(uint8_t*)valPtr;           break;
		case 25: TAKE(2); val = be16toh(*(uint16_t*)valPtr); break;
		case 26: TAKE(4); val = be32toh(*(uint32_t*)valPtr); break;
		case 27: TAKE(8); val = be64toh(*(uint64_t*)valPtr); break;
		case 28: case 29: case 30: CBOR_INVALID("reserved additional info value: %d", ai);
	}

	if (ai == 31) {
		switch (mt) {
			case M_UINT:
			case M_NINT:
			case M_TAG:
			case M_REAL:
				CBOR_INVALID("invalid indefinite length for major type %d", mt);

			case M_BSTR:
			case M_UTF8:
			case M_ARR:
			case M_MAP:
				break;
		}
	}

	switch (mt) {
		case M_TAG:
			if (tagsPtr) TEST_OK(Tcl_ListObjAppendElement(interp, *tagsPtr, Tcl_NewWideIntObj(val)));
			goto read_dataitem;

		case M_UINT:	replace_tclobj(&res, new_tcl_uint64(val));	break;
		case M_NINT:	replace_tclobj(&res, new_tcl_nint64(val));	break;

		case M_BSTR:
		case M_UTF8:
		{
			if (ai == 31) {
				size_t	ofs = 0;
				replace_tclobj(&res,
					mt == M_UTF8 ?
						Tcl_NewObj() :
						Tcl_NewByteArrayObj(NULL, 0));

				for (;;) {
					TAKE(1);
					if (valPtr[0] == 0xFF) {break;}
					const enum cbor_mt	chunk_mt = valPtr[0] >> 5;
					if (chunk_mt != mt) CBOR_INVALID("String chunk type: %d doesn't match parent: %d", chunk_mt, mt);
					const uint8_t		chunk_ai = valPtr[0] & 0x1f;
					uint64_t			chunk_val;

					if (chunk_ai < 24) {
						chunk_val = chunk_ai;
					} else {
						switch (chunk_ai) {
							case 24: TAKE(1); chunk_val = *(uint8_t*)valPtr;           break;
							case 25: TAKE(2); chunk_val = be16toh(*(uint16_t*)valPtr); break;
							case 26: TAKE(4); chunk_val = be32toh(*(uint32_t*)valPtr); break;
							case 27: TAKE(8); chunk_val = be64toh(*(uint64_t*)valPtr); break;
							default: CBOR_INVALID("invalid chunk additional info: %d", chunk_ai);
						}
					}
					TAKE(chunk_val);

					if (chunk_mt == M_UTF8) {
						Tcl_AppendToObj(res, (const char*)valPtr, chunk_val);
					} else {
						uint8_t*	base = Tcl_SetByteArrayLength(res, ofs + chunk_val);
						memcpy(base + ofs, valPtr, chunk_val);
						ofs += chunk_val;
					}
				}
			} else {
				TAKE(val);
				if (val > INT64_MAX) THROW_ERROR("String / byte array length > INT64_MAX");
				replace_tclobj(&res,
					mt == M_UTF8 ?
						Tcl_NewStringObj((const char*)valPtr, val) :
						Tcl_NewByteArrayObj(valPtr, val));
			}
			break;
		}

		case M_ARR:
		{
			replace_tclobj(&res, Tcl_NewListObj(0, NULL));

			for (size_t i=0; ; i++) {
				if (ai == 31) {
					if (p >= e) CBOR_TRUNCATED();
					if (*p == 0xFF) {p++; break;}
				} else {
					if (i >= val) break;
				}

				Tcl_Obj* elem = NULL;
				defer { replace_tclobj(&elem, NULL); };
				TEST_OK(cbor_get_obj(interp, &p, e, &elem, NULL));
				TEST_OK(Tcl_ListObjAppendElement(interp, res, elem));
			}
			break;
		}

		case M_MAP:
		{
			replace_tclobj(&res, Tcl_NewDictObj());
			for (size_t i=0; ; i++) {
				if (ai == 31) {
					if (p >= e) CBOR_TRUNCATED();
					if (*p == 0xFF) {p++; break;}
				} else {
					if (i >= val) break;
				}

				Tcl_Obj*	k = NULL;
				Tcl_Obj*	v = NULL;
				defer {
					replace_tclobj(&k, NULL);
					replace_tclobj(&v, NULL);
				};

				TEST_OK(cbor_get_obj(interp, &p, e, &k, NULL));
				TEST_OK(cbor_get_obj(interp, &p, e, &v, NULL));
				TEST_OK(Tcl_DictObjPut(interp, res, k, v));
			}
			break;
		}

		case M_REAL:
		{
			if (ai < 20) {
				replace_tclobj(&res, Tcl_ObjPrintf("simple(%" PRIu64 ")", val));
			} else {
				switch (ai) {
					case S_FALSE:				replace_tclobj(&res, l->cbor_false);		break;
					case S_TRUE:				replace_tclobj(&res, l->cbor_true);			break;
					case S_NULL:				replace_tclobj(&res, l->cbor_null);			break;
					case S_UNDEF:				replace_tclobj(&res, l->cbor_undefined);	break;
					case 24:
						if (val < 32 || val > 255) CBOR_INVALID("invalid simple value: %" PRIu64, val);
						replace_tclobj(&res, Tcl_ObjPrintf("simple(%" PRIu64 ")", val));
						break;
					case 25:	replace_tclobj(&res, Tcl_NewDoubleObj( decode_half(valPtr)   ));	break;
					case 26:	replace_tclobj(&res, Tcl_NewDoubleObj( decode_float(valPtr)  ));	break;
					case 27:	replace_tclobj(&res, Tcl_NewDoubleObj( decode_double(valPtr) ));	break;
					default:	CBOR_INVALID("invalid additional info: %d", ai);
				}
			}
			break;
		}
	}

	replace_tclobj(resPtr, res);

	return TCL_OK;
}

//}}}
static int cbor_match_map(Tcl_Interp* interp, uint8_t ai, uint64_t val, const uint8_t** pPtr, const uint8_t* e, Tcl_Obj* pathElem, int* matchesPtr) //{{{
{
	Tcl_HashTable	remaining;
	Tcl_Size		size;
	int				skipping = 0;

	const uint8_t*	p = *pPtr;		defer { *pPtr = p; };

	Tcl_InitHashTable(&remaining, TCL_ONE_WORD_KEYS);	defer { Tcl_DeleteHashTable(&remaining); }

	Tcl_Obj*		dict = NULL;	defer { replace_tclobj(&dict, NULL); };

	if (TCL_OK == Tcl_DictObjSize(NULL, pathElem, &size)) {
		replace_tclobj(&dict, pathElem);	// Take a private copy to remove entries as we match them
	} else {
		// Not a dict, so we can't match (but it's not an error)
		skipping = 1;
	}

	if (!skipping && ai != 31 && val != (uint64_t)size)
		skipping = 1;	// If there are different numbers of entries, we can't match

	for (size_t i=0; ; i++) {
		if (ai == 31) {
			if (p >= e) CBOR_TRUNCATED();
			if (*p == 0xFF) {p++; break;}
		} else {
			if (i >= val) break;
		}

		if (skipping) {
			TEST_OK(well_formed(interp, &p, e, 0, NULL));
			TEST_OK(well_formed(interp, &p, e, 0, NULL));
			continue;
		}

		const uint8_t		key_mt = *p >> 5;
		switch (key_mt) {
			case M_TAG: continue;

			case M_UINT:
			case M_NINT:
			case M_BSTR:
			case M_UTF8:
			case M_REAL:
			{
				Tcl_Obj*	valobj = NULL;	// On loan from the dict
				int			matches;
				Tcl_Obj*	key = NULL;		defer { replace_tclobj(&key, NULL); };

				TEST_OK(cbor_get_obj(interp, &p, e, &key, NULL));
				TEST_OK(Tcl_DictObjGet(interp, dict, key, &valobj));
				if (valobj == NULL) {
					skipping = 1;
					TEST_OK(well_formed(interp, &p, e, 0, NULL));
					continue;
				}
				TEST_OK(cbor_matches(interp, &p, e, valobj, &matches));
				if (matches) {
					TEST_OK(Tcl_DictObjRemove(interp, dict, key));
					valobj = NULL;
					size--;
				} else {
					skipping = 1;
				}
				continue;
			}

			case M_ARR:
			case M_MAP:
			{
				// Defer dealing with these until there is nothing else that could cause a mismatch
				Tcl_HashEntry*	he = NULL;
				Tcl_CreateHashEntry(&remaining, i, NULL);
				Tcl_SetHashValue(he, (void*)p);
				TEST_OK(well_formed(interp, &p, e, 0, NULL));
				break;
			}
		}
	}

	if (skipping) goto mismatch;

	// Having knocked out the easier matches, try to match up the compound types
	Tcl_HashSearch	search;
	for (Tcl_HashEntry* he=Tcl_FirstHashEntry(&remaining, &search); he; he=Tcl_NextHashEntry(&search)) {
		if (size == 0) goto mismatch; // No more entries in the path, so we can't match

		const uint8_t*	rp = (const uint8_t*)Tcl_GetHashValue(he);
		int				done;
		Tcl_Obj*		k = NULL;	// On loan from the dict
		Tcl_Obj*		v = NULL;	// On loan from the dict
		int				matches = 0;

		Tcl_DictSearch	search;
		TEST_OK(Tcl_DictObjFirst(interp, dict, &search, &k, &v, &done));
		defer { Tcl_DictObjDone(&search); };
		for (; !done; Tcl_DictObjNext(&search, &k, &v, &done)) {
			const uint8_t*	rpc = rp;
			TEST_OK(cbor_matches(interp, &rpc, e, k, &matches));
			if (matches) {
				TEST_OK(cbor_matches(interp, &rpc, e, v, &matches));
				if (matches) {
					TEST_OK(Tcl_DictObjRemove(interp, dict, k));
					k = NULL;
					v = NULL;
					size--;
					break;
				}
			} else {
				TEST_OK(well_formed(interp, &p, e, 0, NULL));
			}
		}
		if (!matches) goto mismatch;	// No matching entry found in dict for remaining entry in map
	}

	if (size > 0) goto mismatch;	// Unmatched keys remain in dict

	*matchesPtr = 1;
	return TCL_OK;

mismatch:
	*matchesPtr = 0;
	return TCL_OK;
}

//}}}
static int cbor_matches(Tcl_Interp* interp, const uint8_t** pPtr, const uint8_t* e, Tcl_Obj* pathElem, int* matchesPtr) //{{{
{
	const uint8_t*			p = *pPtr;		defer { *pPtr = p; };
	int						matches = 0;

	const uint8_t*	valPtr;

data_item: // loop: read off tags
	// Read head {{{
	TAKE(1);
	const uint8_t		ib = valPtr[0];
	const enum cbor_mt	mt = ib >> 5;
	const uint8_t		ai = ib & 0x1f;
	uint64_t			val = ai;

	switch (ai) {
		case 24: TAKE(1); val = *(uint8_t*)valPtr;           break;
		case 25: TAKE(2); val = be16toh(*(uint16_t*)valPtr); break;
		case 26: TAKE(4); val = be32toh(*(uint32_t*)valPtr); break;
		case 27: TAKE(8); val = be64toh(*(uint64_t*)valPtr); break;
		case 28: case 29: case 30: CBOR_INVALID("reserved additional info value: %d", ai);
	}
	//}}}

	if (ai == 31) {
		switch (mt) {
			case M_UINT:
			case M_NINT:
			case M_TAG:
			case M_REAL:
				CBOR_INVALID("invalid indefinite length for major type %d", mt);

			case M_BSTR:
			case M_UTF8:
			case M_ARR:
			case M_MAP:
				break;
		}
	}

	switch (mt) {
		case M_TAG:	goto data_item;	// Skip tags
		case M_UINT: case M_NINT: // Compare as integers {{{
		{
			// Fast path: CBOR value fits in Tcl_WideInt
			if ((mt == M_UINT && val <= (uint64_t)INT64_MAX) ||
				(mt == M_NINT && val <= (uint64_t)(-(INT64_MIN+1)))) {
				Tcl_WideInt		cbor_wide = (mt == M_UINT) ? (Tcl_WideInt)val : (-1 - (Tcl_WideInt)val);
				Tcl_WideInt		path_wide;
				if (Tcl_GetWideIntFromObj(NULL, pathElem, &path_wide) != TCL_OK) {
					*matchesPtr = 0; // path element isn't an integer — no match
					return TCL_OK;
				}
				*matchesPtr = (path_wide == cbor_wide);
				return TCL_OK;
			}

			// Slow path: CBOR value exceeds WideInt range, compare as bignums
			{
				int		rc;
				mp_int	cbor_bn, path_bn;

				rc = (mt == M_UINT)
					? bignum_from_uint64(&cbor_bn, val)
					: bignum_from_nint64(&cbor_bn, val);
				if (rc != 0)
					THROW_ERROR("bignum construction failed in CBOR integer comparison");
				defer { bignum_clear(&cbor_bn); };

				if (Tcl_GetBignumFromObj(NULL, pathElem, &path_bn) != TCL_OK) {
					*matchesPtr = 0; // path element isn't a number — no match
					return TCL_OK;
				}
				defer { bignum_clear(&path_bn); };

				*matchesPtr = (bignum_cmp(&cbor_bn, &path_bn) == 0);
				return TCL_OK;
			}
		}
		//}}}
		case M_BSTR: // Compare as byte strings {{{
		{
			Tcl_Size			pathlen;
			const uint8_t*		pathval = (const uint8_t*)Tcl_GetBytesFromObj(NULL, pathElem, &pathlen);
			const uint8_t*const	pathend = pathval ? pathval + pathlen : NULL;
			const uint8_t*const	pe = (ai != 31) ? p + val : NULL;

			if (ai == 31) { // Indefinite length bytes: comprised of a sequence of definite-length byte dataitems {{{
				int skipping = !pathval; // no match possible if path isn't a byte string

				for (;;) {
					const uint8_t		chunk_ib = *p++;
					const enum cbor_mt	chunk_mt = chunk_ib >> 5;
					const uint8_t		chunk_ai = chunk_ib & 0x1f;
					uint64_t			chunk_val = ai;

					if (chunk_ib == 0xFF) break;
					if (chunk_mt != M_BSTR) CBOR_INVALID("wrong type for binary chunk: %d", chunk_mt);

					switch (chunk_ai) {
						case 24: TAKE(1); chunk_val = *(uint8_t*)valPtr;           break;
						case 25: TAKE(2); chunk_val = be16toh(*(uint16_t*)valPtr); break;
						case 26: TAKE(4); chunk_val = be32toh(*(uint32_t*)valPtr); break;
						case 27: TAKE(8); chunk_val = be64toh(*(uint64_t*)valPtr); break;
						case 28: case 29: case 30: CBOR_INVALID("reserved additional info value: %d", chunk_ai);
						case 31: CBOR_INVALID("cannot nest indefinite length chunks");
					}

					const uint8_t*	chunk_pe = p + chunk_val;

					if (skipping) {
						p = chunk_pe;
					} else {
						for (; pathval < pathend && p < chunk_pe;) {
							if (*pathval++ != *p++) {
								p = pe;
								skipping = 1;
								break;
							}
						}
					}
				}
				*matchesPtr = (!skipping && (pathval == pathend && p == pe));
				return TCL_OK;
				//}}}
			} else { // Definite length bytes {{{
				if (!pathval) { p = pe; *matchesPtr = 0; return TCL_OK; }
				for (; pathval < pathend && p < pe;) {
					if (*pathval++ != *p++) {
						p = pe;
						*matchesPtr = 0;
						return TCL_OK;
					}
				}
				*matchesPtr = (pathval == pathend && p == pe);
				return TCL_OK;
				//}}}
			}
			break;
		}
		//}}}
		case M_UTF8: // Compare as UTF-8 strings {{{
		{
			Tcl_Size			s_pathlen;
			const uint8_t*		s_pathval = (const uint8_t*)Tcl_GetStringFromObj(pathElem, &s_pathlen);
			const uint8_t*const	s_pathend = s_pathval + s_pathlen;
			const uint8_t*const	s_pe = p + val;

			if (ai == 31) { // Indefinite length UTF-8 string: comprised of a sequence of definite-length utf-8 dataitems {{{
				int skipping = 0;

				for (;;) {
					const uint8_t		chunk_ib = *p++;
					const enum cbor_mt	chunk_mt = chunk_ib >> 5;
					const uint8_t		chunk_ai = chunk_ib & 0x1f;
					uint64_t			chunk_val = ai;

					if (chunk_ib == 0xFF) break;
					if (chunk_mt != M_UTF8) CBOR_INVALID("wrong type for UTF-8 chunk: %d", chunk_mt);

					switch (chunk_ai) {
						case 24: TAKE(1); chunk_val = *(uint8_t*)valPtr;           break;
						case 25: TAKE(2); chunk_val = be16toh(*(uint16_t*)valPtr); break;
						case 26: TAKE(4); chunk_val = be32toh(*(uint32_t*)valPtr); break;
						case 27: TAKE(8); chunk_val = be64toh(*(uint64_t*)valPtr); break;
						case 28: case 29: case 30: CBOR_INVALID("reserved additional info value: %d", chunk_ai);
						case 31: CBOR_INVALID("cannot nest indefinite length chunks");
					}

					const uint8_t*	c_pe = p + chunk_val;

					if (skipping) {
						p = c_pe;
					} else {
						for (; s_pathval < s_pathend && p < c_pe;) {
							uint32_t	path_c;
							uint32_t	di_c;

							TEST_OK(utf8_codepoint(interp, &s_pathval, s_pathend, &path_c));
							TEST_OK(utf8_codepoint(interp, &p,         c_pe,      &di_c));
							if (path_c != di_c) {
								p = c_pe;
								skipping = 1;
								break;
							}
						}
					}
				}
				*matchesPtr = (!skipping && (s_pathval == s_pathend && p == s_pe));
				return TCL_OK;
				//}}}
			} else { // Definite length UTF-8 string {{{
				for (; s_pathval < s_pathend && p < s_pe;) {
					uint32_t	path_c;
					uint32_t	di_c;

					TEST_OK(utf8_codepoint(interp, &s_pathval, s_pathend, &path_c));
					TEST_OK(utf8_codepoint(interp, &p,         s_pe,      &di_c));
					if (path_c != di_c) {
						p = s_pe;
						*matchesPtr = 0;
						return TCL_OK;
					}
				}
				*matchesPtr = (s_pathval == s_pathend && p == s_pe);
				return TCL_OK;
				//}}}
			}
			break;
		}
		//}}}
		case M_ARR:  // Compare as a list {{{
		{
			Tcl_Size	oc;
			Tcl_Obj**	ov;
			if (TCL_OK != Tcl_ListObjGetElements(NULL, pathElem, &oc, &ov)) {
				// Skip remaining elements {{{
				for (;;) {
					int			res;
					TEST_OK(well_formed(interp, &p, e, 1, &res));
					if (res == -1) break;
				}
				//}}}
				*matchesPtr = 0;
				return TCL_OK;
			}
			if (ai == 31) { // Indefinite length array {{{
				for (int i=0; i<oc; i++) {
					if (*p == 0xFF) { // End of indefinite length array before end of pathelem
						p++;
						*matchesPtr = 0;
						return TCL_OK;
					}
					TEST_OK(cbor_matches(interp, &p, e, ov[i], &matches));
					if (!matches) {
						// Skip remaining elements {{{
						for (;;) {
							int		res;
							TEST_OK(well_formed(interp, &p, e, 1, &res));
							if (res == -1) break;
						}
						//}}}
						*matchesPtr = 0;
						return TCL_OK;
					}
				}
				if (*p != 0xFF) { // End of pathelem before end of indefinite length array
					// Skip remaining elements {{{
					for (;;) {
						int		res;
						TEST_OK(well_formed(interp, &p, e, 1, &res));
						if (res == -1) break;
					}
					//}}}
					*matchesPtr = 0;
					return TCL_OK;
				}
				*matchesPtr = 1;
				return TCL_OK;
				//}}}
			} else { // Definite length array {{{
				int		skipping = (size_t)oc != val;
				for (size_t i=0; i<val; i++) {
					if (skipping) {
						TEST_OK(well_formed(interp, &p, e, 0, NULL));
					} else {
						TEST_OK(cbor_matches(interp, &p, e, ov[i], &matches));
						if (!matches) {
							skipping = 1;
							continue;
						}
					}
				}
				if (!skipping) {
					*matchesPtr = 1;
					return TCL_OK;
				}
				//}}}
			}
			*matchesPtr = 0;
			return TCL_OK;
		}
		//}}}
		case M_MAP:  // Compare as dictionary {{{
		{
			TEST_OK(cbor_match_map(interp, ai, val, &p, e, pathElem, &matches));
			*matchesPtr = matches;
			return TCL_OK;
		}
		//}}}
		case M_REAL: // Compare as real values / simple values {{{
		{
			double rvalue;

			switch (ai) {
				case 20: case 21:	// Simple value: false / true
				{
					int boolval;
					TEST_OK(Tcl_GetBooleanFromObj(interp, pathElem, &boolval));
					*matchesPtr = (boolval == (ai == 21));
					return TCL_OK;
				}
				case 22: case 23:	// Simple value: null / undefined - treat zero length string as matching
				{
					Tcl_Size len;
					Tcl_GetStringFromObj(pathElem, &len);
					*matchesPtr = (len == 0);
					return TCL_OK;
				}
				case 25:	rvalue = decode_half(valPtr);	break;
				case 26:	rvalue = decode_float(valPtr);	break;
				case 27:	rvalue = decode_double(valPtr);	break;
				default:	CBOR_INVALID("invalid additional info: %d", ai);
			}

			double		pathval;
			TEST_OK(Tcl_GetDoubleFromObj(interp, pathElem, &pathval));
			*matchesPtr = (pathval == rvalue);
			return TCL_OK;
		}
		//}}}
	}

	return TCL_OK;
}

//}}}
// Diagnostic notation pretty-printer (RFC 8949 Section 8 / RFC 8610 EDN) {{{
static void diag_indent(Tcl_DString* ds, int depth) //{{{
{
	for (int i = 0; i < depth; i++) Tcl_DStringAppend(ds, "  ", 2);
}

//}}}
static int diag_item(Tcl_Interp* interp, const uint8_t** pPtr, const uint8_t* e, Tcl_DString* ds, int depth) //{{{
{
	const uint8_t*	p = *pPtr;		defer { *pPtr = p; };
	const uint8_t*	valPtr;

	TAKE(1);
	const uint8_t	ib = valPtr[0];
	const uint8_t	mt = ib >> 5;
	const uint8_t	ai = ib & 0x1f;
	uint64_t		val = ai;

	switch (ai) {
		case 24: TAKE(1); val = *(uint8_t*)valPtr;           break;
		case 25: TAKE(2); val = be16toh(*(uint16_t*)valPtr); break;
		case 26: TAKE(4); val = be32toh(*(uint32_t*)valPtr); break;
		case 27: TAKE(8); val = be64toh(*(uint64_t*)valPtr); break;
		case 28: case 29: case 30: CBOR_INVALID("reserved additional info value: %d", ai);
	}

	switch (mt) {
		case M_UINT: { //{{{
			char buf[24];
			snprintf(buf, sizeof(buf), "%" PRIu64, val);
			Tcl_DStringAppend(ds, buf, -1);
			return TCL_OK;
		}
		//}}}
		case M_NINT: { //{{{
			// -1 - val
			if (val <= (uint64_t)INT64_MAX) {
				char buf[24];
				snprintf(buf, sizeof(buf), "%" PRId64, (int64_t)(-1 - (int64_t)val));
				Tcl_DStringAppend(ds, buf, -1);
			} else {
				// Exceeds int64: -1 - val where val > INT64_MAX
				mp_int bn;
				if (bignum_from_nint64(&bn, val) != MP_OKAY)
					THROW_ERROR("bignum_from_nint64 failed");
				defer { bignum_clear(&bn); }

				char* s = NULL;
				size_t slen;
				if (mp_to_radix(&bn, NULL, 0, &slen, 10) != MP_OKAY)
					THROW_ERROR("mp_to_radix size failed");
				s = ckalloc(slen);
				defer { ckfree(s); }
				if (mp_to_radix(&bn, s, slen, NULL, 10) != MP_OKAY)
					THROW_ERROR("mp_to_radix failed");
				Tcl_DStringAppend(ds, s, -1);
			}
			return TCL_OK;
		}
		//}}}
		case M_BSTR: { //{{{
			if (ai == 31) {
				Tcl_DStringAppend(ds, "(_ ", 3);
				int first = 1;
				for (;;) {
					if (*p == 0xFF) { p++; break; }
					if (!first) Tcl_DStringAppend(ds, ", ", 2);
					first = 0;
					TEST_OK(diag_item(interp, &p, e, ds, depth));
				}
				Tcl_DStringAppend(ds, ")", 1);
			} else {
				TAKE(val);
				Tcl_DStringAppend(ds, "h'", 2);
				for (uint64_t i = 0; i < val; i++) {
					char hex[3];
					snprintf(hex, sizeof(hex), "%02x", valPtr[i]);
					Tcl_DStringAppend(ds, hex, 2);
				}
				Tcl_DStringAppend(ds, "'", 1);
			}
			return TCL_OK;
		}
		//}}}
		case M_UTF8: { //{{{
			if (ai == 31) {
				Tcl_DStringAppend(ds, "(_ ", 3);
				int first = 1;
				for (;;) {
					if (*p == 0xFF) { p++; break; }
					if (!first) Tcl_DStringAppend(ds, ", ", 2);
					first = 0;
					TEST_OK(diag_item(interp, &p, e, ds, depth));
				}
				Tcl_DStringAppend(ds, ")", 1);
			} else {
				TAKE(val);
				Tcl_DStringAppend(ds, "\"", 1);
				for (uint64_t i = 0; i < val; ) {
					uint8_t c = valPtr[i];
					if (c == '"')		{ Tcl_DStringAppend(ds, "\\\"", 2); i++; }
					else if (c == '\\')	{ Tcl_DStringAppend(ds, "\\\\", 2); i++; }
					else if (c == '\n')	{ Tcl_DStringAppend(ds, "\\n", 2); i++; }
					else if (c == '\r')	{ Tcl_DStringAppend(ds, "\\r", 2); i++; }
					else if (c == '\t')	{ Tcl_DStringAppend(ds, "\\t", 2); i++; }
					else if (c < 0x20)	{
						char esc[8];
						snprintf(esc, sizeof(esc), "\\u%04x", c);
						Tcl_DStringAppend(ds, esc, 6);
						i++;
					} else {
						// Pass through valid UTF-8 sequences
						const uint8_t* start = valPtr + i;
						i++;
						while (i < val && (valPtr[i] & 0xC0) == 0x80) i++; // skip continuation bytes
						Tcl_DStringAppend(ds, (const char*)start, (valPtr + i) - start);
					}
				}
				Tcl_DStringAppend(ds, "\"", 1);
			}
			return TCL_OK;
		}
		//}}}
		case M_ARR: { //{{{
			if (ai == 31) {
				Tcl_DStringAppend(ds, "[_ ", 3);
				int first = 1;
				for (;;) {
					if (*p == 0xFF) { p++; break; }
					if (!first) Tcl_DStringAppend(ds, ",\n", 2);
					else Tcl_DStringAppend(ds, "\n", 1);
					first = 0;
					diag_indent(ds, depth + 1);
					TEST_OK(diag_item(interp, &p, e, ds, depth + 1));
				}
				Tcl_DStringAppend(ds, "\n", 1);
				diag_indent(ds, depth);
				Tcl_DStringAppend(ds, "]", 1);
			} else if (val == 0) {
				Tcl_DStringAppend(ds, "[]", 2);
			} else {
				Tcl_DStringAppend(ds, "[\n", 2);
				for (uint64_t i = 0; i < val; i++) {
					if (i) Tcl_DStringAppend(ds, ",\n", 2);
					diag_indent(ds, depth + 1);
					TEST_OK(diag_item(interp, &p, e, ds, depth + 1));
				}
				Tcl_DStringAppend(ds, "\n", 1);
				diag_indent(ds, depth);
				Tcl_DStringAppend(ds, "]", 1);
			}
			return TCL_OK;
		}
		//}}}
		case M_MAP: { //{{{
			if (ai == 31) {
				Tcl_DStringAppend(ds, "{_ ", 3);
				int first = 1;
				for (;;) {
					if (*p == 0xFF) { p++; break; }
					if (!first) Tcl_DStringAppend(ds, ",\n", 2);
					else Tcl_DStringAppend(ds, "\n", 1);
					first = 0;
					diag_indent(ds, depth + 1);
					TEST_OK(diag_item(interp, &p, e, ds, depth + 1));
					Tcl_DStringAppend(ds, ": ", 2);
					TEST_OK(diag_item(interp, &p, e, ds, depth + 1));
				}
				Tcl_DStringAppend(ds, "\n", 1);
				diag_indent(ds, depth);
				Tcl_DStringAppend(ds, "}", 1);
			} else if (val == 0) {
				Tcl_DStringAppend(ds, "{}", 2);
			} else {
				Tcl_DStringAppend(ds, "{\n", 2);
				for (uint64_t i = 0; i < val; i++) {
					if (i) Tcl_DStringAppend(ds, ",\n", 2);
					diag_indent(ds, depth + 1);
					TEST_OK(diag_item(interp, &p, e, ds, depth + 1));
					Tcl_DStringAppend(ds, ": ", 2);
					TEST_OK(diag_item(interp, &p, e, ds, depth + 1));
				}
				Tcl_DStringAppend(ds, "\n", 1);
				diag_indent(ds, depth);
				Tcl_DStringAppend(ds, "}", 1);
			}
			return TCL_OK;
		}
		//}}}
		case M_TAG: { //{{{
			char buf[24];
			snprintf(buf, sizeof(buf), "%" PRIu64 "(", val);
			Tcl_DStringAppend(ds, buf, -1);
			TEST_OK(diag_item(interp, &p, e, ds, depth));
			Tcl_DStringAppend(ds, ")", 1);
			return TCL_OK;
		}
		//}}}
		case M_REAL: { //{{{
			if (ai < 20) {
				char buf[32];
				snprintf(buf, sizeof(buf), "simple(%" PRIu64 ")", val);
				Tcl_DStringAppend(ds, buf, -1);
			} else switch (ai) {
				case 20: Tcl_DStringAppend(ds, "false", 5); break;
				case 21: Tcl_DStringAppend(ds, "true", 4); break;
				case 22: Tcl_DStringAppend(ds, "null", 4); break;
				case 23: Tcl_DStringAppend(ds, "undefined", 9); break;
				case 24: {
					char buf[32];
					snprintf(buf, sizeof(buf), "simple(%" PRIu64 ")", val);
					Tcl_DStringAppend(ds, buf, -1);
					break;
				}
				case 25: { // Half-precision float
					double d = decode_half(valPtr);
					if (isnan(d))		Tcl_DStringAppend(ds, "NaN", 3);
					else if (isinf(d))	Tcl_DStringAppend(ds, d > 0 ? "Infinity" : "-Infinity", -1);
					else {
						char buf[32];
						snprintf(buf, sizeof(buf), "%g", d);
						// Ensure there's a decimal point for float distinction
						if (!strchr(buf, '.') && !strchr(buf, 'e'))
							strcat(buf, ".0");
						strcat(buf, "_1");
						Tcl_DStringAppend(ds, buf, -1);
					}
					break;
				}
				case 26: { // Single-precision float
					double d = (double)decode_float(valPtr);
					if (isnan(d))		Tcl_DStringAppend(ds, "NaN", 3);
					else if (isinf(d))	Tcl_DStringAppend(ds, d > 0 ? "Infinity" : "-Infinity", -1);
					else {
						char buf[32];
						snprintf(buf, sizeof(buf), "%.8g", d);
						if (!strchr(buf, '.') && !strchr(buf, 'e'))
							strcat(buf, ".0");
						strcat(buf, "_2");
						Tcl_DStringAppend(ds, buf, -1);
					}
					break;
				}
				case 27: { // Double-precision float
					double d = decode_double(valPtr);
					if (isnan(d))		Tcl_DStringAppend(ds, "NaN", 3);
					else if (isinf(d))	Tcl_DStringAppend(ds, d > 0 ? "Infinity" : "-Infinity", -1);
					else {
						char buf[32];
						int n = snprintf(buf, sizeof(buf), "%.15g", d);
						// Trim trailing zeros for readability, keep at least x.0
						char* dot = strchr(buf, '.');
						if (dot && !strchr(buf, 'e')) {
							while (n > 2 && buf[n-1] == '0' && buf[n-2] != '.') n--;
							buf[n] = '\0';
						}
						if (!dot && !strchr(buf, 'e'))
							strcat(buf, ".0");
						strcat(buf, "_3");
						Tcl_DStringAppend(ds, buf, -1);
					}
					break;
				}
			}
			return TCL_OK;
		}
		//}}}
	}
	return TCL_OK;
}

//}}}
static int cbor_pretty(Tcl_Interp* interp, const uint8_t* bytes, Tcl_Size len, Tcl_DString* ds) //{{{
{
	const uint8_t* p = bytes;
	const uint8_t* e = bytes + len;

	TEST_OK(diag_item(interp, &p, e, ds, 0));
	if (p != e) CBOR_TRAILING();
	return TCL_OK;
}

//}}}
// Diagnostic notation }}}
// Private API }}}
// Stubs API {{{
int CBOR_GetDataItemFromPath(Tcl_Interp* interp, Tcl_Obj* cborObj, Tcl_Obj* pathObj, const uint8_t** dataitemPtr, const uint8_t** ePtr, Tcl_DString* tagsPtr) //{{{
{
	Tcl_Obj**				pathv = NULL;
	Tcl_Size				pathc = 0;
	const uint8_t*			p = NULL;
	Tcl_Size				byteslen = 0;
	const uint8_t*			bytes = NULL;
	constexpr uint8_t		CIRCULAR_STATIC_SLOTS = 20;
	const uint8_t*			circular_buf[CIRCULAR_STATIC_SLOTS];
	const uint8_t**			circular = circular_buf;

	defer {
		if (circular != circular_buf) {
			ckfree(circular);
			circular = circular_buf;
		}
	}

	bytes = Tcl_GetBytesFromObj(interp, cborObj, &byteslen);
	if (bytes == NULL) return TCL_ERROR;
	TEST_OK(Tcl_ListObjGetElements(interp, pathObj, &pathc, &pathv));

	const uint8_t*const	e = bytes + byteslen;
	p = bytes;
	*ePtr = e;

	const uint8_t*	valPtr;

	for (int pathi=0; pathi<pathc; pathi++) {
		Tcl_Obj*	pathElem = pathv[pathi];
		Tcl_DStringSetLength(tagsPtr, 0);

	data_item: // loop: read off tags
		// Read head {{{
		TAKE(1);
		const uint8_t	ib = valPtr[0];
		const uint8_t	mt = ib >> 5;
		const uint8_t	ai = ib & 0x1f;
		uint64_t		val = ai;

		#define BAD_PATH() do { \
			Tcl_SetObjResult(interp, Tcl_ObjPrintf("Cannot index into atomic value type %d with path element %d: \"%s\"", mt, pathi, Tcl_GetString(pathElem))); \
			return TCL_ERROR; \
		} while (0)

		switch (ai) {
			case 24: TAKE(1); val = *(uint8_t*)valPtr;           break;
			case 25: TAKE(2); val = be16toh(*(uint16_t*)valPtr); break;
			case 26: TAKE(4); val = be32toh(*(uint32_t*)valPtr); break;
			case 27: TAKE(8); val = be64toh(*(uint64_t*)valPtr); break;
			case 28: case 29: case 30: CBOR_INVALID("reserved additional info value: %d", ai);
		}

		if (ai == 31) {
			switch (mt) {
				case M_UINT:
				case M_NINT:
				case M_TAG:
				case M_REAL:
					CBOR_INVALID("invalid indefinite length for major type %d", mt);

				case M_BSTR:
				case M_UTF8:
				case M_ARR:
				case M_MAP:
					break;
			}
		}
		//}}}

		switch (mt) {
			case M_ARR: //{{{
			{
				enum indexmode	mode;
				ssize_t			ofs;

				TEST_OK(parse_index(interp, pathElem, &mode, &ofs));
				const size_t	absofs = ofs < 0 ? ofs*-1 : ofs;
				if (mode == IDX_ENDREL) { //{{{
					if (ai == 31) { // end-x, indefinite length array {{{
						int		elem_mt;

						// Skip absofs elements
						for (size_t i=0; i<absofs; i++) {
							TEST_OK(well_formed(interp, &p, e, 1, &elem_mt));
							if (elem_mt == -1) goto not_found;	// Index before start: reached end of indefinite array before skipping enough
						}

						// Start recording the offsets so that when we reach the end we can look back ofs elements
						if (circular != circular_buf) {
							ckfree(circular);
							circular = circular_buf;
						}
						if (absofs > CIRCULAR_STATIC_SLOTS) circular = ckalloc(absofs * sizeof(uint8_t*));
						for (ssize_t c=0; ; c = (c+1)%absofs) {
							circular[c] = p;
							TEST_OK(well_formed_indefinite(interp, &p, e, 1, &elem_mt, mt));
							if (elem_mt == -1) {
								// Found the end: c-1 is the index of the last element, circular[(c-1+ofs)%absofs] is the indexed element
								p = circular[((c-1+ofs)%absofs + absofs)%absofs];
								break;
							}
						}
						//}}}
					} else { // end-x, known length array {{{
						if ((int64_t)val-1 - ofs < 0) goto not_found;	// Index before start
						// Skip val-1+ofs elements
						const ssize_t skip = val-1+ofs;
						for (ssize_t i=0; i<skip; i++) TEST_OK(well_formed(interp, &p, e, 1, NULL));
					}
					//}}}
					//}}}
				} else { //{{{
					if (ai == 31) { // ofs x, indefinite length array {{{
						const uint8_t*	last_p = p;
						for (ssize_t i=0; i<ofs+1; i++) { // Need to visit the referenced elem to be sure it isn't the break symbol (end of array)
							int	elem_mt;
							last_p = p;
							TEST_OK(well_formed_indefinite(interp, &p, e, 1, &elem_mt, mt));
							if (elem_mt == -1) goto not_found;	// Index beyond end
						}
						p = last_p;
						//}}}
					} else { // ofs x, known length array {{{
						if (ofs > (int64_t)val-1) goto not_found;	// Index beyond end
						for (ssize_t i=0; i<ofs; i++) TEST_OK(well_formed(interp, &p, e, 1, NULL));
						//}}}
					}
					//}}}
				}
				goto next_path_elem;
			}
			//}}}
			case M_MAP: //{{{
				for (size_t i=0; ; i++) {
					if (ai == 31) {
						if (p >= e) CBOR_TRUNCATED();
						if (*p == 0xFF) {p++; break;}
					} else {
						if (i >= val) break;
					}

					int matches;
					TEST_OK(cbor_matches(interp, &p, e, pathElem, &matches));
					if (matches) goto next_path_elem;
					TEST_OK(well_formed(interp, &p, e, 0, NULL));	// Skip value
				}
				goto not_found;
			//}}}
			case M_TAG: //{{{
				Tcl_DStringAppend(tagsPtr, (const char*)&val, sizeof(val));
				goto data_item;
				//}}}
			default: BAD_PATH(); // Can only index into arrays and maps
		}

		#undef BAD_PATH
	next_path_elem:
		continue;
	}

	*dataitemPtr = p;
	return TCL_OK;

not_found:
	*dataitemPtr = NULL;
	return TCL_OK;
}

//}}}
int CBOR_WellFormed(Tcl_Interp* interp, const uint8_t* bytes, size_t len) //{{{
{
	const uint8_t*	p = bytes;
	const uint8_t*	e = bytes + len;

	TEST_OK(well_formed(interp, &p, e, 0, NULL));
	if (p != e) {
		Tcl_SetErrorCode(interp, "CBOR", "TRAILING", NULL);
		Tcl_SetObjResult(interp, Tcl_ObjPrintf("Excess bytes after CBOR value"));
		return TCL_ERROR;
	}
	return TCL_OK;
}

//}}}
int CBOR_Length(Tcl_Interp* interp, const uint8_t* p, const uint8_t* e, size_t* lenPtr) //{{{
{
	const uint8_t*	pl = p;

	TEST_OK(well_formed(interp, &p, e, 0, NULL));
	*lenPtr = p-pl;

	return TCL_OK;
}

//}}}
// Stubs API }}}
// Script API {{{
static int cbor_nr_cmd(ClientData cdata, Tcl_Interp* interp, int objc, Tcl_Obj*const objv[]) //{{{
{
	struct interp_cx*	l = cdata;
	static const char* ops[] = {
		"get",
		"tget",			// Get with tags
		"extract",
		"wellformed",
		"apply_tag",
		"template",
		"encode",
		"pretty",
		NULL
	};
	enum {
		OP_GET,
		OP_TGET,
		OP_EXTRACT,
		OP_WELLFORMED,
		OP_APPLY_TAG,
		OP_TEMPLATE,
		OP_ENCODE,
		OP_PRETTY,
	} op;

	enum {A_cmd, A_OP, A_args};
	CHECK_MIN_ARGS("op ?args ...?");

	TEST_OK(Tcl_GetIndexFromObj(interp, objv[A_OP], ops, "op", TCL_EXACT, &op));

	switch (op) {
		case OP_GET: //{{{
		{
			enum {A_cmd=A_OP, A_CBOR, A_args, A_PATH=A_args};
			CHECK_MIN_ARGS("cbor ?key ...?");

			Tcl_Obj*	path = NULL;
			replace_tclobj(&path, Tcl_NewListObj(objc-A_PATH, objv+A_PATH));
			defer { replace_tclobj(&path, NULL); };

			Tcl_DString	tags;
			Tcl_DStringInit(&tags);
			defer { Tcl_DStringFree(&tags); };

			const uint8_t*	dataitem = NULL;
			const uint8_t*	e = NULL;
			TEST_OK(CBOR_GetDataItemFromPath(interp, objv[A_CBOR], path, &dataitem, &e, &tags));
			if (dataitem == NULL) {
				Tcl_SetErrorCode(interp, "CBOR", "NOTFOUND", Tcl_GetString(path), NULL);
				THROW_ERROR("path not found");
			}

			Tcl_Obj*	res = NULL;
			TEST_OK(cbor_get_obj(interp, &dataitem, e, &res, NULL));
			defer { replace_tclobj(&res, NULL); };
			Tcl_SetObjResult(interp, res);
			return TCL_OK;
		}
		//}}}
		case OP_TGET: //{{{
		{
			enum {A_cmd=A_OP, A_TAGSVAR, A_CBOR, A_args, A_PATH=A_args};
			CHECK_MIN_ARGS("tagsvar cbor ?key ...?");

			Tcl_Obj*	path = NULL;
			replace_tclobj(&path, Tcl_NewListObj(objc-A_PATH, objv+A_PATH));
			defer { replace_tclobj(&path, NULL); };

			Tcl_DString	tags;
			Tcl_DStringInit(&tags);
			defer { Tcl_DStringFree(&tags); };

			const uint8_t*	dataitem = NULL;
			const uint8_t*	e = NULL;
			TEST_OK(CBOR_GetDataItemFromPath(interp, objv[A_CBOR], path, &dataitem, &e, &tags));
			if (dataitem == NULL) {
				Tcl_SetErrorCode(interp, "CBOR", "NOTFOUND", Tcl_GetString(path), NULL);
				THROW_ERROR("path not found");
			}

			Tcl_Obj*	tagslist = NULL;
			Tcl_Obj*	res = NULL;

			replace_tclobj(&tagslist, Tcl_NewListObj(0, NULL));
			defer { replace_tclobj(&tagslist, NULL); };

			TEST_OK(cbor_get_obj(interp, &dataitem, e, &res, &tagslist));
			defer { replace_tclobj(&res, NULL); };

			if (NULL == Tcl_ObjSetVar2(interp, objv[A_TAGSVAR], NULL, tagslist, TCL_LEAVE_ERR_MSG))
				return TCL_ERROR;

			Tcl_SetObjResult(interp, res);
			return TCL_OK;
		}
		//}}}
		case OP_EXTRACT: //{{{
		{
			enum {A_cmd=A_OP, A_CBOR, A_args, A_PATH=A_args};
			CHECK_MIN_ARGS("cbor ?key ...?");

			Tcl_Obj*	path = NULL;
			replace_tclobj(&path, Tcl_NewListObj(objc-A_PATH, objv+A_PATH));
			defer { replace_tclobj(&path, NULL); };

			Tcl_DString	tags;
			Tcl_DStringInit(&tags);
			defer { Tcl_DStringFree(&tags); };

			const uint8_t*	dataitem = NULL;
			const uint8_t*	e = NULL;
			TEST_OK(CBOR_GetDataItemFromPath(interp, objv[A_CBOR], path, &dataitem, &e, &tags));
			if (dataitem == NULL) {
				Tcl_SetErrorCode(interp, "CBOR", "NOTFOUND", Tcl_GetString(path), NULL);
				THROW_ERROR("path not found");
			}

			size_t		len = 0;
			TEST_OK(CBOR_Length(interp, dataitem, e, &len));
			Tcl_SetObjResult(interp, Tcl_NewByteArrayObj(dataitem, len));
			return TCL_OK;
		}
		//}}}
		case OP_WELLFORMED: //{{{
		{
			enum {A_cmd=A_OP, A_BYTES, A_objc};
			CHECK_ARGS("bytes");

			Tcl_Size		len;
			const uint8_t*	bytes = Tcl_GetByteArrayFromObj(objv[A_BYTES], &len);
			const uint8_t*	p = bytes;

			TEST_OK(well_formed(interp, &p, bytes+len, 0, NULL));
			if (p != bytes+len) CBOR_TRAILING();

			Tcl_SetObjResult(interp, l->tcl_true);
			return TCL_OK;
		}
		//}}}
		case OP_APPLY_TAG: //{{{
		{
			enum {A_cmd=A_OP, A_TAG, A_VALUE, A_objc};
			CHECK_ARGS("tag value");

			Tcl_WideInt	tag;
			TEST_OK(Tcl_GetWideIntFromObj(interp, objv[A_TAG], &tag));

			switch (tag) {
				case 2: // Unsigned bignum
				{
					Tcl_Size		bytelen;
					const uint8_t*	bytes = Tcl_GetBytesFromObj(interp, objv[A_VALUE], &bytelen);
					if (bytes == NULL) {
						Tcl_SetObjResult(interp, Tcl_ObjPrintf("Tag 2 (unsigned bignum) requires a byte array value"));
						return TCL_ERROR;
					}

					mp_int	n;
					if (bignum_from_ubin(&n, bytes, bytelen) != 0)
						THROW_ERROR("Failed to convert byte array to bignum");
					defer { bignum_clear(&n); }

					Tcl_SetObjResult(interp, Tcl_NewBignumObj(&n));
					return TCL_OK;
				}

				case 3: // Negative bignum: -1 - n
				{
					Tcl_Size		bytelen;
					const uint8_t*	bytes = Tcl_GetBytesFromObj(interp, objv[A_VALUE], &bytelen);
					if (bytes == NULL) {
						Tcl_SetObjResult(interp, Tcl_ObjPrintf("Tag 3 (negative bignum) requires a byte array value"));
						return TCL_ERROR;
					}

					mp_int	n;
					if (bignum_from_nbin(&n, bytes, bytelen) != 0)
						THROW_ERROR("Failed to convert byte array to negative bignum");
					defer { bignum_clear(&n); }

					Tcl_SetObjResult(interp, Tcl_NewBignumObj(&n));
					return TCL_OK;
				}

				default:
					Tcl_SetObjResult(interp, Tcl_ObjPrintf("Tag %" TCL_LL_MODIFIER "d not supported", tag));
					return TCL_ERROR;
			}
		}
		//}}}
		case OP_TEMPLATE:
			return cbor_template_cmd(l, interp, objc - A_OP, objv + A_OP);
		case OP_ENCODE:
			return cbor_encode_cmd(l, interp, objc - A_OP, objv + A_OP);
		case OP_PRETTY: //{{{
		{
			enum {A_cmd=A_OP, A_CBOR, A_objc};
			CHECK_ARGS("cbor");

			Tcl_Size		byteslen;
			const uint8_t*	bytes = Tcl_GetBytesFromObj(interp, objv[A_CBOR], &byteslen);
			if (!bytes) return TCL_ERROR;

			Tcl_DString ds;
			Tcl_DStringInit(&ds);
			defer { Tcl_DStringFree(&ds); }

			TEST_OK(cbor_pretty(interp, bytes, byteslen, &ds));
			Tcl_SetObjResult(interp, Tcl_NewStringObj(Tcl_DStringValue(&ds), Tcl_DStringLength(&ds)));
			return TCL_OK;
		}
		//}}}
		default: THROW_ERROR("op not implemented yet");
	}

	THROW_PRINTF("Fell through switch on op \"%s\"", ops[op]);
}

//}}}
static int cbor_cmd(ClientData cdata, Tcl_Interp* interp, int objc, Tcl_Obj*const objv[]) //{{{
{
	return Tcl_NRCallObjProc(interp, cbor_nr_cmd, cdata, objc, objv);
}

//}}}
// Script API }}}

static void free_interp_cx(ClientData cdata, Tcl_Interp* interp) //{{{
{
	(void)interp;
	struct interp_cx* l = cdata;
	release_tclobj(&l->cbor_true);
	release_tclobj(&l->cbor_false);
	release_tclobj(&l->cbor_null);
	release_tclobj(&l->cbor_undefined);
	release_tclobj(&l->tcl_true);
	ckfree(l);
}

//}}}

DLLEXPORT int Cbor_Init(Tcl_Interp* interp) //{{{
{
	if (!Tcl_InitStubs(interp, TCL_VERSION, 0)) return TCL_ERROR;

	struct interp_cx*	l = NULL;
	l = (struct interp_cx*)ckalloc(sizeof(*l));

	memset(l, 0, sizeof(*l));
	replace_tclobj(&l->cbor_true,		Tcl_NewStringObj("true",  4));
	replace_tclobj(&l->cbor_false,		Tcl_NewStringObj("false", 5));
	replace_tclobj(&l->cbor_null,		Tcl_NewStringObj("", 0));
	replace_tclobj(&l->cbor_undefined,	Tcl_NewStringObj("", 0));
	replace_tclobj(&l->tcl_true,		Tcl_NewBooleanObj(1));

	Tcl_SetAssocData(interp, "cbor", free_interp_cx, l);	defer { if (l) Tcl_DeleteAssocData(interp, "cbor"); };

	Tcl_NRCreateCommand(interp, "::cbor", cbor_cmd, cbor_nr_cmd, l, NULL);

	TEST_OK(Tcl_PkgProvide(interp, PACKAGE_NAME, PACKAGE_VERSION));
	l = NULL;

	return TCL_OK;
}

//}}}
DLLEXPORT int Cbor_SafeInit(Tcl_Interp* interp) //{{{
{
	return Cbor_Init(interp);
}

//}}}

// vim: foldmethod=marker foldmarker={{{,}}} ts=4 shiftwidth=4
