cmake_minimum_required(VERSION 3.20)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    project(RinGL LANGUAGES C)
endif()

# This target is intentionally usable both standalone and as a Ladybird
# subdirectory.  The latter is the RinOS WebGL production path; keeping the
# source list here prevents the embedded build from silently omitting a RinGL
# translation unit that Meson builds and tests.
add_library(RinGL STATIC
    src/backend/ringpu_backend.c
    src/context/context.c
    src/context/framebuffer.c
    src/context/state.c
    src/context/sync.c
    src/context/vertex_attrib.c
    src/objects/object_table.c
    src/objects/buffer.c
    src/objects/vertex_array.c
    src/objects/framebuffer.c
    src/objects/texture.c
    src/shader/glsl_lower.c
    src/shader/glsl_parser.c
    src/shader/program.c
    src/shader/reflection.c
    src/shader/shader.c
    src/shader/shader_ir.c
    src/shader/texture_lower.c
    src/shader/varying_lower.c
    src/translate/draw.c
    src/translate/pipeline_cache.c
    src/translate/vertex_layout.c
)

# This is an embedding layer, not a generic RinGL dependency. A host that
# supplies RinGPU and Aquamarine backing storage enables it before adding the
# RinGL subdirectory; the standalone library remains platform-neutral.
option(RINGL_ENABLE_AQUAMARINE_SURFACE
       "Build the caller-owned Aquamarine/RinGPU embedding surface" OFF)
if (RINGL_ENABLE_AQUAMARINE_SURFACE)
    target_sources(RinGL PRIVATE
        src/platform/aquamarine_surface.c
    )
endif()

add_library(RinGL::RinGL ALIAS RinGL)

target_compile_features(RinGL PUBLIC c_std_11)
set_target_properties(RinGL PROPERTIES
    C_EXTENSIONS OFF
)

target_compile_options(RinGL PRIVATE
    $<$<COMPILE_LANG_AND_ID:C,Clang,GNU>:-Wall;-Wextra;-Werror>
)

target_include_directories(RinGL
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# texture_lower.c uses the C finite-value predicates. Keep this dependency
# transitive: LibWeb links a static RinGL archive in the embedded build.
target_link_libraries(RinGL PUBLIC m)
