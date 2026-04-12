set name "Alice"
set age 30
set cbor [cbor template {{"name": "~S:name", "age": "~N:age"}}]
cbor get $cbor
# => name Alice age 30
