// A minimal extension consuming cbor through its stubs table, built the way a
// downstream meson extension would (dependency('cbor') -> stub lib +
// -DUSE_CBOR_STUBS=1).  Not installed: only loaded by tests/stubs.test.

#include <tcl.h>
#include <cbor.h>

#include "stubs_api.c"

#ifndef USE_CBOR_STUBS
#	error "Expected to be built against the cbor stubs (USE_CBOR_STUBS)"
#endif

DLLEXPORT int Cborstubsconsumer_Init(Tcl_Interp* interp)
{
	if (Tcl_InitStubs(interp, TCL_VERSION, 0) == NULL) return TCL_ERROR;
	if (Cbor_InitStubs(interp) == NULL) return TCL_ERROR;
	return stubs_api_register(interp, "::cbor_stubs");
}
