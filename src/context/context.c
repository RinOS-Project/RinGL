/* SPDX-License-Identifier: MIT */
#include "ringl_internal.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static _Thread_local RinGLContext* ringl_current_context;

static int ringl_context_is_valid(const RinGLContext* context)
{
    return context != NULL && context->magic == RINGL_CONTEXT_MAGIC;
}

static int ringl_validate_ringpu_ops(const RinGLRinGpuOpsV1* ops)
{
    const size_t minimum_size = offsetof(RinGLRinGpuOpsV1,
                                         create_shader_module);

    if (ops == NULL)
        return 1;
    if (ops->struct_size < minimum_size ||
        ops->api_version != RINGL_API_VERSION ||
        ops->create_buffer == NULL ||
        ops->upload_buffer == NULL ||
        ops->destroy_object == NULL) {
        return 0;
    }
    return 1;
}

static int ringl_validate_ringpu_binding(const RinGLRinGpuBindingV1* binding)
{
    if (binding == NULL)
        return 1;

    if (binding->struct_size < sizeof(*binding))
        return 0;
    if (binding->api_version != RINGL_API_VERSION)
        return 0;
    if (binding->reserved0 != 0u)
        return 0;
    if (!ringl_validate_ringpu_ops(binding->ops))
        return 0;

    return 1;
}

int ringl_context_create(const RinGLContextDescV1* desc,
                         RinGLContext** context_out)
{
    RinGLContext* context;

    if (context_out == NULL)
        return -1;
    *context_out = NULL;

    if (desc != NULL) {
        if (desc->struct_size < sizeof(*desc) ||
            desc->api_version != RINGL_API_VERSION ||
            desc->reserved0 != 0u ||
            !ringl_validate_ringpu_binding(desc->ringpu)) {
            return -1;
        }
    }

    context = calloc(1, sizeof(*context));
    if (context == NULL)
        return -2;

    context->magic = RINGL_CONTEXT_MAGIC;
    context->pending_error = RINGL_NO_ERROR;
    context->dirty_bits = RINGL_DIRTY_ALL;

    if (desc != NULL) {
        context->flags = desc->flags;
        if (desc->ringpu != NULL) {
            memcpy(&context->ringpu, desc->ringpu, sizeof(context->ringpu));
            context->has_ringpu = 1;
            if (desc->ringpu->ops != NULL) {
                size_t copy_size = desc->ringpu->ops->struct_size;
                if (copy_size > sizeof(context->ringpu_ops))
                    copy_size = sizeof(context->ringpu_ops);
                memset(&context->ringpu_ops, 0, sizeof(context->ringpu_ops));
                memcpy(&context->ringpu_ops, desc->ringpu->ops, copy_size);
                context->ringpu.ops = &context->ringpu_ops;
                context->has_ringpu_ops = 1;
            }
        }
    }

    *context_out = context;
    return 0;
}

void ringl_context_destroy(RinGLContext* context)
{
    if (!ringl_context_is_valid(context))
        return;

    if (ringl_current_context == context)
        ringl_current_context = NULL;

    ringl_shader_objects_destroy_all(context);
    ringl_buffer_objects_destroy_all(context);
    context->magic = 0u;
    memset(&context->ringpu_ops, 0, sizeof(context->ringpu_ops));
    memset(&context->ringpu, 0, sizeof(context->ringpu));
    free(context);
}

int ringl_make_current(RinGLContext* context)
{
    if (context != NULL && !ringl_context_is_valid(context))
        return -1;

    ringl_current_context = context;
    return 0;
}

RinGLContext* ringl_get_current_context(void)
{
    return ringl_current_context;
}

uint32_t ringl_get_error(void)
{
    RinGLContext* context = ringl_current_context;
    uint32_t error;

    if (!ringl_context_is_valid(context))
        return RINGL_NO_ERROR;

    error = context->pending_error;
    context->pending_error = RINGL_NO_ERROR;
    return error;
}

uint32_t ringl_context_dirty_bits(const RinGLContext* context)
{
    if (!ringl_context_is_valid(context))
        return 0u;
    return context->dirty_bits;
}

void ringl_context_record_error(RinGLContext* context, uint32_t error)
{
    if (!ringl_context_is_valid(context) || error == RINGL_NO_ERROR)
        return;

    if (context->pending_error == RINGL_NO_ERROR)
        context->pending_error = error;
}

void ringl_context_mark_dirty(RinGLContext* context, uint32_t bits)
{
    if (!ringl_context_is_valid(context))
        return;
    context->dirty_bits |= bits & RINGL_DIRTY_ALL;
}

void ringl_context_clear_dirty(RinGLContext* context, uint32_t bits)
{
    if (!ringl_context_is_valid(context))
        return;
    context->dirty_bits &= ~(bits & RINGL_DIRTY_ALL);
}
