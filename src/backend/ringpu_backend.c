/* SPDX-License-Identifier: MIT */
#include "ringl_internal.h"

int ringl_backend_create_buffer(RinGLContext* context,
                                uint64_t size_bytes,
                                uint64_t* buffer_out)
{
    if (context == NULL || buffer_out == NULL ||
        !context->has_ringpu_ops ||
        context->ringpu_ops.create_buffer == NULL) {
        return -1;
    }

    *buffer_out = 0u;
    return context->ringpu_ops.create_buffer(context->ringpu.session,
                                             size_bytes,
                                             buffer_out);
}

int ringl_backend_upload_buffer(RinGLContext* context,
                                uint64_t buffer,
                                uint64_t offset,
                                const void* data,
                                uint64_t size_bytes)
{
    if (context == NULL || !context->has_ringpu_ops ||
        context->ringpu_ops.upload_buffer == NULL) {
        return -1;
    }

    return context->ringpu_ops.upload_buffer(context->ringpu.session,
                                             buffer,
                                             offset,
                                             data,
                                             size_bytes);
}

void ringl_backend_destroy_object(RinGLContext* context, uint64_t object)
{
    if (context == NULL || object == 0u || !context->has_ringpu_ops ||
        context->ringpu_ops.destroy_object == NULL) {
        return;
    }

    (void)context->ringpu_ops.destroy_object(context->ringpu.session, object);
}
