library cbor
interface cbor

# Decoding <<<
declare 0 generic {
	int CBOR_WellFormed(Tcl_Interp* interp, const uint8_t* bytes, size_t len)
}
declare 1 generic {
	int CBOR_Length(Tcl_Interp* interp, const uint8_t* p, const uint8_t* e, size_t* lenPtr)
}
declare 2 generic {
	int CBOR_GetDataItemFromPath(Tcl_Interp* interp, Tcl_Obj* cborObj, Tcl_Obj* pathObj, const uint8_t** dataitemPtr, const uint8_t** ePtr, Tcl_DString* tagsPtr)
}
declare 3 generic {
	int CBOR_ReadHead(Tcl_Interp* interp, const uint8_t** pPtr, const uint8_t* e, CBOR_Head* head)
}
declare 4 generic {
	int CBOR_DecodeValue(Tcl_Interp* interp, const uint8_t** pPtr, const uint8_t* e, Tcl_Obj** resPtr, Tcl_Obj** tagsPtr)
}
declare 5 generic {
	int CBOR_Get(Tcl_Interp* interp, Tcl_Obj* cborObj, Tcl_Obj* pathObj, Tcl_Obj** resPtr, Tcl_Obj** tagsPtr)
}
declare 6 generic {
	int CBOR_Extract(Tcl_Interp* interp, Tcl_Obj* cborObj, Tcl_Obj* pathObj, Tcl_Obj** resPtr)
}
declare 7 generic {
	int CBOR_Pretty(Tcl_Interp* interp, const uint8_t* bytes, size_t len, Tcl_DString* ds)
}
declare 8 generic {
	int CBOR_ApplyTag(Tcl_Interp* interp, uint64_t tag, Tcl_Obj* valueObj, Tcl_Obj** resPtr)
}
# Decoding >>>
# Encoding <<<
declare 9 generic {
	int CBOR_Template(Tcl_Interp* interp, Tcl_Obj* templateObj, Tcl_Obj* dictObj, Tcl_Obj** resPtr)
}
declare 10 generic {
	void CBOR_BufInit(CBOR_Buf* buf)
}
declare 11 generic {
	void CBOR_BufFree(CBOR_Buf* buf)
}
declare 12 generic {
	Tcl_Obj* CBOR_BufResult(CBOR_Buf* buf)
}
declare 13 generic {
	void CBOR_EmitHead(CBOR_Buf* buf, CBOR_MajorType mt, uint64_t val)
}
declare 14 generic {
	int CBOR_EmitIndefinite(Tcl_Interp* interp, CBOR_Buf* buf, CBOR_MajorType mt)
}
declare 15 generic {
	void CBOR_EmitBreak(CBOR_Buf* buf)
}
declare 16 generic {
	void CBOR_EmitBytes(CBOR_Buf* buf, const uint8_t* bytes, size_t len)
}
declare 17 generic {
	void CBOR_EmitText(CBOR_Buf* buf, const char* str, size_t len)
}
declare 18 generic {
	void CBOR_EmitWideInt(CBOR_Buf* buf, Tcl_WideInt val)
}
declare 19 generic {
	void CBOR_EmitFloat(CBOR_Buf* buf, double val)
}
declare 20 generic {
	int CBOR_EmitNumber(Tcl_Interp* interp, CBOR_Buf* buf, Tcl_Obj* numObj)
}
declare 21 generic {
	void CBOR_EmitBool(CBOR_Buf* buf, int val)
}
declare 22 generic {
	void CBOR_EmitNull(CBOR_Buf* buf)
}
declare 23 generic {
	void CBOR_EmitUndefined(CBOR_Buf* buf)
}
declare 24 generic {
	int CBOR_EmitSimple(Tcl_Interp* interp, CBOR_Buf* buf, int val)
}
declare 25 generic {
	void CBOR_EmitRaw(CBOR_Buf* buf, const uint8_t* bytes, size_t len)
}
# Encoding >>>

# vim: ft=tcl foldmethod=marker foldmarker=<<<,>>>
