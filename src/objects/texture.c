/* SPDX-License-Identifier: MIT */
#include "../ringl_internal.h"

#include <string.h>

static int texture_target_valid(uint32_t target)
{
    return target == RINGL_TEXTURE_2D;
}

void ringl_gen_textures(int32_t count, uint32_t* textures)
{
    RinGLContext* context = ringl_get_current_context();
    int32_t i;

    if (context == NULL)
        return;
    if (count < 0) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (count == 0)
        return;
    if (textures == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }

    for (i = 0; i < count; ++i)
        textures[i] = 0u;
    for (i = 0; i < count; ++i) {
        textures[i] = ringl_object_allocate(context, RINGL_OBJECT_TEXTURE);
        if (textures[i] == 0u) {
            int32_t rollback;
            for (rollback = 0; rollback < i; ++rollback) {
                ringl_object_release(context, textures[rollback],
                                     RINGL_OBJECT_TEXTURE);
                textures[rollback] = 0u;
            }
            ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
            return;
        }
    }
}

void ringl_delete_textures(int32_t count, const uint32_t* textures)
{
    RinGLContext* context = ringl_get_current_context();
    int32_t i;

    if (context == NULL)
        return;
    if (count < 0) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (count == 0)
        return;
    if (textures == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }

    for (i = 0; i < count; ++i) {
        uint32_t name = textures[i];
        uint32_t slot_index;
        uint32_t unit;

        if (name == 0u ||
            ringl_object_lookup(context, name, RINGL_OBJECT_TEXTURE) == NULL)
            continue;

        for (unit = 0u; unit < RINGL_MAX_TEXTURE_UNITS; ++unit) {
            if (context->bound_texture_2d[unit] == name)
                context->bound_texture_2d[unit] = 0u;
        }

        slot_index = ringl_object_slot_index(name);
        if (slot_index < RINGL_OBJECT_SLOT_COUNT) {
            ringl_backend_destroy_object(context,
                                         context->textures[slot_index].ringpu_image);
            memset(&context->textures[slot_index], 0,
                   sizeof(context->textures[slot_index]));
        }
        ringl_object_release(context, name, RINGL_OBJECT_TEXTURE);
        ringl_context_mark_dirty(context, RINGL_DIRTY_BINDINGS);
    }
}

void ringl_bind_texture(uint32_t target, uint32_t texture)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLObjectSlot* slot;
    uint32_t* binding;

    if (context == NULL)
        return;
    if (!texture_target_valid(target)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (context->active_texture_unit >= RINGL_MAX_TEXTURE_UNITS) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }

    if (texture != 0u) {
        slot = ringl_object_lookup(context, texture, RINGL_OBJECT_TEXTURE);
        if (slot == NULL) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return;
        }
        ringl_object_promote(slot);
    }

    binding = &context->bound_texture_2d[context->active_texture_unit];
    if (*binding != texture) {
        *binding = texture;
        ringl_context_mark_dirty(context, RINGL_DIRTY_BINDINGS);
    }
}

int ringl_is_texture(uint32_t texture)
{
    RinGLContext* context = ringl_get_current_context();
    const RinGLObjectSlot* slot;

    if (context == NULL || texture == 0u)
        return 0;
    slot = ringl_object_lookup_const(context, texture, RINGL_OBJECT_TEXTURE);
    return slot != NULL && slot->state == RINGL_OBJECT_LIVE;
}

void ringl_active_texture(uint32_t texture_unit)
{
    RinGLContext* context = ringl_get_current_context();
    uint32_t unit;

    if (context == NULL)
        return;
    if (texture_unit < RINGL_TEXTURE0) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    unit = texture_unit - RINGL_TEXTURE0;
    if (unit >= RINGL_MAX_TEXTURE_UNITS) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    context->active_texture_unit = unit;
}

uint32_t ringl_get_active_texture(void)
{
    RinGLContext* context = ringl_get_current_context();
    if (context == NULL)
        return RINGL_TEXTURE0;
    return RINGL_TEXTURE0 + context->active_texture_unit;
}

uint32_t ringl_get_bound_texture(uint32_t target)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL)
        return 0u;
    if (!texture_target_valid(target)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return 0u;
    }
    if (context->active_texture_unit >= RINGL_MAX_TEXTURE_UNITS)
        return 0u;
    return context->bound_texture_2d[context->active_texture_unit];
}

void ringl_texture_objects_destroy_all(RinGLContext* context)
{
    uint32_t index;

    if (context == NULL)
        return;
    for (index = 0u; index < RINGL_OBJECT_SLOT_COUNT; ++index) {
        if (context->objects[index].type != RINGL_OBJECT_TEXTURE ||
            context->objects[index].state == RINGL_OBJECT_FREE)
            continue;
        ringl_backend_destroy_object(context, context->textures[index].ringpu_image);
        context->textures[index].ringpu_image = 0u;
    }
}
