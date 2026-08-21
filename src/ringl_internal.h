/* SPDX-License-Identifier: MIT */
#ifndef RINGL_INTERNAL_H
#define RINGL_INTERNAL_H

#include <ringl/ringl.h>

#include "objects/object_table.h"

#define RINGL_CONTEXT_MAGIC 0x52474c43u /* RGLC */

typedef struct RinGLBufferObject {
    uint64_t ringpu_handle;
    uint64_t size_bytes;
    uint32_t usage;
    uint32_t reserved0;
} RinGLBufferObject;

struct RinGLContext {
    uint32_t magic;
    uint32_t pending_error;
    uint32_t dirty_bits;
    uint32_t flags;
    RinGLRinGpuBindingV1 ringpu;
    int has_ringpu;

    RinGLObjectSlot objects[RINGL_OBJECT_SLOT_COUNT];
    RinGLBufferObject buffers[RINGL_OBJECT_SLOT_COUNT];
    uint32_t array_buffer;
    uint32_t element_array_buffer;
};

void ringl_context_record_error(RinGLContext* context, uint32_t error);
void ringl_context_mark_dirty(RinGLContext* context, uint32_t bits);
void ringl_context_clear_dirty(RinGLContext* context, uint32_t bits);

#endif /* RINGL_INTERNAL_H */
