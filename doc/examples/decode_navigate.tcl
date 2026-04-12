# {"scores": [95, 87, 92]}
# Encoded as: A1 66 73636F726573 83 18 5F 18 57 18 5C
set data [binary decode hex {a1 66 73636f726573 83 18 5f 18 57 18 5c}]

cbor get $data scores        ;# => 95 87 92
cbor get $data scores 0      ;# => 95
cbor get $data scores end-0  ;# => 92
