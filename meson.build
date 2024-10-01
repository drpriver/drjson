project('DrJson', 'c', version: '3.2.0')
COMPAT_VERSION='3.0.0' # for macos dylib

if host_machine.system() == 'windows'
  ignore_bogus_deprecations = ['-D_CRT_NONSTDC_NO_WARNINGS', '-D_CRT_SECURE_NO_WARNINGS']
else
  ignore_bogus_deprecations = []
endif

if host_machine.system() == 'darwin'
  arches = ['-arch', 'x86_64', '-arch', 'arm64']
else
  arches = []
endif

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required: false)

install_headers('DrJson/drjson.h', subdir:'DrJson')

drjson_dylib = both_libraries(
  'drjson',
  'DrJson/drjson.c',
  install:true,
  dependencies:[m_dep],
  version: meson.project_version(),
  soversion: meson.project_version(),
  darwin_versions:[COMPAT_VERSION, meson.project_version()],
  c_args: ignore_bogus_deprecations + arches,
  link_args: arches,
)
executable('drjson', 'DrJson/drjson_cli.c', install:true, c_args:ignore_bogus_deprecations)

test('test-drjson',
  executable('test-drjson', 'DrJson/test_drjson.c', link_with:drjson_dylib, c_args: ignore_bogus_deprecations+arches))
test('test-drjson-static',
  executable('test-drjson-static', 'DrJson/test_drjson.c', 'DrJson/drjson.c', c_args: ignore_bogus_deprecations+arches+['-DDRJSON_STATIC_LIB=1']))
test('test-drjson-unity',
  executable('test-drjson-unity', 'DrJson/tdrj.c',  c_args: ignore_bogus_deprecations+arches))
