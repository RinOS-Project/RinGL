/* SPDX-License-Identifier: MIT */
#ifndef RINGL_GLSL_LOWER_H
#define RINGL_GLSL_LOWER_H

#include <stddef.h>
#include <stdint.h>

#include "glsl_parser.h"

#define RINGL_GLSL_RSH1_MAX_BYTES (64u + 128u * 16u)

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

int ringl_glsl_lower_rsh1(uint32_t shader_type,
                          const char* source,
                          size_t source_length,
                          RinGLGlslLowerResult* result);

#endif
