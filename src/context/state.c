/* SPDX-License-Identifier: MIT */
#include "ringl_internal.h"

#include <stddef.h>

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

static int depth_func_valid(uint32_t func)
{
    return func >= RINGL_NEVER && func <= RINGL_ALWAYS;
}

static int blend_factor_valid(uint32_t factor)
{
    return factor == RINGL_ZERO || factor == RINGL_ONE ||
           factor == RINGL_SRC_ALPHA || factor == RINGL_ONE_MINUS_SRC_ALPHA ||
           factor == RINGL_DST_ALPHA || factor == RINGL_ONE_MINUS_DST_ALPHA;
}

static int blend_equation_valid(uint32_t mode)
{
    return mode == RINGL_FUNC_ADD || mode == RINGL_FUNC_SUBTRACT ||
           mode == RINGL_FUNC_REVERSE_SUBTRACT || mode == RINGL_MIN ||
           mode == RINGL_MAX;
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

void ringl_depth_func(uint32_t func)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL)
        return;
    if (!depth_func_valid(func)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (context->depth_func == func)
        return;
    context->depth_func = func;
    ringl_context_mark_dirty(context, RINGL_DIRTY_PIPELINE);
}

void ringl_depth_mask(uint32_t enabled)
{
    RinGLContext* context = ringl_get_current_context();
    uint32_t value;

    if (context == NULL)
        return;
    value = enabled != 0u ? RINGL_TRUE : RINGL_FALSE;
    if (context->depth_write_mask == value)
        return;
    context->depth_write_mask = value;
    ringl_context_mark_dirty(context, RINGL_DIRTY_PIPELINE);
}

void ringl_blend_func_separate(uint32_t source_rgb, uint32_t destination_rgb,
                               uint32_t source_alpha,
                               uint32_t destination_alpha)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL)
        return;
    if (!blend_factor_valid(source_rgb) ||
        !blend_factor_valid(destination_rgb) ||
        !blend_factor_valid(source_alpha) ||
        !blend_factor_valid(destination_alpha)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (context->blend_source_rgb == source_rgb &&
        context->blend_destination_rgb == destination_rgb &&
        context->blend_source_alpha == source_alpha &&
        context->blend_destination_alpha == destination_alpha) {
        return;
    }
    context->blend_source_rgb = source_rgb;
    context->blend_destination_rgb = destination_rgb;
    context->blend_source_alpha = source_alpha;
    context->blend_destination_alpha = destination_alpha;
    ringl_context_mark_dirty(context, RINGL_DIRTY_PIPELINE);
}

void ringl_blend_func(uint32_t source_factor, uint32_t destination_factor)
{
    ringl_blend_func_separate(source_factor, destination_factor,
                              source_factor, destination_factor);
}

void ringl_blend_equation_separate(uint32_t mode_rgb, uint32_t mode_alpha)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL)
        return;
    if (!blend_equation_valid(mode_rgb) || !blend_equation_valid(mode_alpha)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (context->blend_equation_rgb == mode_rgb &&
        context->blend_equation_alpha == mode_alpha) {
        return;
    }
    context->blend_equation_rgb = mode_rgb;
    context->blend_equation_alpha = mode_alpha;
    ringl_context_mark_dirty(context, RINGL_DIRTY_PIPELINE);
}

void ringl_blend_equation(uint32_t mode)
{
    ringl_blend_equation_separate(mode, mode);
}

void ringl_color_mask(uint32_t red, uint32_t green, uint32_t blue,
                      uint32_t alpha)
{
    RinGLContext* context = ringl_get_current_context();
    uint32_t mask;

    if (context == NULL)
        return;
    mask = (red != 0u ? 0x01u : 0u) |
           (green != 0u ? 0x02u : 0u) |
           (blue != 0u ? 0x04u : 0u) |
           (alpha != 0u ? 0x08u : 0u);
    if (context->color_write_mask == mask)
        return;
    context->color_write_mask = mask;
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
    case RINGL_DEPTH_FUNC:
        values[0] = (int32_t)context->depth_func;
        return;
    case RINGL_DEPTH_WRITEMASK:
        values[0] = (int32_t)context->depth_write_mask;
        return;
    case RINGL_BLEND_SRC_RGB:
        values[0] = (int32_t)context->blend_source_rgb;
        return;
    case RINGL_BLEND_DST_RGB:
        values[0] = (int32_t)context->blend_destination_rgb;
        return;
    case RINGL_BLEND_SRC_ALPHA:
        values[0] = (int32_t)context->blend_source_alpha;
        return;
    case RINGL_BLEND_DST_ALPHA:
        values[0] = (int32_t)context->blend_destination_alpha;
        return;
    case RINGL_BLEND_EQUATION_RGB:
        values[0] = (int32_t)context->blend_equation_rgb;
        return;
    case RINGL_BLEND_EQUATION_ALPHA:
        values[0] = (int32_t)context->blend_equation_alpha;
        return;
    case RINGL_COLOR_WRITEMASK:
        values[0] = (context->color_write_mask & 0x01u) != 0u;
        values[1] = (context->color_write_mask & 0x02u) != 0u;
        values[2] = (context->color_write_mask & 0x04u) != 0u;
        values[3] = (context->color_write_mask & 0x08u) != 0u;
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
