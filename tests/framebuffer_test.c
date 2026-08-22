/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <string.h>

#include <ringl/ringl.h>

int main(void)
{
    RinGLContext* context = NULL;
    RinGLContextDescV1 desc = {
        .struct_size = sizeof(desc),
        .api_version = RINGL_API_VERSION,
    };
    RinGLDefaultFramebufferV1 framebuffer = {
        .struct_size = sizeof(framebuffer),
        .api_version = RINGL_API_VERSION,
        .color_target = 91u,
        .color_format = 2u,
        .width = 800u,
        .height = 600u,
    };
    RinGLDefaultFramebufferV1 copy;
    uint32_t framebuffer_state = 0xfeedfaceu;

    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_make_current(context) == 0);

    memset(&copy, 0xff, sizeof(copy));
    assert(ringl_get_default_framebuffer(&copy) == 1);
    assert(copy.color_target == 0u);
    assert(ringl_get_default_framebuffer_state(&framebuffer_state) == 1);
    assert(framebuffer_state == 0xfeedfaceu);
    assert(ringl_get_default_depth_framebuffer_state(&framebuffer_state) == 1);
    assert(framebuffer_state == 0xfeedfaceu);
    assert(ringl_set_default_framebuffer_state(
               RINGL_RIN_GPU_IMAGE_UNDEFINED) == -1);
    assert(ringl_get_error() == RINGL_INVALID_OPERATION);

    assert(ringl_set_default_framebuffer(&framebuffer) == 0);
    assert((ringl_context_dirty_bits(context) & RINGL_DIRTY_FRAMEBUFFER) != 0u);
    assert((ringl_context_dirty_bits(context) & RINGL_DIRTY_PIPELINE) != 0u);
    assert(ringl_get_default_framebuffer_state(&framebuffer_state) == 0);
    assert(framebuffer_state == RINGL_RIN_GPU_IMAGE_PRESENT);
    assert(ringl_set_default_depth_framebuffer_state(
               RINGL_RIN_GPU_IMAGE_UNDEFINED) == -1);
    assert(ringl_get_error() == RINGL_INVALID_OPERATION);
    assert(ringl_set_default_framebuffer_state(
               RINGL_RIN_GPU_IMAGE_UNDEFINED) == 0);
    assert(ringl_set_default_framebuffer_state(
               RINGL_RIN_GPU_IMAGE_COLOR_TARGET) == 0);
    assert(ringl_get_default_framebuffer_state(&framebuffer_state) == 0);
    assert(framebuffer_state == RINGL_RIN_GPU_IMAGE_COLOR_TARGET);
    assert(ringl_set_default_framebuffer_state(
               RINGL_RIN_GPU_IMAGE_PRESENT) == 0);
    assert(ringl_set_default_framebuffer_state(99u) == -1);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);

    memset(&copy, 0, sizeof(copy));
    assert(ringl_get_default_framebuffer(&copy) == 0);
    assert(copy.color_target == framebuffer.color_target);
    assert(copy.color_format == framebuffer.color_format);
    assert(copy.width == framebuffer.width);
    assert(copy.height == framebuffer.height);

    framebuffer.depth_target = 92u;
    framebuffer.depth_format = RINGL_RIN_GPU_FORMAT_D32_FLOAT;
    assert(ringl_set_default_framebuffer(&framebuffer) == 0);
    assert(ringl_get_default_depth_framebuffer_state(&framebuffer_state) == 0);
    assert(framebuffer_state == RINGL_RIN_GPU_IMAGE_UNDEFINED);
    assert(ringl_set_default_depth_framebuffer_state(
               RINGL_RIN_GPU_IMAGE_DEPTH_TARGET) == 0);
    assert(ringl_get_default_depth_framebuffer_state(&framebuffer_state) == 0);
    assert(framebuffer_state == RINGL_RIN_GPU_IMAGE_DEPTH_TARGET);
    assert(ringl_set_default_depth_framebuffer_state(
               RINGL_RIN_GPU_IMAGE_PRESENT) == -1);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);

    framebuffer.width = 0u;
    assert(ringl_set_default_framebuffer(&framebuffer) == -1);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    memset(&copy, 0, sizeof(copy));
    assert(ringl_get_default_framebuffer(&copy) == 0);
    assert(copy.width == 800u);

    assert(ringl_set_default_framebuffer(NULL) == 0);
    memset(&copy, 0xff, sizeof(copy));
    assert(ringl_get_default_framebuffer(&copy) == 1);
    assert(copy.color_target == 0u);

    ringl_context_destroy(context);
    return 0;
}
