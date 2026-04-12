---
name: encoding-decoding-cbor
description: Encode and decode CBOR (RFC 8949) in Tcl using the cbor package. Provides JSON-based templates for encoding, low-level encode primitives, and path-based decoding. Use when working with CBOR binary data, building CBOR documents, or when the user mentions cbor commands or CBOR encoding/decoding.
---

## Loading

```tcl
package require cbor
```

CBOR values are Tcl byte arrays. Encoding uses either JSON templates
(like rl_json's template mechanism) or low-level primitives.

## Decoding

**cbor get** *cbor* ?*key* ...?
:   Decode to Tcl values. Keys navigate: integers index arrays (0-based,
    `end-N`), strings match map keys.

```tcl
set data [binary decode hex {a2 61 61 01 61 62 82 02 03}]
cbor get $data           ;# => a 1 b {2 3}
cbor get $data b 0       ;# => 2
```

**cbor tget** *tagsvar* *cbor* ?*key* ...?
:   Like `get`, but stores CBOR tag numbers in *tagsvar*.

**cbor extract** *cbor* ?*key* ...?
:   Returns raw CBOR bytes at path (no decoding).

**cbor wellformed** *bytes*
:   Validates one well-formed CBOR data item. Throws on error.

**cbor pretty** *cbor*
:   Human-readable RFC 8949 diagnostic notation. Indented, with EDN
    float suffixes (`_1`/`_2`/`_3`), `h'...'` for bytes, `N(...)` for tags.

**cbor apply_tag** *tag* *value*
:   Interprets tagged values. Tags 2/3 convert byte strings to bignums.

## Encoding with Templates

**cbor template** *jsonTemplate* ?*dict*?
:   JSON template to CBOR. Values from *dict* or caller's variables.
    Missing variables produce CBOR null.

```tcl
# Static
cbor template {[1, "hello", true, null]}

# With dict substitution
cbor template {{"name": "~S:n", "age": "~N:a"}} {n Alice a 30}

# With variable substitution
set name Alice
cbor template {{"name": "~S:name"}}
```

### Substitution Markers

In JSON string values (both keys and values), `~X:varname` triggers substitution:

| Marker | Output | Notes |
|--------|--------|-------|
| `~S:var` | text string | |
| `~N:var` | integer or float | auto-detected from Tcl value |
| `~B:var` | boolean | any Tcl boolean |
| `~C:var` | raw CBOR | interpolated verbatim; **must be well-formed** |
| `~BY:var` | byte string | for binary data |
| `~L:text` | literal string | no variable lookup; text after prefix is the value |
| `~T:var` | recursive template | value is JSON, processed with same dict |

### Non-String Map Keys

Markers in key position enable maps with integer or byte-string keys
(required by COSE, WebAuthn, CTAP2):

```tcl
# COSE-style: {1: -7, 3: -35}
cbor template {{"~N:alg": "~N:av", "~N:kty": "~N:kv"}} {alg 1 av -7 kty 3 kv -35}
```

### Tag Directives

A JSON object with exactly keys `~tag` (integer) and `~val` encodes a CBOR tag:

```tcl
cbor template {{"~tag": 0, "~val": "~S:date"}} {date 2024-01-15T10:30:00Z}
```

Keys may appear in either order. Objects without exactly `~tag`+`~val` are normal maps.

### Fragment Composition with ~C:

Build CBOR incrementally — `~C:` interpolates pre-built CBOR fragments
(validated for well-formedness):

```tcl
set header [cbor template {{"version": 1, "type": "request"}}]
set body   [cbor template {{"method": "~S:m"}} {m add}]
set msg    [cbor template {["~C:hdr", "~C:body"]} \
                [dict create hdr $header body $body]]
```

## Encoding with Primitives

**cbor encode** *type* ?*value* ...?

Types: `string`, `int`, `uint`, `nint`, `float`, `bytes`, `bool`,
`null`, `undefined`, `simple` *N*, `tag` *N* *cborval*,
`array` *list-of-cbor*, `map` *dict*, `imap` *list*.

`int` auto-selects smallest encoding; bignums produce tag 2/3.
`float` auto-selects half/single/double for smallest exact representation.
`simple` encodes CBOR simple values 0-23, 32-255.
`array` takes a list of pre-encoded CBOR byte arrays.
`map` takes a dict with **plain string keys** and pre-encoded CBOR byte array values.
`imap` takes a flat list of alternating pre-encoded CBOR key-value pairs (any key type).

```tcl
cbor encode string hello
cbor encode int 42
cbor encode float 3.14
cbor encode null
cbor encode simple 16              ;# simple(16)

# Array: list of pre-encoded CBOR values
set items [list [cbor encode int 1] [cbor encode int 2]]
cbor encode array $items

# Map with string keys
set m [dict create name [cbor encode string Bob] age [cbor encode int 25]]
cbor encode map $m

# Map with arbitrary key types (COSE-style integer keys)
set m [list [cbor encode int 1] [cbor encode int -7] \
            [cbor encode int 4] [cbor encode bytes \x01\x02]]
cbor encode imap $m

cbor encode tag 0 [cbor encode string 2024-01-15T10:30:00Z]
```

## Error Codes

| Code | Meaning |
|------|---------|
| `CBOR TRUNCATED` | Incomplete data item |
| `CBOR INVALID` | Syntax error |
| `CBOR TRAILING` | Extra bytes after data item |
| `CBOR NOTFOUND` | Path key not found |
