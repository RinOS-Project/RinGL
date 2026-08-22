/* SPDX-License-Identifier: MIT */
#ifndef RINGL_REFLECTION_H
#define RINGL_REFLECTION_H

#include <stdint.h>

#include <ringl/ringl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RinGLProgramReflectionV1 {
    uint32_t struct_size;
    uint32_t api_version;
    uint32_t vertex_input_count;
    uint32_t vertex_output_count;
    uint32_t fragment_input_count;
    uint32_t fragment_output_count;
    uint32_t active_uniform_count;
    uint32_t reserved0;
    uint64_t vertex_shader_module;
    uint64_t fragment_shader_module;
} RinGLProgramReflectionV1;

int ringl_get_program_reflection(uint32_t program,
                                 RinGLProgramReflectionV1* reflection);

#ifdef __cplusplus
}
#endif

#endif /* RINGL_REFLECTION_H */
