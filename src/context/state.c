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
    case RINGL_STENCIL_TEST:
        return &context->stencil_test_enabled;
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

static int stencil_operation_valid(uint32_t operation)
{
    return operation == RINGL_KEEP || operation == RINGL_ZERO ||
           operation == RINGL_REPLACE || operation == RINGL_INCR ||
           operation == RINGL_DECR || operation == RINGL_INVERT ||
           operation == RINGL_INCR_WRAP || operation == RINGL_DECR_WRAP;
}

static int stencil_face_valid(uint32_t face)
{
    return face == RINGL_FRONT || face == RINGL_BACK ||
           face == RINGL_FRONT_AND_BACK;
}

static void stencil_face_fields(RinGLContext* context, uint32_t face,
                                uint32_t** function_out,
                                uint32_t** reference_out,
                                uint32_t** value_mask_out,
                                uint32_t** write_mask_out,
                                uint32_t** fail_out,
                                uint32_t** depth_fail_out,
                                uint32_t** pass_out)
{
    if (face == RINGL_FRONT) {
        *function_out = &context->stencil_func;
        *reference_out = &context->stencil_reference;
        *value_mask_out = &context->stencil_value_mask;
        *write_mask_out = &context->stencil_write_mask;
        *fail_out = &context->stencil_fail_operation;
        *depth_fail_out = &context->stencil_depth_fail_operation;
        *pass_out = &context->stencil_pass_operation;
        return;
    }
    *function_out = &context->back_stencil_func;
    *reference_out = &context->back_stencil_reference;
    *value_mask_out = &context->back_stencil_value_mask;
    *write_mask_out = &context->back_stencil_write_mask;
    *fail_out = &context->back_stencil_fail_operation;
    *depth_fail_out = &context->back_stencil_depth_fail_operation;
    *pass_out = &context->back_stencil_pass_operation;
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

void ringl_stencil_func(uint32_t func, int32_t reference, uint32_t mask)
{
    ringl_stencil_func_separate(RINGL_FRONT_AND_BACK, func, reference, mask);
}

void ringl_stencil_func_separate(uint32_t face, uint32_t func,
                                 int32_t reference, uint32_t mask)
{
    RinGLContext* context = ringl_get_current_context();
    uint32_t clamped_reference;
    uint32_t faces[2];
    uint32_t count;
    uint32_t changed = RINGL_FALSE;

    if (context == NULL)
        return;
    if (!stencil_face_valid(face) || !depth_func_valid(func)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    clamped_reference = (uint32_t)reference & 0xffu;
    mask &= 0xffu;
    faces[0] = face == RINGL_BACK ? RINGL_BACK : RINGL_FRONT;
    count = face == RINGL_FRONT_AND_BACK ? 2u : 1u;
    if (count == 2u) faces[1] = RINGL_BACK;
    for (uint32_t index = 0u; index < count; ++index) {
        uint32_t* current_func;
        uint32_t* current_reference;
        uint32_t* current_mask;
        uint32_t* ignored_write_mask;
        uint32_t* ignored_fail;
        uint32_t* ignored_depth_fail;
        uint32_t* ignored_pass;

        stencil_face_fields(context, faces[index], &current_func,
                            &current_reference, &current_mask,
                            &ignored_write_mask, &ignored_fail,
                            &ignored_depth_fail, &ignored_pass);
        if (*current_func != func || *current_reference != clamped_reference ||
            *current_mask != mask) {
            *current_func = func;
            *current_reference = clamped_reference;
            *current_mask = mask;
            changed = RINGL_TRUE;
        }
    }
    if (changed)
        ringl_context_mark_dirty(context, RINGL_DIRTY_PIPELINE);
}

void ringl_stencil_mask(uint32_t mask)
{
    ringl_stencil_mask_separate(RINGL_FRONT_AND_BACK, mask);
}

void ringl_stencil_mask_separate(uint32_t face, uint32_t mask)
{
    RinGLContext* context = ringl_get_current_context();
    uint32_t faces[2];
    uint32_t count;
    uint32_t changed = RINGL_FALSE;

    if (context == NULL)
        return;
    if (!stencil_face_valid(face)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    mask &= 0xffu;
    faces[0] = face == RINGL_BACK ? RINGL_BACK : RINGL_FRONT;
    count = face == RINGL_FRONT_AND_BACK ? 2u : 1u;
    if (count == 2u) faces[1] = RINGL_BACK;
    for (uint32_t index = 0u; index < count; ++index) {
        uint32_t* ignored_func;
        uint32_t* ignored_reference;
        uint32_t* ignored_value_mask;
        uint32_t* current_mask;
        uint32_t* ignored_fail;
        uint32_t* ignored_depth_fail;
        uint32_t* ignored_pass;

        stencil_face_fields(context, faces[index], &ignored_func,
                            &ignored_reference, &ignored_value_mask,
                            &current_mask, &ignored_fail,
                            &ignored_depth_fail, &ignored_pass);
        if (*current_mask != mask) {
            *current_mask = mask;
            changed = RINGL_TRUE;
        }
    }
    if (changed)
        ringl_context_mark_dirty(context, RINGL_DIRTY_PIPELINE);
}

void ringl_stencil_op(uint32_t stencil_fail, uint32_t depth_fail,
                      uint32_t depth_pass)
{
    ringl_stencil_op_separate(RINGL_FRONT_AND_BACK, stencil_fail, depth_fail,
                              depth_pass);
}

void ringl_stencil_op_separate(uint32_t face, uint32_t stencil_fail,
                               uint32_t depth_fail, uint32_t depth_pass)
{
    RinGLContext* context = ringl_get_current_context();
    uint32_t faces[2];
    uint32_t count;
    uint32_t changed = RINGL_FALSE;

    if (context == NULL)
        return;
    if (!stencil_face_valid(face) ||
        !stencil_operation_valid(stencil_fail) ||
        !stencil_operation_valid(depth_fail) ||
        !stencil_operation_valid(depth_pass)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    faces[0] = face == RINGL_BACK ? RINGL_BACK : RINGL_FRONT;
    count = face == RINGL_FRONT_AND_BACK ? 2u : 1u;
    if (count == 2u) faces[1] = RINGL_BACK;
    for (uint32_t index = 0u; index < count; ++index) {
        uint32_t* ignored_func;
        uint32_t* ignored_reference;
        uint32_t* ignored_value_mask;
        uint32_t* ignored_write_mask;
        uint32_t* current_fail;
        uint32_t* current_depth_fail;
        uint32_t* current_pass;

        stencil_face_fields(context, faces[index], &ignored_func,
                            &ignored_reference, &ignored_value_mask,
                            &ignored_write_mask, &current_fail,
                            &current_depth_fail, &current_pass);
        if (*current_fail != stencil_fail ||
            *current_depth_fail != depth_fail || *current_pass != depth_pass) {
            *current_fail = stencil_fail;
            *current_depth_fail = depth_fail;
            *current_pass = depth_pass;
            changed = RINGL_TRUE;
        }
    }
    if (changed)
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
    case RINGL_FRAMEBUFFER_BINDING:
        values[0] = (int32_t)context->framebuffer_binding;
        return;
    case RINGL_RENDERBUFFER_BINDING:
        values[0] = (int32_t)context->renderbuffer_binding;
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
    case RINGL_STENCIL_FUNC:
        values[0] = (int32_t)context->stencil_func;
        return;
    case RINGL_STENCIL_REF:
        values[0] = (int32_t)context->stencil_reference;
        return;
    case RINGL_STENCIL_VALUE_MASK:
        values[0] = (int32_t)context->stencil_value_mask;
        return;
    case RINGL_STENCIL_FAIL:
        values[0] = (int32_t)context->stencil_fail_operation;
        return;
    case RINGL_STENCIL_PASS_DEPTH_FAIL:
        values[0] = (int32_t)context->stencil_depth_fail_operation;
        return;
    case RINGL_STENCIL_PASS_DEPTH_PASS:
        values[0] = (int32_t)context->stencil_pass_operation;
        return;
    case RINGL_STENCIL_WRITEMASK:
        values[0] = (int32_t)context->stencil_write_mask;
        return;
    case RINGL_STENCIL_BACK_FUNC:
        values[0] = (int32_t)context->back_stencil_func;
        return;
    case RINGL_STENCIL_BACK_FAIL:
        values[0] = (int32_t)context->back_stencil_fail_operation;
        return;
    case RINGL_STENCIL_BACK_PASS_DEPTH_FAIL:
        values[0] = (int32_t)context->back_stencil_depth_fail_operation;
        return;
    case RINGL_STENCIL_BACK_PASS_DEPTH_PASS:
        values[0] = (int32_t)context->back_stencil_pass_operation;
        return;
    case RINGL_STENCIL_BACK_REF:
        values[0] = (int32_t)context->back_stencil_reference;
        return;
    case RINGL_STENCIL_BACK_VALUE_MASK:
        values[0] = (int32_t)context->back_stencil_value_mask;
        return;
    case RINGL_STENCIL_BACK_WRITEMASK:
        values[0] = (int32_t)context->back_stencil_write_mask;
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
