project('cbor', 'c',
  version:            '1.0',
  default_options:    ['c_std=gnu2y,gnu23,gnu2x', 'warning_level=3', 'werror=true'],
  meson_version:      '>=1.3.0',
)

fs = import('fs')
cc = meson.get_compiler('c')

add_project_arguments('-Wno-missing-field-initializers', language: 'c')
add_project_arguments('-DHAVE_CONFIG_H', language: 'c')

pkg_sources = []
conf = configuration_data()

tea_want_tommath = true

deps = []
inc = [include_directories('.')]

subdir('teabase/init')

if tclver_major.to_int() < 9
  error('This extension requires Tcl 9 or later (found: ' + tclver + ')')
endif

defer_test = '''
  int main(void) {
    int x = 0;
    _Defer { x = 1; }
    return x;
  }
'''

if cc.compiles(defer_test, name: 'native _Defer')
  conf.set10('HAVE_NATIVE_DEFER', true)
elif cc.compiles(defer_test, args: ['-fdefer-ts'], name: 'native _Defer (-fdefer-ts)')
  conf.set10('HAVE_NATIVE_DEFER', true)
  add_project_arguments('-fdefer-ts', language: 'c')
elif cc.compiles('''
  void cleanup(int *p) { (void)p; }
  int main(void) {
    __attribute__((__cleanup__(cleanup))) int x;
    auto void f(int*);
    inline auto void f(__attribute__((__unused__)) int *p) {}
    return 0;
  }
''', name: 'GCC defer polyfill support')
  conf.set10('HAVE_DEFER_POLYFILL', true)
else
  error('No defer support: need _Defer (clang 22+ with -fdefer-ts) or GCC 9+ nested functions')
endif

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

pkg_sources += files(
  'cbor.c',
  'cbor_encode.c',
  'bignum_ops.c',
)

subdir('teabase')
subdir('doc')

meson.add_dist_script('tools/dist_fixup.sh')

# vim: foldmethod=marker foldmarker=<<<,>>> shiftwidth=2 expandtab
