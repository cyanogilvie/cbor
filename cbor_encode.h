#ifndef CBOR_ENCODE_H
#define CBOR_ENCODE_H

#include <tcl.h>

/* Template encoder: JSON template -> CBOR bytes */
MODULE_SCOPE int cbor_template_cmd(ClientData cdata, Tcl_Interp* interp, int objc, Tcl_Obj*const objv[]);

/* Primitive encoder: type/value pairs -> CBOR bytes */
MODULE_SCOPE int cbor_encode_cmd(ClientData cdata, Tcl_Interp* interp, int objc, Tcl_Obj*const objv[]);

#endif /* CBOR_ENCODE_H */
