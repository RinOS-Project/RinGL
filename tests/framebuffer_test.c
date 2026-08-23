/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <string.h>

#include <ringl/ringl.h>

static RinGLFramebufferAttachmentInfoV1 attachment_info(void)
{
    RinGLFramebufferAttachmentInfoV1 info;

    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    info.api_version = RINGL_API_VERSION;
    return info;
}

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
    uint32_t framebuffers[1] = { 0u };
    uint32_t renderbuffers[2] = { 0u, 0u };
    RinGLFramebufferAttachmentInfoV1 attachment;
    RinGLRenderbufferInfoV1 renderbuffer_info = {
        .struct_size = sizeof(renderbuffer_info),
        .api_version = RINGL_API_VERSION,
    };

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

    ringl_gen_renderbuffers(2, renderbuffers);
    ringl_bind_renderbuffer(RINGL_RENDERBUFFER, renderbuffers[0]);
    ringl_renderbuffer_storage(RINGL_RENDERBUFFER, RINGL_RGBA8, 4, 4);
    ringl_bind_renderbuffer(RINGL_RENDERBUFFER, renderbuffers[1]);
    ringl_renderbuffer_storage(RINGL_RENDERBUFFER, RINGL_DEPTH_COMPONENT16,
                               4, 4);
    ringl_gen_framebuffers(1, framebuffers);
    ringl_bind_framebuffer(RINGL_FRAMEBUFFER, framebuffers[0]);
    ringl_framebuffer_renderbuffer(RINGL_FRAMEBUFFER, RINGL_COLOR_ATTACHMENT0,
                                   RINGL_RENDERBUFFER, renderbuffers[0]);
    ringl_framebuffer_renderbuffer(RINGL_FRAMEBUFFER, RINGL_DEPTH_ATTACHMENT,
                                   RINGL_RENDERBUFFER, renderbuffers[1]);
    assert(ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) ==
           RINGL_FRAMEBUFFER_COMPLETE);
    assert(ringl_get_renderbuffer_info(RINGL_RENDERBUFFER,
                                       &renderbuffer_info) == 0);
    assert(renderbuffer_info.internal_format == RINGL_DEPTH_COMPONENT16);
    assert(renderbuffer_info.depth_size == 16u);
    assert(renderbuffer_info.stencil_size == 0u);
    ringl_bind_renderbuffer(RINGL_RENDERBUFFER, renderbuffers[1]);
    ringl_renderbuffer_storage(RINGL_RENDERBUFFER, RINGL_DEPTH_COMPONENT32F,
                               2, 4);
    assert(ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) ==
           RINGL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT);
    ringl_renderbuffer_storage(RINGL_RENDERBUFFER, RINGL_DEPTH_COMPONENT32F,
                               4, 4);
    assert(ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) ==
           RINGL_FRAMEBUFFER_COMPLETE);
    ringl_framebuffer_renderbuffer(RINGL_FRAMEBUFFER, RINGL_DEPTH_ATTACHMENT,
                                   RINGL_RENDERBUFFER, 0u);
    assert(ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) ==
           RINGL_FRAMEBUFFER_COMPLETE);
    ringl_bind_renderbuffer(RINGL_RENDERBUFFER, renderbuffers[1]);
    ringl_renderbuffer_storage(RINGL_RENDERBUFFER, RINGL_DEPTH24_STENCIL8,
                               4, 4);
    ringl_framebuffer_renderbuffer(RINGL_FRAMEBUFFER,
                                   RINGL_DEPTH_STENCIL_ATTACHMENT,
                                   RINGL_RENDERBUFFER, renderbuffers[1]);
    assert(ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) ==
           RINGL_FRAMEBUFFER_COMPLETE);
    attachment = attachment_info();
    assert(ringl_get_framebuffer_attachment(
               RINGL_DEPTH_STENCIL_ATTACHMENT, &attachment) == 0);
    assert(attachment.kind == RINGL_FRAMEBUFFER_ATTACHMENT_RENDERBUFFER);
    assert(attachment.object == renderbuffers[1]);
    assert(attachment.level == 0);
    ringl_framebuffer_renderbuffer(RINGL_FRAMEBUFFER, RINGL_DEPTH_ATTACHMENT,
                                   RINGL_RENDERBUFFER, renderbuffers[1]);
    assert(ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) ==
           RINGL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT);
    ringl_framebuffer_renderbuffer(RINGL_FRAMEBUFFER,
                                   RINGL_STENCIL_ATTACHMENT,
                                   RINGL_RENDERBUFFER, renderbuffers[1]);
    assert(ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) ==
           RINGL_FRAMEBUFFER_COMPLETE);
    ringl_framebuffer_renderbuffer(RINGL_FRAMEBUFFER,
                                   RINGL_STENCIL_ATTACHMENT,
                                   RINGL_RENDERBUFFER, 0u);
    assert(ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) ==
           RINGL_FRAMEBUFFER_COMPLETE);
    ringl_bind_framebuffer(RINGL_FRAMEBUFFER, 0u);

    ringl_context_destroy(context);
    return 0;
}
