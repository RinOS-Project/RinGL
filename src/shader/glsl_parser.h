/* SPDX-License-Identifier: MIT */
#ifndef RINGL_GLSL_PARSER_H
#define RINGL_GLSL_PARSER_H

#include <stddef.h>
#include <stdint.h>

#define RINGL_GLSL_DIAGNOSTIC_MAX 160u
#define RINGL_GLSL_MAX_SYMBOLS 64u
#define RINGL_GLSL_MAX_ATTRIBUTES 16u
#define RINGL_GLSL_MAX_SAMPLER_UNIFORMS 8u
#define RINGL_GLSL_MAX_FLOAT_UNIFORMS 8u
#define RINGL_GLSL_MAX_INT_UNIFORMS 8u
#define RINGL_GLSL_MAX_VEC2_UNIFORMS 8u
#define RINGL_GLSL_MAX_VEC3_UNIFORMS 8u
#define RINGL_GLSL_MAX_VEC4_UNIFORMS 8u
#define RINGL_GLSL_MAX_IVEC2_UNIFORMS 8u
#define RINGL_GLSL_MAX_IVEC3_UNIFORMS 8u
#define RINGL_GLSL_MAX_IVEC4_UNIFORMS 8u
#define RINGL_GLSL_MAX_MAT4_UNIFORMS 4u
#define RINGL_GLSL_MAX_VARYINGS 8u
#define RINGL_GLSL_NAME_MAX 64u

typedef struct RinGLGlslParseResult {
    uint32_t ok;
    /* OES_standard_derivatives is an opt-in WebGL capability.  Keep its
     * source-level declaration separate from actual builtin use so the
     * context can reject a shader compiled before getExtension(). */
    uint32_t standard_derivatives_enabled;
    uint32_t uses_standard_derivatives;
    uint32_t declaration_count;
    uint32_t statement_count;
    uint32_t attribute_count;
    uint32_t sampler_uniform_count;
    uint32_t float_uniform_count;
    uint32_t int_uniform_count;
    uint32_t vec2_uniform_count;
    uint32_t vec3_uniform_count;
    uint32_t vec4_uniform_count;
    uint32_t ivec2_uniform_count;
    uint32_t ivec3_uniform_count;
    uint32_t ivec4_uniform_count;
    uint32_t mat4_uniform_count;
    uint32_t varying_count;
    char attribute_names[RINGL_GLSL_MAX_ATTRIBUTES][RINGL_GLSL_NAME_MAX];
    uint32_t attribute_widths[RINGL_GLSL_MAX_ATTRIBUTES];
    char sampler_uniform_names[RINGL_GLSL_MAX_SAMPLER_UNIFORMS][RINGL_GLSL_NAME_MAX];
    char float_uniform_names[RINGL_GLSL_MAX_FLOAT_UNIFORMS][RINGL_GLSL_NAME_MAX];
    char int_uniform_names[RINGL_GLSL_MAX_INT_UNIFORMS][RINGL_GLSL_NAME_MAX];
    char vec2_uniform_names[RINGL_GLSL_MAX_VEC2_UNIFORMS][RINGL_GLSL_NAME_MAX];
    char vec3_uniform_names[RINGL_GLSL_MAX_VEC3_UNIFORMS][RINGL_GLSL_NAME_MAX];
    char vec4_uniform_names[RINGL_GLSL_MAX_VEC4_UNIFORMS][RINGL_GLSL_NAME_MAX];
    char ivec2_uniform_names[RINGL_GLSL_MAX_IVEC2_UNIFORMS][RINGL_GLSL_NAME_MAX];
    char ivec3_uniform_names[RINGL_GLSL_MAX_IVEC3_UNIFORMS][RINGL_GLSL_NAME_MAX];
    char ivec4_uniform_names[RINGL_GLSL_MAX_IVEC4_UNIFORMS][RINGL_GLSL_NAME_MAX];
    char mat4_uniform_names[RINGL_GLSL_MAX_MAT4_UNIFORMS][RINGL_GLSL_NAME_MAX];
    char varying_names[RINGL_GLSL_MAX_VARYINGS][RINGL_GLSL_NAME_MAX];
    uint32_t varying_widths[RINGL_GLSL_MAX_VARYINGS];
    char diagnostic[RINGL_GLSL_DIAGNOSTIC_MAX];
} RinGLGlslParseResult;

int ringl_glsl_parse(uint32_t shader_type,
                     const char* source,
                     size_t source_length,
                     RinGLGlslParseResult* result);

#endif /* RINGL_GLSL_PARSER_H */
