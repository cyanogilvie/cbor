# NAME

cbor - CBOR encoder/decoder for Tcl

## SYNOPSIS

**package require cbor** ?1.2.2?

**cbor** **get** *cbor* ?*key* …?  
**cbor** **tget** *tagsvar* *cbor* ?*key* …?  
**cbor** **extract** *cbor* ?*key* …?  
**cbor** **wellformed** *bytes*  
**cbor** **apply_tag** *tag* *value*  
**cbor** **pretty** *cbor*  
**cbor** **template** *jsonTemplate* ?*dict*?  
**cbor** **encode** *type* ?*value* …?

## DESCRIPTION

This package provides commands for encoding and decoding CBOR (Concise
Binary Object Representation, RFC 8949) data in Tcl.

CBOR is a binary serialization format designed for small code size,
small message size, and extensibility without version negotiation. It is
used in a wide range of protocols and applications, including CoAP,
COSE, CWT, WebAuthn, and CTAP2.

CBOR values are represented in Tcl as byte arrays. The **cbor** commands
decode these byte arrays into native Tcl values, and encode Tcl values
into CBOR using either a JSON-based template system or low-level
primitives.

### Type Mapping

CBOR types are mapped to Tcl values as follows:

| CBOR Type               | Tcl Value                          |
|-------------------------|------------------------------------|
| unsigned integer        | integer                            |
| negative integer        | integer                            |
| byte string             | byte array                         |
| text string             | string                             |
| array                   | list                               |
| map                     | list (alternating keys and values) |
| tag 0 (date/time)       | string (as tagged)                 |
| tag 1 (epoch time)      | number (as tagged)                 |
| tag 2 (unsigned bignum) | bignum                             |
| tag 3 (negative bignum) | bignum                             |
| true                    | `true`                             |
| false                   | `false`                            |
| null                    | empty string                       |
| undefined               | empty string                       |
| half/single/double      | double                             |
| simple(N)               | `simple(N)`                        |

## COMMANDS

**cbor** **get** *cbor* ?*key* …?  
Decode *cbor* and return the resulting Tcl value. If *key* arguments are
given, navigate into the CBOR structure before decoding: integer keys
index into arrays (0-based, with `end-N` relative indexing), and string
keys look up map entries by matching the key’s CBOR-encoded value.
Throws **CBOR** **NOTFOUND** if the path does not exist.

**cbor** **tget** *tagsvar* *cbor* ?*key* …?  
Like **cbor get**, but also stores a list of CBOR tag numbers that were
applied to the resolved data item in the variable named *tagsvar*. This
allows the caller to distinguish tagged values (e.g., date/time strings
tagged with tag 0) from plain values.

**cbor** **extract** *cbor* ?*key* …?  
Navigate to the data item addressed by *key* arguments (as for **cbor
get**), then return the raw CBOR bytes of that data item rather than
decoding it. This is useful for extracting a sub-structure to pass to
another CBOR consumer without the cost of decoding and re-encoding.
Throws **CBOR** **NOTFOUND** if the path does not exist.

**cbor** **wellformed** *bytes*  
Validate that *bytes* contains exactly one well-formed CBOR data item
with no trailing bytes. Returns true on success. Throws **CBOR**
**TRUNCATED**, **CBOR** **INVALID**, or **CBOR** **TRAILING** on
failure.

**cbor** **apply_tag** *tag* *value*  
Apply a CBOR tag interpretation to *value* and return the result.
Currently supported tags:

**2** (unsigned bignum)  
Interprets *value* as a big-endian unsigned byte array and returns the
corresponding Tcl bignum.

**3** (negative bignum)  
Interprets *value* as a big-endian unsigned byte array representing *n*,
and returns *-1 - n* as a Tcl bignum.

**cbor** **pretty** *cbor*  
Return a human-readable representation of *cbor* using the CBOR
diagnostic notation defined in RFC 8949 Section 8, with Extended
Diagnostic Notation (EDN) float precision suffixes from RFC 8610. Nested
structures are indented for readability. Byte strings use `h'...'` hex
notation, tags use `N(...)` notation, and floats show their encoding
precision as `_1` (half), `_2` (single), or `_3` (double).

**cbor** **template** *jsonTemplate* ?*dict*?  
Encode a CBOR value from a JSON template. The *jsonTemplate* is valid
JSON text whose structure maps directly to CBOR (see **TEMPLATE SYNTAX**
below). String values in the template may contain substitution markers
(`~S:`, `~N:`, etc.) that are replaced with values from *dict* or, if
*dict* is omitted, from variables in the caller’s scope. Returns a byte
array containing well-formed CBOR.

**cbor** **encode** *type* ?*value* …?  
Encode a single CBOR data item. The *type* determines how *value* is
interpreted:

**string** *value*  
UTF-8 text string.

**int** *value*  
Integer (positive or negative). Automatically selects the smallest CBOR
encoding. Values exceeding the 64-bit range are encoded as tagged
bignums (tag 2 or 3).

**uint** *value*  
Unsigned integer. Errors if *value* is negative.

**nint** *value*  
CBOR negative integer argument. Encodes the value *-1 - N* where *N* is
the non-negative *value*.

**float** *value*  
IEEE 754 floating-point number. Automatically selects the smallest
precision (half, single, or double) that represents *value* exactly.
Accepts **Inf**, **-Inf**, and **NaN**.

**bytes** *value*  
Byte string. *value* must be a byte array.

**bool** *value*  
CBOR boolean. Accepts any Tcl boolean value.

**null**  
CBOR null. No *value* argument.

**undefined**  
CBOR undefined. No *value* argument.

**simple** *value*  
CBOR simple value. *value* must be 0-23 or 32-255 (the range 24-31 is
reserved).

**tag** *tagnum* *cborvalue*  
CBOR tag. *tagnum* is a non-negative integer and *cborvalue* is a byte
array containing a complete CBOR data item.

**array** *list*  
CBOR array. *list* is a Tcl list of byte arrays, each a complete CBOR
data item.

**map** *dict*  
CBOR map with string keys. *dict* is a Tcl dictionary where keys are
strings (encoded as CBOR text strings) and values are byte arrays
containing complete CBOR data items.

**imap** *list*  
CBOR map with arbitrary key types. *list* is a flat list of alternating
pre-encoded CBOR key-value pairs. This allows integer keys, byte string
keys, or any other CBOR type as map keys, as required by protocols like
COSE and WebAuthn.

## TEMPLATE SYNTAX

The **cbor template** command accepts valid JSON as its template. Each
JSON construct maps to CBOR as follows:

| JSON construct   | CBOR encoding                                   |
|------------------|-------------------------------------------------|
| `null`           | null (simple value 22)                          |
| `true` / `false` | boolean (simple values 21 / 20)                 |
| integer literal  | unsigned or negative integer, smallest encoding |
| float literal    | float, smallest IEEE 754 precision              |
| plain string     | UTF-8 text string                               |
| array `[...]`    | definite-length array                           |
| object `{...}`   | definite-length map (unless a tag directive)    |

### Substitution Markers

JSON string values beginning with `~` followed by a type letter and `:`
are substitution markers. The portion after the prefix names a variable
(or dictionary key if *dict* is supplied). If the named variable does
not exist, CBOR null is emitted.

| Marker    | CBOR output                                                                                                     |
|-----------|-----------------------------------------------------------------------------------------------------------------|
| `~S:var`  | UTF-8 text string from *var*                                                                                    |
| `~N:var`  | integer or float, auto-detected from *var*                                                                      |
| `~B:var`  | boolean from *var*                                                                                              |
| `~C:var`  | raw CBOR bytes from *var*, copied verbatim (must be well-formed)                                                |
| `~BY:var` | byte string from *var*                                                                                          |
| `~L:text` | literal text string (the text after `~L:` is the value)                                                         |
| `~T:var`  | recursive template (value of *var* is JSON, processed with the same dictionary)                                 |
| `~J:var`  | the JSON value in *var*, encoded as if it were a literal part of the template, but without substituting markers |

Substitution markers work in both **value** and **key** positions of
JSON objects. This enables maps with non-string keys, as required by
protocols like COSE:

``` tcl
# COSE-style map with integer keys: {1: -7, 4: h'...'}
cbor template {{"~N:alg": "~N:algval", "~N:kid": "~BY:kidval"}} \
    {alg 1 algval -7 kid 4 kidval \x01\x02}
```

Plain (unsubstituted) keys are encoded as UTF-8 text strings as usual.

The value of a **~J:** marker is JSON text (such as an **rl_json**
value), converted as RFC 8949 section 6.2 describes, which is the same
way the template’s own literals are converted: objects become maps with
text string keys in document order, arrays become arrays, strings become
text strings, numbers without a fraction or exponent become integers
(tags 2 and 3 for those beyond 64 bits) and other numbers become floats
in the smallest precision that represents them exactly, and **true**,
**false** and **null** become the corresponding simple values. Unlike
**~T:**, the value is data, not a template: strings that look like
substitution markers, and objects shaped like tag directives, are
encoded as they are. No other conversions are attempted (strings are
never turned into byte strings, dates or numbers), since JSON can’t say
which strings were meant as any of those.

### Tag Directives

A JSON object with exactly two keys, `~tag` and `~val`, is interpreted
as a CBOR tag rather than a map. The `~tag` value must be a non-negative
integer (the tag number), and `~val` is any template expression:

``` json
{"~tag": 0, "~val": "~S:datevar"}
```

The keys may appear in either order. Tag directives nest naturally:

``` json
{"~tag": 1, "~val": {"~tag": 55799, "~val": "~N:epoch"}}
```

## PATH NAVIGATION

The *key* arguments to **get**, **tget**, and **extract** form a path
into the CBOR structure. At each level:

- For **arrays**, the key is an integer index (0-based). The `end-N`
  syntax is supported for indexing relative to the end of the array.
  Both definite-length and indefinite-length arrays are handled.

- For **maps**, the key is matched against map entry keys. The first
  matching key’s value is selected.

- **Tags** are transparent: the path continues through tagged values to
  the underlying data item. Use **tget** to retrieve the tag numbers.

Attempting to index into an atomic value (integer, string, etc.) is an
error.

## EXCEPTIONS

**CBOR** **TRUNCATED**  
The CBOR data ended unexpectedly before a complete data item could be
read.

**CBOR** **INVALID** *detail*  
The CBOR data contains a syntax error, such as a reserved additional
info value or an invalid indefinite-length encoding for a type that does
not support it.

**CBOR** **TRAILING**  
The input contained additional bytes after a complete CBOR data item
(only raised by **wellformed**).

**CBOR** **NOTFOUND** *path*  
The specified path does not exist in the CBOR structure (index out of
range, or map key not found).

## EXAMPLES

Decode a simple CBOR integer:

``` tcl
# CBOR encoding of the integer 42 (0x18 0x2a)
set data [binary decode hex {18 2a}]
cbor get $data
# => 42
```

Navigate into a nested CBOR structure (a map containing an array):

``` tcl
# {"scores": [95, 87, 92]}
# Encoded as: A1 66 73636F726573 83 18 5F 18 57 18 5C
set data [binary decode hex {a1 66 73636f726573 83 18 5f 18 57 18 5c}]

cbor get $data scores        ;# => 95 87 92
cbor get $data scores 0      ;# => 95
cbor get $data scores end-0  ;# => 92
```

Extract raw CBOR bytes for a sub-structure:

``` tcl
set data [binary decode hex {a1 66 73636f726573 83 18 5f 18 57 18 5c}]

set sub [cbor extract $data scores]
cbor wellformed $sub  ;# => 1
cbor get $sub         ;# => 95 87 92
```

Decode a CBOR value and check its tags:

``` tcl
# Tag 0 wrapping a date/time string: C0 74 ...
set data [binary decode hex {c0 74 32 30 31 33 2d 30 33 2d 32 31 54 32 30 3a 30 34 3a 30 30 5a}]

cbor tget tags $data
# => 2013-03-21T20:04:00Z
set tags
# => 0
```

Encode CBOR from a JSON template:

``` tcl
set name "Alice"
set age 30
set cbor [cbor template {{"name": "~S:name", "age": "~N:age"}}]
cbor get $cbor
# => name Alice age 30
```

Encode with a substitution dictionary:

``` tcl
set cbor [cbor template {
    {"scores": ["~N:math", "~N:sci", "~N:eng"]}
} {math 95 sci 87 eng 92}]

cbor get $cbor scores 0
# => 95
```

Encode a tagged value:

``` tcl
set cbor [cbor template {{"~tag": 0, "~val": "2024-01-15T10:30:00Z"}}]
cbor tget tags $cbor
# => 2024-01-15T10:30:00Z
set tags
# => 0
```

Build CBOR incrementally with `~C:` fragment interpolation:

``` tcl
set header [cbor template {{"version": 1, "type": "request"}}]
set body   [cbor template {{"method": "~S:m", "params": ["~N:x"]}} {m add x 42}]
set msg    [cbor template {["~C:hdr", "~C:body"]} \
                [dict create hdr $header body $body]]
```

Encode individual CBOR values with **cbor encode**:

``` tcl
cbor encode string "hello"          ;# UTF-8 text string
cbor encode int 42                  ;# smallest integer encoding
cbor encode float 3.14              ;# smallest float precision
cbor encode bool yes                ;# CBOR true

# Build an array from parts
set items [list [cbor encode int 1] [cbor encode int 2] [cbor encode int 3]]
cbor encode array $items            ;# definite-length array [1, 2, 3]

# Tag a value
cbor encode tag 0 [cbor encode string "2024-01-15T10:30:00Z"]
```

## C API

The package exports a C API through a Tcl stubs table, for other
extensions (and JIT-compiled C) that need to produce or consume CBOR
without going through the script-level commands. The public header is
**cbor.h**, installed in the package directory alongside **cborDecls.h**
and the stubs library **libcborstub.a**.

### Using the API

From a meson-built extension, depend on **cbor** (resolved through the
installed **cbor.pc**, or a subproject wrap providing the **cbor**
dependency), which supplies the include path, the stubs library and
**-DUSE_CBOR_STUBS=1**, and initialise the stubs after Tcl’s:

``` c
#include <cbor.h>

DLLEXPORT int Myext_Init(Tcl_Interp* interp)
{
    if (Tcl_InitStubs(interp, TCL_VERSION, 0) == NULL) return TCL_ERROR;
    if (Cbor_InitStubs(interp) == NULL) return TCL_ERROR;
    ...
}
```

**Cbor_InitStubs** does a **package require** of at least the version of
cbor the extension was compiled against. Other build systems can get the
same flags from `pkg-config --cflags --libs cbor`.

The package registers its build information with **Tcl_RegisterConfig**
(TIP 59) as **cbor::pkgconfig**, with the keys **header**, **library**,
**stublib**, **includedir,runtime**, **libdir,runtime** and
**packagedir,runtime** (the latter three are the directory the package
was loaded from). This lets **jitc** link C code directly against the
package:

``` tcl
package require jitc
jitc::capply {
    package {cbor 1.2.2}
    code {
        OBJCMD(pair) {
            CBOR_Buf b;
            CBOR_BufInit(&b);
            CBOR_EmitHead(&b, CBOR_MT_ARRAY, 2);
            CBOR_EmitText(&b, "pi", 2);
            CBOR_EmitFloat(&b, 3.14159);
            Tcl_SetObjResult(interp, CBOR_BufResult(&b));
            CBOR_BufFree(&b);
            return TCL_OK;
        }
    }
} pair
```

### Conventions

Functions returning **int** return **TCL_OK** or **TCL_ERROR**, and on
error leave a message (replacing the interp result) and, for malformed
or truncated CBOR, the same **-errorcode** as the script commands (see
**EXCEPTIONS**). *interp* may be NULL for functions that only report
errors through it.

Results returned through a **Tcl_Obj\*\*** argument are assigned in the
manner of **replace_tclobj**: any object the variable already references
is released, and the variable then holds a reference to the result,
which the caller must eventually release. Initialise such variables to
NULL.

Decoding functions take the bounds of the CBOR data as a pointer *p* to
the start of a data item and a pointer *e* just past the end of the
buffer, and never read beyond *e*. Functions taking **pPtr** advance it
past the data they consume.

### Decoding

**int CBOR_WellFormed**(*interp*, const uint8_t\* *bytes*, size_t *len*)  
As **cbor wellformed**: check that *bytes* holds exactly one well-formed
data item.

**int CBOR_Length**(*interp*, const uint8_t\* *p*, const uint8_t\* *e*, size_t\* *lenPtr*)  
Store in *lenPtr* the length in bytes of the (well-formed) data item at
*p*, including any tags. Use it to skip an item.

**int CBOR_ReadHead**(*interp*, const uint8_t\*\* *pPtr*, const uint8_t\* *e*, CBOR_Head\* *head*)  
Decode the head of the data item at *\*pPtr* into *head* and advance
*\*pPtr* past it, for walking CBOR one item at a time. The fields of
**CBOR_Head** are: **mt** (a **CBOR_MajorType**: **CBOR_MT_UINT**,
**\_NINT**, **\_BYTES**, **\_TEXT**, **\_ARRAY**, **\_MAP**, **\_TAG**
or **\_SIMPLE**), **ai** (the additional information), **val** (the
argument: the integer value, the byte length of a string, the element or
pair count, the tag number or the simple value), **indefinite** (set for
indefinite-length strings, arrays and maps), **isbreak** (set for the
break stop code that ends them) and **fval** (the value of a half,
single or double float). The payload of a definite-length byte or text
string (**val** bytes) follows the head and is not consumed.

**int CBOR_DecodeValue**(*interp*, const uint8_t\*\* *pPtr*, const uint8_t\* *e*, Tcl_Obj\*\* *resPtr*, Tcl_Obj\*\* *tagsPtr*)  
Decode the complete data item at *\*pPtr* to a Tcl value (as described
in **Type Mapping**), and advance *\*pPtr* past it. If *tagsPtr* is not
NULL it receives the list of tag numbers applied to the item.

**int CBOR_GetDataItemFromPath**(*interp*, Tcl_Obj\* *cborObj*, Tcl_Obj\* *pathObj*, const uint8_t\*\* *dataitemPtr*, const uint8_t\*\* *ePtr*, Tcl_DString\* *tagsPtr*)  
Resolve the list of keys *pathObj* (as for **cbor get**; NULL for the
root) in the byte array *cborObj* without decoding, setting
*\*dataitemPtr* to the addressed data item (NULL if the path doesn’t
exist) and *\*ePtr* to the end of the buffer. The pointers refer into
*cborObj*’s byte array and are valid while it is unmodified. If
*tagsPtr* is not NULL it receives the tags of the last container
traversed, as packed **uint64_t** values.

**int CBOR_Get**(*interp*, Tcl_Obj\* *cborObj*, Tcl_Obj\* *pathObj*, Tcl_Obj\*\* *resPtr*, Tcl_Obj\*\* *tagsPtr*)  
As **cbor get** (or **cbor tget** when *tagsPtr* is not NULL). Throws
**CBOR NOTFOUND** if the path doesn’t exist.

**int CBOR_Extract**(*interp*, Tcl_Obj\* *cborObj*, Tcl_Obj\* *pathObj*, Tcl_Obj\*\* *resPtr*)  
As **cbor extract**.

**int CBOR_Pretty**(*interp*, const uint8_t\* *bytes*, size_t *len*, Tcl_DString\* *ds*)  
As **cbor pretty**, appending to the initialised *ds*.

**int CBOR_ApplyTag**(*interp*, uint64_t *tag*, Tcl_Obj\* *valueObj*, Tcl_Obj\*\* *resPtr*)  
As **cbor apply_tag**.

### Encoding

**int CBOR_Template**(*interp*, Tcl_Obj\* *templateObj*, Tcl_Obj\* *dictObj*, Tcl_Obj\*\* *resPtr*)  
As **cbor template**. If *dictObj* is NULL, substitutions resolve
variables in the current call frame.

To build CBOR item by item, stack-allocate a **CBOR_Buf**, initialise it
with **CBOR_BufInit**, append items with the functions below, and
finally take the result with **CBOR_BufResult** and release the buffer
with **CBOR_BufFree**. The object returned by **CBOR_BufResult** is
owned by the buffer: take a reference to it (or pass it to
**Tcl_SetObjResult**) before **CBOR_BufFree**. A buffer must be freed
even if encoding fails part way. Container heads are written explicitly,
so the caller is responsible for following an array head of *n* with *n*
items, a map head of *n* with *n* key / value pairs, and an
indefinite-length head with its items and a break.

**void CBOR_BufInit**(CBOR_Buf\* *buf*)  
**void CBOR_BufFree**(CBOR_Buf\* *buf*)  
**Tcl_Obj\* CBOR_BufResult**(CBOR_Buf\* *buf*) : Buffer lifecycle, as
described above.

**void CBOR_EmitHead**(CBOR_Buf\* *buf*, CBOR_MajorType *mt*, uint64_t *val*)  
Append a head with the smallest encoding of *val*: an unsigned integer
(**CBOR_MT_UINT**), a negative integer *-1-val* (**CBOR_MT_NINT**), a
tag (**CBOR_MT_TAG**), or the head of a definite-length array or map
(**CBOR_MT_ARRAY**, **CBOR_MT_MAP**) of *val* items or pairs.

**int CBOR_EmitIndefinite**(*interp*, CBOR_Buf\* *buf*, CBOR_MajorType
*mt*)  
**void CBOR_EmitBreak**(CBOR_Buf\* *buf*) : Start an indefinite-length
byte string, text string, array or map (an error for any other major
type), and end it.

**void CBOR_EmitBytes**(CBOR_Buf\* *buf*, const uint8_t\* *bytes*,
size_t *len*)  
**void CBOR_EmitText**(CBOR_Buf\* *buf*, const char\* *str*, size_t
*len*) : Append a byte string, or a text string (*str* must be UTF-8).

**void CBOR_EmitWideInt**(CBOR_Buf\* *buf*, Tcl_WideInt *val*)  
Append an integer.

**void CBOR_EmitFloat**(CBOR_Buf\* *buf*, double *val*)  
Append a float in the smallest precision that represents *val* exactly,
as for **cbor encode float**.

**int CBOR_EmitNumber**(*interp*, CBOR_Buf\* *buf*, Tcl_Obj\* *numObj*)  
Append the Tcl number *numObj*, as for the **~N:** template marker:
integers (including bignums, as tags 2 and 3) as integers, and doubles
as floats.

**void CBOR_EmitBool**(CBOR_Buf\* *buf*, int *val*)  
**void CBOR_EmitNull**(CBOR_Buf\* *buf*)  
**void CBOR_EmitUndefined**(CBOR_Buf\* *buf*)  
**int CBOR_EmitSimple**(*interp*, CBOR_Buf\* *buf*, int *val*) : Append
a simple value. **CBOR_EmitSimple** rejects the reserved values 24-31
and values outside 0-255.

**void CBOR_EmitRaw**(CBOR_Buf\* *buf*, const uint8_t\* *bytes*, size_t *len*)  
Append pre-encoded CBOR verbatim (not validated).

## BUILDING

This package requires **Tcl 9.0** or later and a C compiler supporting
**C2y** (or at minimum C23 with GNU extensions). The `defer` keyword
from the C2y draft standard is used throughout; on compilers that do not
yet implement it natively (such as GCC), a polyfill based on GCC nested
functions and `__attribute__((cleanup))` is used automatically. Clang
22.1+ supports `defer` natively via the `-fdefer-ts` flag, which the
build system detects and enables.

The build system is **meson** (\>= 1.3.0). If no installed Tcl 9 is
found, meson will build Tcl from source automatically using the bundled
wrap file.

### From a Release Tarball

``` sh
wget https://github.com/cyanogilvie/cbor/releases/download/v1.2.2/cbor-v1.2.2.tar.gz
tar xf cbor-v1.2.2.tar.gz
cd cbor-v1.2.2
meson setup builddir --buildtype=release
meson install -C builddir
```

### From the Git Sources

``` sh
git clone --recurse-submodules https://github.com/cyanogilvie/cbor
cd cbor
meson setup builddir --buildtype=release
meson install -C builddir
```

If Tcl 9 is installed in a non-standard location, point meson to it via
`PKG_CONFIG_PATH`:

``` sh
PKG_CONFIG_PATH=/path/to/tcl9/lib/pkgconfig meson setup builddir --buildtype=release
```

### In a Docker Build

``` dockerfile
WORKDIR /tmp/cbor
RUN wget https://github.com/cyanogilvie/cbor/releases/download/v1.2.2/cbor-v1.2.2.tar.gz -O - | tar xz --strip-components=1 && \
    meson setup builddir --buildtype=release && \
    meson install -C builddir && \
    strip /usr/local/lib/libcbor*.so && \
    cd .. && rm -rf cbor
```

### Testing

``` sh
meson test -C builddir
```

## CONFORMING TO

RFC 8949 - Concise Binary Object Representation (CBOR).

## BUGS

Please report bugs at: https://github.com/cyanogilvie/cbor/issues

## CLAUDE CODE SKILL

A Claude Code skill file is included to give Claude expert knowledge of
this package’s API. To install it, symlink the skill directory into your
Claude skills directory:

``` sh
ln -s /path/to/cbor/claude/skills/encoding-decoding-cbor ~/.claude/skills/encoding-decoding-cbor
```

Once installed, Claude Code will automatically use the skill when
working with CBOR data in Tcl.

## SEE ALSO

RFC 8949, **rl_json**(n)

## LICENSE

This package is Copyright 2025-2026 Cyan Ogilvie, and is made available
under the same license terms as the Tcl Core.
