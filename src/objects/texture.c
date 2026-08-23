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
    texture->ringpu_image_state = RINGL_RIN_GPU_IMAGE_UNDEFINED;
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

static int texture_level0_storage_defined(const RinGLTextureObject* texture)
{
    if (texture == NULL || !texture->defined || texture->width == 0u ||
        texture->height == 0u || texture->shadow_bytes == NULL ||
        texture->shadow_size == 0u) {
        return 0;
    }

    return 1;
}

static int texture_level0_complete(const RinGLTextureObject* texture)
{
    if (!texture_level0_storage_defined(texture))
        return 0;

    if (texture->min_filter == RINGL_NEAREST ||
        texture->min_filter == RINGL_LINEAR) {
        return 1;
    }

    /* The current profile only stores mip level zero. A mipmapped min filter
     * is therefore complete only for a 1x1 base level. */
    return texture->width == 1u && texture->height == 1u;
}

static int texture_color_format(uint32_t format)
{
    return format == RINGL_RGBA || format == RINGL_RGB ||
           format == RINGL_ALPHA || format == RINGL_LUMINANCE ||
           format == RINGL_LUMINANCE_ALPHA;
}

static uint32_t texture_external_texel_bytes(uint32_t format)
{
    switch (format) {
    case RINGL_RGBA:
        return 4u;
    case RINGL_RGB:
        return 3u;
    case RINGL_LUMINANCE_ALPHA:
        return 2u;
    case RINGL_ALPHA:
    case RINGL_LUMINANCE:
        return 1u;
    default:
        return 4u;
    }
}

static uint64_t texture_source_row_pitch(uint32_t width, uint32_t format,
                                         uint32_t alignment)
{
    uint64_t row_bytes = (uint64_t)width *
                         texture_external_texel_bytes(format);

    return (row_bytes + alignment - 1u) & ~(uint64_t)(alignment - 1u);
}

/* The final source row has no required trailing alignment padding. This
 * calculation is an embedding boundary, so retain overflow checks even though
 * the current texture dimensions are deliberately small. */
static int texture_required_source_bytes(uint32_t width, uint32_t height,
                                         uint32_t format, uint32_t alignment,
                                         uint64_t* bytes_out)
{
    uint64_t row_bytes;
    uint64_t row_pitch;

    if (bytes_out == NULL || alignment == 0u)
        return -1;
    if (width == 0u || height == 0u) {
        *bytes_out = 0u;
        return 0;
    }

    row_bytes = (uint64_t)width * texture_external_texel_bytes(format);
    if (row_bytes > UINT64_MAX - ((uint64_t)alignment - 1u))
        return -1;
    row_pitch = texture_source_row_pitch(width, format, alignment);
    if ((uint64_t)(height - 1u) > (UINT64_MAX - row_bytes) / row_pitch)
        return -1;

    *bytes_out = (uint64_t)(height - 1u) * row_pitch + row_bytes;
    return 0;
}

static void texture_copy_color_texels(uint8_t* destination,
                                      const uint8_t* source,
                                      uint32_t format, uint32_t texel_count)
{
    uint32_t index;
    uint32_t source_texel_bytes = texture_external_texel_bytes(format);

    for (index = 0u; index < texel_count; ++index) {
        const uint8_t* source_texel =
            source + (uint64_t)index * source_texel_bytes;
        uint8_t* destination_texel = destination + (uint64_t)index * 4u;

        switch (format) {
        case RINGL_RGBA:
            memcpy(destination_texel, source_texel, 4u);
            break;
        case RINGL_RGB:
            destination_texel[0] = source_texel[0];
            destination_texel[1] = source_texel[1];
            destination_texel[2] = source_texel[2];
            destination_texel[3] = UINT8_MAX;
            break;
        case RINGL_ALPHA:
            destination_texel[0] = 0u;
            destination_texel[1] = 0u;
            destination_texel[2] = 0u;
            destination_texel[3] = source_texel[0];
            break;
        case RINGL_LUMINANCE:
            destination_texel[0] = source_texel[0];
            destination_texel[1] = source_texel[0];
            destination_texel[2] = source_texel[0];
            destination_texel[3] = UINT8_MAX;
            break;
        case RINGL_LUMINANCE_ALPHA:
            destination_texel[0] = source_texel[0];
            destination_texel[1] = source_texel[0];
            destination_texel[2] = source_texel[0];
            destination_texel[3] = source_texel[1];
            break;
        default:
            break;
        }
    }
}

static uint32_t texture_storage_texel_bytes(uint32_t format)
{
    return format == RINGL_DEPTH24_STENCIL8 ? 8u : 4u;
}

static void texture_copy_depth_stencil_texels(uint8_t* destination,
                                              const uint8_t* source,
                                              uint32_t texel_count)
{
    uint32_t index;

    for (index = 0u; index < texel_count; ++index) {
        uint32_t packed;
        float depth;

        memcpy(&packed, source + (uint64_t)index * sizeof(packed),
               sizeof(packed));
        depth = (float)(packed >> 8u) / 16777215.0f;
        memcpy(destination + (uint64_t)index * 8u, &depth, sizeof(depth));
        destination[(uint64_t)index * 8u + 4u] = (uint8_t)packed;
        memset(destination + (uint64_t)index * 8u + 5u, 0, 3u);
    }
}

static uint32_t sampler_filter(uint32_t value)
{
    if (value == RINGL_NEAREST || value == RINGL_NEAREST_MIPMAP_NEAREST ||
        value == RINGL_NEAREST_MIPMAP_LINEAR) {
        return RINGL_RIN_GPU_SAMPLER_NEAREST;
    }
    return RINGL_RIN_GPU_SAMPLER_LINEAR;
}

static uint32_t sampler_mip_filter(uint32_t value)
{
    if (value == RINGL_NEAREST_MIPMAP_LINEAR ||
        value == RINGL_LINEAR_MIPMAP_LINEAR) {
        return RINGL_RIN_GPU_SAMPLER_LINEAR;
    }
    return RINGL_RIN_GPU_SAMPLER_NEAREST;
}

static uint32_t sampler_address(uint32_t value)
{
    if (value == RINGL_CLAMP_TO_EDGE)
        return RINGL_RIN_GPU_ADDRESS_CLAMP;
    if (value == RINGL_MIRRORED_REPEAT)
        return RINGL_RIN_GPU_ADDRESS_MIRRORED;
    return RINGL_RIN_GPU_ADDRESS_REPEAT;
}

static void texture_discard_image(RinGLContext* context,
                                  RinGLTextureObject* texture)
{
    if (texture == NULL)
        return;
    ringl_backend_destroy_object(context, texture->ringpu_image);
    texture->ringpu_image = 0u;
    texture->ringpu_image_state = RINGL_RIN_GPU_IMAGE_UNDEFINED;
}

static int texture_realize_image(RinGLContext* context,
                                 RinGLTextureObject* texture)
{
    RinGLRinGpuSampledImage2DV1 desc;
    RinGLRinGpuImage2DV1 color_target_desc;
    RinGLRinGpuImageUpload2DV1 upload;
    uint64_t image = 0u;

    if (texture->ringpu_image != 0u)
        return 0;
    if ((texture->requires_color_target ||
         texture->format == RINGL_DEPTH_COMPONENT32F ||
         texture->format == RINGL_DEPTH24_STENCIL8)
            ? !texture_level0_storage_defined(texture)
            : !texture_level0_complete(texture))
        return -1;

    if (texture->format == RINGL_DEPTH_COMPONENT32F ||
        texture->format == RINGL_DEPTH24_STENCIL8) {
        memset(&color_target_desc, 0, sizeof(color_target_desc));
        color_target_desc.width = texture->width;
        color_target_desc.height = texture->height;
        color_target_desc.format = texture->format == RINGL_DEPTH24_STENCIL8
            ? RINGL_RIN_GPU_FORMAT_D32_FLOAT_S8_UINT
            : RINGL_RIN_GPU_FORMAT_D32_FLOAT;
        color_target_desc.usage =
            RINGL_RIN_GPU_IMAGE_USAGE_COPY_DESTINATION |
            RINGL_RIN_GPU_IMAGE_USAGE_DEPTH_STENCIL;
        if (texture->format == RINGL_DEPTH_COMPONENT32F)
            color_target_desc.usage |= RINGL_RIN_GPU_IMAGE_USAGE_SAMPLED;
        if (ringl_backend_create_image_2d(context, &color_target_desc,
                                          &image) != 0 || image == 0u) {
            return -1;
        }
    } else if (texture->format == RINGL_RGBA && texture->requires_color_target) {
        memset(&color_target_desc, 0, sizeof(color_target_desc));
        color_target_desc.width = texture->width;
        color_target_desc.height = texture->height;
        color_target_desc.format = RINGL_RIN_GPU_FORMAT_RGBA8_UNORM;
        color_target_desc.usage = RINGL_RIN_GPU_IMAGE_USAGE_COPY_DESTINATION |
                                  RINGL_RIN_GPU_IMAGE_USAGE_SAMPLED |
                                  RINGL_RIN_GPU_IMAGE_USAGE_COLOR_TARGET |
                                  RINGL_RIN_GPU_IMAGE_USAGE_COPY_SOURCE;
        if (ringl_backend_create_image_2d(context, &color_target_desc, &image) != 0 ||
            image == 0u) {
            return -1;
        }
    } else if (texture_color_format(texture->format)) {
        memset(&desc, 0, sizeof(desc));
        desc.width = texture->width;
        desc.height = texture->height;
        desc.format = RINGL_RIN_GPU_FORMAT_RGBA8_UNORM;
        if (ringl_backend_create_sampled_image_2d(context, &desc, &image) != 0 ||
            image == 0u) {
            return -1;
        }
    } else {
        return -1;
    }

    memset(&upload, 0, sizeof(upload));
    upload.width = texture->width;
    upload.height = texture->height;
    upload.source_row_pitch_bytes = (uint64_t)texture->width *
                                    texture_storage_texel_bytes(texture->format);
    if (ringl_backend_upload_image_2d(context, image, &upload,
                                      texture->shadow_bytes,
                                      texture->shadow_size) != 0) {
        ringl_backend_destroy_object(context, image);
        return -1;
    }

    texture->ringpu_image = image;
    texture->ringpu_image_state = RINGL_RIN_GPU_IMAGE_UNDEFINED;
    return 0;
}

static int texture_realize_sampler(RinGLContext* context,
                                   RinGLTextureObject* texture)
{
    RinGLRinGpuSamplerV1 desc;
    uint64_t sampler = 0u;

    if (texture->ringpu_sampler != 0u)
        return 0;

    memset(&desc, 0, sizeof(desc));
    desc.min_filter = sampler_filter(texture->min_filter);
    desc.mag_filter = sampler_filter(texture->mag_filter);
    desc.mip_filter = sampler_mip_filter(texture->min_filter);
    desc.address_u = sampler_address(texture->wrap_s);
    desc.address_v = sampler_address(texture->wrap_t);
    if (ringl_backend_create_sampler(context, &desc, &sampler) != 0 ||
        sampler == 0u) {
        return -1;
    }
    texture->ringpu_sampler = sampler;
    return 0;
}

int ringl_texture_realize_unit(RinGLContext* context, uint32_t unit,
                               uint64_t* image_out, uint64_t* sampler_out)
{
    uint32_t name;
    uint32_t slot_index;
    RinGLTextureObject* texture;

    if (context == NULL || image_out == NULL || sampler_out == NULL ||
        unit >= RINGL_MAX_TEXTURE_UNITS) {
        return -1;
    }
    *image_out = 0u;
    *sampler_out = 0u;
    name = context->bound_texture_2d[unit];
    if (name == 0u ||
        ringl_object_lookup(context, name, RINGL_OBJECT_TEXTURE) == NULL) {
        return -1;
    }
    slot_index = ringl_object_slot_index(name);
    if (slot_index >= RINGL_OBJECT_SLOT_COUNT)
        return -1;
    texture = &context->textures[slot_index];
    if (!(texture_color_format(texture->format) ||
          texture->format == RINGL_DEPTH_COMPONENT32F) ||
        !texture_level0_complete(texture) ||
        texture_realize_image(context, texture) != 0 ||
        texture_realize_sampler(context, texture) != 0) {
        return -1;
    }
    *image_out = texture->ringpu_image;
    *sampler_out = texture->ringpu_sampler;
    return 0;
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

        ringl_framebuffer_detach_texture(context, name);

        for (unit = 0u; unit < RINGL_MAX_TEXTURE_UNITS; ++unit) {
            if (context->bound_texture_2d[unit] == name)
                context->bound_texture_2d[unit] = 0u;
        }

        slot_index = ringl_object_slot_index(name);
        if (slot_index < RINGL_OBJECT_SLOT_COUNT) {
            ringl_backend_destroy_object(context,
                                         context->textures[slot_index].ringpu_sampler);
            texture_discard_image(context, &context->textures[slot_index]);
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

static void ringl_tex_image_2d_impl(uint32_t target, int32_t level,
                                    uint32_t internal_format, int32_t width,
                                    int32_t height, int32_t border,
                                    uint32_t format, uint32_t type,
                                    const void* pixels, uint64_t pixels_size)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLTextureObject* texture;
    uint8_t* replacement = NULL;
    uint64_t size;

    if (context == NULL)
        return;
    if (!texture_target_valid(target) ||
        !((texture_color_format(internal_format) && internal_format == format &&
           type == RINGL_UNSIGNED_BYTE) ||
          (internal_format == RINGL_DEPTH_COMPONENT32F &&
           format == RINGL_DEPTH_COMPONENT && type == RINGL_FLOAT) ||
          (internal_format == RINGL_DEPTH24_STENCIL8 &&
           format == RINGL_DEPTH_STENCIL && type == RINGL_UNSIGNED_INT_24_8))) {
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
    if (pixels != NULL) {
        uint64_t required_source_bytes;

        if (texture_required_source_bytes((uint32_t)width, (uint32_t)height,
                                          format, context->unpack_alignment,
                                          &required_source_bytes) != 0 ||
            pixels_size < required_source_bytes) {
            ringl_context_record_error(context, RINGL_INVALID_VALUE);
            return;
        }
    }

    size = (uint64_t)(uint32_t)width * (uint64_t)(uint32_t)height *
           texture_storage_texel_bytes(internal_format);
    if (size != 0u) {
        replacement = malloc((size_t)size);
        if (replacement == NULL) {
            ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
            return;
        }
        if (pixels != NULL) {
            if (texture_color_format(internal_format)) {
                uint32_t row;
                uint64_t source_row_pitch = texture_source_row_pitch(
                    (uint32_t)width, format,
                    context->unpack_alignment);

                for (row = 0u; row < (uint32_t)height; ++row) {
                    texture_copy_color_texels(
                        replacement + (uint64_t)row * (uint32_t)width * 4u,
                        (const uint8_t*)pixels + row * source_row_pitch,
                        internal_format, (uint32_t)width);
                }
            } else if (internal_format == RINGL_DEPTH24_STENCIL8) {
                uint32_t row;
                uint64_t source_row_pitch = texture_source_row_pitch(
                    (uint32_t)width, format,
                    context->unpack_alignment);

                for (row = 0u; row < (uint32_t)height; ++row) {
                    texture_copy_depth_stencil_texels(
                        replacement + (uint64_t)row * (uint32_t)width * 8u,
                        (const uint8_t*)pixels + row * source_row_pitch,
                        (uint32_t)width);
                }
            } else {
                uint32_t row;
                uint64_t source_row_pitch = texture_source_row_pitch(
                    (uint32_t)width, format,
                    context->unpack_alignment);

                for (row = 0u; row < (uint32_t)height; ++row) {
                    memcpy(replacement + (uint64_t)row * (uint32_t)width * 4u,
                           (const uint8_t*)pixels + row * source_row_pitch,
                           (size_t)(uint32_t)width * 4u);
                }
            }
        } else {
            memset(replacement, 0, (size_t)size);
        }
    }

    texture_discard_image(context, texture);
    free(texture->shadow_bytes);
    texture->shadow_bytes = replacement;
    texture->shadow_size = size;
    texture->width = (uint32_t)width;
    texture->height = (uint32_t)height;
    texture->format = internal_format;
    texture->defined = RINGL_TRUE;
    ringl_context_mark_dirty(context, RINGL_DIRTY_BINDINGS);
}

void ringl_tex_image_2d(uint32_t target, int32_t level,
                        uint32_t internal_format, int32_t width, int32_t height,
                        int32_t border, uint32_t format, uint32_t type,
                        const void* pixels)
{
    ringl_tex_image_2d_impl(target, level, internal_format, width, height,
                            border, format, type, pixels, UINT64_MAX);
}

void ringl_tex_image_2d_from_bytes(uint32_t target, int32_t level,
                                   uint32_t internal_format, int32_t width,
                                   int32_t height, int32_t border,
                                   uint32_t format, uint32_t type,
                                   const void* pixels, uint64_t pixels_size)
{
    ringl_tex_image_2d_impl(target, level, internal_format, width, height,
                            border, format, type, pixels, pixels_size);
}

static void ringl_tex_sub_image_2d_impl(uint32_t target, int32_t level,
                                        int32_t xoffset, int32_t yoffset,
                                        int32_t width, int32_t height,
                                        uint32_t format, uint32_t type,
                                        const void* pixels,
                                        uint64_t pixels_size)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLTextureObject* texture;
    uint32_t row;

    if (context == NULL)
        return;
    if (!texture_target_valid(target)) {
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
    if (!((texture_color_format(texture->format) &&
           texture->format == format && type == RINGL_UNSIGNED_BYTE) ||
          (texture->format == RINGL_DEPTH_COMPONENT32F &&
           format == RINGL_DEPTH_COMPONENT && type == RINGL_FLOAT) ||
          (texture->format == RINGL_DEPTH24_STENCIL8 &&
           format == RINGL_DEPTH_STENCIL && type == RINGL_UNSIGNED_INT_24_8))) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
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
    {
        uint64_t required_source_bytes;

        if (texture_required_source_bytes((uint32_t)width, (uint32_t)height,
                                          format, context->unpack_alignment,
                                          &required_source_bytes) != 0 ||
            pixels_size < required_source_bytes) {
            ringl_context_record_error(context, RINGL_INVALID_VALUE);
            return;
        }
    }

    for (row = 0u; row < (uint32_t)height; ++row) {
        uint64_t destination_offset =
            ((uint64_t)((uint32_t)yoffset + row) * texture->width +
             (uint32_t)xoffset) * texture_storage_texel_bytes(texture->format);
        uint64_t source_offset = (uint64_t)row *
            texture_source_row_pitch((uint32_t)width, format,
                                     context->unpack_alignment);

        if (texture_color_format(texture->format)) {
            texture_copy_color_texels(
                texture->shadow_bytes + destination_offset,
                (const uint8_t*)pixels + source_offset, texture->format,
                (uint32_t)width);
        } else if (texture->format == RINGL_DEPTH24_STENCIL8) {
            texture_copy_depth_stencil_texels(
                texture->shadow_bytes + destination_offset,
                (const uint8_t*)pixels + source_offset, (uint32_t)width);
        } else {
            memcpy(texture->shadow_bytes + destination_offset,
                   (const uint8_t*)pixels + source_offset,
                   (size_t)(uint32_t)width * 4u);
        }
    }

    /* Drop the realized image and rebuild the complete level lazily. This
     * avoids depending on immediate-image-upload availability while an image
     * may still be retained by a recorded command list. */
    texture_discard_image(context, texture);
    ringl_context_mark_dirty(context, RINGL_DIRTY_BINDINGS);
}

void ringl_copy_tex_sub_image_2d(uint32_t target, int32_t level,
                                 int32_t xoffset, int32_t yoffset,
                                 int32_t x, int32_t y,
                                 int32_t width, int32_t height)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLTextureObject* texture;
    RinGLColorTarget source;
    uint64_t size;
    uint8_t* replacement;
    uint32_t row;

    if (context == NULL)
        return;
    if (!texture_target_valid(target)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (level != 0 || xoffset < 0 || yoffset < 0 || x < 0 || y < 0 ||
        width < 0 || height < 0) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    texture = bound_texture_2d(context);
    if (texture == NULL || !texture->defined || texture->format != RINGL_RGBA ||
        texture->shadow_bytes == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if ((uint32_t)xoffset > texture->width ||
        (uint32_t)yoffset > texture->height ||
        (uint32_t)width > texture->width - (uint32_t)xoffset ||
        (uint32_t)height > texture->height - (uint32_t)yoffset) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (context->framebuffer_binding != 0u &&
        ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) !=
            RINGL_FRAMEBUFFER_COMPLETE) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if (ringl_resolve_color_target(context, &source) != 0) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if ((uint64_t)(uint32_t)x + (uint64_t)(uint32_t)width > source.width ||
        (uint64_t)(uint32_t)y + (uint64_t)(uint32_t)height > source.height) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (width == 0 || height == 0)
        return;
    size = (uint64_t)(uint32_t)width * (uint64_t)(uint32_t)height * 4u;
    if (size > SIZE_MAX) {
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return;
    }
    replacement = malloc((size_t)size);
    if (replacement == NULL) {
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return;
    }
    if (ringl_read_color_target_rgba(context, x, y, width, height,
                                     replacement) != 0) {
        free(replacement);
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    for (row = 0u; row < (uint32_t)height; ++row) {
        uint64_t destination_offset =
            ((uint64_t)((uint32_t)yoffset + row) * texture->width +
             (uint32_t)xoffset) * 4u;

        memcpy(texture->shadow_bytes + destination_offset,
               replacement + (uint64_t)row * (uint32_t)width * 4u,
               (size_t)(uint32_t)width * 4u);
    }
    free(replacement);
    texture_discard_image(context, texture);
    ringl_context_mark_dirty(context, RINGL_DIRTY_BINDINGS);
}

void ringl_tex_sub_image_2d(uint32_t target, int32_t level,
                            int32_t xoffset, int32_t yoffset,
                            int32_t width, int32_t height,
                            uint32_t format, uint32_t type,
                            const void* pixels)
{
    ringl_tex_sub_image_2d_impl(target, level, xoffset, yoffset, width,
                                height, format, type, pixels, UINT64_MAX);
}

void ringl_tex_sub_image_2d_from_bytes(uint32_t target, int32_t level,
                                       int32_t xoffset, int32_t yoffset,
                                       int32_t width, int32_t height,
                                       uint32_t format, uint32_t type,
                                       const void* pixels,
                                       uint64_t pixels_size)
{
    ringl_tex_sub_image_2d_impl(target, level, xoffset, yoffset, width,
                                height, format, type, pixels, pixels_size);
}

void ringl_copy_tex_image_2d(uint32_t target, int32_t level,
                             uint32_t internal_format, int32_t x, int32_t y,
                             int32_t width, int32_t height, int32_t border)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLTextureObject* texture;
    RinGLColorTarget source;
    uint64_t size;
    uint8_t* replacement;

    if (context == NULL)
        return;
    if (!texture_target_valid(target) || internal_format != RINGL_RGBA) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (level != 0 || border != 0 || x < 0 || y < 0 || width <= 0 ||
        height <= 0 || (uint32_t)width > RINGL_MAX_TEXTURE_SIZE ||
        (uint32_t)height > RINGL_MAX_TEXTURE_SIZE) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    texture = bound_texture_2d(context);
    if (texture == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if (context->framebuffer_binding != 0u &&
        ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) !=
            RINGL_FRAMEBUFFER_COMPLETE) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if (ringl_resolve_color_target(context, &source) != 0) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if ((uint64_t)(uint32_t)x + (uint64_t)(uint32_t)width > source.width ||
        (uint64_t)(uint32_t)y + (uint64_t)(uint32_t)height > source.height) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    size = (uint64_t)(uint32_t)width * (uint64_t)(uint32_t)height * 4u;
    if (size > SIZE_MAX) {
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return;
    }
    replacement = malloc((size_t)size);
    if (replacement == NULL) {
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return;
    }
    if (ringl_read_color_target_rgba(context, x, y, width, height,
                                     replacement) != 0) {
        free(replacement);
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }

    texture_discard_image(context, texture);
    free(texture->shadow_bytes);
    texture->shadow_bytes = replacement;
    texture->shadow_size = size;
    texture->width = (uint32_t)width;
    texture->height = (uint32_t)height;
    texture->format = RINGL_RGBA;
    texture->defined = RINGL_TRUE;
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
        texture_discard_image(context, &context->textures[index]);
        context->textures[index].ringpu_sampler = 0u;
        free(context->textures[index].shadow_bytes);
        context->textures[index].shadow_bytes = NULL;
        context->textures[index].shadow_size = 0u;
    }
}

int ringl_texture_require_color_target(RinGLContext* context, uint32_t texture)
{
    uint32_t index;
    RinGLTextureObject* object;

    if (context == NULL || texture == 0u ||
        ringl_object_lookup(context, texture, RINGL_OBJECT_TEXTURE) == NULL)
        return -1;
    index = ringl_object_slot_index(texture);
    if (index >= RINGL_OBJECT_SLOT_COUNT)
        return -1;
    object = &context->textures[index];
    if (!object->requires_color_target) {
        object->requires_color_target = RINGL_TRUE;
        texture_discard_image(context, object);
        ringl_context_mark_dirty(context, RINGL_DIRTY_BINDINGS |
                                          RINGL_DIRTY_FRAMEBUFFER);
    }
    return 0;
}

int ringl_texture_realize_color_target(RinGLContext* context, uint32_t texture,
                                       uint64_t* image_out,
                                       uint32_t** image_state_out,
                                       uint32_t* width_out,
                                       uint32_t* height_out)
{
    uint32_t index;
    RinGLTextureObject* object;

    if (context == NULL || image_out == NULL || image_state_out == NULL ||
        width_out == NULL || height_out == NULL ||
        ringl_texture_require_color_target(context, texture) != 0)
        return -1;
    index = ringl_object_slot_index(texture);
    if (index >= RINGL_OBJECT_SLOT_COUNT)
        return -1;
    object = &context->textures[index];
    if (!texture_level0_storage_defined(object) ||
        texture_realize_image(context, object) != 0 ||
        object->ringpu_image == 0u) {
        return -1;
    }
    *image_out = object->ringpu_image;
    *image_state_out = &object->ringpu_image_state;
    *width_out = object->width;
    *height_out = object->height;
    return 0;
}

int ringl_texture_realize_depth_target(RinGLContext* context, uint32_t texture,
                                       uint64_t* image_out,
                                       uint32_t** image_state_out,
                                       uint32_t* width_out,
                                       uint32_t* height_out)
{
    uint32_t index;
    RinGLTextureObject* object;

    if (context == NULL || texture == 0u || image_out == NULL ||
        image_state_out == NULL || width_out == NULL || height_out == NULL ||
        ringl_object_lookup(context, texture, RINGL_OBJECT_TEXTURE) == NULL)
        return -1;
    index = ringl_object_slot_index(texture);
    if (index >= RINGL_OBJECT_SLOT_COUNT)
        return -1;
    object = &context->textures[index];
    if (!texture_level0_storage_defined(object) ||
        (object->format != RINGL_DEPTH_COMPONENT32F &&
         object->format != RINGL_DEPTH24_STENCIL8) ||
        texture_realize_image(context, object) != 0 ||
        object->ringpu_image == 0u) {
        return -1;
    }
    *image_out = object->ringpu_image;
    *image_state_out = &object->ringpu_image_state;
    *width_out = object->width;
    *height_out = object->height;
    return 0;
}
