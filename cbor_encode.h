#ifndef CBOR_ENCODE_H
#define CBOR_ENCODE_H

#include <tcl.h>

/* Well-formedness check (defined in cbor.c) */
int CBOR_WellFormed(Tcl_Interp* interp, const uint8_t* bytes, size_t len);

/* Template encoder: JSON template -> CBOR bytes */
int cbor_template_cmd(ClientData cdata, Tcl_Interp* interp, int objc, Tcl_Obj*const objv[]);

/* Primitive encoder: type/value pairs -> CBOR bytes */
int cbor_encode_cmd(ClientData cdata, Tcl_Interp* interp, int objc, Tcl_Obj*const objv[]);

#endif /* CBOR_ENCODE_H */
