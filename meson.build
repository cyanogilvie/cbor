project('cbor', 'c',
  version:            '0.1',
  default_options:    ['c_std=c11', 'warning_level=2', 'werror=true'],
  meson_version:      '>=1.3.0',
)

fs = import('fs')
cc = meson.get_compiler('c')

add_project_arguments('-Wno-missing-field-initializers', language: 'c')
add_project_arguments('-DHAVE_CONFIG_H', language: 'c')

pkg_sources = []
conf = configuration_data()

tea_want_shims = [
  'tip445',
  'Tcl_GetBytesFromObj',
]

tea_want_tommath = true

deps = []
inc = [include_directories('generic')]

subdir('teabase/init')

if tcl_has_internal_tommath
  tommath_proj = subproject('libtommath', default_options: [
    'tcl_alloc=true',
    'mp_digit_size=@0@'.format(tcl_mp_digit_size),
  ])
  tommath_dep = tommath_proj.get_variable('libtommath_dep')
else
  tommath_dep = dependency('libtommath', version: '>=1.3.0')
endif
deps += tommath_dep

# bignum_ops.c includes <tommath.h> from the subproject — match digit width
if tcl_mp_digit_size == 4
  add_project_arguments('-DMP_32BIT', language: 'c')
elif tcl_mp_digit_size == 2
  add_project_arguments('-DMP_16BIT', language: 'c')
elif tcl_mp_digit_size == 1
  add_project_arguments('-DMP_8BIT', language: 'c')
endif

subdir('generic')

subdir('teabase')
subdir('doc')

# vim: foldmethod=marker foldmarker=<<<,>>> shiftwidth=2 expandtab
