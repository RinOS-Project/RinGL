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
    RinGLRinGpuOpsV1 ringpu_ops;
    int has_ringpu;
    int has_ringpu_ops;

    RinGLObjectSlot objects[RINGL_OBJECT_SLOT_COUNT];
    RinGLBufferObject buffers[RINGL_OBJECT_SLOT_COUNT];
    uint32_t array_buffer;
    uint32_t element_array_buffer;
};

void ringl_context_record_error(RinGLContext* context, uint32_t error);
void ringl_context_mark_dirty(RinGLContext* context, uint32_t bits);
void ringl_context_clear_dirty(RinGLContext* context, uint32_t bits);

int ringl_backend_create_buffer(RinGLContext* context,
                                uint64_t size_bytes,
                                uint64_t* buffer_out);
int ringl_backend_upload_buffer(RinGLContext* context,
                                uint64_t buffer,
                                uint64_t offset,
                                const void* data,
                                uint64_t size_bytes);
void ringl_backend_destroy_object(RinGLContext* context, uint64_t object);
void ringl_buffer_objects_destroy_all(RinGLContext* context);

#endif /* RINGL_INTERNAL_H */
