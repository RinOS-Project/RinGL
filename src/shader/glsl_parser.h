/* SPDX-License-Identifier: MIT */
#ifndef RINGL_GLSL_PARSER_H
#define RINGL_GLSL_PARSER_H

#include <stddef.h>
#include <stdint.h>

#define RINGL_GLSL_DIAGNOSTIC_MAX 160u
#define RINGL_GLSL_MAX_SYMBOLS 64u
#define RINGL_GLSL_MAX_ATTRIBUTES 16u
#define RINGL_GLSL_MAX_SAMPLER_UNIFORMS 8u
#define RINGL_GLSL_MAX_VARYINGS 8u
#define RINGL_GLSL_NAME_MAX 64u

typedef struct RinGLGlslParseResult {
    uint32_t ok;
    uint32_t declaration_count;
    uint32_t statement_count;
    uint32_t attribute_count;
    uint32_t sampler_uniform_count;
    uint32_t varying_count;
    char attribute_names[RINGL_GLSL_MAX_ATTRIBUTES][RINGL_GLSL_NAME_MAX];
    uint32_t attribute_widths[RINGL_GLSL_MAX_ATTRIBUTES];
    char sampler_uniform_names[RINGL_GLSL_MAX_SAMPLER_UNIFORMS][RINGL_GLSL_NAME_MAX];
    char varying_names[RINGL_GLSL_MAX_VARYINGS][RINGL_GLSL_NAME_MAX];
    uint32_t varying_widths[RINGL_GLSL_MAX_VARYINGS];
    char diagnostic[RINGL_GLSL_DIAGNOSTIC_MAX];
} RinGLGlslParseResult;

int ringl_glsl_parse(uint32_t shader_type,
                     const char* source,
                     size_t source_length,
                     RinGLGlslParseResult* result);

#endif /* RINGL_GLSL_PARSER_H */
