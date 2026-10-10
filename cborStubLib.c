#if HAVE_CONFIG_H
#	include <config.h>
#endif

#undef USE_TCL_STUBS
#undef USE_CBOR_STUBS
#define USE_TCL_STUBS 1
#define USE_CBOR_STUBS 1

#include <cbor.h>

MODULE_SCOPE const CborStubs*	cborStubsPtr;
const CborStubs*				cborStubsPtr = NULL;

#undef CborInitializeStubs
MODULE_SCOPE const char* CborInitializeStubs(Tcl_Interp* interp)
{
	const char*	got = Tcl_PkgRequireEx(interp, PACKAGE_NAME, PACKAGE_VERSION, 0, &cborStubsPtr);

	if (got && !cborStubsPtr) {
		// Something provided the package without a stubs table
		Tcl_SetObjResult(interp, Tcl_ObjPrintf("%s %s was provided without a stubs table", PACKAGE_NAME, got));
		return NULL;
	}

	return got;
}
