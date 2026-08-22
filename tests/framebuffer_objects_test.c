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
    uint32_t framebuffer = 0u;
    uint32_t texture = 0u;
    uint32_t depth_texture = 0u;
    uint32_t renderbuffer = 0u;
    RinGLFramebufferAttachmentInfoV1 attachment;
    int32_t value = -1;

    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_make_current(context) == 0);

    ringl_gen_framebuffers(1, &framebuffer);
    assert(framebuffer != 0u);
    assert(!ringl_is_framebuffer(framebuffer));
    ringl_bind_framebuffer(RINGL_FRAMEBUFFER, framebuffer);
    assert(ringl_is_framebuffer(framebuffer));
    assert(ringl_get_bound_framebuffer(RINGL_FRAMEBUFFER) == framebuffer);
    ringl_get_integerv(RINGL_FRAMEBUFFER_BINDING, &value);
    assert((uint32_t)value == framebuffer);

    attachment = attachment_info();
    assert(ringl_get_framebuffer_color_attachment(&attachment) == 0);
    assert(attachment.kind == RINGL_FRAMEBUFFER_ATTACHMENT_NONE);
    assert(attachment.object == 0u);
    assert(ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) ==
           RINGL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT);

    ringl_gen_textures(1, &texture);
    ringl_bind_texture(RINGL_TEXTURE_2D, texture);
    ringl_framebuffer_texture_2d(RINGL_FRAMEBUFFER, RINGL_COLOR_ATTACHMENT0,
                                 RINGL_TEXTURE_2D, texture, 0);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) ==
           RINGL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT);
    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_RGBA, 1, 1, 0,
                       RINGL_RGBA, RINGL_UNSIGNED_BYTE, NULL);
    assert(ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) ==
           RINGL_FRAMEBUFFER_COMPLETE);
    attachment = attachment_info();
    assert(ringl_get_framebuffer_color_attachment(&attachment) == 0);
    assert(attachment.kind == RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D);
    assert(attachment.object == texture);
    assert(attachment.level == 0);

    ringl_gen_textures(1, &depth_texture);
    ringl_bind_texture(RINGL_TEXTURE_2D, depth_texture);
    ringl_framebuffer_texture_2d(RINGL_FRAMEBUFFER, RINGL_DEPTH_ATTACHMENT,
                                 RINGL_TEXTURE_2D, depth_texture, 0);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) ==
           RINGL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT);
    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_DEPTH_COMPONENT32F, 1, 1,
                       0, RINGL_DEPTH_COMPONENT, RINGL_FLOAT, NULL);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) ==
           RINGL_FRAMEBUFFER_COMPLETE);
    ringl_tex_sub_image_2d(RINGL_TEXTURE_2D, 0, 0, 0, 1, 1,
                           RINGL_DEPTH_COMPONENT, RINGL_FLOAT,
                           &(float){ 0.5f });
    assert(ringl_get_error() == RINGL_NO_ERROR);

    ringl_framebuffer_texture_2d(
        RINGL_FRAMEBUFFER, RINGL_DEPTH_STENCIL_ATTACHMENT, RINGL_TEXTURE_2D,
        depth_texture, 0);
    assert(ringl_get_error() == RINGL_INVALID_OPERATION);
    ringl_framebuffer_texture_2d(RINGL_FRAMEBUFFER, RINGL_DEPTH_ATTACHMENT,
                                 RINGL_TEXTURE_2D, 0u, 0);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) ==
           RINGL_FRAMEBUFFER_COMPLETE);
    ringl_delete_textures(1, &depth_texture);

    ringl_framebuffer_texture_2d(RINGL_FRAMEBUFFER, RINGL_COLOR_ATTACHMENT0,
                                 RINGL_TEXTURE_2D, texture, 1);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    attachment = attachment_info();
    assert(ringl_get_framebuffer_color_attachment(&attachment) == 0);
    assert(attachment.kind == RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D);
    assert(attachment.object == texture);

    ringl_delete_textures(1, &texture);
    attachment = attachment_info();
    assert(ringl_get_framebuffer_color_attachment(&attachment) == 0);
    assert(attachment.kind == RINGL_FRAMEBUFFER_ATTACHMENT_NONE);

    ringl_gen_renderbuffers(1, &renderbuffer);
    assert(renderbuffer != 0u);
    assert(!ringl_is_renderbuffer(renderbuffer));
    ringl_bind_renderbuffer(RINGL_RENDERBUFFER, renderbuffer);
    assert(ringl_is_renderbuffer(renderbuffer));
    assert(ringl_get_bound_renderbuffer(RINGL_RENDERBUFFER) == renderbuffer);
    ringl_get_integerv(RINGL_RENDERBUFFER_BINDING, &value);
    assert((uint32_t)value == renderbuffer);
    ringl_renderbuffer_storage(RINGL_RENDERBUFFER, RINGL_RGBA8, 32, 16);
    assert(ringl_get_error() == RINGL_NO_ERROR);

    ringl_framebuffer_renderbuffer(RINGL_FRAMEBUFFER, RINGL_COLOR_ATTACHMENT0,
                                   RINGL_RENDERBUFFER, renderbuffer);
    assert(ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) ==
           RINGL_FRAMEBUFFER_COMPLETE);
    attachment = attachment_info();
    assert(ringl_get_framebuffer_color_attachment(&attachment) == 0);
    assert(attachment.kind == RINGL_FRAMEBUFFER_ATTACHMENT_RENDERBUFFER);
    assert(attachment.object == renderbuffer);

    ringl_delete_renderbuffers(1, &renderbuffer);
    attachment = attachment_info();
    assert(ringl_get_framebuffer_color_attachment(&attachment) == 0);
    assert(attachment.kind == RINGL_FRAMEBUFFER_ATTACHMENT_NONE);
    assert(ringl_get_bound_renderbuffer(RINGL_RENDERBUFFER) == 0u);

    ringl_bind_framebuffer(0u, framebuffer);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);

    ringl_delete_framebuffers(1, &framebuffer);
    assert(!ringl_is_framebuffer(framebuffer));
    assert(ringl_get_bound_framebuffer(RINGL_FRAMEBUFFER) == 0u);

    ringl_context_destroy(context);
    return 0;
}
