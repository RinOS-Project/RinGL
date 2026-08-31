/* SPDX-License-Identifier: MIT */
#ifndef RINGL_VARYING_LOWER_H
#define RINGL_VARYING_LOWER_H

#include <stddef.h>
#include <stdint.h>

#include "glsl_lower.h"

typedef struct RinGLContext RinGLContext;

int ringl_glsl_lower_varying_rsh1(RinGLContext* context,
                                  uint32_t shader_type,
                                  const char* source,
                                  size_t source_length,
                                  RinGLGlslLowerResult* result);
int ringl_glsl_lower_varying_rsh1_with_uniforms(
    RinGLContext* context, uint32_t shader_type, const char* source,
    size_t source_length,
    const RinGLGlslUniformValue* uniforms, uint32_t uniform_count,
    RinGLGlslLowerResult* result);

#endif /* RINGL_VARYING_LOWER_H */
