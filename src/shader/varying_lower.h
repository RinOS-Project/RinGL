/* SPDX-License-Identifier: MIT */
#ifndef RINGL_VARYING_LOWER_H
#define RINGL_VARYING_LOWER_H

#include <stddef.h>
#include <stdint.h>

#include "glsl_lower.h"

int ringl_glsl_lower_varying_rsh1(uint32_t shader_type,
                                  const char* source,
                                  size_t source_length,
                                  RinGLGlslLowerResult* result);

#endif /* RINGL_VARYING_LOWER_H */
