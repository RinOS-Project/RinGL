project(
  'ringl',
  'c',
  version: '0.1.0',
  license: 'MIT',
  default_options: [
    'c_std=c11',
    'warning_level=3',
    'werror=true',
  ],
)

ringl_inc = include_directories('include', 'src')

ringl_sources = files(
  'src/backend/ringpu_backend.c',
  'src/context/context.c',
  'src/context/framebuffer.c',
  'src/context/state.c',
  'src/context/vertex_attrib.c',
  'src/objects/object_table.c',
  'src/objects/buffer.c',
  'src/objects/texture.c',
  'src/shader/glsl_lower.c',
  'src/shader/glsl_parser.c',
  'src/shader/program.c',
  'src/shader/reflection.c',
  'src/shader/shader.c',
  'src/shader/shader_ir.c',
  'src/translate/draw.c',
  'src/translate/pipeline_cache.c',
  'src/translate/vertex_layout.c',
)

ringl_lib = static_library(
  'ringl',
  ringl_sources,
  include_directories: ringl_inc,
  install: false,
)

ringl_dep = declare_dependency(
  link_with: ringl_lib,
  include_directories: ringl_inc,
)

subdir('tests')
