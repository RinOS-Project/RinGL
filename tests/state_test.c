/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <ringl/ringl.h>

static RinGLContext* make_context(void)
{
    RinGLContext* context = NULL;
    RinGLContextDescV1 desc = {
        .struct_size = sizeof(desc),
        .api_version = RINGL_API_VERSION,
    };
    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_make_current(context) == 0);
    return context;
}

int main(void)
{
    RinGLContext* context = make_context();
    int32_t values[4] = {0};
    uint32_t embedding_clear_error = RINGL_NO_ERROR;
    RinGLClearValuesV1 clear_values = {
        .struct_size = sizeof(clear_values),
        .api_version = RINGL_API_VERSION,
    };
    RinGLBlendColorV1 blend_color = {
        .struct_size = sizeof(blend_color),
        .api_version = RINGL_API_VERSION,
    };
    RinGLDepthRangeV1 depth_range = {
        .struct_size = sizeof(depth_range),
        .api_version = RINGL_API_VERSION,
    };
    RinGLLineWidthV1 line_width = {
        .struct_size = sizeof(line_width),
        .api_version = RINGL_API_VERSION,
    };
    RinGLSampleCoverageV1 sample_coverage = {
        .struct_size = sizeof(sample_coverage),
        .api_version = RINGL_API_VERSION,
    };
    RinGLDefaultFramebufferV1 framebuffer = {
        .struct_size = sizeof(framebuffer),
        .api_version = RINGL_API_VERSION,
        .color_target = 1u,
        .color_format = RINGL_RIN_GPU_FORMAT_RGBA8_UNORM,
        .width = 320u,
        .height = 200u,
        .display_id = 0u,
    };

    assert(!ringl_is_enabled(RINGL_SCISSOR_TEST));
    assert(!ringl_is_enabled(RINGL_CULL_FACE));
    assert(ringl_is_enabled(RINGL_DITHER));
    assert(!ringl_is_enabled(RINGL_DEPTH_TEST));
    assert(!ringl_is_enabled(RINGL_POLYGON_OFFSET_FILL));
    assert(!ringl_is_enabled(RINGL_STENCIL_TEST));
    assert(!ringl_is_enabled(RINGL_BLEND));
    assert(strcmp(ringl_get_string(RINGL_VENDOR), "RinOS Project") == 0);
    assert(strcmp(ringl_get_string(RINGL_RENDERER), "RinGL") == 0);
    assert(strcmp(ringl_get_string(RINGL_VERSION),
                  "RinGL v1 bounded profile") == 0);
    assert(strcmp(ringl_get_string(RINGL_SHADING_LANGUAGE_VERSION),
                  "RinGL RSH1 (GLSL ES 1.00 subset)") == 0);
    assert(ringl_get_string(0xdeadbeefu) == NULL);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);

    assert(ringl_get_clear_values(&clear_values) == 0);
    assert(clear_values.red == 0.0f && clear_values.green == 0.0f &&
           clear_values.blue == 0.0f && clear_values.alpha == 0.0f &&
           clear_values.depth == 1.0f && clear_values.stencil == 0);
    ringl_clear_color(0.25f, 0.5f, 0.75f, 1.0f);
    ringl_clear_depth(0.125f);
    ringl_clear_stencil(-1);
    assert(ringl_get_clear_values(&clear_values) == 0);
    assert(clear_values.red == 0.25f && clear_values.green == 0.5f &&
           clear_values.blue == 0.75f && clear_values.alpha == 1.0f &&
           clear_values.depth == 0.125f && clear_values.stencil == 255);
    clear_values.struct_size = sizeof(clear_values) - 1u;
    assert(ringl_get_clear_values(&clear_values) == -1);
    clear_values.struct_size = sizeof(clear_values);
    clear_values.api_version = RINGL_API_VERSION + 1u;
    assert(ringl_get_clear_values(&clear_values) == -1);
    clear_values.api_version = RINGL_API_VERSION;

    assert(ringl_get_blend_color(&blend_color) == 0);
    assert(blend_color.red == 0.0f && blend_color.green == 0.0f &&
           blend_color.blue == 0.0f && blend_color.alpha == 0.0f);
    ringl_blend_color(-0.25f, 0.5f, 1.25f, 1.0f);
    assert(ringl_get_blend_color(&blend_color) == 0);
    assert(blend_color.red == 0.0f && blend_color.green == 0.5f &&
           blend_color.blue == 1.0f && blend_color.alpha == 1.0f);
    assert(ringl_enable_webgl_float_color_buffer() == 0);
    ringl_blend_color(-0.25f, 0.5f, 1.25f, 2.0f);
    assert(ringl_get_blend_color(&blend_color) == 0);
    assert(blend_color.red == -0.25f && blend_color.green == 0.5f &&
           blend_color.blue == 1.25f && blend_color.alpha == 2.0f);
    blend_color.struct_size = sizeof(blend_color) - 1u;
    blend_color.red = -1.0f;
    assert(ringl_get_blend_color(&blend_color) == -1);
    assert(blend_color.red == -1.0f);
    blend_color.struct_size = sizeof(blend_color);
    blend_color.api_version = RINGL_API_VERSION + 1u;
    assert(ringl_get_blend_color(&blend_color) == -1);
    blend_color.api_version = RINGL_API_VERSION;

    ringl_get_integerv(RINGL_CULL_FACE_MODE, values);
    assert(values[0] == (int32_t)RINGL_BACK);
    ringl_get_integerv(RINGL_FRONT_FACE, values);
    assert(values[0] == (int32_t)RINGL_CCW);
    ringl_get_integerv(RINGL_DEPTH_FUNC, values);
    assert(values[0] == (int32_t)RINGL_LESS);
    ringl_get_integerv(RINGL_DEPTH_WRITEMASK, values);
    assert(values[0] == (int32_t)RINGL_TRUE);
    ringl_get_integerv(RINGL_STENCIL_FUNC, values);
    assert(values[0] == (int32_t)RINGL_ALWAYS);
    ringl_get_integerv(RINGL_STENCIL_REF, values);
    assert(values[0] == 0);
    ringl_get_integerv(RINGL_STENCIL_VALUE_MASK, values);
    assert(values[0] == 0xff);
    ringl_get_integerv(RINGL_STENCIL_WRITEMASK, values);
    assert(values[0] == 0xff);
    ringl_get_integerv(RINGL_BLEND_SRC_RGB, values);
    assert(values[0] == (int32_t)RINGL_ONE);
    ringl_get_integerv(RINGL_BLEND_DST_RGB, values);
    assert(values[0] == (int32_t)RINGL_ZERO);
    ringl_get_integerv(RINGL_BLEND_SRC_ALPHA, values);
    assert(values[0] == (int32_t)RINGL_ONE);
    ringl_get_integerv(RINGL_BLEND_DST_ALPHA, values);
    assert(values[0] == (int32_t)RINGL_ZERO);
    ringl_get_integerv(RINGL_BLEND_EQUATION_RGB, values);
    assert(values[0] == (int32_t)RINGL_FUNC_ADD);
    ringl_get_integerv(RINGL_BLEND_EQUATION_ALPHA, values);
    assert(values[0] == (int32_t)RINGL_FUNC_ADD);
    ringl_get_integerv(RINGL_COLOR_WRITEMASK, values);
    assert(values[0] == 1 && values[1] == 1 && values[2] == 1 && values[3] == 1);
    ringl_get_integerv(RINGL_MAX_TEXTURE_SIZE_QUERY, values);
    assert(values[0] == (int32_t)RINGL_MAX_TEXTURE_SIZE);
    ringl_get_integerv(RINGL_MAX_TEXTURE_IMAGE_UNITS, values);
    assert(values[0] == (int32_t)RINGL_MAX_TEXTURE_UNITS);
    ringl_get_integerv(RINGL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, values);
    assert(values[0] == (int32_t)RINGL_MAX_TEXTURE_UNITS);
    ringl_get_integerv(RINGL_MAX_VERTEX_ATTRIBS_QUERY, values);
    assert(values[0] == (int32_t)RINGL_MAX_VERTEX_ATTRIBS);
    ringl_get_integerv(RINGL_ACTIVE_TEXTURE, values);
    assert(values[0] == (int32_t)RINGL_TEXTURE0);
    ringl_get_integerv(RINGL_TEXTURE_BINDING_2D, values);
    assert(values[0] == 0);
    ringl_get_integerv(RINGL_CURRENT_PROGRAM, values);
    assert(values[0] == 0);

    ringl_enable(RINGL_SCISSOR_TEST);
    ringl_enable(RINGL_CULL_FACE);
    ringl_enable(RINGL_DEPTH_TEST);
    ringl_enable(RINGL_POLYGON_OFFSET_FILL);
    ringl_enable(RINGL_STENCIL_TEST);
    ringl_enable(RINGL_BLEND);
    assert(ringl_is_enabled(RINGL_SCISSOR_TEST));
    assert(ringl_is_enabled(RINGL_CULL_FACE));
    assert(ringl_is_enabled(RINGL_DEPTH_TEST));
    assert(ringl_is_enabled(RINGL_POLYGON_OFFSET_FILL));
    assert(ringl_is_enabled(RINGL_STENCIL_TEST));
    assert(ringl_is_enabled(RINGL_BLEND));
    ringl_disable(RINGL_DITHER);
    assert(!ringl_is_enabled(RINGL_DITHER));
    ringl_enable(RINGL_DITHER);
    assert(ringl_is_enabled(RINGL_DITHER));
    ringl_disable(RINGL_BLEND);
    assert(!ringl_is_enabled(RINGL_BLEND));
    ringl_polygon_offset(0.5f, -2.0f);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_line_width(3.0f);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_get_line_width(&line_width) == 0);
    assert(line_width.width == 3.0f && line_width.minimum == 1.0f &&
           line_width.maximum == 64.0f);
    ringl_line_width(0.5f);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    assert(ringl_get_sample_coverage(&sample_coverage) == 0);
    assert(sample_coverage.enabled == RINGL_FALSE &&
           sample_coverage.value == 1.0f &&
           sample_coverage.invert == RINGL_FALSE);
    assert(RINGL_SAMPLE_BUFFERS == 0x80a8u);
    assert(RINGL_SAMPLES == 0x80a9u);
    assert(RINGL_SAMPLE_COVERAGE_VALUE == 0x80aau);
    assert(RINGL_SAMPLE_COVERAGE_VALUE != RINGL_SAMPLE_BUFFERS);
    ringl_enable(RINGL_SAMPLE_COVERAGE);
    ringl_sample_coverage(0.0f, RINGL_FALSE);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_get_sample_coverage(&sample_coverage) == 0);
    assert(sample_coverage.enabled == RINGL_TRUE &&
           sample_coverage.value == 0.0f &&
           sample_coverage.invert == RINGL_FALSE);
    ringl_sample_coverage(2.0f, RINGL_TRUE);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_get_sample_coverage(&sample_coverage) == 0);
    assert(sample_coverage.value == 1.0f &&
           sample_coverage.invert == RINGL_TRUE);
    ringl_sample_coverage(0.5f, 2u);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    assert(ringl_get_sample_coverage(&sample_coverage) == 0);
    assert(sample_coverage.value == 1.0f &&
           sample_coverage.invert == RINGL_TRUE);
    ringl_get_integerv(RINGL_SAMPLE_COVERAGE_INVERT, values);
    assert(values[0] == (int32_t)RINGL_TRUE);
    ringl_get_integerv(RINGL_SAMPLE_BUFFERS, values);
    assert(values[0] == 0);
    ringl_get_integerv(RINGL_SAMPLES, values);
    assert(values[0] == 0);
    ringl_get_integerv(RINGL_MAX_RENDERBUFFER_SIZE, values);
    assert(values[0] == (int32_t)RINGL_MAX_TEXTURE_SIZE);
    ringl_get_integerv(RINGL_MAX_VIEWPORT_DIMS, values);
    assert(values[0] == (int32_t)RINGL_MAX_TEXTURE_SIZE &&
           values[1] == (int32_t)RINGL_MAX_TEXTURE_SIZE);
    ringl_get_integerv(RINGL_ALIASED_POINT_SIZE_RANGE, values);
    assert(values[0] == 1 && values[1] == 1);
    {
        size_t compressed_format_count = 1u;

        assert(ringl_get_compressed_texture_format_count(
                   &compressed_format_count) == 0);
        assert(compressed_format_count == 1u);
        assert(ringl_get_compressed_texture_format_count(NULL) == -1);
        assert(ringl_get_error() == RINGL_INVALID_OPERATION);
    }
    ringl_hint(RINGL_GENERATE_MIPMAP_HINT, RINGL_NICEST);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_hint(RINGL_FRAGMENT_SHADER_DERIVATIVE_HINT, RINGL_NICEST);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);
    values[0] = 123;
    assert(ringl_get_integerv_bounded(
               RINGL_FRAGMENT_SHADER_DERIVATIVE_HINT, values, 1u) == -1);
    assert(values[0] == 123);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);
    assert(ringl_enable_webgl_standard_derivatives() == 0);
    assert(ringl_get_integerv_bounded(
               RINGL_FRAGMENT_SHADER_DERIVATIVE_HINT, values, 1u) == 0);
    assert(values[0] == (int32_t)RINGL_DONT_CARE);
    ringl_hint(RINGL_FRAGMENT_SHADER_DERIVATIVE_HINT, RINGL_NICEST);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_get_integerv(RINGL_FRAGMENT_SHADER_DERIVATIVE_HINT, values);
    assert(values[0] == (int32_t)RINGL_NICEST);
    {
        uint32_t nan_bits = 0x7fc00000u;
        float nan;

        memcpy(&nan, &nan_bits, sizeof(nan));
        ringl_polygon_offset(nan, 0.0f);
    }
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    assert(ringl_is_enabled(RINGL_POLYGON_OFFSET_FILL));
    ringl_disable(RINGL_POLYGON_OFFSET_FILL);
    assert(!ringl_is_enabled(RINGL_POLYGON_OFFSET_FILL));

    ringl_enable(0xdeadbeefu);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);
    assert(!ringl_is_enabled(0xdeadbeefu));
    assert(ringl_get_error() == RINGL_INVALID_ENUM);

    /* The embedding-only default clear reports its own failure without
     * stealing an author-visible pending error. */
    ringl_enable(0xdeadbeefu);
    assert(ringl_clear_default_framebuffer_for_embedding(
               &embedding_clear_error) == -1);
    assert(embedding_clear_error == RINGL_INVALID_OPERATION);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);
    assert(ringl_clear_default_framebuffer_for_embedding(NULL) == -1);

    ringl_viewport(2, 3, -1, 10);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    ringl_viewport(2, 3, 100, 80);
    ringl_get_integerv(RINGL_VIEWPORT, values);
    assert(values[0] == 2 && values[1] == 3 && values[2] == 100 && values[3] == 80);
    ringl_viewport(2, 3, (int32_t)RINGL_MAX_TEXTURE_SIZE + 1, 80);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    ringl_get_integerv(RINGL_VIEWPORT, values);
    assert(values[0] == 2 && values[1] == 3 && values[2] == 100 && values[3] == 80);

    ringl_scissor(4, 5, 20, -2);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    ringl_scissor(4, 5, 20, 30);
    ringl_get_integerv(RINGL_SCISSOR_BOX, values);
    assert(values[0] == 4 && values[1] == 5 && values[2] == 20 && values[3] == 30);

    ringl_cull_face(0u);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);
    ringl_cull_face(RINGL_FRONT_AND_BACK);
    ringl_get_integerv(RINGL_CULL_FACE_MODE, values);
    assert(values[0] == (int32_t)RINGL_FRONT_AND_BACK);

    ringl_front_face(0u);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);
    ringl_front_face(RINGL_CW);
    ringl_get_integerv(RINGL_FRONT_FACE, values);
    assert(values[0] == (int32_t)RINGL_CW);

    ringl_depth_func(0xdeadbeefu);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);
    ringl_depth_func(RINGL_GEQUAL);
    ringl_depth_mask(0u);
    ringl_get_integerv(RINGL_DEPTH_FUNC, values);
    assert(values[0] == (int32_t)RINGL_GEQUAL);
    ringl_get_integerv(RINGL_DEPTH_WRITEMASK, values);
    assert(values[0] == (int32_t)RINGL_FALSE);

    assert(ringl_get_depth_range(&depth_range) == 0);
    assert(depth_range.z_near == 0.0f && depth_range.z_far == 1.0f);
    ringl_depth_range(-0.5f, 1.5f);
    assert(ringl_get_depth_range(&depth_range) == 0);
    assert(depth_range.z_near == 0.0f && depth_range.z_far == 1.0f);
    ringl_depth_range(0.75f, 0.25f);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_get_depth_range(&depth_range) == 0);
    assert(depth_range.z_near == 0.75f && depth_range.z_far == 0.25f);
    {
        uint32_t nan_bits = 0x7fc00000u;
        float nan;

        memcpy(&nan, &nan_bits, sizeof(nan));
        ringl_depth_range(nan, 0.5f);
    }
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    assert(ringl_get_depth_range(&depth_range) == 0);
    assert(depth_range.z_near == 0.75f && depth_range.z_far == 0.25f);
    depth_range.struct_size = sizeof(depth_range) - 1u;
    depth_range.z_near = 0.125f;
    depth_range.z_far = 0.875f;
    assert(ringl_get_depth_range(&depth_range) == -1);
    assert(depth_range.z_near == 0.125f && depth_range.z_far == 0.875f);
    depth_range.struct_size = sizeof(depth_range);

    ringl_stencil_func(0xdeadbeefu, 0, 0xffu);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);
    ringl_stencil_func(RINGL_GEQUAL, -1, 0x123u);
    ringl_stencil_mask(0x1a5u);
    ringl_stencil_op(RINGL_REPLACE, RINGL_INCR_WRAP, RINGL_DECR);
    ringl_get_integerv(RINGL_STENCIL_FUNC, values);
    assert(values[0] == (int32_t)RINGL_GEQUAL);
    ringl_get_integerv(RINGL_STENCIL_REF, values);
    assert(values[0] == 0xff);
    ringl_get_integerv(RINGL_STENCIL_VALUE_MASK, values);
    assert(values[0] == 0x23);
    ringl_get_integerv(RINGL_STENCIL_FAIL, values);
    assert(values[0] == (int32_t)RINGL_REPLACE);
    ringl_get_integerv(RINGL_STENCIL_PASS_DEPTH_FAIL, values);
    assert(values[0] == (int32_t)RINGL_INCR_WRAP);
    ringl_get_integerv(RINGL_STENCIL_PASS_DEPTH_PASS, values);
    assert(values[0] == (int32_t)RINGL_DECR);
    ringl_get_integerv(RINGL_STENCIL_WRITEMASK, values);
    assert(values[0] == 0xa5);
    ringl_stencil_func_separate(RINGL_BACK, RINGL_LESS, 0x5a, 0x3cu);
    ringl_stencil_mask_separate(RINGL_BACK, 0xf0u);
    ringl_stencil_op_separate(RINGL_BACK, RINGL_ZERO, RINGL_DECR_WRAP,
                              RINGL_REPLACE);
    ringl_get_integerv(RINGL_STENCIL_BACK_FUNC, values);
    assert(values[0] == (int32_t)RINGL_LESS);
    ringl_get_integerv(RINGL_STENCIL_BACK_REF, values);
    assert(values[0] == 0x5a);
    ringl_get_integerv(RINGL_STENCIL_BACK_VALUE_MASK, values);
    assert(values[0] == 0x3c);
    ringl_get_integerv(RINGL_STENCIL_BACK_FAIL, values);
    assert(values[0] == (int32_t)RINGL_ZERO);
    ringl_get_integerv(RINGL_STENCIL_BACK_PASS_DEPTH_FAIL, values);
    assert(values[0] == (int32_t)RINGL_DECR_WRAP);
    ringl_get_integerv(RINGL_STENCIL_BACK_PASS_DEPTH_PASS, values);
    assert(values[0] == (int32_t)RINGL_REPLACE);
    ringl_get_integerv(RINGL_STENCIL_BACK_WRITEMASK, values);
    assert(values[0] == 0xf0);
    ringl_stencil_func_separate(0u, RINGL_ALWAYS, 0, 0xffu);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);
    ringl_stencil_mask_separate(0u, 0xffu);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);
    ringl_stencil_op_separate(0u, RINGL_KEEP, RINGL_KEEP, RINGL_KEEP);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);
    ringl_stencil_func(RINGL_ALWAYS, 0x12, 0x7fu);
    ringl_stencil_mask(0x55u);
    ringl_stencil_op(RINGL_KEEP, RINGL_KEEP, RINGL_INVERT);
    ringl_get_integerv(RINGL_STENCIL_BACK_FUNC, values);
    assert(values[0] == (int32_t)RINGL_ALWAYS);
    ringl_get_integerv(RINGL_STENCIL_BACK_REF, values);
    assert(values[0] == 0x12);
    ringl_get_integerv(RINGL_STENCIL_BACK_VALUE_MASK, values);
    assert(values[0] == 0x7f);
    ringl_get_integerv(RINGL_STENCIL_BACK_WRITEMASK, values);
    assert(values[0] == 0x55);
    ringl_stencil_op(0xdeadbeefu, RINGL_KEEP, RINGL_KEEP);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);

    ringl_blend_func(0xdeadbeefu, RINGL_ZERO);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);
    ringl_blend_func(RINGL_SRC_ALPHA, RINGL_ONE_MINUS_SRC_ALPHA);
    ringl_get_integerv(RINGL_BLEND_SRC_RGB, values);
    assert(values[0] == (int32_t)RINGL_SRC_ALPHA);
    ringl_get_integerv(RINGL_BLEND_DST_RGB, values);
    assert(values[0] == (int32_t)RINGL_ONE_MINUS_SRC_ALPHA);
    ringl_get_integerv(RINGL_BLEND_SRC_ALPHA, values);
    assert(values[0] == (int32_t)RINGL_SRC_ALPHA);
    ringl_get_integerv(RINGL_BLEND_DST_ALPHA, values);
    assert(values[0] == (int32_t)RINGL_ONE_MINUS_SRC_ALPHA);

    ringl_blend_func_separate(RINGL_ONE, RINGL_ZERO,
                              RINGL_DST_ALPHA, RINGL_ONE_MINUS_DST_ALPHA);
    ringl_get_integerv(RINGL_BLEND_SRC_RGB, values);
    assert(values[0] == (int32_t)RINGL_ONE);
    ringl_get_integerv(RINGL_BLEND_DST_RGB, values);
    assert(values[0] == (int32_t)RINGL_ZERO);
    ringl_get_integerv(RINGL_BLEND_SRC_ALPHA, values);
    assert(values[0] == (int32_t)RINGL_DST_ALPHA);
    ringl_get_integerv(RINGL_BLEND_DST_ALPHA, values);
    assert(values[0] == (int32_t)RINGL_ONE_MINUS_DST_ALPHA);

    ringl_blend_equation(0xdeadbeefu);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);
    ringl_blend_equation(RINGL_FUNC_SUBTRACT);
    ringl_get_integerv(RINGL_BLEND_EQUATION_RGB, values);
    assert(values[0] == (int32_t)RINGL_FUNC_SUBTRACT);
    ringl_get_integerv(RINGL_BLEND_EQUATION_ALPHA, values);
    assert(values[0] == (int32_t)RINGL_FUNC_SUBTRACT);
    ringl_blend_equation_separate(RINGL_FUNC_ADD, RINGL_MAX);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);
    ringl_get_integerv(RINGL_BLEND_EQUATION_RGB, values);
    assert(values[0] == (int32_t)RINGL_FUNC_SUBTRACT);
    ringl_get_integerv(RINGL_BLEND_EQUATION_ALPHA, values);
    assert(values[0] == (int32_t)RINGL_FUNC_SUBTRACT);
    assert(ringl_enable_webgl_blend_minmax() == 0);
    ringl_blend_equation_separate(RINGL_FUNC_ADD, RINGL_MAX);
    ringl_get_integerv(RINGL_BLEND_EQUATION_RGB, values);
    assert(values[0] == (int32_t)RINGL_FUNC_ADD);
    ringl_get_integerv(RINGL_BLEND_EQUATION_ALPHA, values);
    assert(values[0] == (int32_t)RINGL_MAX);

    ringl_color_mask(1u, 0u, 7u, 0u);
    ringl_get_integerv(RINGL_COLOR_WRITEMASK, values);
    assert(values[0] == 1 && values[1] == 0 && values[2] == 1 && values[3] == 0);

    ringl_viewport(11, 22, 33, 44);
    assert(ringl_get_integerv_bounded(RINGL_VIEWPORT, NULL, 4u) == -1);
    assert(ringl_get_error() == RINGL_INVALID_OPERATION);
    values[0] = 101;
    values[1] = 102;
    values[2] = 103;
    values[3] = 104;
    assert(ringl_get_integerv_bounded(RINGL_VIEWPORT, values, 3u) == -1);
    assert(values[0] == 101 && values[1] == 102 && values[2] == 103 && values[3] == 104);
    assert(ringl_get_error() == RINGL_INVALID_OPERATION);
    assert(ringl_get_integerv_bounded(RINGL_VIEWPORT, values, 4u) == 0);
    assert(values[0] == 11 && values[1] == 22 && values[2] == 33 && values[3] == 44);

    values[0] = 201;
    assert(ringl_get_integerv_bounded(0xdeadbeefu, values, 4u) == -1);
    assert(values[0] == 201);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);

    ringl_get_integerv(0xdeadbeefu, values);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);

    ringl_context_destroy(context);

    context = make_context();
    assert(ringl_set_default_framebuffer(&framebuffer) == 0);
    ringl_get_integerv(RINGL_VIEWPORT, values);
    assert(values[0] == 0 && values[1] == 0 && values[2] == 320 && values[3] == 200);
    ringl_get_integerv(RINGL_RED_BITS, values);
    assert(values[0] == 8);
    ringl_get_integerv(RINGL_GREEN_BITS, values);
    assert(values[0] == 8);
    ringl_get_integerv(RINGL_BLUE_BITS, values);
    assert(values[0] == 8);
    ringl_get_integerv(RINGL_ALPHA_BITS, values);
    assert(values[0] == 8);
    ringl_get_integerv(RINGL_DEPTH_BITS, values);
    assert(values[0] == 0);
    ringl_get_integerv(RINGL_STENCIL_BITS, values);
    assert(values[0] == 0);

    framebuffer.depth_target = 2u;
    framebuffer.depth_format = RINGL_RIN_GPU_FORMAT_D32_FLOAT_S8_UINT;
    framebuffer.flags = RINGL_DEFAULT_FRAMEBUFFER_EXPLICIT_ASPECTS |
        RINGL_DEFAULT_FRAMEBUFFER_STENCIL;
    assert(ringl_set_default_framebuffer(&framebuffer) == 0);
    ringl_get_integerv(RINGL_DEPTH_BITS, values);
    assert(values[0] == 0);
    ringl_get_integerv(RINGL_STENCIL_BITS, values);
    assert(values[0] == 8);

    framebuffer.flags = RINGL_DEFAULT_FRAMEBUFFER_EXPLICIT_ASPECTS |
        RINGL_DEFAULT_FRAMEBUFFER_DEPTH;
    assert(ringl_set_default_framebuffer(&framebuffer) == 0);
    ringl_get_integerv(RINGL_DEPTH_BITS, values);
    assert(values[0] == 32);
    ringl_get_integerv(RINGL_STENCIL_BITS, values);
    assert(values[0] == 0);
    framebuffer.width = RINGL_MAX_TEXTURE_SIZE + 1u;
    assert(ringl_set_default_framebuffer(&framebuffer) == -1);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    framebuffer.width = 320u;
    {
        RinGLDefaultFramebufferV1 current_framebuffer = { 0 };

        assert(ringl_get_default_framebuffer(&current_framebuffer) == 0);
        assert(current_framebuffer.width == 320u &&
               current_framebuffer.height == 200u);
    }
    ringl_context_destroy(context);
    return 0;
}
