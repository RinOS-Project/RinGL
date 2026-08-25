/* SPDX-License-Identifier: MIT */
#ifndef RINGL_GLSL_LOWER_H
#define RINGL_GLSL_LOWER_H

#include <stddef.h>
#include <stdint.h>

#include "glsl_parser.h"

#define RINGL_GLSL_RSH1_MAX_BYTES (64u + 128u * 16u)
#define RINGL_GLSL_MAX_UNIFORMS \
    (RINGL_GLSL_MAX_FLOAT_UNIFORMS + RINGL_GLSL_MAX_INT_UNIFORMS + RINGL_GLSL_MAX_VEC2_UNIFORMS + \
     RINGL_GLSL_MAX_VEC3_UNIFORMS + RINGL_GLSL_MAX_VEC4_UNIFORMS + \
     RINGL_GLSL_MAX_IVEC2_UNIFORMS + RINGL_GLSL_MAX_IVEC3_UNIFORMS + \
     RINGL_GLSL_MAX_IVEC4_UNIFORMS + RINGL_GLSL_MAX_MAT4_UNIFORMS)

typedef struct RinGLGlslLowerResult {
    uint32_t ok;
    uint32_t instruction_count;
    uint32_t register_count;
    uint32_t input_count;
    uint32_t output_count;
    uint32_t byte_size;
    uint32_t sampler_binding_count;
    uint32_t sampler_binding_indices[RINGL_GLSL_MAX_SAMPLER_UNIFORMS];
    char diagnostic[160];
    uint8_t bytes[RINGL_GLSL_RSH1_MAX_BYTES];
} RinGLGlslLowerResult;

/* Program values passed to the bounded uniform lowering path. A missing name
 * uses WebGL's successful-link zero default for its declared type. */
typedef struct RinGLGlslUniformValue {
    const char* name;
    uint32_t type;
    union {
        float values[16];
        int32_t i32_values[16];
    };
} RinGLGlslUniformValue;

int ringl_glsl_lower_rsh1(uint32_t shader_type,
                          const char* source,
                          size_t source_length,
                          RinGLGlslLowerResult* result);
int ringl_glsl_lower_rsh1_with_uniforms(
    uint32_t shader_type, const char* source, size_t source_length,
    const RinGLGlslUniformValue* uniforms, uint32_t uniform_count,
    RinGLGlslLowerResult* result);

#endif
