# Tag 0 wrapping a date/time string: C0 74 ...
set data [binary decode hex {c0 74 32 30 31 33 2d 30 33 2d 32 31 54 32 30 3a 30 34 3a 30 30 5a}]

cbor tget tags $data
# => 2013-03-21T20:04:00Z
set tags
# => 0
