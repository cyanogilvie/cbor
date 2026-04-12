pandoc = find_program('pandoc', native: true, required: false)

if pandoc.found()
  include_filter = meson.project_source_root() / 'tools/include-code.lua'
  doc_source_dir = meson.project_source_root() / 'doc'

  doc_md = configure_file(
    input:          'cbor.md.in',
    output:         '@BASENAME@',
    configuration:  teaconf,
  )

  # The examples are included via the Lua filter at build time;
  # declare them as dependencies so rebuilds trigger on changes.
  doc_examples = files(
    'examples/decode_basic.tcl',
    'examples/decode_navigate.tcl',
    'examples/decode_extract.tcl',
    'examples/decode_tags.tcl',
    'examples/template_basic.tcl',
    'examples/template_dict.tcl',
    'examples/template_tag.tcl',
    'examples/template_fragment.tcl',
    'examples/encode_primitives.tcl',
  )

  doc_targets = []

  doc_targets += custom_target('htmldoc',
    input:    [doc_md, doc_examples],
    output:   meson.project_name() + '.html',
    command:  [pandoc, '--standalone', '--metadata', 'title=' + meson.project_name(),
      '--lua-filter', include_filter, '--resource-path', doc_source_dir,
      '--from', 'markdown', '--to', 'html',
      '@INPUT0@', '--output', '@OUTPUT@'],
    install:          true,
    install_dir:      get_option('datadir') / 'doc',
    build_by_default: true,
  )
  doc_targets += custom_target('manpage',
    input:    [doc_md, doc_examples],
    output:   meson.project_name() + '.n',
    command:  [pandoc, '--standalone',
      '--lua-filter', include_filter, '--resource-path', doc_source_dir,
      '--from', 'markdown', '--to', 'man',
      '@INPUT0@', '--output', '@OUTPUT@'],
    install:          true,
    install_dir:      get_option('mandir'),
    build_by_default: true,
  )
  doc_targets += custom_target('readme',
    input:    [doc_md, doc_examples],
    output:   'README.md',
    command:  [pandoc,
      '--lua-filter', include_filter, '--resource-path', doc_source_dir,
      '--from', 'markdown', '--to', 'gfm',
      '@INPUT0@', '--output', '@OUTPUT@'],
    install_dir:      meson.project_source_root(),
    install_tag:      'source',
    build_by_default: false,
  )

  alias_target('doc', doc_targets)
endif
