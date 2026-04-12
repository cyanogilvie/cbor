set data [binary decode hex {a1 66 73636f726573 83 18 5f 18 57 18 5c}]

set sub [cbor extract $data scores]
cbor wellformed $sub  ;# => 1
cbor get $sub         ;# => 95 87 92
