# CBOR encoding of the integer 42 (0x18 0x2a)
set data [binary decode hex {18 2a}]
cbor get $data
# => 42
