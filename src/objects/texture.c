/* SPDX-License-Identifier: MIT */
#include "../ringl_internal.h"

#include <stdlib.h>
#include <string.h>

static int texture_target_valid(uint32_t target)
{
    return target == RINGL_TEXTURE_2D;
}

static int min_filter_valid(uint32_t value)
{
    return value == RINGL_NEAREST || value == RINGL_LINEAR ||
           value == RINGL_NEAREST_MIPMAP_NEAREST ||
           value == RINGL_LINEAR_MIPMAP_NEAREST ||
           value == RINGL_NEAREST_MIPMAP_LINEAR ||
           value == RINGL_LINEAR_MIPMAP_LINEAR;
}

static int mag_filter_valid(uint32_t value)
{
    return value == RINGL_NEAREST || value == RINGL_LINEAR;
}

static int wrap_valid(uint32_t value)
{
    return value == RINGL_REPEAT || value == RINGL_CLAMP_TO_EDGE ||
           value == RINGL_MIRRORED_REPEAT;
}

static void texture_init_defaults(RinGLTextureObject* texture)
{
    if (texture == NULL)
        return;
    texture->min_filter = RINGL_NEAREST_MIPMAP_LINEAR;
    texture->mag_filter = RINGL_LINEAR;
    texture->wrap_s = RINGL_REPEAT;
    texture->wrap_t = RINGL_REPEAT;
}

static RinGLTextureObject* bound_texture_2d(RinGLContext* context)
{
    uint32_t name;
    uint32_t slot_index;

    if (context == NULL || context->active_texture_unit >= RINGL_MAX_TEXTURE_UNITS)
        return NULL;
    name = context->bound_texture_2d[context->active_texture_unit];
    if (name == 0u ||
        ringl_object_lookup(context, name, RINGL_OBJECT_TEXTURE) == NULL)
        return NULL;
    slot_index = ringl_object_slot_index(name);
    if (slot_index >= RINGL_OBJECT_SLOT_COUNT)
        return NULL;
    return &context->textures[slot_index];
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
                                         context->textures[slot_index].ringpu_sampler);
            ringl_backend_destroy_object(context,
                                         context->textures[slot_index].ringpu_image);
            free(context->textures[slot_index].shadow_bytes);
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
        if (slot->state == RINGL_OBJECT_RESERVED) {
            uint32_t slot_index = ringl_object_slot_index(texture);
            if (slot_index >= RINGL_OBJECT_SLOT_COUNT) {
                ringl_context_record_error(context, RINGL_INVALID_OPERATION);
                return;
            }
            texture_init_defaults(&context->textures[slot_index]);
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

void ringl_tex_parameteri(uint32_t target, uint32_t pname, int32_t param)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLTextureObject* texture;
    uint32_t value = (uint32_t)param;
    uint32_t* field;

    if (context == NULL)
        return;
    if (!texture_target_valid(target)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    texture = bound_texture_2d(context);
    if (texture == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }

    if (pname == RINGL_TEXTURE_MIN_FILTER) {
        if (!min_filter_valid(value)) {
            ringl_context_record_error(context, RINGL_INVALID_ENUM);
            return;
        }
        field = &texture->min_filter;
    } else if (pname == RINGL_TEXTURE_MAG_FILTER) {
        if (!mag_filter_valid(value)) {
            ringl_context_record_error(context, RINGL_INVALID_ENUM);
            return;
        }
        field = &texture->mag_filter;
    } else if (pname == RINGL_TEXTURE_WRAP_S) {
        if (!wrap_valid(value)) {
            ringl_context_record_error(context, RINGL_INVALID_ENUM);
            return;
        }
        field = &texture->wrap_s;
    } else if (pname == RINGL_TEXTURE_WRAP_T) {
        if (!wrap_valid(value)) {
            ringl_context_record_error(context, RINGL_INVALID_ENUM);
            return;
        }
        field = &texture->wrap_t;
    } else {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }

    if (*field != value) {
        ringl_backend_destroy_object(context, texture->ringpu_sampler);
        texture->ringpu_sampler = 0u;
        *field = value;
        ringl_context_mark_dirty(context, RINGL_DIRTY_BINDINGS);
    }
}

int32_t ringl_get_tex_parameteri(uint32_t target, uint32_t pname)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLTextureObject* texture;

    if (context == NULL)
        return 0;
    if (!texture_target_valid(target)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return 0;
    }
    texture = bound_texture_2d(context);
    if (texture == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return 0;
    }

    if (pname == RINGL_TEXTURE_MIN_FILTER)
        return (int32_t)texture->min_filter;
    if (pname == RINGL_TEXTURE_MAG_FILTER)
        return (int32_t)texture->mag_filter;
    if (pname == RINGL_TEXTURE_WRAP_S)
        return (int32_t)texture->wrap_s;
    if (pname == RINGL_TEXTURE_WRAP_T)
        return (int32_t)texture->wrap_t;
    ringl_context_record_error(context, RINGL_INVALID_ENUM);
    return 0;
}

void ringl_tex_image_2d(uint32_t target, int32_t level,
                        uint32_t internal_format, int32_t width, int32_t height,
                        int32_t border, uint32_t format, uint32_t type,
                        const void* pixels)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLTextureObject* texture;
    uint8_t* replacement = NULL;
    uint64_t size;

    if (context == NULL)
        return;
    if (!texture_target_valid(target) || internal_format != RINGL_RGBA ||
        format != RINGL_RGBA || type != RINGL_UNSIGNED_BYTE) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (level != 0 || border != 0 || width < 0 || height < 0 ||
        (uint32_t)width > RINGL_MAX_TEXTURE_SIZE ||
        (uint32_t)height > RINGL_MAX_TEXTURE_SIZE) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }

    texture = bound_texture_2d(context);
    if (texture == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }

    size = (uint64_t)(uint32_t)width * (uint64_t)(uint32_t)height * 4u;
    if (size != 0u) {
        replacement = malloc((size_t)size);
        if (replacement == NULL) {
            ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
            return;
        }
        if (pixels != NULL)
            memcpy(replacement, pixels, (size_t)size);
        else
            memset(replacement, 0, (size_t)size);
    }

    ringl_backend_destroy_object(context, texture->ringpu_image);
    texture->ringpu_image = 0u;
    free(texture->shadow_bytes);
    texture->shadow_bytes = replacement;
    texture->shadow_size = size;
    texture->width = (uint32_t)width;
    texture->height = (uint32_t)height;
    texture->format = RINGL_RGBA;
    texture->defined = RINGL_TRUE;
    ringl_context_mark_dirty(context, RINGL_DIRTY_BINDINGS);
}

void ringl_tex_sub_image_2d(uint32_t target, int32_t level,
                            int32_t xoffset, int32_t yoffset,
                            int32_t width, int32_t height,
                            uint32_t format, uint32_t type,
                            const void* pixels)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLTextureObject* texture;
    uint32_t row;

    if (context == NULL)
        return;
    if (!texture_target_valid(target) || format != RINGL_RGBA ||
        type != RINGL_UNSIGNED_BYTE) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (level != 0 || xoffset < 0 || yoffset < 0 || width < 0 || height < 0) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }

    texture = bound_texture_2d(context);
    if (texture == NULL || !texture->defined) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if ((uint32_t)xoffset > texture->width || (uint32_t)yoffset > texture->height ||
        (uint32_t)width > texture->width - (uint32_t)xoffset ||
        (uint32_t)height > texture->height - (uint32_t)yoffset) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (width == 0 || height == 0)
        return;
    if (pixels == NULL || texture->shadow_bytes == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }

    for (row = 0u; row < (uint32_t)height; ++row) {
        uint64_t destination_offset =
            ((uint64_t)((uint32_t)yoffset + row) * texture->width +
             (uint32_t)xoffset) * 4u;
        uint64_t source_offset = (uint64_t)row * (uint32_t)width * 4u;
        memcpy(texture->shadow_bytes + destination_offset,
               (const uint8_t*)pixels + source_offset,
               (size_t)(uint32_t)width * 4u);
    }

    ringl_backend_destroy_object(context, texture->ringpu_image);
    texture->ringpu_image = 0u;
    ringl_context_mark_dirty(context, RINGL_DIRTY_BINDINGS);
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
        ringl_backend_destroy_object(context, context->textures[index].ringpu_sampler);
        ringl_backend_destroy_object(context, context->textures[index].ringpu_image);
        context->textures[index].ringpu_sampler = 0u;
        context->textures[index].ringpu_image = 0u;
        free(context->textures[index].shadow_bytes);
        context->textures[index].shadow_bytes = NULL;
        context->textures[index].shadow_size = 0u;
    }
}
