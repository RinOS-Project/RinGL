/* SPDX-License-Identifier: MIT */
#ifndef RINGL_GLSL_PARSER_H
#define RINGL_GLSL_PARSER_H

#include <stddef.h>
#include <stdint.h>

#define RINGL_GLSL_DIAGNOSTIC_MAX 160u
#define RINGL_GLSL_MAX_SYMBOLS 64u

typedef struct RinGLGlslParseResult {
    uint32_t ok;
    uint32_t declaration_count;
    uint32_t statement_count;
    uint32_t attribute_count;
    char diagnostic[RINGL_GLSL_DIAGNOSTIC_MAX];
} RinGLGlslParseResult;

int ringl_glsl_parse(uint32_t shader_type,
                     const char* source,
                     size_t source_length,
                     RinGLGlslParseResult* result);

#endif /* RINGL_GLSL_PARSER_H */
