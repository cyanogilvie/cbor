#ifndef _CBORINT_H
#define _CBORINT_H

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <tcl.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <endian.h>

#include <tclstuff.h>
#include <tip445.h>
#include <getbytes.h>
#include <tclTomMath.h>
#include <bignum_ops.h>

#ifndef TCL_SIZE_MODIFIER
#define TCL_SIZE_MODIFIER
#endif

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

#define NS "::cbor"

struct cbor_cx {
	Tcl_Interp*	interp;
	Tcl_Obj*	cbor_true;
	Tcl_Obj*	cbor_false;
	Tcl_Obj*	cbor_null;
	Tcl_Obj*	cbor_undefined;
	Tcl_Obj*	tcl_true;
};

#endif /* _CBORINT_H */
