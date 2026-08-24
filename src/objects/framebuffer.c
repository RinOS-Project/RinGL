/* SPDX-License-Identifier: MIT */
#include "../ringl_internal.h"

#include <string.h>

static int framebuffer_target_valid(uint32_t target)
{
    return target == RINGL_FRAMEBUFFER;
}

static int renderbuffer_target_valid(uint32_t target)
{
    return target == RINGL_RENDERBUFFER;
}

static int color_attachment_valid(uint32_t attachment)
{
    return attachment >= RINGL_COLOR_ATTACHMENT0 &&
           attachment < RINGL_COLOR_ATTACHMENT0 + RINGL_MAX_COLOR_ATTACHMENTS;
}

static uint32_t color_attachment_index(uint32_t attachment)
{
    return attachment - RINGL_COLOR_ATTACHMENT0;
}

static int color_attachment_format_valid(uint32_t format)
{
    return format == RINGL_RGBA8 || format == RINGL_RGB565 ||
           format == RINGL_RGBA4 || format == RINGL_RGB5_A1 ||
           format == RINGL_SRGB8_ALPHA8_EXT ||
           format == RINGL_RGB16F ||
           format == RINGL_RGBA16F ||
           format == RINGL_RGBA32F;
}

static uint32_t color_attachment_ringpu_format(uint32_t format)
{
    if (format == RINGL_RGB565)
        return RINGL_RIN_GPU_FORMAT_RGB565_UNORM;
    if (format == RINGL_RGBA4)
        return RINGL_RIN_GPU_FORMAT_RGBA4_UNORM;
    if (format == RINGL_RGB5_A1)
        return RINGL_RIN_GPU_FORMAT_RGB5_A1_UNORM;
    if (format == RINGL_RGB16F || format == RINGL_RGBA16F)
        return RINGL_RIN_GPU_FORMAT_RGBA16_FLOAT;
    if (format == RINGL_RGBA32F)
        return RINGL_RIN_GPU_FORMAT_RGBA32_FLOAT;
    if (format == RINGL_SRGB8_ALPHA8_EXT)
        return RINGL_RIN_GPU_FORMAT_RGBA32_FLOAT;
    return RINGL_RIN_GPU_FORMAT_RGBA8_UNORM;
}

static int depth_attachment_valid(uint32_t attachment)
{
    return attachment == RINGL_DEPTH_ATTACHMENT ||
           attachment == RINGL_STENCIL_ATTACHMENT ||
           attachment == RINGL_DEPTH_STENCIL_ATTACHMENT;
}

static int depth_attachment_format_valid(uint32_t format,
                                         uint32_t has_depth,
                                         uint32_t has_stencil)
{
    if (has_depth == 0u && has_stencil == 0u)
        return 0;
    if (format == RINGL_DEPTH_COMPONENT16 ||
        format == RINGL_DEPTH_COMPONENT32F)
        return has_depth != 0u && has_stencil == 0u;
    if (format == RINGL_STENCIL_INDEX8)
        return has_depth == 0u && has_stencil != 0u;
    return format == RINGL_DEPTH24_STENCIL8;
}

static RinGLFramebufferObject* bound_framebuffer(RinGLContext* context)
{
    uint32_t index;

    if (context == NULL || context->framebuffer_binding == 0u ||
        ringl_object_lookup(context, context->framebuffer_binding,
                            RINGL_OBJECT_FRAMEBUFFER) == NULL) {
        return NULL;
    }
    index = ringl_object_slot_index(context->framebuffer_binding);
    if (index >= RINGL_OBJECT_SLOT_COUNT)
        return NULL;
    return &context->framebuffers[index];
}

static RinGLRenderbufferObject* bound_renderbuffer(RinGLContext* context)
{
    uint32_t index;

    if (context == NULL || context->renderbuffer_binding == 0u ||
        ringl_object_lookup(context, context->renderbuffer_binding,
                            RINGL_OBJECT_RENDERBUFFER) == NULL) {
        return NULL;
    }
    index = ringl_object_slot_index(context->renderbuffer_binding);
    if (index >= RINGL_OBJECT_SLOT_COUNT)
        return NULL;
    return &context->renderbuffers[index];
}

static void reset_color_attachment(RinGLFramebufferObject* framebuffer,
                                   uint32_t attachment_index)
{
    if (framebuffer == NULL || attachment_index >= RINGL_MAX_COLOR_ATTACHMENTS)
        return;
    framebuffer->color_attachment_kind[attachment_index] =
        RINGL_FRAMEBUFFER_ATTACHMENT_NONE;
    framebuffer->color_attachment_object[attachment_index] = 0u;
    framebuffer->color_attachment_level[attachment_index] = 0;
}

static void reset_depth_attachment(RinGLFramebufferObject* framebuffer)
{
    if (framebuffer == NULL)
        return;
    framebuffer->depth_attachment_kind = RINGL_FRAMEBUFFER_ATTACHMENT_NONE;
    framebuffer->depth_attachment_object = 0u;
    framebuffer->depth_attachment_level = 0;
    framebuffer->depth_attachment_has_depth = RINGL_FALSE;
    framebuffer->depth_attachment_has_stencil = RINGL_FALSE;
}

static void reset_stencil_attachment(RinGLFramebufferObject* framebuffer)
{
    if (framebuffer == NULL)
        return;
    framebuffer->stencil_attachment_kind = RINGL_FRAMEBUFFER_ATTACHMENT_NONE;
    framebuffer->stencil_attachment_object = 0u;
    framebuffer->stencil_attachment_level = 0;
}

static int texture_attachment_dimensions(const RinGLTextureObject* texture,
                                         int32_t level, uint32_t* width_out,
                                         uint32_t* height_out)
{
    const RinGLTextureMipStorage* storage;

    if (texture == NULL || width_out == NULL || height_out == NULL ||
        level < 0 || (uint32_t)level >= RINGL_MAX_TEXTURE_MIP_LEVELS)
        return -1;
    if (level == 0) {
        if (!texture->defined || texture->width == 0u || texture->height == 0u ||
            texture->shadow_bytes == NULL || texture->shadow_size == 0u)
            return -1;
        *width_out = texture->width;
        *height_out = texture->height;
        return 0;
    }
    storage = &texture->mip_storage[(uint32_t)level - 1u];
    if (!storage->defined || storage->width == 0u || storage->height == 0u ||
        storage->shadow_bytes == NULL || storage->shadow_size == 0u)
        return -1;
    *width_out = storage->width;
    *height_out = storage->height;
    return 0;
}

static int nonzero_color_mip_backend_supported(const RinGLContext* context)
{
    return context != NULL && context->ringpu_ops.create_image_2d_mip_v2 != NULL &&
        context->ringpu_ops.upload_image_2d_mip_v2 != NULL &&
        context->ringpu_ops.transition_image_2d_mip_v2 != NULL &&
        context->ringpu_ops.begin_render_pass_mip_v2 != NULL &&
        context->ringpu_ops.draw_vertices_mip_v3 != NULL &&
        context->ringpu_ops.draw_vertices_bindings_mip_v3 != NULL &&
        context->ringpu_ops.draw_indexed_mip_v3 != NULL &&
        context->ringpu_ops.draw_indexed_bindings_mip_v3 != NULL;
}

static int nonzero_depth_stencil_mip_backend_supported(
    const RinGLContext* context, const RinGLFramebufferObject* framebuffer)
{
    int combined;

    if (context == NULL || framebuffer == NULL ||
        context->ringpu_ops.create_image_2d_mip_v2 == NULL ||
        context->ringpu_ops.upload_image_2d_mip_v2 == NULL ||
        context->ringpu_ops.transition_image_2d_mip_v2 == NULL)
        return 0;
    combined = framebuffer->depth_attachment_kind !=
                    RINGL_FRAMEBUFFER_ATTACHMENT_NONE &&
        framebuffer->stencil_attachment_kind !=
                    RINGL_FRAMEBUFFER_ATTACHMENT_NONE &&
        framebuffer->depth_attachment_kind ==
            framebuffer->stencil_attachment_kind &&
        framebuffer->depth_attachment_object ==
            framebuffer->stencil_attachment_object &&
        framebuffer->depth_attachment_level ==
            framebuffer->stencil_attachment_level;
    return combined
        ? context->ringpu_ops.begin_render_pass_depth_mip_v2 != NULL
        : (framebuffer->depth_attachment_kind ==
                   RINGL_FRAMEBUFFER_ATTACHMENT_NONE ||
           framebuffer->stencil_attachment_kind ==
                   RINGL_FRAMEBUFFER_ATTACHMENT_NONE)
            ? context->ringpu_ops.begin_render_pass_depth_mip_v2 != NULL
            : context->ringpu_ops.begin_render_pass_depth_stencil_mip_v2 !=
                NULL;
}

static int color_attachment_dimensions(RinGLContext* context,
                                       const RinGLFramebufferObject* framebuffer,
                                       uint32_t attachment_index,
                                       uint32_t* width_out, uint32_t* height_out)
{
    uint32_t index;

    if (context == NULL || framebuffer == NULL || width_out == NULL ||
        height_out == NULL || attachment_index >= RINGL_MAX_COLOR_ATTACHMENTS)
        return -1;
    if (framebuffer->color_attachment_kind[attachment_index] ==
        RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D) {
        RinGLTextureObject* texture;

        if (ringl_object_lookup(context,
                                framebuffer->color_attachment_object[attachment_index],
                                RINGL_OBJECT_TEXTURE) == NULL)
            return -1;
        index = ringl_object_slot_index(
            framebuffer->color_attachment_object[attachment_index]);
        if (index >= RINGL_OBJECT_SLOT_COUNT)
            return -1;
        texture = &context->textures[index];
        if ((texture->format != RINGL_RGBA &&
             texture->srgb_encoding == 0u &&
            !color_attachment_format_valid(texture->format)) ||
            texture_attachment_dimensions(texture,
                                          framebuffer->color_attachment_level[
                                              attachment_index],
                                          width_out, height_out) != 0)
            return -1;
        return 0;
    }
    if (framebuffer->color_attachment_kind[attachment_index] ==
        RINGL_FRAMEBUFFER_ATTACHMENT_RENDERBUFFER) {
        RinGLRenderbufferObject* renderbuffer;

        if (ringl_object_lookup(context,
                                framebuffer->color_attachment_object[attachment_index],
                                RINGL_OBJECT_RENDERBUFFER) == NULL)
            return -1;
        index = ringl_object_slot_index(
            framebuffer->color_attachment_object[attachment_index]);
        if (index >= RINGL_OBJECT_SLOT_COUNT)
            return -1;
        renderbuffer = &context->renderbuffers[index];
        if (!renderbuffer->defined ||
            !color_attachment_format_valid(renderbuffer->internal_format) ||
            renderbuffer->width == 0u || renderbuffer->height == 0u)
            return -1;
        *width_out = renderbuffer->width;
        *height_out = renderbuffer->height;
        return 0;
    }
    return -1;
}

static int color_attachment_ringpu_format_for(
    const RinGLContext* context, const RinGLFramebufferObject* framebuffer,
    uint32_t attachment_index, uint32_t* format_out)
{
    uint32_t object_index;

    if (context == NULL || framebuffer == NULL || format_out == NULL ||
        attachment_index >= RINGL_MAX_COLOR_ATTACHMENTS)
        return -1;
    object_index = ringl_object_slot_index(
        framebuffer->color_attachment_object[attachment_index]);
    if (object_index >= RINGL_OBJECT_SLOT_COUNT)
        return -1;
    if (framebuffer->color_attachment_kind[attachment_index] ==
        RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D) {
        const RinGLTextureObject* texture = &context->textures[object_index];

        if (texture->format == RINGL_RGBA || texture->srgb_encoding != 0u) {
            *format_out = texture->color_component_type == RINGL_FLOAT
                ? RINGL_RIN_GPU_FORMAT_RGBA32_FLOAT
                : texture->color_component_type == RINGL_HALF_FLOAT_OES
                    ? RINGL_RIN_GPU_FORMAT_RGBA16_FLOAT
                    : RINGL_RIN_GPU_FORMAT_RGBA8_UNORM;
            return 0;
        }
        *format_out = color_attachment_ringpu_format(texture->format);
        return 0;
    }
    if (framebuffer->color_attachment_kind[attachment_index] ==
        RINGL_FRAMEBUFFER_ATTACHMENT_RENDERBUFFER) {
        *format_out = color_attachment_ringpu_format(
            context->renderbuffers[object_index].storage_format != 0u
                ? context->renderbuffers[object_index].storage_format
                : context->renderbuffers[object_index].internal_format);
        return 0;
    }
    return -1;
}

static int depth_attachment_dimensions(RinGLContext* context,
                                       const RinGLFramebufferObject* framebuffer,
                                       uint32_t* width_out, uint32_t* height_out)
{
    uint32_t index;

    if (context == NULL || framebuffer == NULL || width_out == NULL ||
        height_out == NULL)
        return -1;
    if (framebuffer->depth_attachment_kind ==
        RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D) {
        RinGLTextureObject* texture;

        if (ringl_object_lookup(context, framebuffer->depth_attachment_object,
                                RINGL_OBJECT_TEXTURE) == NULL)
            return -1;
        index = ringl_object_slot_index(framebuffer->depth_attachment_object);
        if (index >= RINGL_OBJECT_SLOT_COUNT)
            return -1;
        texture = &context->textures[index];
        if (!texture->defined ||
            !depth_attachment_format_valid(
                texture->format, framebuffer->depth_attachment_has_depth,
                framebuffer->depth_attachment_has_stencil) ||
             texture_attachment_dimensions(texture,
                                           framebuffer->depth_attachment_level,
                                           width_out, height_out) != 0)
            return -1;
        return 0;
    }
    if (framebuffer->depth_attachment_kind !=
            RINGL_FRAMEBUFFER_ATTACHMENT_RENDERBUFFER ||
        ringl_object_lookup(context, framebuffer->depth_attachment_object,
                            RINGL_OBJECT_RENDERBUFFER) == NULL)
        return -1;
    {
        RinGLRenderbufferObject* renderbuffer;

        index = ringl_object_slot_index(framebuffer->depth_attachment_object);
        if (index >= RINGL_OBJECT_SLOT_COUNT)
            return -1;
        renderbuffer = &context->renderbuffers[index];
        if (!renderbuffer->defined ||
            !depth_attachment_format_valid(
                renderbuffer->internal_format,
                framebuffer->depth_attachment_has_depth,
                framebuffer->depth_attachment_has_stencil) ||
            renderbuffer->width == 0u || renderbuffer->height == 0u)
            return -1;
        *width_out = renderbuffer->width;
        *height_out = renderbuffer->height;
    }
    return 0;
}

static int stencil_attachment_dimensions(
    RinGLContext* context, const RinGLFramebufferObject* framebuffer,
    uint32_t* width_out, uint32_t* height_out)
{
    uint32_t index;

    if (context == NULL || framebuffer == NULL || width_out == NULL ||
        height_out == NULL)
        return -1;
    if (framebuffer->stencil_attachment_kind ==
        RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D) {
        RinGLTextureObject* texture;

        if (ringl_object_lookup(context, framebuffer->stencil_attachment_object,
                                RINGL_OBJECT_TEXTURE) == NULL)
            return -1;
        index = ringl_object_slot_index(framebuffer->stencil_attachment_object);
        if (index >= RINGL_OBJECT_SLOT_COUNT)
            return -1;
        texture = &context->textures[index];
        if (!texture->defined ||
            !depth_attachment_format_valid(texture->format, RINGL_FALSE,
                                           RINGL_TRUE) ||
            texture_attachment_dimensions(texture,
                                          framebuffer->stencil_attachment_level,
                                          width_out, height_out) != 0)
            return -1;
        return 0;
    }
    if (framebuffer->stencil_attachment_kind !=
            RINGL_FRAMEBUFFER_ATTACHMENT_RENDERBUFFER ||
        ringl_object_lookup(context, framebuffer->stencil_attachment_object,
                            RINGL_OBJECT_RENDERBUFFER) == NULL)
        return -1;
    {
        RinGLRenderbufferObject* renderbuffer;

        index = ringl_object_slot_index(framebuffer->stencil_attachment_object);
        if (index >= RINGL_OBJECT_SLOT_COUNT)
            return -1;
        renderbuffer = &context->renderbuffers[index];
        if (!renderbuffer->defined ||
            !depth_attachment_format_valid(renderbuffer->internal_format,
                                           RINGL_FALSE, RINGL_TRUE) ||
            renderbuffer->width == 0u || renderbuffer->height == 0u)
            return -1;
        *width_out = renderbuffer->width;
        *height_out = renderbuffer->height;
    }
    return 0;
}

static void generate_objects(RinGLContext* context, int32_t count,
                             uint32_t* objects, RinGLObjectType type)
{
    int32_t index;

    if (count < 0) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (count == 0)
        return;
    if (objects == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }

    for (index = 0; index < count; ++index)
        objects[index] = 0u;
    for (index = 0; index < count; ++index) {
        objects[index] = ringl_object_allocate(context, type);
        if (objects[index] == 0u) {
            int32_t rollback;

            for (rollback = 0; rollback < index; ++rollback) {
                ringl_object_release(context, objects[rollback], type);
                objects[rollback] = 0u;
            }
            ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
            return;
        }
    }
}

void ringl_gen_framebuffers(int32_t count, uint32_t* framebuffers)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL)
        return;
    generate_objects(context, count, framebuffers, RINGL_OBJECT_FRAMEBUFFER);
}

void ringl_delete_framebuffers(int32_t count, const uint32_t* framebuffers)
{
    RinGLContext* context = ringl_get_current_context();
    int32_t item;

    if (context == NULL)
        return;
    if (count < 0) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (count == 0)
        return;
    if (framebuffers == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }

    for (item = 0; item < count; ++item) {
        uint32_t name = framebuffers[item];
        uint32_t index;

        if (name == 0u ||
            ringl_object_lookup(context, name, RINGL_OBJECT_FRAMEBUFFER) == NULL)
            continue;
        if (context->framebuffer_binding == name)
            context->framebuffer_binding = 0u;
        index = ringl_object_slot_index(name);
        if (index < RINGL_OBJECT_SLOT_COUNT)
            memset(&context->framebuffers[index], 0,
                   sizeof(context->framebuffers[index]));
        ringl_object_release(context, name, RINGL_OBJECT_FRAMEBUFFER);
        ringl_context_mark_dirty(context, RINGL_DIRTY_FRAMEBUFFER);
    }
}

void ringl_bind_framebuffer(uint32_t target, uint32_t framebuffer)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLObjectSlot* slot;

    if (context == NULL)
        return;
    if (!framebuffer_target_valid(target)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (framebuffer != 0u) {
        slot = ringl_object_lookup(context, framebuffer,
                                   RINGL_OBJECT_FRAMEBUFFER);
        if (slot == NULL) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return;
        }
        ringl_object_promote(slot);
    }
    if (context->framebuffer_binding != framebuffer) {
        context->framebuffer_binding = framebuffer;
        ringl_context_mark_dirty(context, RINGL_DIRTY_FRAMEBUFFER);
    }
}

int ringl_is_framebuffer(uint32_t framebuffer)
{
    RinGLContext* context = ringl_get_current_context();
    const RinGLObjectSlot* slot;

    if (context == NULL || framebuffer == 0u)
        return 0;
    slot = ringl_object_lookup_const(context, framebuffer,
                                     RINGL_OBJECT_FRAMEBUFFER);
    return slot != NULL && slot->state == RINGL_OBJECT_LIVE;
}

uint32_t ringl_get_bound_framebuffer(uint32_t target)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL)
        return 0u;
    if (!framebuffer_target_valid(target)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return 0u;
    }
    return context->framebuffer_binding;
}

void ringl_draw_buffers(int32_t count, const uint32_t* buffers)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLFramebufferObject* framebuffer;
    uint32_t mask = 0u;

    if (context == NULL)
        return;
    if (context->webgl_draw_buffers_enabled == RINGL_FALSE) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if (count < 0 || (uint32_t)count > RINGL_MAX_COLOR_ATTACHMENTS ||
        (count > 0 && buffers == NULL)) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (context->framebuffer_binding == 0u) {
        if (count != 1 || (buffers[0] != RINGL_BACK &&
                           buffers[0] != RINGL_NONE)) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return;
        }
        if (context->default_draw_buffer == buffers[0])
            return;
        context->default_draw_buffer = buffers[0];
        ringl_context_mark_dirty(context,
                                 RINGL_DIRTY_FRAMEBUFFER | RINGL_DIRTY_PIPELINE);
        return;
    }
    framebuffer = bound_framebuffer(context);
    if (framebuffer == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    for (uint32_t index = 0u; index < (uint32_t)count; ++index) {
        if (buffers[index] == RINGL_NONE)
            continue;
        if (buffers[index] != RINGL_COLOR_ATTACHMENT0 + index) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return;
        }
        mask |= UINT32_C(1) << index;
    }
    framebuffer->draw_buffer_mask = mask;
    framebuffer->draw_buffer_state_initialized = RINGL_TRUE;
    ringl_context_mark_dirty(context,
                             RINGL_DIRTY_FRAMEBUFFER | RINGL_DIRTY_PIPELINE);
}

uint32_t ringl_effective_color_write_mask(const RinGLContext* context)
{
    const RinGLFramebufferObject* framebuffer;
    uint32_t index;

    if (context == NULL)
        return 0u;
    if (context->webgl_draw_buffers_enabled == RINGL_FALSE)
        return context->color_write_mask;
    if (context->framebuffer_binding == 0u) {
        return context->default_draw_buffer == RINGL_BACK
            ? context->color_write_mask
            : 0u;
    }
    if (ringl_object_lookup_const(context, context->framebuffer_binding,
                                  RINGL_OBJECT_FRAMEBUFFER) == NULL)
        return 0u;
    index = ringl_object_slot_index(context->framebuffer_binding);
    if (index >= RINGL_OBJECT_SLOT_COUNT)
        return 0u;
    framebuffer = &context->framebuffers[index];
    if (framebuffer->draw_buffer_state_initialized != RINGL_FALSE &&
        framebuffer->draw_buffer_mask == 0u)
        return 0u;
    return context->color_write_mask;
}

void ringl_framebuffer_texture_2d(uint32_t target, uint32_t attachment,
                                  uint32_t textarget, uint32_t texture,
                                  int32_t level)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLFramebufferObject* framebuffer;
    uint32_t attachment_index = 0u;

    if (context == NULL)
        return;
    if (!framebuffer_target_valid(target) ||
        (!color_attachment_valid(attachment) && !depth_attachment_valid(attachment)) ||
        textarget != RINGL_TEXTURE_2D) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (color_attachment_valid(attachment)) {
        attachment_index = color_attachment_index(attachment);
        if (attachment_index != 0u &&
            context->webgl_draw_buffers_enabled == RINGL_FALSE) {
            ringl_context_record_error(context, RINGL_INVALID_ENUM);
            return;
        }
    }
    if (level < 0 || (uint32_t)level >= RINGL_MAX_TEXTURE_MIP_LEVELS) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    framebuffer = bound_framebuffer(context);
    if (framebuffer == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if (texture != 0u && !ringl_is_texture(texture)) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if (texture == 0u && color_attachment_valid(attachment)) {
        reset_color_attachment(framebuffer, attachment_index);
    } else if (texture == 0u && attachment == RINGL_DEPTH_ATTACHMENT) {
        reset_depth_attachment(framebuffer);
    } else if (texture == 0u && attachment == RINGL_STENCIL_ATTACHMENT) {
        reset_stencil_attachment(framebuffer);
    } else if (texture == 0u) {
        reset_depth_attachment(framebuffer);
        reset_stencil_attachment(framebuffer);
    } else if (color_attachment_valid(attachment)) {
        if (ringl_texture_require_color_target(context, texture) != 0) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return;
        }
        framebuffer->color_attachment_kind[attachment_index] =
            RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D;
        framebuffer->color_attachment_object[attachment_index] = texture;
        framebuffer->color_attachment_level[attachment_index] = level;
    } else {
        uint32_t texture_index = ringl_object_slot_index(texture);
        uint32_t has_depth = attachment != RINGL_STENCIL_ATTACHMENT;
        uint32_t has_stencil = attachment != RINGL_DEPTH_ATTACHMENT;

        if (texture_index >= RINGL_OBJECT_SLOT_COUNT ||
            (context->textures[texture_index].defined != 0u &&
             !depth_attachment_format_valid(context->textures[texture_index].format,
                                            has_depth, has_stencil))) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return;
        }
        if (has_depth != 0u) {
            framebuffer->depth_attachment_kind =
                RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D;
            framebuffer->depth_attachment_object = texture;
            framebuffer->depth_attachment_level = level;
            framebuffer->depth_attachment_has_depth = RINGL_TRUE;
            framebuffer->depth_attachment_has_stencil = has_stencil;
        }
        if (has_stencil != 0u) {
            framebuffer->stencil_attachment_kind =
                RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D;
            framebuffer->stencil_attachment_object = texture;
            framebuffer->stencil_attachment_level = level;
        }
    }
    ringl_context_mark_dirty(context, RINGL_DIRTY_FRAMEBUFFER);
}

int ringl_get_framebuffer_attachment(
    uint32_t attachment_point, RinGLFramebufferAttachmentInfoV1* attachment)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLFramebufferObject* framebuffer;
    uint32_t kind;
    uint32_t object;
    int32_t level;

    if (context == NULL || attachment == NULL)
        return -1;
    if (attachment->struct_size < sizeof(*attachment) ||
        attachment->api_version != RINGL_API_VERSION ||
        attachment->reserved0 != 0u) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return -1;
    }
    framebuffer = bound_framebuffer(context);
    if (framebuffer == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    if (color_attachment_valid(attachment_point)) {
        uint32_t attachment_index = color_attachment_index(attachment_point);

        if (attachment_index != 0u &&
            context->webgl_draw_buffers_enabled == RINGL_FALSE) {
            ringl_context_record_error(context, RINGL_INVALID_ENUM);
            return -1;
        }
        kind = framebuffer->color_attachment_kind[attachment_index];
        object = framebuffer->color_attachment_object[attachment_index];
        level = framebuffer->color_attachment_level[attachment_index];
    } else if (attachment_point == RINGL_DEPTH_ATTACHMENT &&
               framebuffer->depth_attachment_has_depth != 0u) {
        kind = framebuffer->depth_attachment_kind;
        object = framebuffer->depth_attachment_object;
        level = framebuffer->depth_attachment_level;
    } else if (attachment_point == RINGL_STENCIL_ATTACHMENT &&
               framebuffer->stencil_attachment_kind !=
                   RINGL_FRAMEBUFFER_ATTACHMENT_NONE) {
        kind = framebuffer->stencil_attachment_kind;
        object = framebuffer->stencil_attachment_object;
        level = framebuffer->stencil_attachment_level;
    } else if (attachment_point == RINGL_DEPTH_STENCIL_ATTACHMENT &&
               framebuffer->depth_attachment_has_depth != 0u &&
               framebuffer->stencil_attachment_kind ==
                   framebuffer->depth_attachment_kind &&
               framebuffer->stencil_attachment_object ==
                   framebuffer->depth_attachment_object &&
               framebuffer->stencil_attachment_level ==
                   framebuffer->depth_attachment_level) {
        kind = framebuffer->depth_attachment_kind;
        object = framebuffer->depth_attachment_object;
        level = framebuffer->depth_attachment_level;
    } else if (depth_attachment_valid(attachment_point)) {
        kind = RINGL_FRAMEBUFFER_ATTACHMENT_NONE;
        object = 0u;
        level = 0;
    } else {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return -1;
    }
    attachment->kind = kind;
    attachment->object = object;
    attachment->level = level;
    return 0;
}

int ringl_get_framebuffer_color_attachment(
    RinGLFramebufferAttachmentInfoV1* attachment)
{
    return ringl_get_framebuffer_attachment(RINGL_COLOR_ATTACHMENT0,
                                            attachment);
}

int ringl_framebuffer_color_attachment_component_type(uint32_t* type_out)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLFramebufferObject* framebuffer;
    uint32_t component_type = RINGL_UNSIGNED_BYTE;
    uint32_t index;

    if (context == NULL)
        return -1;
    if (type_out == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return -1;
    }
    /* The default drawing buffer is supplied by the embedding as a normalized
     * color target. It cannot become a Float target through this API. */
    if (context->framebuffer_binding == 0u) {
        *type_out = component_type;
        return 0;
    }
    framebuffer = bound_framebuffer(context);
    if (framebuffer == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    if (framebuffer->color_attachment_kind[0] ==
        RINGL_FRAMEBUFFER_ATTACHMENT_NONE) {
        *type_out = component_type;
        return 0;
    }
    index = ringl_object_slot_index(framebuffer->color_attachment_object[0]);
    if (index >= RINGL_OBJECT_SLOT_COUNT) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    if (framebuffer->color_attachment_kind[0] ==
        RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D) {
        RinGLTextureObject* texture;

        if (ringl_object_lookup(context, framebuffer->color_attachment_object[0],
                                RINGL_OBJECT_TEXTURE) == NULL) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return -1;
        }
        texture = &context->textures[index];
        if (!texture->defined) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return -1;
        }
        /* EXT_sRGB is logically normalized 8-bit storage even though RinGL
         * realizes a Float32 linear image for sampling and blending. */
        component_type = texture->srgb_encoding != 0u
            ? RINGL_UNSIGNED_BYTE : texture->color_component_type;
    } else if (framebuffer->color_attachment_kind[0] ==
               RINGL_FRAMEBUFFER_ATTACHMENT_RENDERBUFFER) {
        RinGLRenderbufferObject* renderbuffer;

        if (ringl_object_lookup(context, framebuffer->color_attachment_object[0],
                                RINGL_OBJECT_RENDERBUFFER) == NULL) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return -1;
        }
        index = ringl_object_slot_index(framebuffer->color_attachment_object[0]);
        if (index >= RINGL_OBJECT_SLOT_COUNT) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return -1;
        }
        renderbuffer = &context->renderbuffers[index];
        if (!renderbuffer->defined) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return -1;
        }
        component_type = renderbuffer->internal_format == RINGL_RGBA32F
            ? RINGL_FLOAT
            : (renderbuffer->internal_format == RINGL_RGB16F ||
               renderbuffer->internal_format == RINGL_RGBA16F)
                ? RINGL_HALF_FLOAT_OES : RINGL_UNSIGNED_BYTE;
    } else {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    *type_out = component_type;
    return 0;
}

int ringl_framebuffer_color_attachment_is_float(uint32_t* is_float_out)
{
    uint32_t component_type;

    if (is_float_out == NULL)
        return ringl_framebuffer_color_attachment_component_type(NULL);
    if (ringl_framebuffer_color_attachment_component_type(&component_type) !=
        0) {
        return -1;
    }
    *is_float_out = component_type == RINGL_FLOAT ? RINGL_TRUE : RINGL_FALSE;
    return 0;
}

int ringl_framebuffer_color_attachment_is_srgb(uint32_t* is_srgb_out)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLFramebufferObject* framebuffer;
    uint32_t index;

    if (context == NULL)
        return -1;
    if (is_srgb_out == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return -1;
    }
    *is_srgb_out = RINGL_FALSE;
    if (context->framebuffer_binding == 0u)
        return 0;
    framebuffer = bound_framebuffer(context);
    if (framebuffer == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    if (framebuffer->color_attachment_kind[0] ==
        RINGL_FRAMEBUFFER_ATTACHMENT_NONE) {
        return 0;
    }
    index = ringl_object_slot_index(framebuffer->color_attachment_object[0]);
    if (index >= RINGL_OBJECT_SLOT_COUNT) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    if (framebuffer->color_attachment_kind[0] ==
        RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D) {
        if (ringl_object_lookup(context, framebuffer->color_attachment_object[0],
                                RINGL_OBJECT_TEXTURE) == NULL) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return -1;
        }
        *is_srgb_out = context->textures[index].srgb_encoding != 0u
            ? RINGL_TRUE : RINGL_FALSE;
        return 0;
    }
    if (framebuffer->color_attachment_kind[0] ==
        RINGL_FRAMEBUFFER_ATTACHMENT_RENDERBUFFER) {
        if (ringl_object_lookup(context, framebuffer->color_attachment_object[0],
                                RINGL_OBJECT_RENDERBUFFER) == NULL) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return -1;
        }
        *is_srgb_out = context->renderbuffers[index].srgb_encoding != 0u
            ? RINGL_TRUE : RINGL_FALSE;
        return 0;
    }
    ringl_context_record_error(context, RINGL_INVALID_OPERATION);
    return -1;
}

uint32_t ringl_framebuffer_operation_error(const RinGLContext* context)
{
    if (context != NULL &&
        context->framebuffer_binding != 0u &&
        ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) !=
            RINGL_FRAMEBUFFER_COMPLETE) {
        return RINGL_INVALID_FRAMEBUFFER_OPERATION;
    }
    return RINGL_INVALID_OPERATION;
}

uint32_t ringl_check_framebuffer_status(uint32_t target)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLFramebufferObject* framebuffer;
    uint32_t color_width;
    uint32_t color_height;
    uint32_t color_format = 0u;
    uint32_t depth_width;
    uint32_t depth_height;
    uint32_t stencil_width;
    uint32_t stencil_height;
    uint32_t color_count = 0u;
    uint32_t color_mip_nonzero = RINGL_FALSE;

    if (context == NULL)
        return 0u;
    if (!framebuffer_target_valid(target)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return 0u;
    }
    if (context->framebuffer_binding == 0u) {
        return context->has_default_framebuffer
            ? RINGL_FRAMEBUFFER_COMPLETE
            : RINGL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
    }
    framebuffer = bound_framebuffer(context);
    if (framebuffer == NULL)
        return RINGL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
    for (uint32_t attachment = 0u;
         attachment < RINGL_MAX_COLOR_ATTACHMENTS; ++attachment) {
        uint32_t width;
        uint32_t height;
        uint32_t format;
        uint32_t object_index;

        if (framebuffer->color_attachment_kind[attachment] ==
            RINGL_FRAMEBUFFER_ATTACHMENT_NONE) {
            continue;
        }
        if (framebuffer->color_attachment_kind[attachment] ==
            RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D) {
            object_index = ringl_object_slot_index(
                framebuffer->color_attachment_object[attachment]);
            if (object_index >= RINGL_OBJECT_SLOT_COUNT)
                return RINGL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
            /* Compressed uploads are decoded only to make their sampler
             * contents executable. They remain logically compressed and are
             * never a renderable WebGL color attachment. Check before the
             * physical decoded-format validation so callers observe the
             * deliberate unsupported result rather than an implementation
             * accident. */
            if (context->textures[object_index].compressed_format != 0u)
                return RINGL_FRAMEBUFFER_UNSUPPORTED;
        }
        if (color_attachment_dimensions(context, framebuffer, attachment,
                                        &width, &height) != 0 ||
            color_attachment_ringpu_format_for(context, framebuffer,
                                                attachment, &format) != 0) {
            return RINGL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        }
        object_index = ringl_object_slot_index(
            framebuffer->color_attachment_object[attachment]);
        if (object_index >= RINGL_OBJECT_SLOT_COUNT)
            return RINGL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        /* RGB16F is not part of the guaranteed WebGL color-attachment
         * profile. Keep it rejected rather than silently widening to RGBA. */
        if ((framebuffer->color_attachment_kind[attachment] ==
                 RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D &&
             context->textures[object_index].format == RINGL_RGB &&
             context->textures[object_index].color_component_type ==
                 RINGL_HALF_FLOAT_OES) ||
            (framebuffer->color_attachment_kind[attachment] ==
                 RINGL_FRAMEBUFFER_ATTACHMENT_RENDERBUFFER &&
             context->renderbuffers[object_index].internal_format ==
                 RINGL_RGB16F)) {
            return RINGL_FRAMEBUFFER_UNSUPPORTED;
        }
        if (color_count == 0u) {
            color_width = width;
            color_height = height;
            color_format = format;
        } else if (color_width != width || color_height != height ||
                   color_format != format) {
            /* RinGPU's current graphics pipeline has one color format; do
             * not claim this heterogeneous FBO is executable. */
            return RINGL_FRAMEBUFFER_UNSUPPORTED;
        }
        for (uint32_t previous = 0u; previous < attachment; ++previous) {
            if (framebuffer->color_attachment_kind[previous] !=
                    RINGL_FRAMEBUFFER_ATTACHMENT_NONE &&
                framebuffer->color_attachment_kind[previous] ==
                    framebuffer->color_attachment_kind[attachment] &&
                framebuffer->color_attachment_object[previous] ==
                    framebuffer->color_attachment_object[attachment] &&
                framebuffer->color_attachment_level[previous] ==
                    framebuffer->color_attachment_level[attachment]) {
                return RINGL_FRAMEBUFFER_UNSUPPORTED;
            }
        }
        color_count++;
    }
    if (color_count == 0u)
        return RINGL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
    if (framebuffer->depth_attachment_kind !=
            RINGL_FRAMEBUFFER_ATTACHMENT_NONE &&
        (depth_attachment_dimensions(context, framebuffer, &depth_width,
                                     &depth_height) != 0 ||
         color_width != depth_width || color_height != depth_height))
        return RINGL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    if (framebuffer->stencil_attachment_kind !=
            RINGL_FRAMEBUFFER_ATTACHMENT_NONE &&
        (stencil_attachment_dimensions(context, framebuffer, &stencil_width,
                                       &stencil_height) != 0 ||
         color_width != stencil_width || color_height != stencil_height))
        return RINGL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    for (uint32_t attachment = 0u;
         attachment < RINGL_MAX_COLOR_ATTACHMENTS; ++attachment) {
        if (framebuffer->color_attachment_kind[attachment] ==
                RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D &&
            framebuffer->color_attachment_level[attachment] != 0) {
            color_mip_nonzero = RINGL_TRUE;
            if (!nonzero_color_mip_backend_supported(context))
                return RINGL_FRAMEBUFFER_UNSUPPORTED;
        }
    }
    if ((framebuffer->depth_attachment_kind ==
             RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D &&
         framebuffer->depth_attachment_level != 0) ||
        (framebuffer->stencil_attachment_kind ==
             RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D &&
         framebuffer->stencil_attachment_level != 0) ||
         ((framebuffer->depth_attachment_kind !=
               RINGL_FRAMEBUFFER_ATTACHMENT_NONE ||
           framebuffer->stencil_attachment_kind !=
               RINGL_FRAMEBUFFER_ATTACHMENT_NONE) &&
          color_mip_nonzero != RINGL_FALSE)) {
        if (!nonzero_depth_stencil_mip_backend_supported(context, framebuffer))
            return RINGL_FRAMEBUFFER_UNSUPPORTED;
    }
    return RINGL_FRAMEBUFFER_COMPLETE;
}

void ringl_gen_renderbuffers(int32_t count, uint32_t* renderbuffers)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL)
        return;
    generate_objects(context, count, renderbuffers, RINGL_OBJECT_RENDERBUFFER);
}

void ringl_delete_renderbuffers(int32_t count, const uint32_t* renderbuffers)
{
    RinGLContext* context = ringl_get_current_context();
    int32_t item;

    if (context == NULL)
        return;
    if (count < 0) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (count == 0)
        return;
    if (renderbuffers == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }

    for (item = 0; item < count; ++item) {
        uint32_t name = renderbuffers[item];
        uint32_t index;

        if (name == 0u ||
            ringl_object_lookup(context, name, RINGL_OBJECT_RENDERBUFFER) == NULL)
            continue;
        ringl_framebuffer_detach_renderbuffer(context, name);
        if (context->renderbuffer_binding == name)
            context->renderbuffer_binding = 0u;
        index = ringl_object_slot_index(name);
        if (index < RINGL_OBJECT_SLOT_COUNT)
            ringl_backend_destroy_object(
                context, context->renderbuffers[index].ringpu_image);
        if (index < RINGL_OBJECT_SLOT_COUNT)
            memset(&context->renderbuffers[index], 0,
                   sizeof(context->renderbuffers[index]));
        ringl_object_release(context, name, RINGL_OBJECT_RENDERBUFFER);
        ringl_context_mark_dirty(context, RINGL_DIRTY_FRAMEBUFFER);
    }
}

void ringl_bind_renderbuffer(uint32_t target, uint32_t renderbuffer)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLObjectSlot* slot;

    if (context == NULL)
        return;
    if (!renderbuffer_target_valid(target)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (renderbuffer != 0u) {
        slot = ringl_object_lookup(context, renderbuffer,
                                   RINGL_OBJECT_RENDERBUFFER);
        if (slot == NULL) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return;
        }
        ringl_object_promote(slot);
    }
    if (context->renderbuffer_binding != renderbuffer) {
        context->renderbuffer_binding = renderbuffer;
        ringl_context_mark_dirty(context, RINGL_DIRTY_FRAMEBUFFER);
    }
}

int ringl_is_renderbuffer(uint32_t renderbuffer)
{
    RinGLContext* context = ringl_get_current_context();
    const RinGLObjectSlot* slot;

    if (context == NULL || renderbuffer == 0u)
        return 0;
    slot = ringl_object_lookup_const(context, renderbuffer,
                                     RINGL_OBJECT_RENDERBUFFER);
    return slot != NULL && slot->state == RINGL_OBJECT_LIVE;
}

uint32_t ringl_get_bound_renderbuffer(uint32_t target)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL)
        return 0u;
    if (!renderbuffer_target_valid(target)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return 0u;
    }
    return context->renderbuffer_binding;
}

int ringl_get_renderbuffer_info(uint32_t target, RinGLRenderbufferInfoV1* info)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLRenderbufferObject* renderbuffer;
    RinGLRenderbufferInfoV1 result;

    if (context == NULL || info == NULL)
        return -1;
    if (info->struct_size < sizeof(*info) || info->api_version != RINGL_API_VERSION)
        return -1;
    if (!renderbuffer_target_valid(target)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return -1;
    }
    renderbuffer = bound_renderbuffer(context);
    if (renderbuffer == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }

    memset(&result, 0, sizeof(result));
    result.struct_size = sizeof(result);
    result.api_version = RINGL_API_VERSION;
    /* GLES exposes an unallocated renderbuffer as zero-sized RGBA4 state. */
    result.internal_format = RINGL_RGBA4;
    if (renderbuffer->defined) {
        result.width = renderbuffer->width;
        result.height = renderbuffer->height;
        result.internal_format = renderbuffer->internal_format;
        if (renderbuffer->internal_format == RINGL_RGBA8) {
            result.red_size = 8u;
            result.green_size = 8u;
            result.blue_size = 8u;
            result.alpha_size = 8u;
        } else if (renderbuffer->internal_format == RINGL_RGBA32F) {
            result.red_size = 32u;
            result.green_size = 32u;
            result.blue_size = 32u;
            result.alpha_size = 32u;
        } else if (renderbuffer->internal_format == RINGL_SRGB8_ALPHA8_EXT) {
            result.red_size = 8u;
            result.green_size = 8u;
            result.blue_size = 8u;
            result.alpha_size = 8u;
        } else if (renderbuffer->internal_format == RINGL_RGB16F) {
            result.red_size = 16u;
            result.green_size = 16u;
            result.blue_size = 16u;
        } else if (renderbuffer->internal_format == RINGL_RGBA16F) {
            result.red_size = 16u;
            result.green_size = 16u;
            result.blue_size = 16u;
            result.alpha_size = 16u;
        } else if (renderbuffer->internal_format == RINGL_RGB565) {
            result.red_size = 5u;
            result.green_size = 6u;
            result.blue_size = 5u;
        } else if (renderbuffer->internal_format == RINGL_RGBA4) {
            result.red_size = 4u;
            result.green_size = 4u;
            result.blue_size = 4u;
            result.alpha_size = 4u;
        } else if (renderbuffer->internal_format == RINGL_RGB5_A1) {
            result.red_size = 5u;
            result.green_size = 5u;
            result.blue_size = 5u;
            result.alpha_size = 1u;
        } else if (renderbuffer->internal_format == RINGL_DEPTH_COMPONENT16) {
            result.depth_size = 16u;
        } else if (renderbuffer->internal_format == RINGL_DEPTH_COMPONENT32F) {
            result.depth_size = 32u;
        } else if (renderbuffer->internal_format == RINGL_STENCIL_INDEX8) {
            result.stencil_size = 8u;
        } else if (renderbuffer->internal_format == RINGL_DEPTH24_STENCIL8) {
            result.depth_size = 24u;
            result.stencil_size = 8u;
        }
    }
    *info = result;
    return 0;
}

void ringl_renderbuffer_storage(uint32_t target, uint32_t internal_format,
                                int32_t width, int32_t height)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLRenderbufferObject* renderbuffer;

    if (context == NULL)
        return;
    if (!renderbuffer_target_valid(target)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (internal_format != RINGL_RGBA8 && internal_format != RINGL_RGB16F &&
        internal_format != RINGL_RGBA16F &&
        internal_format != RINGL_RGBA32F &&
        internal_format != RINGL_SRGB8_ALPHA8_EXT &&
        internal_format != RINGL_RGB565 &&
        internal_format != RINGL_RGBA4 && internal_format != RINGL_RGB5_A1 &&
        internal_format != RINGL_DEPTH_COMPONENT16 &&
        internal_format != RINGL_DEPTH_COMPONENT32F &&
        internal_format != RINGL_STENCIL_INDEX8 &&
        internal_format != RINGL_DEPTH24_STENCIL8) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (width <= 0 || height <= 0 ||
        (uint32_t)width > RINGL_MAX_TEXTURE_SIZE ||
        (uint32_t)height > RINGL_MAX_TEXTURE_SIZE) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    renderbuffer = bound_renderbuffer(context);
    if (renderbuffer == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    ringl_backend_destroy_object(context, renderbuffer->ringpu_image);
    renderbuffer->ringpu_image = 0u;
    renderbuffer->ringpu_image_state = RINGL_RIN_GPU_IMAGE_UNDEFINED;
    renderbuffer->internal_format = internal_format;
    renderbuffer->storage_format = internal_format == RINGL_SRGB8_ALPHA8_EXT
        ? RINGL_RGBA32F : internal_format;
    renderbuffer->srgb_encoding = internal_format == RINGL_SRGB8_ALPHA8_EXT
        ? RINGL_TRUE : RINGL_FALSE;
    renderbuffer->width = (uint32_t)width;
    renderbuffer->height = (uint32_t)height;
    renderbuffer->defined = RINGL_TRUE;
    ringl_context_mark_dirty(context, RINGL_DIRTY_FRAMEBUFFER);
}

int ringl_renderbuffer_realize_color_target(RinGLContext* context,
                                            uint32_t renderbuffer,
                                            uint64_t* image_out,
                                            uint32_t** image_state_out,
                                            uint32_t* width_out,
                                            uint32_t* height_out)
{
    uint32_t index;
    RinGLRenderbufferObject* object;
    RinGLRinGpuImage2DV1 desc;
    uint64_t image = 0u;

    if (context == NULL || image_out == NULL || image_state_out == NULL ||
        width_out == NULL || height_out == NULL || renderbuffer == 0u ||
        ringl_object_lookup(context, renderbuffer,
                            RINGL_OBJECT_RENDERBUFFER) == NULL)
        return -1;
    index = ringl_object_slot_index(renderbuffer);
    if (index >= RINGL_OBJECT_SLOT_COUNT)
        return -1;
    object = &context->renderbuffers[index];
    if (!object->defined ||
        !color_attachment_format_valid(object->internal_format) ||
        object->width == 0u || object->height == 0u)
        return -1;
    if (object->ringpu_image == 0u) {
        memset(&desc, 0, sizeof(desc));
        desc.width = object->width;
        desc.height = object->height;
        desc.format = color_attachment_ringpu_format(
            object->storage_format != 0u ? object->storage_format
                                         : object->internal_format);
        desc.usage = RINGL_RIN_GPU_IMAGE_USAGE_COLOR_TARGET |
                     RINGL_RIN_GPU_IMAGE_USAGE_COPY_SOURCE;
        if (ringl_backend_create_image_2d(context, &desc, &image) != 0 ||
            image == 0u) {
            return -1;
        }
        object->ringpu_image = image;
        object->ringpu_image_state = RINGL_RIN_GPU_IMAGE_UNDEFINED;
    }
    *image_out = object->ringpu_image;
    *image_state_out = &object->ringpu_image_state;
    *width_out = object->width;
    *height_out = object->height;
    return 0;
}

int ringl_renderbuffer_realize_depth_target(RinGLContext* context,
                                            uint32_t renderbuffer,
                                            uint64_t* image_out,
                                            uint32_t** image_state_out,
                                            uint32_t* width_out,
                                            uint32_t* height_out)
{
    uint32_t index;
    RinGLRenderbufferObject* object;
    RinGLRinGpuImage2DV1 desc;
    uint64_t image = 0u;

    if (context == NULL || image_out == NULL || image_state_out == NULL ||
        width_out == NULL || height_out == NULL || renderbuffer == 0u ||
        ringl_object_lookup(context, renderbuffer,
                            RINGL_OBJECT_RENDERBUFFER) == NULL) {
        return -1;
    }
    index = ringl_object_slot_index(renderbuffer);
    if (index >= RINGL_OBJECT_SLOT_COUNT)
        return -1;
    object = &context->renderbuffers[index];
    if (!object->defined ||
        (object->internal_format != RINGL_DEPTH_COMPONENT16 &&
         object->internal_format != RINGL_DEPTH_COMPONENT32F &&
         object->internal_format != RINGL_STENCIL_INDEX8 &&
         object->internal_format != RINGL_DEPTH24_STENCIL8) ||
        object->width == 0u || object->height == 0u) {
        return -1;
    }
    if (object->ringpu_image == 0u) {
        memset(&desc, 0, sizeof(desc));
        desc.width = object->width;
        desc.height = object->height;
        desc.format = object->internal_format == RINGL_STENCIL_INDEX8
            ? RINGL_RIN_GPU_FORMAT_S8_UINT
            : object->internal_format == RINGL_DEPTH24_STENCIL8
            ? RINGL_RIN_GPU_FORMAT_D32_FLOAT_S8_UINT
            : RINGL_RIN_GPU_FORMAT_D32_FLOAT;
        desc.usage = RINGL_RIN_GPU_IMAGE_USAGE_DEPTH_STENCIL;
        if (ringl_backend_create_image_2d(context, &desc, &image) != 0 ||
            image == 0u) {
            return -1;
        }
        object->ringpu_image = image;
        object->ringpu_image_state = RINGL_RIN_GPU_IMAGE_UNDEFINED;
    }
    *image_out = object->ringpu_image;
    *image_state_out = &object->ringpu_image_state;
    *width_out = object->width;
    *height_out = object->height;
    return 0;
}

void ringl_framebuffer_objects_destroy_all(RinGLContext* context)
{
    uint32_t index;

    if (context == NULL)
        return;
    for (index = 0u; index < RINGL_OBJECT_SLOT_COUNT; ++index) {
        if (context->objects[index].type != RINGL_OBJECT_RENDERBUFFER ||
            context->objects[index].state == RINGL_OBJECT_FREE) {
            continue;
        }
        ringl_backend_destroy_object(context,
                                     context->renderbuffers[index].ringpu_image);
        context->renderbuffers[index].ringpu_image = 0u;
    }
}

void ringl_framebuffer_renderbuffer(uint32_t target, uint32_t attachment,
                                    uint32_t renderbuffer_target,
                                    uint32_t renderbuffer)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLFramebufferObject* framebuffer;
    uint32_t attachment_index = 0u;

    if (context == NULL)
        return;
    if (!framebuffer_target_valid(target) ||
        (!color_attachment_valid(attachment) && !depth_attachment_valid(attachment)) ||
        !renderbuffer_target_valid(renderbuffer_target)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (color_attachment_valid(attachment)) {
        attachment_index = color_attachment_index(attachment);
        if (attachment_index != 0u &&
            context->webgl_draw_buffers_enabled == RINGL_FALSE) {
            ringl_context_record_error(context, RINGL_INVALID_ENUM);
            return;
        }
    }
    framebuffer = bound_framebuffer(context);
    if (framebuffer == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if (renderbuffer != 0u && !ringl_is_renderbuffer(renderbuffer)) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if (renderbuffer == 0u && color_attachment_valid(attachment)) {
        reset_color_attachment(framebuffer, attachment_index);
    } else if (renderbuffer == 0u && attachment == RINGL_DEPTH_ATTACHMENT) {
        reset_depth_attachment(framebuffer);
    } else if (renderbuffer == 0u && attachment == RINGL_STENCIL_ATTACHMENT) {
        reset_stencil_attachment(framebuffer);
    } else if (renderbuffer == 0u) {
        reset_depth_attachment(framebuffer);
        reset_stencil_attachment(framebuffer);
    } else {
        if (color_attachment_valid(attachment)) {
            framebuffer->color_attachment_kind[attachment_index] =
                RINGL_FRAMEBUFFER_ATTACHMENT_RENDERBUFFER;
            framebuffer->color_attachment_object[attachment_index] = renderbuffer;
            framebuffer->color_attachment_level[attachment_index] = 0;
        } else {
            uint32_t renderbuffer_index = ringl_object_slot_index(renderbuffer);
            uint32_t has_depth = attachment != RINGL_STENCIL_ATTACHMENT;
            uint32_t has_stencil = attachment != RINGL_DEPTH_ATTACHMENT;

            if (renderbuffer_index >= RINGL_OBJECT_SLOT_COUNT ||
                (context->renderbuffers[renderbuffer_index].defined != 0u &&
                 !depth_attachment_format_valid(
                     context->renderbuffers[renderbuffer_index].internal_format,
                     has_depth, has_stencil))) {
                ringl_context_record_error(context, RINGL_INVALID_OPERATION);
                return;
            }
            if (has_depth != 0u) {
            framebuffer->depth_attachment_kind =
                RINGL_FRAMEBUFFER_ATTACHMENT_RENDERBUFFER;
            framebuffer->depth_attachment_object = renderbuffer;
            framebuffer->depth_attachment_level = 0;
                framebuffer->depth_attachment_has_depth = RINGL_TRUE;
                framebuffer->depth_attachment_has_stencil = has_stencil;
            }
            if (has_stencil != 0u) {
                framebuffer->stencil_attachment_kind =
                    RINGL_FRAMEBUFFER_ATTACHMENT_RENDERBUFFER;
                framebuffer->stencil_attachment_object = renderbuffer;
                framebuffer->stencil_attachment_level = 0;
            }
        }
    }
    ringl_context_mark_dirty(context, RINGL_DIRTY_FRAMEBUFFER);
}

void ringl_framebuffer_detach_texture(RinGLContext* context, uint32_t texture)
{
    uint32_t index;

    if (context == NULL || texture == 0u)
        return;
    for (index = 0u; index < RINGL_OBJECT_SLOT_COUNT; ++index) {
        RinGLFramebufferObject* framebuffer = &context->framebuffers[index];

        if (context->objects[index].type != RINGL_OBJECT_FRAMEBUFFER ||
            context->objects[index].state == RINGL_OBJECT_FREE) {
            continue;
        }
        for (uint32_t attachment = 0u;
             attachment < RINGL_MAX_COLOR_ATTACHMENTS; ++attachment) {
            if (framebuffer->color_attachment_kind[attachment] ==
                    RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D &&
                framebuffer->color_attachment_object[attachment] == texture) {
                reset_color_attachment(framebuffer, attachment);
                ringl_context_mark_dirty(context, RINGL_DIRTY_FRAMEBUFFER);
            }
        }
        if (framebuffer->depth_attachment_kind ==
                RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D &&
            framebuffer->depth_attachment_object == texture) {
            reset_depth_attachment(framebuffer);
            ringl_context_mark_dirty(context, RINGL_DIRTY_FRAMEBUFFER);
        }
        if (framebuffer->stencil_attachment_kind ==
                RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D &&
            framebuffer->stencil_attachment_object == texture) {
            reset_stencil_attachment(framebuffer);
            ringl_context_mark_dirty(context, RINGL_DIRTY_FRAMEBUFFER);
        }
    }
}

void ringl_framebuffer_detach_renderbuffer(RinGLContext* context,
                                           uint32_t renderbuffer)
{
    uint32_t index;

    if (context == NULL || renderbuffer == 0u)
        return;
    for (index = 0u; index < RINGL_OBJECT_SLOT_COUNT; ++index) {
        RinGLFramebufferObject* framebuffer = &context->framebuffers[index];

        if (context->objects[index].type != RINGL_OBJECT_FRAMEBUFFER ||
            context->objects[index].state == RINGL_OBJECT_FREE) {
            continue;
        }
        for (uint32_t attachment = 0u;
             attachment < RINGL_MAX_COLOR_ATTACHMENTS; ++attachment) {
            if (framebuffer->color_attachment_kind[attachment] ==
                    RINGL_FRAMEBUFFER_ATTACHMENT_RENDERBUFFER &&
                framebuffer->color_attachment_object[attachment] == renderbuffer) {
                reset_color_attachment(framebuffer, attachment);
                ringl_context_mark_dirty(context, RINGL_DIRTY_FRAMEBUFFER);
            }
        }
        if (framebuffer->depth_attachment_kind ==
                RINGL_FRAMEBUFFER_ATTACHMENT_RENDERBUFFER &&
            framebuffer->depth_attachment_object == renderbuffer) {
            reset_depth_attachment(framebuffer);
            ringl_context_mark_dirty(context, RINGL_DIRTY_FRAMEBUFFER);
        }
        if (framebuffer->stencil_attachment_kind ==
                RINGL_FRAMEBUFFER_ATTACHMENT_RENDERBUFFER &&
            framebuffer->stencil_attachment_object == renderbuffer) {
            reset_stencil_attachment(framebuffer);
            ringl_context_mark_dirty(context, RINGL_DIRTY_FRAMEBUFFER);
        }
    }
}
