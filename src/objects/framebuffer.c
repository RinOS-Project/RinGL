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
    return attachment == RINGL_COLOR_ATTACHMENT0;
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
    if (format == RINGL_DEPTH_COMPONENT32F)
        return has_depth != 0u && has_stencil == 0u;
    return format == RINGL_DEPTH24_STENCIL8 && has_stencil != 0u;
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

static void reset_color_attachment(RinGLFramebufferObject* framebuffer)
{
    if (framebuffer == NULL)
        return;
    framebuffer->color_attachment_kind = RINGL_FRAMEBUFFER_ATTACHMENT_NONE;
    framebuffer->color_attachment_object = 0u;
    framebuffer->color_attachment_level = 0;
}

static void reset_depth_attachment(RinGLFramebufferObject* framebuffer)
{
    if (framebuffer == NULL)
        return;
    framebuffer->depth_attachment_kind = RINGL_FRAMEBUFFER_ATTACHMENT_NONE;
    framebuffer->depth_attachment_object = 0u;
    framebuffer->depth_attachment_has_depth = RINGL_FALSE;
    framebuffer->depth_attachment_has_stencil = RINGL_FALSE;
}

static int color_attachment_dimensions(RinGLContext* context,
                                       const RinGLFramebufferObject* framebuffer,
                                       uint32_t* width_out, uint32_t* height_out)
{
    uint32_t index;

    if (context == NULL || framebuffer == NULL || width_out == NULL ||
        height_out == NULL)
        return -1;
    if (framebuffer->color_attachment_kind ==
        RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D) {
        RinGLTextureObject* texture;

        if (ringl_object_lookup(context, framebuffer->color_attachment_object,
                                RINGL_OBJECT_TEXTURE) == NULL ||
            framebuffer->color_attachment_level != 0)
            return -1;
        index = ringl_object_slot_index(framebuffer->color_attachment_object);
        if (index >= RINGL_OBJECT_SLOT_COUNT)
            return -1;
        texture = &context->textures[index];
        if (!texture->defined || texture->format != RINGL_RGBA ||
            texture->width == 0u || texture->height == 0u)
            return -1;
        *width_out = texture->width;
        *height_out = texture->height;
        return 0;
    }
    if (framebuffer->color_attachment_kind ==
        RINGL_FRAMEBUFFER_ATTACHMENT_RENDERBUFFER) {
        RinGLRenderbufferObject* renderbuffer;

        if (ringl_object_lookup(context, framebuffer->color_attachment_object,
                                RINGL_OBJECT_RENDERBUFFER) == NULL)
            return -1;
        index = ringl_object_slot_index(framebuffer->color_attachment_object);
        if (index >= RINGL_OBJECT_SLOT_COUNT)
            return -1;
        renderbuffer = &context->renderbuffers[index];
        if (!renderbuffer->defined ||
            renderbuffer->internal_format != RINGL_RGBA8 ||
            renderbuffer->width == 0u || renderbuffer->height == 0u)
            return -1;
        *width_out = renderbuffer->width;
        *height_out = renderbuffer->height;
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
            texture->width == 0u || texture->height == 0u)
            return -1;
        *width_out = texture->width;
        *height_out = texture->height;
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

void ringl_framebuffer_texture_2d(uint32_t target, uint32_t attachment,
                                  uint32_t textarget, uint32_t texture,
                                  int32_t level)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLFramebufferObject* framebuffer;

    if (context == NULL)
        return;
    if (!framebuffer_target_valid(target) ||
        (!color_attachment_valid(attachment) && !depth_attachment_valid(attachment)) ||
        textarget != RINGL_TEXTURE_2D) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (level != 0) {
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
        reset_color_attachment(framebuffer);
    } else if (texture == 0u) {
        reset_depth_attachment(framebuffer);
    } else if (color_attachment_valid(attachment)) {
        if (ringl_texture_require_color_target(context, texture) != 0) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return;
        }
        framebuffer->color_attachment_kind =
            RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D;
        framebuffer->color_attachment_object = texture;
        framebuffer->color_attachment_level = level;
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
        framebuffer->depth_attachment_kind =
            RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D;
        framebuffer->depth_attachment_object = texture;
        framebuffer->depth_attachment_has_depth = has_depth;
        framebuffer->depth_attachment_has_stencil = has_stencil;
    }
    ringl_context_mark_dirty(context, RINGL_DIRTY_FRAMEBUFFER);
}

int ringl_get_framebuffer_color_attachment(
    RinGLFramebufferAttachmentInfoV1* attachment)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLFramebufferObject* framebuffer;

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
    attachment->kind = framebuffer->color_attachment_kind;
    attachment->object = framebuffer->color_attachment_object;
    attachment->level = framebuffer->color_attachment_level;
    return 0;
}

uint32_t ringl_check_framebuffer_status(uint32_t target)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLFramebufferObject* framebuffer;
    uint32_t color_width;
    uint32_t color_height;
    uint32_t depth_width;
    uint32_t depth_height;

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
    if (framebuffer->color_attachment_kind ==
        RINGL_FRAMEBUFFER_ATTACHMENT_NONE)
        return RINGL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
    if (color_attachment_dimensions(context, framebuffer, &color_width,
                                    &color_height) != 0)
        return RINGL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
    if (framebuffer->depth_attachment_kind ==
        RINGL_FRAMEBUFFER_ATTACHMENT_NONE)
        return RINGL_FRAMEBUFFER_COMPLETE;
    if (depth_attachment_dimensions(context, framebuffer, &depth_width,
                                    &depth_height) != 0 ||
        color_width != depth_width || color_height != depth_height)
        return RINGL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
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
        } else if (renderbuffer->internal_format == RINGL_DEPTH_COMPONENT32F) {
            result.depth_size = 32u;
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
    if (internal_format != RINGL_RGBA8 &&
        internal_format != RINGL_DEPTH_COMPONENT32F &&
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
    if (!object->defined || object->internal_format != RINGL_RGBA8 ||
        object->width == 0u || object->height == 0u)
        return -1;
    if (object->ringpu_image == 0u) {
        memset(&desc, 0, sizeof(desc));
        desc.width = object->width;
        desc.height = object->height;
        desc.format = RINGL_RIN_GPU_FORMAT_RGBA8_UNORM;
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
        (object->internal_format != RINGL_DEPTH_COMPONENT32F &&
         object->internal_format != RINGL_DEPTH24_STENCIL8) ||
        object->width == 0u || object->height == 0u) {
        return -1;
    }
    if (object->ringpu_image == 0u) {
        memset(&desc, 0, sizeof(desc));
        desc.width = object->width;
        desc.height = object->height;
        desc.format = object->internal_format == RINGL_DEPTH24_STENCIL8
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

    if (context == NULL)
        return;
    if (!framebuffer_target_valid(target) ||
        (!color_attachment_valid(attachment) && !depth_attachment_valid(attachment)) ||
        !renderbuffer_target_valid(renderbuffer_target)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
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
        reset_color_attachment(framebuffer);
    } else if (renderbuffer == 0u) {
        reset_depth_attachment(framebuffer);
    } else {
        if (color_attachment_valid(attachment)) {
            framebuffer->color_attachment_kind =
                RINGL_FRAMEBUFFER_ATTACHMENT_RENDERBUFFER;
            framebuffer->color_attachment_object = renderbuffer;
            framebuffer->color_attachment_level = 0;
        } else {
            framebuffer->depth_attachment_kind =
                RINGL_FRAMEBUFFER_ATTACHMENT_RENDERBUFFER;
            framebuffer->depth_attachment_object = renderbuffer;
            framebuffer->depth_attachment_has_depth =
                attachment != RINGL_STENCIL_ATTACHMENT;
            framebuffer->depth_attachment_has_stencil =
                attachment != RINGL_DEPTH_ATTACHMENT;
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
        if (framebuffer->color_attachment_kind ==
                RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D &&
            framebuffer->color_attachment_object == texture) {
            reset_color_attachment(framebuffer);
            ringl_context_mark_dirty(context, RINGL_DIRTY_FRAMEBUFFER);
        }
        if (framebuffer->depth_attachment_kind ==
                RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D &&
            framebuffer->depth_attachment_object == texture) {
            reset_depth_attachment(framebuffer);
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
        if (framebuffer->color_attachment_kind ==
                RINGL_FRAMEBUFFER_ATTACHMENT_RENDERBUFFER &&
            framebuffer->color_attachment_object == renderbuffer) {
            reset_color_attachment(framebuffer);
            ringl_context_mark_dirty(context, RINGL_DIRTY_FRAMEBUFFER);
        }
        if (framebuffer->depth_attachment_kind ==
                RINGL_FRAMEBUFFER_ATTACHMENT_RENDERBUFFER &&
            framebuffer->depth_attachment_object == renderbuffer) {
            reset_depth_attachment(framebuffer);
            ringl_context_mark_dirty(context, RINGL_DIRTY_FRAMEBUFFER);
        }
    }
}
