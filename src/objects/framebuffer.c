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
    if (!framebuffer_target_valid(target) || !color_attachment_valid(attachment) ||
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
    if (texture == 0u) {
        reset_color_attachment(framebuffer);
    } else {
        framebuffer->color_attachment_kind =
            RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D;
        framebuffer->color_attachment_object = texture;
        framebuffer->color_attachment_level = level;
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
    if (internal_format != RINGL_RGBA4) {
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
    renderbuffer->internal_format = internal_format;
    renderbuffer->width = (uint32_t)width;
    renderbuffer->height = (uint32_t)height;
    renderbuffer->defined = RINGL_TRUE;
    ringl_context_mark_dirty(context, RINGL_DIRTY_FRAMEBUFFER);
}

void ringl_framebuffer_renderbuffer(uint32_t target, uint32_t attachment,
                                    uint32_t renderbuffer_target,
                                    uint32_t renderbuffer)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLFramebufferObject* framebuffer;

    if (context == NULL)
        return;
    if (!framebuffer_target_valid(target) || !color_attachment_valid(attachment) ||
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
    if (renderbuffer == 0u) {
        reset_color_attachment(framebuffer);
    } else {
        framebuffer->color_attachment_kind =
            RINGL_FRAMEBUFFER_ATTACHMENT_RENDERBUFFER;
        framebuffer->color_attachment_object = renderbuffer;
        framebuffer->color_attachment_level = 0;
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
            context->objects[index].state == RINGL_OBJECT_FREE ||
            framebuffer->color_attachment_kind !=
                RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D ||
            framebuffer->color_attachment_object != texture) {
            continue;
        }
        reset_color_attachment(framebuffer);
        ringl_context_mark_dirty(context, RINGL_DIRTY_FRAMEBUFFER);
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
            context->objects[index].state == RINGL_OBJECT_FREE ||
            framebuffer->color_attachment_kind !=
                RINGL_FRAMEBUFFER_ATTACHMENT_RENDERBUFFER ||
            framebuffer->color_attachment_object != renderbuffer) {
            continue;
        }
        reset_color_attachment(framebuffer);
        ringl_context_mark_dirty(context, RINGL_DIRTY_FRAMEBUFFER);
    }
}
