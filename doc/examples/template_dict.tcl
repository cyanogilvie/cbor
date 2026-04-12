set cbor [cbor template {
    {"scores": ["~N:math", "~N:sci", "~N:eng"]}
} {math 95 sci 87 eng 92}]

cbor get $cbor scores 0
# => 95
