set header [cbor template {{"version": 1, "type": "request"}}]
set body   [cbor template {{"method": "~S:m", "params": ["~N:x"]}} {m add x 42}]
set msg    [cbor template {["~C:hdr", "~C:body"]} \
                [dict create hdr $header body $body]]
