/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>

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
    assert(!ringl_is_enabled(RINGL_DEPTH_TEST));
    assert(!ringl_is_enabled(RINGL_BLEND));

    ringl_get_integerv(RINGL_CULL_FACE_MODE, values);
    assert(values[0] == (int32_t)RINGL_BACK);
    ringl_get_integerv(RINGL_FRONT_FACE, values);
    assert(values[0] == (int32_t)RINGL_CCW);
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
    ringl_enable(RINGL_BLEND);
    assert(ringl_is_enabled(RINGL_SCISSOR_TEST));
    assert(ringl_is_enabled(RINGL_CULL_FACE));
    assert(ringl_is_enabled(RINGL_DEPTH_TEST));
    assert(ringl_is_enabled(RINGL_BLEND));
    ringl_disable(RINGL_BLEND);
    assert(!ringl_is_enabled(RINGL_BLEND));

    ringl_enable(0xdeadbeefu);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);
    assert(!ringl_is_enabled(0xdeadbeefu));
    assert(ringl_get_error() == RINGL_INVALID_ENUM);

    ringl_viewport(2, 3, -1, 10);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    ringl_viewport(2, 3, 100, 80);
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

    ringl_get_integerv(0xdeadbeefu, values);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);

    ringl_context_destroy(context);

    context = make_context();
    assert(ringl_set_default_framebuffer(&framebuffer) == 0);
    ringl_get_integerv(RINGL_VIEWPORT, values);
    assert(values[0] == 0 && values[1] == 0 && values[2] == 320 && values[3] == 200);
    ringl_context_destroy(context);
    return 0;
}
