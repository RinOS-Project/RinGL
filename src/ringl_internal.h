/* SPDX-License-Identifier: MIT */
#ifndef RINGL_INTERNAL_H
#define RINGL_INTERNAL_H

#include <ringl/ringl.h>

#define RINGL_CONTEXT_MAGIC 0x52474c43u /* RGLC */

struct RinGLContext {
    uint32_t magic;
    uint32_t pending_error;
    uint32_t dirty_bits;
    uint32_t flags;
    RinGLRinGpuBindingV1 ringpu;
    int has_ringpu;
};

void ringl_context_record_error(RinGLContext* context, uint32_t error);
void ringl_context_mark_dirty(RinGLContext* context, uint32_t bits);
void ringl_context_clear_dirty(RinGLContext* context, uint32_t bits);

#endif /* RINGL_INTERNAL_H */
