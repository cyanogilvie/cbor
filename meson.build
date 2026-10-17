project('cbor', 'c',
  version:            '1.2.2',
  # A single c_std, not a fallback list ('gnu2y,gnu23,gnu2x'): meson (1.9.1)
  # doesn't resolve the list when this is built as a subproject, and passes it
  # to the compiler verbatim, failing every configure check.  gnu2x is C23 on
  # current gcc / clang; defer support is detected separately (teabase).
  default_options:    ['c_std=gnu2x', 'warning_level=3', 'werror=true'],
  meson_version:      '>=1.3.0',
)

fs = import('fs')
cc = meson.get_compiler('c')

add_project_arguments('-Wno-missing-field-initializers', language: 'c')
add_project_arguments('-DHAVE_CONFIG_H', '-DBUILD_cbor', language: 'c')
# Only the stubs API (EXTERN / DLLEXPORT) and Cbor_Init are exported: keep the
# internal helpers from interposing with other packages' symbols when loaded
# RTLD_GLOBAL (as jitc does for package dependencies)
add_project_arguments(cc.get_supported_arguments('-fvisibility=hidden'), language: 'c')

pkg_sources = []
conf = configuration_data()

tea_want_tommath = true
tea_want_defer   = true

deps = []
inc = [include_directories('.')]

teaconf_files = files('cbor.h.in')

# tea_genstubs: 1st: .decls, 2nd: stublib.c
tea_genstubs = files('cbor.decls', 'cborStubLib.c')

subdir('teabase/init')

if tclver_major.to_int() < 9
  error('This extension requires Tcl 9 or later (found: ' + tclver + ')')
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
  'teabase/register_config.c',
)

subdir('teabase')

# Generate relocatable pkg-config file
pc_conf = configuration_data()
pc_conf.set('PACKAGE_NAME', meson.project_name())
pc_conf.set('PACKAGE_VERSION', meson.project_version())
pc_conf.set('PKG_DIR_BASENAME', meson.project_name() + '-' + meson.project_version())
pc_conf.set('STUBLIB', tea_pkg_stub_name)

configure_file(
  input:          meson.project_name() + '.pc.in',
  output:         meson.project_name() + '.pc',
  configuration:  pc_conf,
  install:        true,
  install_tag:    meson.is_subproject() ? 'deps' : 'runtime',
  install_dir:    tcl_dep.get_variable('libdir') / 'pkgconfig',
)

cbor_dep = declare_dependency(
  link_with:            stubs_lib,
  sources:              genstubs[1],
  include_directories:  include_directories('teabase'),
  compile_args:         '-DUSE_CBOR_STUBS=1',
  variables: {
    'tcl_pkg_path': fs.parent(pkg_so.full_path()),
    'version':      meson.project_version(),
  },
)
meson.override_dependency(meson.project_name(), cbor_dep)

# Test-only extension consuming the stubs API the way a downstream meson
# extension would (tests/stubs.test)
_tclstub = tcl_dep.type_name() == 'internal' ? disabler() : cc.find_library('tclstub', dirs: [tcl_dep.get_variable('libdir')], required: false)
if _tclstub.found()
  shared_module('cborstubsconsumer',
    'tests/stubs_consumer.c',
    dependencies: [tcl_hdr_dep, _tclstub, cbor_dep],
    install:      false,
  )
endif
subdir('doc')

meson.add_dist_script('tools/dist_fixup.sh')

# vim: foldmethod=marker foldmarker=<<<,>>> shiftwidth=2 expandtab
