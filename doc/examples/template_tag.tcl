set cbor [cbor template {{"~tag": 0, "~val": "2024-01-15T10:30:00Z"}}]
cbor tget tags $cbor
# => 2024-01-15T10:30:00Z
set tags
# => 0
