cbor encode string "hello"          ;# UTF-8 text string
cbor encode int 42                  ;# smallest integer encoding
cbor encode float 3.14              ;# smallest float precision
cbor encode bool yes                ;# CBOR true

# Build an array from parts
set items [list [cbor encode int 1] [cbor encode int 2] [cbor encode int 3]]
cbor encode array $items            ;# definite-length array [1, 2, 3]

# Tag a value
cbor encode tag 0 [cbor encode string "2024-01-15T10:30:00Z"]
