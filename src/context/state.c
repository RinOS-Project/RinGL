/* SPDX-License-Identifier: MIT */
#include "ringl_internal.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

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
    case RINGL_POLYGON_OFFSET_FILL:
        return &context->polygon_offset_fill_enabled;
    case RINGL_SAMPLE_COVERAGE:
        return &context->sample_coverage_enabled;
    case RINGL_STENCIL_TEST:
        return &context->stencil_test_enabled;
    case RINGL_BLEND:
        return &context->blend_enabled;
    default:
        return NULL;
    }
}

const char* ringl_get_string(uint32_t pname)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL)
        return NULL;
    switch (pname) {
    case RINGL_VENDOR:
        return "RinOS Project";
    case RINGL_RENDERER:
        /* The public core does not claim a specific native driver: a RinGL
         * embedding may select a different RinGPU implementation. */
        return "RinGL";
    case RINGL_VERSION:
        return "RinGL v1 bounded profile";
    case RINGL_SHADING_LANGUAGE_VERSION:
        return "RinGL RSH1 (GLSL ES 1.00 subset)";
    default:
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return NULL;
    }
}

int ringl_get_compressed_texture_format_count(size_t* count)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL)
        return -1;
    if (count == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }

    /* Compressed image upload/storage is deliberately absent from the RSH1
     * profile. Keep the authoritative empty list in RinGL so embeddings do
     * not invent a browser-local texture capability. */
    *count = 0u;
    return 0;
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
           factor == RINGL_SRC_COLOR || factor == RINGL_ONE_MINUS_SRC_COLOR ||
           factor == RINGL_SRC_ALPHA || factor == RINGL_ONE_MINUS_SRC_ALPHA ||
           factor == RINGL_DST_ALPHA || factor == RINGL_ONE_MINUS_DST_ALPHA ||
           factor == RINGL_DST_COLOR || factor == RINGL_ONE_MINUS_DST_COLOR ||
           factor == RINGL_CONSTANT_COLOR ||
           factor == RINGL_ONE_MINUS_CONSTANT_COLOR ||
           factor == RINGL_CONSTANT_ALPHA ||
           factor == RINGL_ONE_MINUS_CONSTANT_ALPHA;
}

static int blend_source_factor_valid(uint32_t factor)
{
    return blend_factor_valid(factor) || factor == RINGL_SRC_ALPHA_SATURATE;
}

static int blend_equation_valid(const RinGLContext* context, uint32_t mode)
{
    return mode == RINGL_FUNC_ADD || mode == RINGL_FUNC_SUBTRACT ||
           mode == RINGL_FUNC_REVERSE_SUBTRACT ||
           (context != NULL && context->webgl_blend_minmax_enabled != RINGL_FALSE &&
            (mode == RINGL_MIN || mode == RINGL_MAX));
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
    if (width < 0 || height < 0 ||
        (uint32_t)width > RINGL_MAX_TEXTURE_SIZE ||
        (uint32_t)height > RINGL_MAX_TEXTURE_SIZE) {
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

static int depth_range_component(float value, float* result)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    if ((bits & 0x7f800000u) == 0x7f800000u)
        return 0;
    if (value <= 0.0f)
        *result = 0.0f;
    else if (value >= 1.0f)
        *result = 1.0f;
    else
        *result = value;
    return 1;
}

int ringl_get_depth_range(RinGLDepthRangeV1* range)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLDepthRangeV1 snapshot;

    if (context == NULL || range == NULL ||
        range->struct_size < sizeof(*range) ||
        range->api_version != RINGL_API_VERSION) {
        return -1;
    }
    snapshot.struct_size = sizeof(snapshot);
    snapshot.api_version = RINGL_API_VERSION;
    snapshot.z_near = context->depth_range_near;
    snapshot.z_far = context->depth_range_far;
    snapshot.reserved0 = 0u;
    *range = snapshot;
    return 0;
}

void ringl_depth_range(float z_near, float z_far)
{
    RinGLContext* context = ringl_get_current_context();
    float clamped_near;
    float clamped_far;

    if (context == NULL)
        return;
    if (!depth_range_component(z_near, &clamped_near) ||
        !depth_range_component(z_far, &clamped_far)) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (context->depth_range_near == clamped_near &&
        context->depth_range_far == clamped_far) {
        return;
    }
    context->depth_range_near = clamped_near;
    context->depth_range_far = clamped_far;
    ringl_context_mark_dirty(context, RINGL_DIRTY_VIEWPORT);
}

void ringl_line_width(float width)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL)
        return;
    if (!isfinite(width) || width < 1.0f || width > 64.0f) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (context->line_width == width)
        return;
    context->line_width = width;
    ringl_context_mark_dirty(context, RINGL_DIRTY_PIPELINE);
}

int ringl_get_line_width(RinGLLineWidthV1* width)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLLineWidthV1 snapshot;

    if (context == NULL || width == NULL || width->struct_size < sizeof(*width) ||
        width->api_version != RINGL_API_VERSION) {
        return -1;
    }
    snapshot.struct_size = sizeof(snapshot);
    snapshot.api_version = RINGL_API_VERSION;
    snapshot.width = context->line_width;
    snapshot.minimum = 1.0f;
    snapshot.maximum = 64.0f;
    snapshot.reserved0 = 0u;
    *width = snapshot;
    return 0;
}

void ringl_sample_coverage(float value, uint32_t invert)
{
    RinGLContext* context = ringl_get_current_context();
    float clamped;

    if (context == NULL)
        return;
    if (!isfinite(value) || invert > RINGL_TRUE) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (value <= 0.0f)
        clamped = 0.0f;
    else if (value >= 1.0f)
        clamped = 1.0f;
    else
        clamped = value;
    if (context->sample_coverage_value == clamped &&
        context->sample_coverage_invert == invert) {
        return;
    }
    context->sample_coverage_value = clamped;
    context->sample_coverage_invert = invert;
    ringl_context_mark_dirty(context, RINGL_DIRTY_PIPELINE);
}

int ringl_get_sample_coverage(RinGLSampleCoverageV1* coverage)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLSampleCoverageV1 snapshot;

    if (context == NULL || coverage == NULL ||
        coverage->struct_size < sizeof(*coverage) ||
        coverage->api_version != RINGL_API_VERSION) {
        return -1;
    }
    snapshot.struct_size = sizeof(snapshot);
    snapshot.api_version = RINGL_API_VERSION;
    snapshot.enabled = context->sample_coverage_enabled;
    snapshot.value = context->sample_coverage_value;
    snapshot.invert = context->sample_coverage_invert;
    snapshot.reserved0 = 0u;
    *coverage = snapshot;
    return 0;
}

void ringl_hint(uint32_t target, uint32_t mode)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL)
        return;
    if (target != RINGL_GENERATE_MIPMAP_HINT) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (mode != RINGL_DONT_CARE && mode != RINGL_FASTEST &&
        mode != RINGL_NICEST) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
    }
}

void ringl_polygon_offset(float factor, float units)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL)
        return;
    if (!isfinite(factor) || !isfinite(units)) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (context->polygon_offset_factor == factor &&
        context->polygon_offset_units == units) {
        return;
    }
    context->polygon_offset_factor = factor;
    context->polygon_offset_units = units;
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
    if (!blend_source_factor_valid(source_rgb) ||
        !blend_factor_valid(destination_rgb) ||
        !blend_source_factor_valid(source_alpha) ||
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

static int blend_color_component(float value, float* result)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    if ((bits & 0x7f800000u) == 0x7f800000u)
        return 0;
    *result = value;
    return 1;
}

static float clamp_blend_color_component(float value)
{
    if (value <= 0.0f)
        return 0.0f;
    if (value >= 1.0f)
        return 1.0f;
    return value;
}

int ringl_get_blend_color(RinGLBlendColorV1* color)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLBlendColorV1 snapshot;

    if (context == NULL || color == NULL ||
        color->struct_size < sizeof(*color) ||
        color->api_version != RINGL_API_VERSION)
        return -1;

    snapshot.struct_size = sizeof(snapshot);
    snapshot.api_version = RINGL_API_VERSION;
    snapshot.red = context->blend_constant_red;
    snapshot.green = context->blend_constant_green;
    snapshot.blue = context->blend_constant_blue;
    snapshot.alpha = context->blend_constant_alpha;
    snapshot.reserved0 = 0u;
    *color = snapshot;
    return 0;
}

void ringl_blend_color(float red, float green, float blue, float alpha)
{
    RinGLContext* context = ringl_get_current_context();
    float next_red;
    float next_green;
    float next_blue;
    float next_alpha;

    if (context == NULL)
        return;
    if (!blend_color_component(red, &next_red) ||
        !blend_color_component(green, &next_green) ||
        !blend_color_component(blue, &next_blue) ||
        !blend_color_component(alpha, &next_alpha)) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (context->webgl_float_color_buffer_enabled == RINGL_FALSE &&
        context->webgl_half_float_color_buffer_enabled == RINGL_FALSE) {
        next_red = clamp_blend_color_component(next_red);
        next_green = clamp_blend_color_component(next_green);
        next_blue = clamp_blend_color_component(next_blue);
        next_alpha = clamp_blend_color_component(next_alpha);
    }
    if (context->blend_constant_red == next_red &&
        context->blend_constant_green == next_green &&
        context->blend_constant_blue == next_blue &&
        context->blend_constant_alpha == next_alpha) {
        return;
    }
    context->blend_constant_red = next_red;
    context->blend_constant_green = next_green;
    context->blend_constant_blue = next_blue;
    context->blend_constant_alpha = next_alpha;
    ringl_context_mark_dirty(context, RINGL_DIRTY_PIPELINE);
}

int ringl_enable_webgl_float_color_buffer(void)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL || context->lost != RINGL_FALSE)
        return -1;
    context->webgl_float_color_buffer_enabled = RINGL_TRUE;
    return 0;
}

int ringl_enable_webgl_half_float_color_buffer(void)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL || context->lost != RINGL_FALSE)
        return -1;
    context->webgl_half_float_color_buffer_enabled = RINGL_TRUE;
    return 0;
}

int ringl_enable_webgl_blend_minmax(void)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL || context->lost != RINGL_FALSE)
        return -1;
    context->webgl_blend_minmax_enabled = RINGL_TRUE;
    return 0;
}

void ringl_blend_equation_separate(uint32_t mode_rgb, uint32_t mode_alpha)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL)
        return;
    if (!blend_equation_valid(context, mode_rgb) ||
        !blend_equation_valid(context, mode_alpha)) {
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

void ringl_pixel_storei(uint32_t pname, int32_t param)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL)
        return;
    if (pname != RINGL_UNPACK_ALIGNMENT) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (param != 1 && param != 2 && param != 4 && param != 8) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    context->unpack_alignment = (uint32_t)param;
}

static size_t ringl_get_integerv_value_count(uint32_t pname)
{
    switch (pname) {
    case RINGL_MAX_VIEWPORT_DIMS:
    case RINGL_ALIASED_POINT_SIZE_RANGE:
        return 2u;
    case RINGL_COLOR_WRITEMASK:
    case RINGL_VIEWPORT:
    case RINGL_SCISSOR_BOX:
        return 4u;
    case RINGL_ARRAY_BUFFER_BINDING:
    case RINGL_ELEMENT_ARRAY_BUFFER_BINDING:
    case RINGL_ACTIVE_TEXTURE:
    case RINGL_TEXTURE_BINDING_2D:
    case RINGL_FRAMEBUFFER_BINDING:
    case RINGL_RENDERBUFFER_BINDING:
    case RINGL_CURRENT_PROGRAM:
    case RINGL_UNPACK_ALIGNMENT:
    case RINGL_RED_BITS:
    case RINGL_GREEN_BITS:
    case RINGL_BLUE_BITS:
    case RINGL_ALPHA_BITS:
    case RINGL_DEPTH_BITS:
    case RINGL_STENCIL_BITS:
    case RINGL_MAX_TEXTURE_SIZE_QUERY:
    case RINGL_MAX_RENDERBUFFER_SIZE:
    case RINGL_MAX_TEXTURE_IMAGE_UNITS:
    case RINGL_MAX_COMBINED_TEXTURE_IMAGE_UNITS:
    case RINGL_MAX_VERTEX_ATTRIBS_QUERY:
    case RINGL_CULL_FACE_MODE:
    case RINGL_FRONT_FACE:
    case RINGL_DEPTH_FUNC:
    case RINGL_DEPTH_WRITEMASK:
    case RINGL_STENCIL_FUNC:
    case RINGL_STENCIL_REF:
    case RINGL_STENCIL_VALUE_MASK:
    case RINGL_STENCIL_FAIL:
    case RINGL_STENCIL_PASS_DEPTH_FAIL:
    case RINGL_STENCIL_PASS_DEPTH_PASS:
    case RINGL_STENCIL_WRITEMASK:
    case RINGL_STENCIL_BACK_FUNC:
    case RINGL_STENCIL_BACK_FAIL:
    case RINGL_STENCIL_BACK_PASS_DEPTH_FAIL:
    case RINGL_STENCIL_BACK_PASS_DEPTH_PASS:
    case RINGL_STENCIL_BACK_REF:
    case RINGL_STENCIL_BACK_VALUE_MASK:
    case RINGL_STENCIL_BACK_WRITEMASK:
    case RINGL_BLEND_SRC_RGB:
    case RINGL_BLEND_DST_RGB:
    case RINGL_BLEND_SRC_ALPHA:
    case RINGL_BLEND_DST_ALPHA:
    case RINGL_BLEND_EQUATION_RGB:
    case RINGL_BLEND_EQUATION_ALPHA:
    case RINGL_SAMPLE_BUFFERS:
    case RINGL_SAMPLES:
    case RINGL_SAMPLE_COVERAGE_INVERT:
        return 1u;
    default:
        return 0u;
    }
}

static uint32_t default_framebuffer_has_depth(
    const RinGLDefaultFramebufferV1* framebuffer)
{
    if (framebuffer == NULL || framebuffer->depth_target == 0u)
        return RINGL_FALSE;
    if (framebuffer->flags == 0u)
        return RINGL_TRUE;
    return (framebuffer->flags & RINGL_DEFAULT_FRAMEBUFFER_DEPTH) != 0u
        ? RINGL_TRUE
        : RINGL_FALSE;
}

static uint32_t default_framebuffer_has_stencil(
    const RinGLDefaultFramebufferV1* framebuffer)
{
    if (framebuffer == NULL || framebuffer->depth_target == 0u ||
        framebuffer->depth_format != RINGL_RIN_GPU_FORMAT_D32_FLOAT_S8_UINT)
        return RINGL_FALSE;
    if (framebuffer->flags == 0u)
        return RINGL_TRUE;
    return (framebuffer->flags & RINGL_DEFAULT_FRAMEBUFFER_STENCIL) != 0u
        ? RINGL_TRUE
        : RINGL_FALSE;
}

static int32_t default_framebuffer_color_bits(
    const RinGLDefaultFramebufferV1* framebuffer, uint32_t pname)
{
    uint32_t red_bits = 0u;
    uint32_t green_bits = 0u;
    uint32_t blue_bits = 0u;
    uint32_t alpha_bits = 0u;

    if (framebuffer == NULL)
        return 0;
    switch (framebuffer->color_format) {
    case RINGL_RIN_GPU_FORMAT_RGBA8_UNORM:
    case RINGL_RIN_GPU_FORMAT_BGRA8_UNORM:
        red_bits = 8u;
        green_bits = 8u;
        blue_bits = 8u;
        alpha_bits = 8u;
        break;
    case RINGL_RIN_GPU_FORMAT_RGB565_UNORM:
        red_bits = 5u;
        green_bits = 6u;
        blue_bits = 5u;
        break;
    case RINGL_RIN_GPU_FORMAT_RGBA4_UNORM:
        red_bits = 4u;
        green_bits = 4u;
        blue_bits = 4u;
        alpha_bits = 4u;
        break;
    case RINGL_RIN_GPU_FORMAT_RGB5_A1_UNORM:
        red_bits = 5u;
        green_bits = 5u;
        blue_bits = 5u;
        alpha_bits = 1u;
        break;
    case RINGL_RIN_GPU_FORMAT_RGBA16_FLOAT:
        red_bits = 16u;
        green_bits = 16u;
        blue_bits = 16u;
        alpha_bits = 16u;
        break;
    case RINGL_RIN_GPU_FORMAT_RGBA32_FLOAT:
        red_bits = 32u;
        green_bits = 32u;
        blue_bits = 32u;
        alpha_bits = 32u;
        break;
    default:
        break;
    }
    switch (pname) {
    case RINGL_RED_BITS:
        return (int32_t)red_bits;
    case RINGL_GREEN_BITS:
        return (int32_t)green_bits;
    case RINGL_BLUE_BITS:
        return (int32_t)blue_bits;
    case RINGL_ALPHA_BITS:
        return (int32_t)alpha_bits;
    default:
        return 0;
    }
}

int ringl_get_integerv_bounded(uint32_t pname, int32_t* values,
                               size_t value_count)
{
    RinGLContext* context = ringl_get_current_context();
    size_t required_values;

    if (context == NULL)
        return -1;
    if (values == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }

    required_values = ringl_get_integerv_value_count(pname);
    if (required_values == 0u) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return -1;
    }
    if (value_count < required_values) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }

    switch (pname) {
    case RINGL_ARRAY_BUFFER_BINDING:
        values[0] = (int32_t)context->array_buffer;
        return 0;
    case RINGL_ELEMENT_ARRAY_BUFFER_BINDING:
        values[0] = (int32_t)context->element_array_buffer;
        return 0;
    case RINGL_ACTIVE_TEXTURE:
        values[0] = (int32_t)(RINGL_TEXTURE0 + context->active_texture_unit);
        return 0;
    case RINGL_TEXTURE_BINDING_2D:
        values[0] = (int32_t)context->bound_texture_2d[context->active_texture_unit];
        return 0;
    case RINGL_FRAMEBUFFER_BINDING:
        values[0] = (int32_t)context->framebuffer_binding;
        return 0;
    case RINGL_RENDERBUFFER_BINDING:
        values[0] = (int32_t)context->renderbuffer_binding;
        return 0;
    case RINGL_CURRENT_PROGRAM:
        values[0] = (int32_t)context->current_program;
        return 0;
    case RINGL_UNPACK_ALIGNMENT:
        values[0] = (int32_t)context->unpack_alignment;
        return 0;
    case RINGL_RED_BITS:
    case RINGL_GREEN_BITS:
    case RINGL_BLUE_BITS:
    case RINGL_ALPHA_BITS:
        values[0] = default_framebuffer_color_bits(
            context->has_default_framebuffer != 0u
                ? &context->default_framebuffer
                : NULL,
            pname);
        return 0;
    case RINGL_DEPTH_BITS:
        values[0] = context->has_default_framebuffer != 0u &&
                default_framebuffer_has_depth(&context->default_framebuffer) !=
                    0u
            ? 32
            : 0;
        return 0;
    case RINGL_STENCIL_BITS:
        values[0] = context->has_default_framebuffer != 0u &&
                default_framebuffer_has_stencil(
                    &context->default_framebuffer) != 0u
            ? 8
            : 0;
        return 0;
    case RINGL_MAX_TEXTURE_SIZE_QUERY:
    case RINGL_MAX_RENDERBUFFER_SIZE:
        values[0] = (int32_t)RINGL_MAX_TEXTURE_SIZE;
        return 0;
    case RINGL_MAX_VIEWPORT_DIMS:
        values[0] = (int32_t)RINGL_MAX_TEXTURE_SIZE;
        values[1] = (int32_t)RINGL_MAX_TEXTURE_SIZE;
        return 0;
    case RINGL_ALIASED_POINT_SIZE_RANGE:
        /* The private Aquamarine/RinGPU target rasterizes exactly one pixel
         * per point. RinGL exposes no programmable point-size state. */
        values[0] = 1;
        values[1] = 1;
        return 0;
    case RINGL_MAX_TEXTURE_IMAGE_UNITS:
    case RINGL_MAX_COMBINED_TEXTURE_IMAGE_UNITS:
        values[0] = (int32_t)RINGL_MAX_TEXTURE_UNITS;
        return 0;
    case RINGL_MAX_VERTEX_ATTRIBS_QUERY:
        values[0] = (int32_t)RINGL_MAX_VERTEX_ATTRIBS;
        return 0;
    case RINGL_CULL_FACE_MODE:
        values[0] = (int32_t)context->cull_face_mode;
        return 0;
    case RINGL_FRONT_FACE:
        values[0] = (int32_t)context->front_face;
        return 0;
    case RINGL_DEPTH_FUNC:
        values[0] = (int32_t)context->depth_func;
        return 0;
    case RINGL_DEPTH_WRITEMASK:
        values[0] = (int32_t)context->depth_write_mask;
        return 0;
    case RINGL_STENCIL_FUNC:
        values[0] = (int32_t)context->stencil_func;
        return 0;
    case RINGL_STENCIL_REF:
        values[0] = (int32_t)context->stencil_reference;
        return 0;
    case RINGL_STENCIL_VALUE_MASK:
        values[0] = (int32_t)context->stencil_value_mask;
        return 0;
    case RINGL_STENCIL_FAIL:
        values[0] = (int32_t)context->stencil_fail_operation;
        return 0;
    case RINGL_STENCIL_PASS_DEPTH_FAIL:
        values[0] = (int32_t)context->stencil_depth_fail_operation;
        return 0;
    case RINGL_STENCIL_PASS_DEPTH_PASS:
        values[0] = (int32_t)context->stencil_pass_operation;
        return 0;
    case RINGL_STENCIL_WRITEMASK:
        values[0] = (int32_t)context->stencil_write_mask;
        return 0;
    case RINGL_STENCIL_BACK_FUNC:
        values[0] = (int32_t)context->back_stencil_func;
        return 0;
    case RINGL_STENCIL_BACK_FAIL:
        values[0] = (int32_t)context->back_stencil_fail_operation;
        return 0;
    case RINGL_STENCIL_BACK_PASS_DEPTH_FAIL:
        values[0] = (int32_t)context->back_stencil_depth_fail_operation;
        return 0;
    case RINGL_STENCIL_BACK_PASS_DEPTH_PASS:
        values[0] = (int32_t)context->back_stencil_pass_operation;
        return 0;
    case RINGL_STENCIL_BACK_REF:
        values[0] = (int32_t)context->back_stencil_reference;
        return 0;
    case RINGL_STENCIL_BACK_VALUE_MASK:
        values[0] = (int32_t)context->back_stencil_value_mask;
        return 0;
    case RINGL_STENCIL_BACK_WRITEMASK:
        values[0] = (int32_t)context->back_stencil_write_mask;
        return 0;
    case RINGL_BLEND_SRC_RGB:
        values[0] = (int32_t)context->blend_source_rgb;
        return 0;
    case RINGL_BLEND_DST_RGB:
        values[0] = (int32_t)context->blend_destination_rgb;
        return 0;
    case RINGL_BLEND_SRC_ALPHA:
        values[0] = (int32_t)context->blend_source_alpha;
        return 0;
    case RINGL_BLEND_DST_ALPHA:
        values[0] = (int32_t)context->blend_destination_alpha;
        return 0;
    case RINGL_BLEND_EQUATION_RGB:
        values[0] = (int32_t)context->blend_equation_rgb;
        return 0;
    case RINGL_BLEND_EQUATION_ALPHA:
        values[0] = (int32_t)context->blend_equation_alpha;
        return 0;
    case RINGL_SAMPLE_BUFFERS:
    case RINGL_SAMPLES:
        /* All native images used by this profile have sample_count == 1;
         * there is no multisample attachment or resolve path. */
        values[0] = 0;
        return 0;
    case RINGL_SAMPLE_COVERAGE_INVERT:
        values[0] = (int32_t)context->sample_coverage_invert;
        return 0;
    case RINGL_COLOR_WRITEMASK:
        values[0] = (context->color_write_mask & 0x01u) != 0u;
        values[1] = (context->color_write_mask & 0x02u) != 0u;
        values[2] = (context->color_write_mask & 0x04u) != 0u;
        values[3] = (context->color_write_mask & 0x08u) != 0u;
        return 0;
    case RINGL_VIEWPORT:
        values[0] = context->viewport_x;
        values[1] = context->viewport_y;
        values[2] = (int32_t)context->viewport_width;
        values[3] = (int32_t)context->viewport_height;
        return 0;
    case RINGL_SCISSOR_BOX:
        values[0] = context->scissor_x;
        values[1] = context->scissor_y;
        values[2] = (int32_t)context->scissor_width;
        values[3] = (int32_t)context->scissor_height;
        return 0;
    default:
        return -1;
    }
}

void ringl_get_integerv(uint32_t pname, int32_t* values)
{
    (void)ringl_get_integerv_bounded(pname, values, 4u);
}
