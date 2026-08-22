/* SPDX-License-Identifier: MIT */
#ifndef RINGL_TEXTURE_LOWER_H
#define RINGL_TEXTURE_LOWER_H

#include <stddef.h>
#include <stdint.h>

#include "glsl_lower.h"

int ringl_glsl_lower_texture2d_rsh1(
    const char* source,
    size_t source_length,
    const char* sampler_name,
    uint32_t sampler_count,
    RinGLGlslLowerResult* result);

#endif /* RINGL_TEXTURE_LOWER_H */
