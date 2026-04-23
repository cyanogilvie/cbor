#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <config.h>

#include <tcl.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <compat_endian.h>

#include <tclstuff.h>
#include <tclTomMath.h>
#include <bignum_ops.h>
#include <defer.h>

#include "cbor_encode.h"

// Major types
enum { MT_UINT=0, MT_NINT=1, MT_BSTR=2, MT_UTF8=3, MT_ARR=4, MT_MAP=5, MT_TAG=6, MT_REAL=7 };

// Byte array accumulator {{{
// Builds a Tcl_Obj byte array in-place with amortized O(1) append,
// avoiding the final copy that a Tcl_DString approach would require.

struct cbor_buf {
	Tcl_Obj*	obj;
	Tcl_Size	len;
	Tcl_Size	cap;
};

static void buf_init(struct cbor_buf* b) //{{{
{
	b->obj = NULL;
	replace_tclobj(&b->obj, Tcl_NewByteArrayObj(NULL, 0));
	b->len = 0;
	b->cap = 0;
}

//}}}
static uint8_t* buf_grow(struct cbor_buf* b, Tcl_Size need) //{{{
{
	Tcl_Size newlen = b->len + need;
	if (newlen > b->cap) {
		b->cap = b->cap < 64 ? 64 : b->cap;
		while (b->cap < newlen) b->cap *= 2;
		Tcl_SetByteArrayLength(b->obj, b->cap);
	}
	return Tcl_SetByteArrayLength(b->obj, b->cap) + b->len;
}

//}}}
static void buf_append(struct cbor_buf* b, const void* data, Tcl_Size n) //{{{
{
	memcpy(buf_grow(b, n), data, n);
	b->len += n;
}

//}}}
static Tcl_Obj* buf_result(struct cbor_buf* b) //{{{
{
	// Trim to actual length.  Our replace_tclobj ref keeps it alive;
	// the caller passes it to Tcl_SetObjResult (which adds another ref),
	// then buf_free releases ours via replace_tclobj(&b->obj, NULL).
	Tcl_SetByteArrayLength(b->obj, b->len);
	return b->obj;
}

//}}}
static void buf_free(struct cbor_buf* b) //{{{
{
	replace_tclobj(&b->obj, NULL);
}

//}}}
// Byte array accumulator }}}
// CBOR Primitive Encoding Helpers {{{

static void emit_head(struct cbor_buf* buf, uint8_t mt, uint64_t val) //{{{
{
	uint8_t head = mt << 5;

	if (val <= 23) {
		head |= (uint8_t)val;
		buf_append(buf, &head, 1);
	} else if (val <= 0xFF) {
		head |= 24;
		uint8_t b = (uint8_t)val;
		uint8_t d[2] = {head, b};
		buf_append(buf, d, 2);
	} else if (val <= 0xFFFF) {
		head |= 25;
		uint16_t b = htobe16((uint16_t)val);
		uint8_t* p = buf_grow(buf, 3);
		p[0] = head; memcpy(p+1, &b, 2);
		buf->len += 3;
	} else if (val <= 0xFFFFFFFF) {
		head |= 26;
		uint32_t b = htobe32((uint32_t)val);
		uint8_t* p = buf_grow(buf, 5);
		p[0] = head; memcpy(p+1, &b, 4);
		buf->len += 5;
	} else {
		head |= 27;
		uint64_t b = htobe64(val);
		uint8_t* p = buf_grow(buf, 9);
		p[0] = head; memcpy(p+1, &b, 8);
		buf->len += 9;
	}
}

//}}}
static void emit_uint(struct cbor_buf* buf, uint64_t val)		{ emit_head(buf, MT_UINT, val); }
static void emit_nint(struct cbor_buf* buf, uint64_t val)		{ emit_head(buf, MT_NINT, val); }
static void emit_bstr(struct cbor_buf* buf, const uint8_t* bytes, size_t len) //{{{
{
	emit_head(buf, MT_BSTR, len);
	buf_append(buf, bytes, len);
}

//}}}
static void emit_utf8(struct cbor_buf* buf, const char* str, size_t len) //{{{
{
	emit_head(buf, MT_UTF8, len);
	buf_append(buf, str, len);
}

//}}}
static void emit_tag(struct cbor_buf* buf, uint64_t tag)		{ emit_head(buf, MT_TAG, tag); }
static void emit_array_head(struct cbor_buf* buf, uint64_t n)	{ emit_head(buf, MT_ARR, n); }
static void emit_map_head(struct cbor_buf* buf, uint64_t n)	{ emit_head(buf, MT_MAP, n); }

static void emit_bool(struct cbor_buf* buf, int val) //{{{
{
	uint8_t b = (MT_REAL << 5) | (val ? 21 : 20);
	buf_append(buf, &b, 1);
}

//}}}
static void emit_null(struct cbor_buf* buf) //{{{
{
	uint8_t b = (MT_REAL << 5) | 22;
	buf_append(buf, &b, 1);
}

//}}}
static void emit_undefined(struct cbor_buf* buf) //{{{
{
	uint8_t b = (MT_REAL << 5) | 23;
	buf_append(buf, &b, 1);
}

//}}}
static int emit_simple(struct cbor_buf* buf, int val) //{{{
{
	if (val < 0 || val > 255 || (val >= 24 && val <= 31))
		return TCL_ERROR;

	if (val <= 23) {
		uint8_t b = (MT_REAL << 5) | (uint8_t)val;
		buf_append(buf, &b, 1);
	} else {
		uint8_t d[2] = {(MT_REAL << 5) | 24, (uint8_t)val};
		buf_append(buf, d, 2);
	}
	return TCL_OK;
}

//}}}
static double decode_half_val(uint16_t half) //{{{
{
	uint32_t exp  = (half >> 10) & 0x1f;
	uint32_t mant = half & 0x3ff;
	double val;

	if (exp == 0)		val = ldexp(mant, -24);
	else if (exp != 31)	val = ldexp(mant + 1024, exp - 25);
	else				val = mant == 0 ? INFINITY : NAN;

	return half & 0x8000 ? -val : val;
}

//}}}
static uint16_t encode_half(double val) //{{{
{
	uint64_t d;
	memcpy(&d, &val, sizeof(d));
	int sign = (d >> 63) & 1;
	int biased_exp = (d >> 52) & 0x7FF;
	int exp  = (int)biased_exp - 1023;
	uint64_t mant = d & 0x000FFFFFFFFFFFFFULL;

	// Zero: biased exponent 0 and mantissa 0
	if (biased_exp == 0 && mant == 0)
		return (uint16_t)(sign << 15);

	uint16_t h;
	if (exp == 1024) {
		// inf or nan
		h = (sign << 15) | 0x7C00 | (mant ? 0x0200 : 0);
	} else if (exp > 15) {
		return 0xFFFF; // overflow sentinel — cannot represent
	} else if (exp >= -14) {
		// normalized: check if mantissa fits in 10 bits
		if (mant & 0x003FFFFFFFFFULL) return 0xFFFF; // precision loss
		h = (sign << 15) | ((exp + 15) << 10) | (uint16_t)(mant >> 42);
	} else if (exp >= -24) {
		// denormalized
		mant |= 0x0010000000000000ULL;
		int shift = 42 - (exp + 14);
		if (shift > 63) return 0xFFFF;
		if (mant & ((1ULL << shift) - 1)) return 0xFFFF; // precision loss
		h = (sign << 15) | (uint16_t)(mant >> shift);
	} else {
		return 0xFFFF; // underflow
	}
	return h;
}

//}}}
static void emit_float(struct cbor_buf* buf, double val) //{{{
{
	// Try half-precision
	uint16_t half = encode_half(val);
	if (half != 0xFFFF) {
		double roundtrip = decode_half_val(half);
		if (roundtrip == val || (isnan(val) && isnan(roundtrip))) {
			uint16_t be = htobe16(half);
			uint8_t* p = buf_grow(buf, 3);
			p[0] = (MT_REAL << 5) | 25; memcpy(p+1, &be, 2);
			buf->len += 3;
			return;
		}
	}

	// Try single-precision
	float fval = (float)val;
	if ((double)fval == val || (isnan(val) && isnan(fval))) {
		uint32_t bits;
		memcpy(&bits, &fval, sizeof(bits));
		bits = htobe32(bits);
		uint8_t* p = buf_grow(buf, 5);
		p[0] = (MT_REAL << 5) | 26; memcpy(p+1, &bits, 4);
		buf->len += 5;
		return;
	}

	// Double-precision
	uint64_t bits;
	memcpy(&bits, &val, sizeof(bits));
	bits = htobe64(bits);
	uint8_t* p = buf_grow(buf, 9);
	p[0] = (MT_REAL << 5) | 27; memcpy(p+1, &bits, 8);
	buf->len += 9;
}

//}}}
static int emit_number_from_obj(Tcl_Interp* interp, struct cbor_buf* buf, Tcl_Obj* obj) //{{{
{
	void*	clientData;
	int		numtype;

	if (Tcl_GetNumberFromObj(interp, obj, &clientData, &numtype) != TCL_OK)
		return TCL_ERROR;

	switch (numtype) {
		case TCL_NUMBER_INT: {
			Tcl_WideInt wide = *(Tcl_WideInt*)clientData;
			if (wide >= 0)	emit_uint(buf, (uint64_t)wide);
			else			emit_nint(buf, (uint64_t)(-1 - wide));
			return TCL_OK;
		}
		case TCL_NUMBER_DOUBLE: {
			emit_float(buf, *(double*)clientData);
			return TCL_OK;
		}
		case TCL_NUMBER_NAN: {
			emit_float(buf, NAN);
			return TCL_OK;
		}
		case TCL_NUMBER_BIG: {
			mp_int* bn = (mp_int*)clientData;
			if (mp_isneg(bn)) {
				mp_int pos;
				if (mp_init(&pos) != MP_OKAY) THROW_ERROR("mp_init failed");
				defer { mp_clear(&pos); }

				if (mp_abs(bn, &pos) != MP_OKAY) THROW_ERROR("mp_abs failed");
				if (mp_sub_d(&pos, 1, &pos) != MP_OKAY) THROW_ERROR("mp_sub_d failed");

				size_t sz = bignum_ubin_size(&pos);
				uint8_t* bytes = ckalloc(sz);
				defer { ckfree(bytes); }

				size_t written;
				if (bignum_to_ubin(&pos, bytes, sz, &written) != MP_OKAY)
					THROW_ERROR("bignum_to_ubin failed");

				emit_tag(buf, 3);
				emit_bstr(buf, bytes, written);
			} else {
				size_t sz = bignum_ubin_size(bn);
				uint8_t* bytes = ckalloc(sz);
				defer { ckfree(bytes); }

				size_t written;
				if (bignum_to_ubin(bn, bytes, sz, &written) != MP_OKAY)
					THROW_ERROR("bignum_to_ubin failed");

				emit_tag(buf, 2);
				emit_bstr(buf, bytes, written);
			}
			return TCL_OK;
		}
		default:
			THROW_ERROR("Unsupported number type");
	}
}

//}}}
// CBOR Primitive Encoding Helpers }}}
// JSON Parser {{{
// Minimal recursive descent JSON parser that emits CBOR directly.

#define JSON_MAX_DEPTH 256

static void json_skip_ws(const char** pp, const char* end) //{{{
{
	const char* p = *pp;
	while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
	*pp = p;
}

//}}}
static int json_parse_string_content(Tcl_Interp* interp, const char** pp, const char* end, Tcl_DString* out) //{{{
{
	// Called after opening '"' is consumed.  Parses until closing '"'.
	const char* p = *pp;

	while (p < end && *p != '"') {
		if (*p != '\\') {
			const char* start = p;
			while (p < end && *p != '"' && *p != '\\') p++;
			Tcl_DStringAppend(out, start, p - start);
			continue;
		}

		p++; // skip backslash
		if (p >= end) THROW_ERROR("Unterminated JSON string escape");

		switch (*p) {
			case '"':  Tcl_DStringAppend(out, "\"", 1); p++; break;
			case '\\': Tcl_DStringAppend(out, "\\", 1); p++; break;
			case '/':  Tcl_DStringAppend(out, "/",  1); p++; break;
			case 'b':  Tcl_DStringAppend(out, "\b", 1); p++; break;
			case 'f':  Tcl_DStringAppend(out, "\f", 1); p++; break;
			case 'n':  Tcl_DStringAppend(out, "\n", 1); p++; break;
			case 'r':  Tcl_DStringAppend(out, "\r", 1); p++; break;
			case 't':  Tcl_DStringAppend(out, "\t", 1); p++; break;
			case 'u': {
				p++;
				if (p + 4 > end) THROW_ERROR("Truncated \\u escape in JSON string");
				unsigned cp = 0;
				for (int i = 0; i < 4; i++) {
					cp <<= 4;
					char c = p[i];
					if (c >= '0' && c <= '9')		cp |= c - '0';
					else if (c >= 'a' && c <= 'f')	cp |= c - 'a' + 10;
					else if (c >= 'A' && c <= 'F')	cp |= c - 'A' + 10;
					else THROW_ERROR("Invalid hex digit in \\u escape");
				}
				p += 4;

				// Reject bare surrogates
				if (cp >= 0xDC00 && cp <= 0xDFFF)
					THROW_ERROR("Bare low surrogate in JSON string");
				if (cp >= 0xD800 && cp <= 0xDBFF) {
					if (p + 6 > end || p[0] != '\\' || p[1] != 'u')
						THROW_ERROR("Lone high surrogate in JSON string");
					p += 2;
					unsigned lo = 0;
					for (int i = 0; i < 4; i++) {
						lo <<= 4;
						char c = p[i];
						if (c >= '0' && c <= '9')		lo |= c - '0';
						else if (c >= 'a' && c <= 'f')	lo |= c - 'a' + 10;
						else if (c >= 'A' && c <= 'F')	lo |= c - 'A' + 10;
						else THROW_ERROR("Invalid hex digit in surrogate pair");
					}
					p += 4;
					if (lo < 0xDC00 || lo > 0xDFFF)
						THROW_ERROR("Invalid low surrogate in JSON string");
					cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
				}

				// Encode codepoint as UTF-8
				char utf8[4];
				int len;
				if (cp < 0x80)          { utf8[0] = cp; len = 1; }
				else if (cp < 0x800)    { utf8[0] = 0xC0|(cp>>6); utf8[1] = 0x80|(cp&0x3F); len = 2; }
				else if (cp < 0x10000)  { utf8[0] = 0xE0|(cp>>12); utf8[1] = 0x80|((cp>>6)&0x3F); utf8[2] = 0x80|(cp&0x3F); len = 3; }
				else                    { utf8[0] = 0xF0|(cp>>18); utf8[1] = 0x80|((cp>>12)&0x3F); utf8[2] = 0x80|((cp>>6)&0x3F); utf8[3] = 0x80|(cp&0x3F); len = 4; }
				Tcl_DStringAppend(out, utf8, len);
				break;
			}
			default:
				THROW_PRINTF("Invalid JSON escape: \\%c", *p);
		}
	}

	if (p >= end) THROW_ERROR("Unterminated JSON string");
	p++; // skip closing '"'
	*pp = p;
	return TCL_OK;
}

//}}}
static int json_skip_value(const char** pp, const char* end) //{{{
{
	const char* p = *pp;
	if (p >= end) return TCL_ERROR;

	switch (*p) {
		case '"': {
			p++;
			while (p < end && *p != '"') {
				if (*p == '\\') { p++; if (p >= end) return TCL_ERROR; }
				p++;
			}
			if (p >= end) return TCL_ERROR;
			p++;
			break;
		}
		case '{': {
			p++;
			int depth = 1;
			while (p < end && depth > 0) {
				if (*p == '{') depth++;
				else if (*p == '}') depth--;
				else if (*p == '"') {
					p++;
					while (p < end && *p != '"') {
						if (*p == '\\') { p++; if (p >= end) return TCL_ERROR; }
						p++;
					}
					if (p >= end) return TCL_ERROR;
				}
				p++;
			}
			break;
		}
		case '[': {
			p++;
			int depth = 1;
			while (p < end && depth > 0) {
				if (*p == '[') depth++;
				else if (*p == ']') depth--;
				else if (*p == '"') {
					p++;
					while (p < end && *p != '"') {
						if (*p == '\\') { p++; if (p >= end) return TCL_ERROR; }
						p++;
					}
					if (p >= end) return TCL_ERROR;
				}
				p++;
			}
			break;
		}
		case 't': p += 4; break;
		case 'f': p += 5; break;
		case 'n': p += 4; break;
		default:
			while (p < end && *p != ',' && *p != '}' && *p != ']'
					&& *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
			break;
	}
	*pp = p;
	return (p <= end) ? TCL_OK : TCL_ERROR;
}

//}}}
static int json_count_container(const char* p, const char* end, char close, size_t* countPtr) //{{{
{
	size_t count = 0;
	const char* pp = p;
	while (pp < end && *pp != close) {
		while (pp < end && (*pp == ' ' || *pp == '\t' || *pp == '\n' || *pp == '\r')) pp++;
		if (pp >= end || *pp == close) break;

		if (close == '}') {
			if (json_skip_value(&pp, end) != TCL_OK) return TCL_ERROR;
			while (pp < end && (*pp == ' ' || *pp == '\t' || *pp == '\n' || *pp == '\r')) pp++;
			if (pp >= end || *pp != ':') return TCL_ERROR;
			pp++;
			while (pp < end && (*pp == ' ' || *pp == '\t' || *pp == '\n' || *pp == '\r')) pp++;
		}

		if (json_skip_value(&pp, end) != TCL_OK) return TCL_ERROR;
		count++;

		while (pp < end && (*pp == ' ' || *pp == '\t' || *pp == '\n' || *pp == '\r')) pp++;
		if (pp < end && *pp == ',') pp++;
		else if (pp < end && *pp != close) return TCL_ERROR;
	}
	*countPtr = count;
	return TCL_OK;
}

//}}}

// Forward declaration
static int json_to_cbor(Tcl_Interp* interp, const char** pp, const char* end, struct cbor_buf* buf, Tcl_Obj* dict, int depth);

static int resolve_var(Tcl_Interp* interp, const char* name, size_t namelen, Tcl_Obj* dict, Tcl_Obj** valuePtr) //{{{
{
	Tcl_Obj* key = NULL;
	replace_tclobj(&key, Tcl_NewStringObj(name, namelen));
	defer { replace_tclobj(&key, NULL); }

	if (dict) {
		Tcl_Obj* val = NULL;
		TEST_OK(Tcl_DictObjGet(interp, dict, key, &val));
		*valuePtr = val;
	} else {
		*valuePtr = Tcl_ObjGetVar2(interp, key, NULL, 0);
	}
	return TCL_OK;
}

//}}}
static int emit_substitution(Tcl_Interp* interp, struct cbor_buf* buf, const char* marker, size_t len, Tcl_Obj* dict, int depth) //{{{
{
	if (len < 3 || marker[0] != '~' || (len >= 3 && marker[2] != ':' && !(len >= 4 && marker[1] == 'B' && marker[2] == 'Y' && marker[3] == ':')))
		goto not_a_marker;

	// ~BY: is 4-char prefix, all others are 3-char
	if (len >= 4 && marker[1] == 'B' && marker[2] == 'Y' && marker[3] == ':') {
		Tcl_Obj* val = NULL;
		TEST_OK(resolve_var(interp, marker + 4, len - 4, dict, &val));
		if (!val) { emit_null(buf); return TCL_OK; }

		Tcl_Size blen;
		const uint8_t* bytes = Tcl_GetBytesFromObj(interp, val, &blen);
		if (!bytes) return TCL_ERROR;
		emit_bstr(buf, bytes, blen);
		return TCL_OK;
	}

	if (marker[2] != ':') goto not_a_marker;

	const char* varname = marker + 3;
	size_t varlen = len - 3;

	switch (marker[1]) {
		case 'S': { // String
			Tcl_Obj* val = NULL;
			TEST_OK(resolve_var(interp, varname, varlen, dict, &val));
			if (!val) { emit_null(buf); return TCL_OK; }
			Tcl_Size slen;
			const char* str = Tcl_GetStringFromObj(val, &slen);
			emit_utf8(buf, str, slen);
			return TCL_OK;
		}
		case 'N': { // Number
			Tcl_Obj* val = NULL;
			TEST_OK(resolve_var(interp, varname, varlen, dict, &val));
			if (!val) { emit_null(buf); return TCL_OK; }
			return emit_number_from_obj(interp, buf, val);
		}
		case 'B': { // Boolean
			Tcl_Obj* val = NULL;
			TEST_OK(resolve_var(interp, varname, varlen, dict, &val));
			if (!val) { emit_null(buf); return TCL_OK; }
			int bval;
			TEST_OK(Tcl_GetBooleanFromObj(interp, val, &bval));
			emit_bool(buf, bval);
			return TCL_OK;
		}
		case 'C': { // CBOR fragment
			Tcl_Obj* val = NULL;
			TEST_OK(resolve_var(interp, varname, varlen, dict, &val));
			if (!val) { emit_null(buf); return TCL_OK; }
			Tcl_Size blen;
			const uint8_t* bytes = Tcl_GetBytesFromObj(interp, val, &blen);
			if (!bytes) return TCL_ERROR;
			TEST_OK(CBOR_WellFormed(interp, bytes, blen));
			buf_append(buf, bytes, blen);
			return TCL_OK;
		}
		case 'L': { // Literal
			emit_utf8(buf, varname, varlen);
			return TCL_OK;
		}
		case 'T': { // Template
			Tcl_Obj* val = NULL;
			TEST_OK(resolve_var(interp, varname, varlen, dict, &val));
			if (!val) { emit_null(buf); return TCL_OK; }
			Tcl_Size tlen;
			const char* tmpl = Tcl_GetStringFromObj(val, &tlen);
			const char* tp = tmpl;
			const char* te = tmpl + tlen;
			json_skip_ws(&tp, te);
			return json_to_cbor(interp, &tp, te, buf, dict, depth + 1);
		}
		default:
			goto not_a_marker;
	}

not_a_marker:
	emit_utf8(buf, marker, len);
	return TCL_OK;
}

//}}}
static int json_to_cbor(Tcl_Interp* interp, const char** pp, const char* end, struct cbor_buf* buf, Tcl_Obj* dict, int depth) //{{{
{
	if (depth > JSON_MAX_DEPTH)
		THROW_ERROR("JSON template nesting too deep");

	const char* p = *pp;
	json_skip_ws(&p, end);
	if (p >= end) THROW_ERROR("Unexpected end of JSON");

	switch (*p) {
		case '"': { //{{{
			p++;
			Tcl_DString str;
			Tcl_DStringInit(&str);
			defer { Tcl_DStringFree(&str); }

			TEST_OK(json_parse_string_content(interp, &p, end, &str));
			*pp = p;

			const char* s = Tcl_DStringValue(&str);
			size_t slen = Tcl_DStringLength(&str);

			if (slen >= 3 && s[0] == '~')
				return emit_substitution(interp, buf, s, slen, dict, depth);

			emit_utf8(buf, s, slen);
			return TCL_OK;
		}
		//}}}
		case '{': { //{{{
			p++;
			json_skip_ws(&p, end);

			if (p < end && *p == '}') {
				p++;
				*pp = p;
				emit_map_head(buf, 0);
				return TCL_OK;
			}

			const char* scan = p;
			size_t count;
			if (json_count_container(scan, end, '}', &count) != TCL_OK)
				THROW_ERROR("Malformed JSON object");

			// Check for tag directive: exactly 2 keys, ~tag and ~val
			if (count == 2) {
				const char* peek = p;
				Tcl_DString key1, key2;
				Tcl_DStringInit(&key1);
				Tcl_DStringInit(&key2);
				defer { Tcl_DStringFree(&key1); Tcl_DStringFree(&key2); }

				int is_tag = 0;
				if (*peek == '"') {
					peek++;
					if (json_parse_string_content(interp, &peek, end, &key1) == TCL_OK) {
						json_skip_ws(&peek, end);
						if (peek < end && *peek == ':') {
							peek++;
							json_skip_ws(&peek, end);
							const char* val1_start = peek;
							if (json_skip_value(&peek, end) == TCL_OK) {
								json_skip_ws(&peek, end);
								if (peek < end && *peek == ',') {
									peek++;
									json_skip_ws(&peek, end);
									if (*peek == '"') {
										peek++;
										if (json_parse_string_content(interp, &peek, end, &key2) == TCL_OK) {
											const char* k1 = Tcl_DStringValue(&key1);
											const char* k2 = Tcl_DStringValue(&key2);
											int k1_len = Tcl_DStringLength(&key1);
											int k2_len = Tcl_DStringLength(&key2);

											int tag_first  = (k1_len == 4 && memcmp(k1, "~tag", 4) == 0 && k2_len == 4 && memcmp(k2, "~val", 4) == 0);
											int val_first  = (k1_len == 4 && memcmp(k1, "~val", 4) == 0 && k2_len == 4 && memcmp(k2, "~tag", 4) == 0);

											if (tag_first || val_first) {
												is_tag = 1;
												(void)val1_start;
											}
										}
									}
								}
							}
						}
					}
				}

				if (is_tag) {
					Tcl_WideInt tag_num = -1;
					const char* val_start = NULL;
					const char* val_end_pos = NULL;

					const char* tp = p;
					for (int i = 0; i < 2; i++) {
						json_skip_ws(&tp, end);
						if (tp >= end || *tp != '"') THROW_ERROR("Expected string key in tag directive");
						tp++;
						Tcl_DString kd;
						Tcl_DStringInit(&kd);
						defer { Tcl_DStringFree(&kd); }
						TEST_OK(json_parse_string_content(interp, &tp, end, &kd));

						json_skip_ws(&tp, end);
						if (tp >= end || *tp != ':') THROW_ERROR("Expected ':' in JSON object");
						tp++;
						json_skip_ws(&tp, end);

						int is_tag_key = (Tcl_DStringLength(&kd) == 4 && memcmp(Tcl_DStringValue(&kd), "~tag", 4) == 0);
						if (is_tag_key) {
							Tcl_Obj* numobj = NULL;
							const char* nstart = tp;
							while (tp < end && *tp != ',' && *tp != '}' && *tp != ' ' && *tp != '\t' && *tp != '\n' && *tp != '\r') tp++;
							replace_tclobj(&numobj, Tcl_NewStringObj(nstart, tp - nstart));
							defer { replace_tclobj(&numobj, NULL); }
							TEST_OK(Tcl_GetWideIntFromObj(interp, numobj, &tag_num));
							if (tag_num < 0) THROW_ERROR("Tag number must be non-negative");
						} else {
							val_start = tp;
							if (json_skip_value(&tp, end) != TCL_OK)
								THROW_ERROR("Invalid value in tag directive");
							val_end_pos = tp;
						}

						json_skip_ws(&tp, end);
						if (tp < end && *tp == ',') tp++;
					}

					json_skip_ws(&tp, end);
					if (tp >= end || *tp != '}') THROW_ERROR("Expected '}' closing tag directive");
					tp++;
					*pp = tp;

					if (tag_num < 0 || !val_start) THROW_ERROR("Incomplete tag directive");

					emit_tag(buf, (uint64_t)tag_num);
					const char* vp = val_start;
					return json_to_cbor(interp, &vp, val_end_pos, buf, dict, depth + 1);
				}
			}

			// Regular object — emit as map
			emit_map_head(buf, count);
			for (size_t i = 0; i < count; i++) {
				json_skip_ws(&p, end);
				if (p >= end || *p != '"') THROW_ERROR("Expected string key in JSON object");
				p++;
				Tcl_DString key;
				Tcl_DStringInit(&key);
				defer { Tcl_DStringFree(&key); }
				TEST_OK(json_parse_string_content(interp, &p, end, &key));

				// Key: check for substitution markers (enables non-string map keys)
				const char* ks = Tcl_DStringValue(&key);
				size_t klen = Tcl_DStringLength(&key);
				if (klen >= 3 && ks[0] == '~') {
					TEST_OK(emit_substitution(interp, buf, ks, klen, dict, depth));
				} else {
					emit_utf8(buf, ks, klen);
				}

				json_skip_ws(&p, end);
				if (p >= end || *p != ':') THROW_ERROR("Expected ':' in JSON object");
				p++;

				TEST_OK(json_to_cbor(interp, &p, end, buf, dict, depth + 1));

				json_skip_ws(&p, end);
				if (p < end && *p == ',') p++;
			}

			json_skip_ws(&p, end);
			if (p >= end || *p != '}') THROW_ERROR("Expected '}' closing JSON object");
			p++;
			*pp = p;
			return TCL_OK;
		}
		//}}}
		case '[': { //{{{
			p++;
			json_skip_ws(&p, end);

			if (p < end && *p == ']') {
				p++;
				*pp = p;
				emit_array_head(buf, 0);
				return TCL_OK;
			}

			const char* scan = p;
			size_t count;
			if (json_count_container(scan, end, ']', &count) != TCL_OK)
				THROW_ERROR("Malformed JSON array");

			emit_array_head(buf, count);
			for (size_t i = 0; i < count; i++) {
				TEST_OK(json_to_cbor(interp, &p, end, buf, dict, depth + 1));
				json_skip_ws(&p, end);
				if (p < end && *p == ',') p++;
			}

			json_skip_ws(&p, end);
			if (p >= end || *p != ']') THROW_ERROR("Expected ']' closing JSON array");
			p++;
			*pp = p;
			return TCL_OK;
		}
		//}}}
		case 't': //{{{
			if (p + 4 > end || memcmp(p, "true", 4) != 0) THROW_ERROR("Invalid JSON value");
			p += 4; *pp = p;
			emit_bool(buf, 1);
			return TCL_OK;
		//}}}
		case 'f': //{{{
			if (p + 5 > end || memcmp(p, "false", 5) != 0) THROW_ERROR("Invalid JSON value");
			p += 5; *pp = p;
			emit_bool(buf, 0);
			return TCL_OK;
		//}}}
		case 'n': //{{{
			if (p + 4 > end || memcmp(p, "null", 4) != 0) THROW_ERROR("Invalid JSON value");
			p += 4; *pp = p;
			emit_null(buf);
			return TCL_OK;
		//}}}
		default: { //{{{
			// Number
			const char* start = p;

			if (*p == '-') p++;
			if (p >= end || (*p < '0' || *p > '9')) THROW_ERROR("Invalid JSON number");
			if (*p == '0' && p + 1 < end && p[1] >= '0' && p[1] <= '9')
				THROW_ERROR("Leading zeros not permitted in JSON numbers");
			while (p < end && *p >= '0' && *p <= '9') p++;
			if (p < end && *p == '.') {
				p++;
				if (p >= end || *p < '0' || *p > '9') THROW_ERROR("Invalid JSON number: digit required after decimal point");
				while (p < end && *p >= '0' && *p <= '9') p++;
			}
			if (p < end && (*p == 'e' || *p == 'E')) {
				p++;
				if (p < end && (*p == '+' || *p == '-')) p++;
				if (p >= end || *p < '0' || *p > '9') THROW_ERROR("Invalid JSON number: digit required in exponent");
				while (p < end && *p >= '0' && *p <= '9') p++;
			}

			Tcl_Obj* numobj = NULL;
			replace_tclobj(&numobj, Tcl_NewStringObj(start, p - start));
			defer { replace_tclobj(&numobj, NULL); }
			*pp = p;

			return emit_number_from_obj(interp, buf, numobj);
		}
		//}}}
	}
}

//}}}
// JSON Parser }}}
// Command implementations {{{

int cbor_encode_cmd(ClientData cdata, Tcl_Interp* interp, int objc, Tcl_Obj*const objv[]) //{{{
{
	(void)cdata;
	static const char* enc_types[] = {
		"string", "int", "uint", "nint", "float", "bytes",
		"bool", "null", "undefined", "simple", "tag", "array", "map", "imap",
		NULL
	};
	enum {
		ET_STRING, ET_INT, ET_UINT, ET_NINT, ET_FLOAT, ET_BYTES,
		ET_BOOL, ET_NULL, ET_UNDEFINED, ET_SIMPLE, ET_TAG, ET_ARRAY, ET_MAP, ET_IMAP,
	} type;

	enum {A_cmd, A_TYPE, A_args};
	CHECK_MIN_ARGS("type ?value ...?");

	TEST_OK(Tcl_GetIndexFromObj(interp, objv[A_TYPE], enc_types, "type", TCL_EXACT, (int*)&type));

	struct cbor_buf ds;
	buf_init(&ds);
	defer { buf_free(&ds); }

	switch (type) {
		case ET_STRING: { //{{{
			enum {A_cmd=A_TYPE, A_VAL, A_objc};
			CHECK_ARGS("value");
			Tcl_Size slen;
			const char* str = Tcl_GetStringFromObj(objv[A_VAL], &slen);
			emit_utf8(&ds, str, slen);
			break;
		}
		//}}}
		case ET_INT: { //{{{
			enum {A_cmd=A_TYPE, A_VAL, A_objc};
			CHECK_ARGS("value");
			TEST_OK(emit_number_from_obj(interp, &ds, objv[A_VAL]));
			break;
		}
		//}}}
		case ET_UINT: { //{{{
			enum {A_cmd=A_TYPE, A_VAL, A_objc};
			CHECK_ARGS("value");
			Tcl_WideInt val;
			TEST_OK(Tcl_GetWideIntFromObj(interp, objv[A_VAL], &val));
			if (val < 0) THROW_ERROR("uint value must be non-negative");
			emit_uint(&ds, (uint64_t)val);
			break;
		}
		//}}}
		case ET_NINT: { //{{{
			enum {A_cmd=A_TYPE, A_VAL, A_objc};
			CHECK_ARGS("value");
			Tcl_WideInt val;
			TEST_OK(Tcl_GetWideIntFromObj(interp, objv[A_VAL], &val));
			if (val < 0) THROW_ERROR("nint argument must be non-negative (encodes -1-N)");
			emit_nint(&ds, (uint64_t)val);
			break;
		}
		//}}}
		case ET_FLOAT: { //{{{
			enum {A_cmd=A_TYPE, A_VAL, A_objc};
			CHECK_ARGS("value");
			void*	clientData;
			int		numtype;
			double	dval;
			if (Tcl_GetNumberFromObj(NULL, objv[A_VAL], &clientData, &numtype) == TCL_OK) {
				if (numtype == TCL_NUMBER_DOUBLE)	dval = *(double*)clientData;
				else if (numtype == TCL_NUMBER_NAN)	dval = NAN;
				else if (numtype == TCL_NUMBER_INT)	dval = (double)*(Tcl_WideInt*)clientData;
				else { TEST_OK(Tcl_GetDoubleFromObj(interp, objv[A_VAL], &dval)); }
			} else {
				const char* s = Tcl_GetString(objv[A_VAL]);
				if (strcasecmp(s, "nan") == 0)			dval = NAN;
				else if (strcasecmp(s, "inf") == 0)		dval = INFINITY;
				else if (strcasecmp(s, "+inf") == 0)	dval = INFINITY;
				else if (strcasecmp(s, "-inf") == 0)	dval = -INFINITY;
				else { TEST_OK(Tcl_GetDoubleFromObj(interp, objv[A_VAL], &dval)); }
			}
			emit_float(&ds, dval);
			break;
		}
		//}}}
		case ET_BYTES: { //{{{
			enum {A_cmd=A_TYPE, A_VAL, A_objc};
			CHECK_ARGS("value");
			Tcl_Size blen;
			const uint8_t* bytes = Tcl_GetBytesFromObj(interp, objv[A_VAL], &blen);
			if (!bytes) return TCL_ERROR;
			emit_bstr(&ds, bytes, blen);
			break;
		}
		//}}}
		case ET_BOOL: { //{{{
			enum {A_cmd=A_TYPE, A_VAL, A_objc};
			CHECK_ARGS("value");
			int bval;
			TEST_OK(Tcl_GetBooleanFromObj(interp, objv[A_VAL], &bval));
			emit_bool(&ds, bval);
			break;
		}
		//}}}
		case ET_NULL: //{{{
			emit_null(&ds);
			break;
		//}}}
		case ET_UNDEFINED: //{{{
			emit_undefined(&ds);
			break;
		//}}}
		case ET_TAG: { //{{{
			enum {A_cmd=A_TYPE, A_TAGNUM, A_CBORVAL, A_objc};
			CHECK_ARGS("tagnum cborvalue");
			Tcl_WideInt tagnum;
			TEST_OK(Tcl_GetWideIntFromObj(interp, objv[A_TAGNUM], &tagnum));
			if (tagnum < 0) THROW_ERROR("Tag number must be non-negative");
			emit_tag(&ds, (uint64_t)tagnum);
			Tcl_Size blen;
			const uint8_t* bytes = Tcl_GetBytesFromObj(interp, objv[A_CBORVAL], &blen);
			if (!bytes) return TCL_ERROR;
			buf_append(&ds, bytes, blen);
			break;
		}
		//}}}
		case ET_ARRAY: { //{{{
			enum {A_cmd=A_TYPE, A_LIST, A_objc};
			CHECK_ARGS("list");
			Tcl_Size llen;
			Tcl_Obj** elems;
			TEST_OK(Tcl_ListObjGetElements(interp, objv[A_LIST], &llen, &elems));
			emit_array_head(&ds, llen);
			for (Tcl_Size i = 0; i < llen; i++) {
				Tcl_Size blen;
				const uint8_t* bytes = Tcl_GetBytesFromObj(interp, elems[i], &blen);
				if (!bytes) return TCL_ERROR;
				buf_append(&ds, bytes, blen);
			}
			break;
		}
		//}}}
		case ET_MAP: { //{{{
			enum {A_cmd=A_TYPE, A_DICT, A_objc};
			CHECK_ARGS("dict");
			Tcl_Size dsize;
			TEST_OK(Tcl_DictObjSize(interp, objv[A_DICT], &dsize));
			emit_map_head(&ds, dsize);

			Tcl_DictSearch search;
			Tcl_Obj *key, *val;
			int done;
			TEST_OK(Tcl_DictObjFirst(interp, objv[A_DICT], &search, &key, &val, &done));
			defer { Tcl_DictObjDone(&search); }

			while (!done) {
				Tcl_Size klen;
				const char* kstr = Tcl_GetStringFromObj(key, &klen);
				emit_utf8(&ds, kstr, klen);

				Tcl_Size blen;
				const uint8_t* bytes = Tcl_GetBytesFromObj(interp, val, &blen);
				if (!bytes) return TCL_ERROR;
				buf_append(&ds, bytes, blen);

				Tcl_DictObjNext(&search, &key, &val, &done);
			}
			break;
		}
		//}}}
		case ET_IMAP: { //{{{
			enum {A_cmd=A_TYPE, A_LIST, A_objc};
			CHECK_ARGS("list");
			Tcl_Size llen;
			Tcl_Obj** elems;
			TEST_OK(Tcl_ListObjGetElements(interp, objv[A_LIST], &llen, &elems));
			if (llen % 2 != 0) THROW_ERROR("imap requires an even number of elements (key-value pairs)");
			emit_map_head(&ds, llen / 2);
			for (Tcl_Size i = 0; i < llen; i++) {
				Tcl_Size blen;
				const uint8_t* bytes = Tcl_GetBytesFromObj(interp, elems[i], &blen);
				if (!bytes) return TCL_ERROR;
				buf_append(&ds, bytes, blen);
			}
			break;
		}
		//}}}
		case ET_SIMPLE: { //{{{
			enum {A_cmd=A_TYPE, A_VAL, A_objc};
			CHECK_ARGS("value");
			Tcl_WideInt val;
			TEST_OK(Tcl_GetWideIntFromObj(interp, objv[A_VAL], &val));
			if (emit_simple(&ds, (int)val) != TCL_OK)
				THROW_ERROR("Invalid simple value (must be 0-23 or 32-255)");
			break;
		}
		//}}}
	}

	Tcl_SetObjResult(interp, buf_result(&ds));
	return TCL_OK;
}

//}}}
int cbor_template_cmd(ClientData cdata, Tcl_Interp* interp, int objc, Tcl_Obj*const objv[]) //{{{
{
	(void)cdata;
	enum {A_cmd, A_TEMPLATE, A_args};
	CHECK_MIN_ARGS("template ?dict?");

	if (objc > A_TEMPLATE + 2) {
		Tcl_WrongNumArgs(interp, 1, objv, "template ?dict?");
		return TCL_ERROR;
	}

	Tcl_Obj* dict = (objc > A_TEMPLATE + 1) ? objv[A_TEMPLATE + 1] : NULL;

	if (dict) {
		Tcl_Size sz;
		TEST_OK(Tcl_DictObjSize(interp, dict, &sz));
	}

	Tcl_Size tlen;
	const char* tmpl = Tcl_GetStringFromObj(objv[A_TEMPLATE], &tlen);
	const char* p = tmpl;
	const char* e = tmpl + tlen;

	struct cbor_buf ds;
	buf_init(&ds);
	defer { buf_free(&ds); }

	json_skip_ws(&p, e);
	TEST_OK(json_to_cbor(interp, &p, e, &ds, dict, 0));

	json_skip_ws(&p, e);
	if (p != e) THROW_ERROR("Trailing content after JSON value");

	Tcl_SetObjResult(interp, buf_result(&ds));
	return TCL_OK;
}

//}}}
// Command implementations }}}

// vim: foldmethod=marker foldmarker={{{,}}} ts=4 shiftwidth=4
