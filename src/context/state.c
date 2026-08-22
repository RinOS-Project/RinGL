/* SPDX-License-Identifier: MIT */
#include "ringl_internal.h"

static uint32_t* capability_field(RinGLContext* context, uint32_t capability)
{
    if (context == NULL)
        return NULL;
    switch (capability) {
    case RINGL_SCISSOR_TEST:
        return &context->scissor_enabled;
    case RINGL_CULL_FACE:
        return &context->cull_face_enabled;
    case RINGL_DEPTH_TEST:
        return &context->depth_test_enabled;
    case RINGL_BLEND:
        return &context->blend_enabled;
    default:
        return NULL;
    }
}

static void set_capability(uint32_t capability, uint32_t enabled)
{
    RinGLContext* context = ringl_get_current_context();
    uint32_t* field;

    if (context == NULL)
        return;
    field = capability_field(context, capability);
    if (field == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (*field == enabled)
        return;
    *field = enabled;
    if (capability == RINGL_SCISSOR_TEST)
        ringl_context_mark_dirty(context, RINGL_DIRTY_VIEWPORT);
    else
        ringl_context_mark_dirty(context, RINGL_DIRTY_PIPELINE);
}

void ringl_enable(uint32_t capability)
{
    set_capability(capability, RINGL_TRUE);
}

void ringl_disable(uint32_t capability)
{
    set_capability(capability, RINGL_FALSE);
}

int ringl_is_enabled(uint32_t capability)
{
    RinGLContext* context = ringl_get_current_context();
    uint32_t* field;

    if (context == NULL)
        return 0;
    field = capability_field(context, capability);
    if (field == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return 0;
    }
    return *field != 0u;
}

void ringl_viewport(int32_t x, int32_t y, int32_t width, int32_t height)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL)
        return;
    if (width < 0 || height < 0) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    context->viewport_x = x;
    context->viewport_y = y;
    context->viewport_width = (uint32_t)width;
    context->viewport_height = (uint32_t)height;
    context->viewport_initialized = RINGL_TRUE;
    ringl_context_mark_dirty(context, RINGL_DIRTY_VIEWPORT);
}

void ringl_scissor(int32_t x, int32_t y, int32_t width, int32_t height)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL)
        return;
    if (width < 0 || height < 0) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    context->scissor_x = x;
    context->scissor_y = y;
    context->scissor_width = (uint32_t)width;
    context->scissor_height = (uint32_t)height;
    ringl_context_mark_dirty(context, RINGL_DIRTY_VIEWPORT);
}

void ringl_cull_face(uint32_t mode)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL)
        return;
    if (mode != RINGL_FRONT && mode != RINGL_BACK &&
        mode != RINGL_FRONT_AND_BACK) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (context->cull_face_mode == mode)
        return;
    context->cull_face_mode = mode;
    ringl_context_mark_dirty(context, RINGL_DIRTY_PIPELINE);
}

void ringl_front_face(uint32_t mode)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL)
        return;
    if (mode != RINGL_CW && mode != RINGL_CCW) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (context->front_face == mode)
        return;
    context->front_face = mode;
    ringl_context_mark_dirty(context, RINGL_DIRTY_PIPELINE);
}

void ringl_get_integerv(uint32_t pname, int32_t* values)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL || values == NULL)
        return;

    switch (pname) {
    case RINGL_ARRAY_BUFFER_BINDING:
        values[0] = (int32_t)context->array_buffer;
        return;
    case RINGL_ELEMENT_ARRAY_BUFFER_BINDING:
        values[0] = (int32_t)context->element_array_buffer;
        return;
    case RINGL_ACTIVE_TEXTURE:
        values[0] = (int32_t)(RINGL_TEXTURE0 + context->active_texture_unit);
        return;
    case RINGL_TEXTURE_BINDING_2D:
        values[0] = (int32_t)context->bound_texture_2d[context->active_texture_unit];
        return;
    case RINGL_CURRENT_PROGRAM:
        values[0] = (int32_t)context->current_program;
        return;
    case RINGL_MAX_TEXTURE_SIZE_QUERY:
        values[0] = (int32_t)RINGL_MAX_TEXTURE_SIZE;
        return;
    case RINGL_MAX_TEXTURE_IMAGE_UNITS:
    case RINGL_MAX_COMBINED_TEXTURE_IMAGE_UNITS:
        values[0] = (int32_t)RINGL_MAX_TEXTURE_UNITS;
        return;
    case RINGL_MAX_VERTEX_ATTRIBS_QUERY:
        values[0] = (int32_t)RINGL_MAX_VERTEX_ATTRIBS;
        return;
    case RINGL_CULL_FACE_MODE:
        values[0] = (int32_t)context->cull_face_mode;
        return;
    case RINGL_FRONT_FACE:
        values[0] = (int32_t)context->front_face;
        return;
    case RINGL_VIEWPORT:
        values[0] = context->viewport_x;
        values[1] = context->viewport_y;
        values[2] = (int32_t)context->viewport_width;
        values[3] = (int32_t)context->viewport_height;
        return;
    case RINGL_SCISSOR_BOX:
        values[0] = context->scissor_x;
        values[1] = context->scissor_y;
        values[2] = (int32_t)context->scissor_width;
        values[3] = (int32_t)context->scissor_height;
        return;
    default:
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
}
