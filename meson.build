project('DrJson', 'c', version: '1.0.0')
COMPAT_VERSION='1.0.0' # for macos dylib

if host_machine.system() == 'windows'
  ignore_bogus_deprecations = ['-D_CRT_NONSTDC_NO_WARNINGS', '-D_CRT_SECURE_NO_WARNINGS']
else
  ignore_bogus_deprecations = []
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
  c_args: ignore_bogus_deprecations,
)
executable('drjson', 'DrJson/drjson_cli.c', install:true, c_args:ignore_bogus_deprecations)
