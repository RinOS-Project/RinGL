/* SPDX-License-Identifier: MIT */
#include "ringl_internal.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static _Thread_local RinGLContext* ringl_current_context;

void ringl_copy_c_string(char* destination, size_t capacity,
                         const char* source)
{
    size_t length = 0u;

    if (destination == NULL || capacity == 0u)
        return;
    if (source != NULL) {
        while (length + 1u < capacity && source[length] != '\0')
            ++length;
        if (length != 0u)
            memmove(destination, source, length);
    }
    destination[length] = '\0';
}

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
            desc->flags != 0u ||
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
    context->cull_face_mode = RINGL_BACK;
    /* OpenGL ES 2.0 enables DITHER at context creation. */
    context->dither_enabled = RINGL_TRUE;
    context->front_face = RINGL_CCW;
    context->depth_func = RINGL_LESS;
    context->depth_write_mask = RINGL_TRUE;
    context->depth_range_near = 0.0f;
    context->depth_range_far = 1.0f;
    context->line_width = 1.0f;
    context->sample_coverage_value = 1.0f;
    context->clear_depth = 1.0f;
    context->stencil_func = RINGL_ALWAYS;
    context->stencil_value_mask = 0xffu;
    context->stencil_write_mask = 0xffu;
    context->stencil_fail_operation = RINGL_KEEP;
    context->stencil_depth_fail_operation = RINGL_KEEP;
    context->stencil_pass_operation = RINGL_KEEP;
    context->back_stencil_func = RINGL_ALWAYS;
    context->back_stencil_value_mask = 0xffu;
    context->back_stencil_write_mask = 0xffu;
    context->back_stencil_fail_operation = RINGL_KEEP;
    context->back_stencil_depth_fail_operation = RINGL_KEEP;
    context->back_stencil_pass_operation = RINGL_KEEP;
    context->blend_source_rgb = RINGL_ONE;
    context->blend_destination_rgb = RINGL_ZERO;
    context->blend_source_alpha = RINGL_ONE;
    context->blend_destination_alpha = RINGL_ZERO;
    context->blend_equation_rgb = RINGL_FUNC_ADD;
    context->blend_equation_alpha = RINGL_FUNC_ADD;
    context->color_write_mask = 0x0fu;
    context->unpack_alignment = 4u;
    context->pack_alignment = 4u;
    for (uint32_t index = 0u; index < RINGL_MAX_VERTEX_ATTRIBS; ++index) {
        context->vertex_attribs[index].size = 4u;
        context->vertex_attribs[index].type = RINGL_FLOAT;
        context->vertex_attribs[index].current_value[3] = 1.0f;
        context->default_vertex_array.vertex_attribs[index].size = 4u;
        context->default_vertex_array.vertex_attribs[index].type = RINGL_FLOAT;
    }

    if (desc != NULL) {
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

    if (context->graphics_command_list != 0u) {
        ringl_backend_destroy_object(context, context->graphics_command_list);
        context->graphics_command_list = 0u;
    }
    if (context->graphics_bind_group != 0u) {
        ringl_backend_destroy_object(context, context->graphics_bind_group);
        context->graphics_bind_group = 0u;
    }
    if (context->finish_fence != 0u) {
        ringl_backend_destroy_object(context, context->finish_fence);
        context->finish_fence = 0u;
    }
    ringl_pipeline_cache_destroy(context);
    ringl_program_objects_destroy_all(context);
    ringl_shader_objects_destroy_all(context);
    ringl_framebuffer_objects_destroy_all(context);
    ringl_texture_objects_destroy_all(context);
    ringl_buffer_objects_destroy_all(context);
    context->magic = 0u;
    memset(&context->sync_ops, 0, sizeof(context->sync_ops));
    memset(&context->ringpu_ops, 0, sizeof(context->ringpu_ops));
    memset(&context->ringpu, 0, sizeof(context->ringpu));
    free(context);
}

int ringl_make_current(RinGLContext* context)
{
    if (context != NULL && !ringl_context_is_valid(context))
        return -1;

    ringl_current_context = context;
    return context != NULL && context->lost ? -1 : 0;
}

RinGLContext* ringl_get_current_context(void)
{
    if (!ringl_context_is_valid(ringl_current_context) ||
        ringl_current_context->lost)
        return NULL;
    return ringl_current_context;
}

uint32_t ringl_get_error(void)
{
    RinGLContext* context = ringl_current_context;
    uint32_t error;

    if (!ringl_context_is_valid(context))
        return RINGL_NO_ERROR;

    if (context->lost) {
        if (context->loss_reported)
            return RINGL_NO_ERROR;
        context->loss_reported = 1u;
        return RINGL_CONTEXT_LOST_WEBGL;
    }

    error = context->pending_error;
    context->pending_error = RINGL_NO_ERROR;
    return error;
}

uint32_t ringl_context_is_lost(const RinGLContext* context)
{
    if (!ringl_context_is_valid(context))
        return RINGL_FALSE;
    return context->lost ? RINGL_TRUE : RINGL_FALSE;
}

uint32_t ringl_context_dirty_bits(const RinGLContext* context)
{
    if (!ringl_context_is_valid(context))
        return 0u;
    return context->dirty_bits;
}

void ringl_context_record_error(RinGLContext* context, uint32_t error)
{
    if (!ringl_context_is_valid(context) || context->lost ||
        error == RINGL_NO_ERROR)
        return;

    if (context->pending_error == RINGL_NO_ERROR)
        context->pending_error = error;
}

void ringl_context_mark_lost(RinGLContext* context)
{
    if (!ringl_context_is_valid(context) || context->lost)
        return;

    context->lost = 1u;
    context->loss_reported = 0u;
    context->pending_error = RINGL_NO_ERROR;
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
