// Commands exercising the cbor C API from a separately compiled consumer.
// Included by the jitc cdef in stubs.test (linked directly against the
// package library) and by stubs_consumer.c (a meson-built extension linked
// through the stubs table), so it uses only plain Tcl and cbor API calls.

#include <inttypes.h>
#include <math.h>

static void api_replace(Tcl_Obj** target, Tcl_Obj* replacement) //{{{
{
	if (*target == replacement) return;
	if (replacement) Tcl_IncrRefCount(replacement);
	if (*target) Tcl_DecrRefCount(*target);
	*target = replacement;
}

//}}}
static int api_heads(ClientData cdata, Tcl_Interp* interp, int objc, Tcl_Obj*const objv[]) //{{{
{
	// List the heads of all the data items in the cbor value
	(void)cdata;
	if (objc != 2) {Tcl_WrongNumArgs(interp, 1, objv, "cbor"); return TCL_ERROR;}

	Tcl_Size		len;
	const uint8_t*	p = Tcl_GetBytesFromObj(interp, objv[1], &len);
	if (!p) return TCL_ERROR;
	const uint8_t*	e = p + len;
	Tcl_Obj*		res = NULL;
	int				code = TCL_OK;

	api_replace(&res, Tcl_NewListObj(0, NULL));
	while (p < e) {
		CBOR_Head	h;
		code = CBOR_ReadHead(interp, &p, e, &h);
		if (code != TCL_OK) break;
		Tcl_Obj*	desc = Tcl_ObjPrintf("%d:%" PRIu64, (int)h.mt, h.val);
		if (h.indefinite)	Tcl_AppendToObj(desc, ":indef", -1);
		if (h.isbreak)		Tcl_AppendToObj(desc, ":break", -1);
		if (h.mt == CBOR_MT_SIMPLE && h.ai >= 25 && h.ai <= 27) {
			if (isnan(h.fval))	Tcl_AppendToObj(desc, ":nan", -1);
			else				Tcl_AppendPrintfToObj(desc, ":%g", h.fval);
		}
		if ((h.mt == CBOR_MT_BYTES || h.mt == CBOR_MT_TEXT) && !h.indefinite)
			p += h.val;		// Skip the payload
		Tcl_ListObjAppendElement(interp, res, desc);
	}
	if (code == TCL_OK) Tcl_SetObjResult(interp, res);
	api_replace(&res, NULL);
	return code;
}

//}}}
static int api_build(ClientData cdata, Tcl_Interp* interp, int objc, Tcl_Obj*const objv[]) //{{{
{
	// Emit a fixed document with the encoder API
	(void)cdata;
	if (objc != 2) {Tcl_WrongNumArgs(interp, 1, objv, "number"); return TCL_ERROR;}

	CBOR_Buf	buf;
	CBOR_BufInit(&buf);
	CBOR_EmitHead(&buf, CBOR_MT_MAP, 7);
	CBOR_EmitText(&buf, "i", 1);	CBOR_EmitWideInt(&buf, -500);
	CBOR_EmitText(&buf, "f", 1);	CBOR_EmitFloat(&buf, 1.5);
	CBOR_EmitText(&buf, "b", 1);	CBOR_EmitBytes(&buf, (const uint8_t*)"\x00\xff", 2);
	CBOR_EmitText(&buf, "t", 1);	CBOR_EmitHead(&buf, CBOR_MT_TAG, 1);	CBOR_EmitFloat(&buf, 1e9);
	CBOR_EmitText(&buf, "a", 1);
	CBOR_EmitIndefinite(interp, &buf, CBOR_MT_ARRAY);
		CBOR_EmitBool(&buf, 1);
		CBOR_EmitNull(&buf);
		CBOR_EmitUndefined(&buf);
		CBOR_EmitSimple(interp, &buf, 32);
		CBOR_EmitRaw(&buf, (const uint8_t*)"\x01", 1);
	CBOR_EmitBreak(&buf);
	CBOR_EmitText(&buf, "n", 1);
	if (CBOR_EmitNumber(interp, &buf, objv[1]) != TCL_OK) goto err;
	CBOR_EmitText(&buf, "x", 1);
	if (CBOR_EmitIndefinite(interp, &buf, CBOR_MT_UINT) == TCL_OK) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj("indefinite uint should be rejected", -1));
		goto err;
	}
	if (CBOR_EmitSimple(interp, &buf, 24) == TCL_OK) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj("reserved simple value should be rejected", -1));
		goto err;
	}
	CBOR_EmitNull(&buf);

	Tcl_SetObjResult(interp, CBOR_BufResult(&buf));
	CBOR_BufFree(&buf);
	return TCL_OK;

err:
	CBOR_BufFree(&buf);
	return TCL_ERROR;
}

//}}}
static int api_tget(ClientData cdata, Tcl_Interp* interp, int objc, Tcl_Obj*const objv[]) //{{{
{
	(void)cdata;
	if (objc != 3) {Tcl_WrongNumArgs(interp, 1, objv, "cbor path"); return TCL_ERROR;}

	Tcl_Obj*	res = NULL;
	Tcl_Obj*	tags = NULL;
	int			code = CBOR_Get(interp, objv[1], objv[2], &res, &tags);
	if (code == TCL_OK) {
		Tcl_Obj*	pair[2] = {res, tags};
		Tcl_SetObjResult(interp, Tcl_NewListObj(2, pair));
	}
	api_replace(&res, NULL);
	api_replace(&tags, NULL);
	return code;
}

//}}}
static int api_extract(ClientData cdata, Tcl_Interp* interp, int objc, Tcl_Obj*const objv[]) //{{{
{
	(void)cdata;
	if (objc != 3) {Tcl_WrongNumArgs(interp, 1, objv, "cbor path"); return TCL_ERROR;}

	Tcl_Obj*	res = NULL;
	int			code = CBOR_Extract(interp, objv[1], objv[2], &res);
	if (code == TCL_OK) Tcl_SetObjResult(interp, res);
	api_replace(&res, NULL);
	return code;
}

//}}}
static int api_decodeall(ClientData cdata, Tcl_Interp* interp, int objc, Tcl_Obj*const objv[]) //{{{
{
	// Decode a sequence of data items with CBOR_Length and CBOR_DecodeValue
	(void)cdata;
	if (objc != 2) {Tcl_WrongNumArgs(interp, 1, objv, "cbor"); return TCL_ERROR;}

	Tcl_Size		len;
	const uint8_t*	p = Tcl_GetBytesFromObj(interp, objv[1], &len);
	if (!p) return TCL_ERROR;
	const uint8_t*	e = p + len;
	Tcl_Obj*		res = NULL;
	Tcl_Obj*		val = NULL;
	Tcl_Obj*		tags = NULL;
	int				code = TCL_OK;

	api_replace(&res, Tcl_NewListObj(0, NULL));
	while (p < e) {
		size_t	itemlen;
		code = CBOR_Length(interp, p, e, &itemlen);
		if (code != TCL_OK) break;
		code = CBOR_DecodeValue(interp, &p, e, &val, &tags);
		if (code != TCL_OK) break;
		Tcl_ListObjAppendElement(interp, res, Tcl_ObjPrintf("%" PRIu64, (uint64_t)itemlen));
		Tcl_ListObjAppendElement(interp, res, val);
		Tcl_ListObjAppendElement(interp, res, tags);
	}
	if (code == TCL_OK) Tcl_SetObjResult(interp, res);
	api_replace(&res, NULL);
	api_replace(&val, NULL);
	api_replace(&tags, NULL);
	return code;
}

//}}}
static int api_tmpl(ClientData cdata, Tcl_Interp* interp, int objc, Tcl_Obj*const objv[]) //{{{
{
	(void)cdata;
	if (objc != 3) {Tcl_WrongNumArgs(interp, 1, objv, "template dict"); return TCL_ERROR;}

	Tcl_Obj*	res = NULL;
	int			code = CBOR_Template(interp, objv[1], objv[2], &res);
	if (code == TCL_OK) Tcl_SetObjResult(interp, res);
	api_replace(&res, NULL);
	return code;
}

//}}}
static int api_pretty(ClientData cdata, Tcl_Interp* interp, int objc, Tcl_Obj*const objv[]) //{{{
{
	(void)cdata;
	if (objc != 2) {Tcl_WrongNumArgs(interp, 1, objv, "cbor"); return TCL_ERROR;}

	Tcl_Size		len;
	const uint8_t*	bytes = Tcl_GetBytesFromObj(interp, objv[1], &len);
	if (!bytes) return TCL_ERROR;
	if (CBOR_WellFormed(interp, bytes, len) != TCL_OK) return TCL_ERROR;

	Tcl_DString	ds;
	Tcl_DStringInit(&ds);
	int code = CBOR_Pretty(interp, bytes, len, &ds);
	if (code == TCL_OK) Tcl_DStringResult(interp, &ds);
	Tcl_DStringFree(&ds);
	return code;
}

//}}}
static int api_apply_tag(ClientData cdata, Tcl_Interp* interp, int objc, Tcl_Obj*const objv[]) //{{{
{
	(void)cdata;
	if (objc != 3) {Tcl_WrongNumArgs(interp, 1, objv, "tag value"); return TCL_ERROR;}

	Tcl_WideInt	tag;
	Tcl_Obj*	res = NULL;
	if (Tcl_GetWideIntFromObj(interp, objv[1], &tag) != TCL_OK) return TCL_ERROR;
	int code = CBOR_ApplyTag(interp, (uint64_t)tag, objv[2], &res);
	if (code == TCL_OK) Tcl_SetObjResult(interp, res);
	api_replace(&res, NULL);
	return code;
}

//}}}
static int stubs_api_register(Tcl_Interp* interp, const char* ns) //{{{
{
	static const struct {
		const char*		name;
		Tcl_ObjCmdProc*	proc;
	} cmds[] = {
		{"heads",		api_heads},
		{"build",		api_build},
		{"tget",		api_tget},
		{"extract",		api_extract},
		{"decodeall",	api_decodeall},
		{"tmpl",		api_tmpl},
		{"pretty",		api_pretty},
		{"apply_tag",	api_apply_tag},
	};

	if (!Tcl_CreateNamespace(interp, ns, NULL, NULL)) return TCL_ERROR;
	for (size_t i=0; i<sizeof(cmds)/sizeof(cmds[0]); i++) {
		Tcl_Obj*	name = NULL;
		api_replace(&name, Tcl_ObjPrintf("%s::%s", ns, cmds[i].name));
		Tcl_CreateObjCommand(interp, Tcl_GetString(name), cmds[i].proc, NULL, NULL);
		api_replace(&name, NULL);
	}
	return TCL_OK;
}

//}}}

// vim: foldmethod=marker foldmarker={{{,}}} ts=4 shiftwidth=4
