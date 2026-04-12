# NAME

cbor - CBOR encoder/decoder for Tcl

## SYNOPSIS

**package require cbor** ?1.0?

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

| Marker    | CBOR output                                                                     |
|-----------|---------------------------------------------------------------------------------|
| `~S:var`  | UTF-8 text string from *var*                                                    |
| `~N:var`  | integer or float, auto-detected from *var*                                      |
| `~B:var`  | boolean from *var*                                                              |
| `~C:var`  | raw CBOR bytes from *var*, copied verbatim (must be well-formed)                |
| `~BY:var` | byte string from *var*                                                          |
| `~L:text` | literal text string (the text after `~L:` is the value)                         |
| `~T:var`  | recursive template (value of *var* is JSON, processed with the same dictionary) |

Substitution markers work in both **value** and **key** positions of
JSON objects. This enables maps with non-string keys, as required by
protocols like COSE:

``` tcl
# COSE-style map with integer keys: {1: -7, 4: h'...'}
cbor template {{"~N:alg": "~N:algval", "~N:kid": "~BY:kidval"}} \
    {alg 1 algval -7 kid 4 kidval \x01\x02}
```

Plain (unsubstituted) keys are encoded as UTF-8 text strings as usual.

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
wget https://github.com/cyanogilvie/cbor/releases/download/v1.0/cbor-v1.0.tar.gz
tar xf cbor-v1.0.tar.gz
cd cbor-v1.0
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
RUN wget https://github.com/cyanogilvie/cbor/releases/download/v1.0/cbor-v1.0.tar.gz -O - | tar xz --strip-components=1 && \
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
