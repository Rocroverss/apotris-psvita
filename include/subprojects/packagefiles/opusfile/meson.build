project('opusfile', ['c'],
    version: '0.12',
    meson_version: '>=1.1.0',
    default_options : ['c_std=gnu99']
)

sources = files(
    'src/info.c',
    'src/internal.c',
    'src/opusfile.c',
    'src/stream.c',
)

includes = include_directories(
    'include',
)

add_project_arguments('-DHAVE_CONFIG_H', language: 'c')

cc = meson.get_compiler('c')

libm = cc.find_library('m', required : false)

opusfile_conf = configuration_data()

# opusfile_conf.set('HAVE_DLFCN_H', cc.check_header('dlfcn.h'))
# opusfile_conf.set('HAVE_INTTYPES_H', cc.check_header('inttypes.h'))
# opusfile_conf.set('HAVE_MEMORY_H', cc.check_header('memory.h'))
# opusfile_conf.set('HAVE_STDINT_H', cc.check_header('stdint.h'))
# opusfile_conf.set('HAVE_STDLIB_H', cc.check_header('stdlib.h'))
# opusfile_conf.set('HAVE_STRINGS_H', cc.check_header('strings.h'))
# opusfile_conf.set('HAVE_STRING_H', cc.check_header('string.h'))
# opusfile_conf.set('HAVE_SYS_STAT_H', cc.check_header('sys/stat.h'))
# opusfile_conf.set('HAVE_SYS_TYPES_H', cc.check_header('sys/types.h'))
# opusfile_conf.set('HAVE_UNISTD_H', cc.check_header('unistd.h'))

opusfile_conf.set('OP_DISABLE_FLOAT_API', not get_option('float-api'))
opusfile_conf.set('OP_ENABLE_ASSERTIONS', false)
opusfile_conf.set('OP_ENABLE_HTTP', false)
opusfile_conf.set('OP_FIXED_POINT', get_option('fixed-point'))
opusfile_conf.set('OP_HAVE_CLOCK_GETTIME', false)
opusfile_conf.set('HAVE_LRINTF', cc.has_function('lrintf', prefix: '#include <math.h>', dependencies: libm))

configure_file(output: 'config.h', configuration: opusfile_conf)

summary(
  {
    'Floating point support': get_option('float-api'),
    'Fixed point support': get_option('fixed-point'),
    'Enable assertions': opusfile_conf.has('OP_ENABLE_ASSERTIONS'),
    'Enable HTTP': opusfile_conf.has('OP_ENABLE_HTTP'),
    'Has clock_gettime': opusfile_conf.has('OP_HAVE_CLOCK_GETTIME'),
    'Has lrintf': opusfile_conf.has('HAVE_LRINTF'),
  },
  section: 'Config',
  bool_yn: true,
  list_sep: ', ',
)

opus_dep = dependency('opus')
ogg_dep = dependency('ogg')

opusfile = library(
    'opusfile',
    sources,
    include_directories: includes,
    dependencies: [opus_dep, ogg_dep, libm]
)

opusfile_dep = declare_dependency(
    include_directories: includes,
    dependencies: [opus_dep, ogg_dep],
    link_with: [opusfile],
)
