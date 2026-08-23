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
    memset(texture->ringpu_image_state, RINGL_RIN_GPU_IMAGE_UNDEFINED,
           sizeof(texture->ringpu_image_state));
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

static RinGLTextureMipStorage* texture_mip_storage(RinGLTextureObject* texture,
                                                    uint32_t level)
{
    if (texture == NULL || level == 0u ||
        level >= RINGL_MAX_TEXTURE_MIP_LEVELS) {
        return NULL;
    }
    return &texture->mip_storage[level - 1u];
}

static const RinGLTextureMipStorage* texture_mip_storage_const(
    const RinGLTextureObject* texture, uint32_t level)
{
    if (texture == NULL || level == 0u ||
        level >= RINGL_MAX_TEXTURE_MIP_LEVELS) {
        return NULL;
    }
    return &texture->mip_storage[level - 1u];
}

static int texture_level_storage_defined(const RinGLTextureObject* texture,
                                         uint32_t level)
{
    const RinGLTextureMipStorage* storage;

    if (texture == NULL)
        return 0;
    if (level == 0u) {
        return texture->defined != 0u && texture->width != 0u &&
               texture->height != 0u && texture->shadow_bytes != NULL &&
               texture->shadow_size != 0u;
    }
    storage = texture_mip_storage_const(texture, level);
    return storage != NULL && storage->defined != 0u && storage->width != 0u &&
           storage->height != 0u && storage->shadow_bytes != NULL &&
           storage->shadow_size != 0u;
}

static uint32_t texture_mip_level_count(uint32_t width, uint32_t height)
{
    uint32_t count = 1u;

    while ((width > 1u || height > 1u) &&
           count < RINGL_MAX_TEXTURE_MIP_LEVELS) {
        if (width > 1u)
            width >>= 1u;
        if (height > 1u)
            height >>= 1u;
        ++count;
    }
    return count;
}

static uint32_t texture_expected_mip_width(const RinGLTextureObject* texture,
                                           uint32_t level)
{
    uint32_t width;

    if (texture == NULL || level >= RINGL_MAX_TEXTURE_MIP_LEVELS)
        return 0u;
    width = texture->width;
    while (level != 0u && width > 1u) {
        width >>= 1u;
        --level;
    }
    return width;
}

static uint32_t texture_expected_mip_height(const RinGLTextureObject* texture,
                                            uint32_t level)
{
    uint32_t height;

    if (texture == NULL || level >= RINGL_MAX_TEXTURE_MIP_LEVELS)
        return 0u;
    height = texture->height;
    while (level != 0u && height > 1u) {
        height >>= 1u;
        --level;
    }
    return height;
}

uint32_t ringl_texture_sampled_mip_count(const RinGLTextureObject* texture)
{
    uint32_t level;
    uint32_t count = 1u;
    uint32_t level_count;

    if (!texture_level_storage_defined(texture, 0u))
        return 0u;
    level_count = texture_mip_level_count(texture->width, texture->height);
    for (level = 1u; level < level_count; ++level) {
        const RinGLTextureMipStorage* storage =
            texture_mip_storage_const(texture, level);

        if (!texture_level_storage_defined(texture, level) || storage == NULL ||
            storage->width != texture_expected_mip_width(texture, level) ||
            storage->height != texture_expected_mip_height(texture, level)) {
            break;
        }
        count = level + 1u;
    }
    return count;
}

/* FBO attachment completeness is defined per level, not by sampler
 * completeness.  A manually defined level 2 may therefore be attached even
 * when level 1 is absent.  The RinGPU image still needs an extent that covers
 * that sparse level; unprovided intermediate levels remain undefined and are
 * never sampled by this bounded profile. */
static uint32_t texture_highest_defined_mip_count(
    const RinGLTextureObject* texture)
{
    uint32_t level;
    uint32_t count = 0u;
    uint32_t level_count;

    if (!texture_level_storage_defined(texture, 0u))
        return 0u;
    level_count = texture_mip_level_count(texture->width, texture->height);
    for (level = 0u; level < level_count; ++level) {
        const RinGLTextureMipStorage* storage;

        if (level == 0u) {
            count = 1u;
            continue;
        }
        storage = texture_mip_storage_const(texture, level);
        if (texture_level_storage_defined(texture, level) && storage != NULL &&
            storage->width == texture_expected_mip_width(texture, level) &&
            storage->height == texture_expected_mip_height(texture, level)) {
            count = level + 1u;
        }
    }
    return count;
}

static void texture_drop_mip_storage_from(RinGLTextureObject* texture,
                                          uint32_t first_level)
{
    uint32_t level;

    if (texture == NULL || first_level == 0u)
        return;
    for (level = first_level; level < RINGL_MAX_TEXTURE_MIP_LEVELS; ++level) {
        RinGLTextureMipStorage* storage = texture_mip_storage(texture, level);

        if (storage == NULL)
            continue;
        free(storage->shadow_bytes);
        memset(storage, 0, sizeof(*storage));
    }
}

static void texture_drop_generated_mips(RinGLTextureObject* texture)
{
    uint32_t level;

    if (texture == NULL)
        return;
    for (level = 1u; level < RINGL_MAX_TEXTURE_MIP_LEVELS; ++level) {
        RinGLTextureMipStorage* storage = texture_mip_storage(texture, level);

        if (storage == NULL || storage->generated == 0u)
            continue;
        free(storage->shadow_bytes);
        memset(storage, 0, sizeof(*storage));
    }
}

static int texture_level0_storage_defined(const RinGLTextureObject* texture)
{
    return texture_level_storage_defined(texture, 0u);
}

static int texture_level0_complete(const RinGLTextureObject* texture)
{
    if (!texture_level0_storage_defined(texture))
        return 0;

    if (texture->min_filter == RINGL_NEAREST ||
        texture->min_filter == RINGL_LINEAR) {
        return 1;
    }

    {
        uint32_t level_count = texture_mip_level_count(texture->width,
                                                        texture->height);
        uint32_t level;

        for (level = 1u; level < level_count; ++level) {
            const RinGLTextureMipStorage* storage =
                texture_mip_storage_const(texture, level);

            if (!texture_level_storage_defined(texture, level) ||
                storage == NULL ||
                storage->width != texture_expected_mip_width(texture, level) ||
                storage->height != texture_expected_mip_height(texture, level)) {
                return 0;
            }
        }
    }
    return 1;
}

static int texture_color_format(uint32_t format)
{
    return format == RINGL_RGBA || format == RINGL_RGB ||
           format == RINGL_ALPHA || format == RINGL_LUMINANCE ||
           format == RINGL_LUMINANCE_ALPHA || format == RINGL_RGB565 ||
           format == RINGL_RGBA4 || format == RINGL_RGB5_A1;
}

static int texture_packed_color_format(uint32_t format)
{
    return format == RINGL_RGB565 || format == RINGL_RGBA4 ||
           format == RINGL_RGB5_A1;
}

static uint32_t texture_external_texel_bytes(uint32_t format, uint32_t type)
{
    if (type == RINGL_UNSIGNED_SHORT_5_6_5 ||
        type == RINGL_UNSIGNED_SHORT_4_4_4_4 ||
        type == RINGL_UNSIGNED_SHORT_5_5_5_1) {
        return 2u;
    }
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
                                          uint32_t type, uint32_t alignment)
{
    uint64_t row_bytes = (uint64_t)width *
                          texture_external_texel_bytes(format, type);

    return (row_bytes + alignment - 1u) & ~(uint64_t)(alignment - 1u);
}

/* The final source row has no required trailing alignment padding. This
 * calculation is an embedding boundary, so retain overflow checks even though
 * the current texture dimensions are deliberately small. */
static int texture_required_source_bytes(uint32_t width, uint32_t height,
                                          uint32_t format, uint32_t type,
                                          uint32_t alignment, uint64_t* bytes_out)
{
    uint64_t row_bytes;
    uint64_t row_pitch;

    if (bytes_out == NULL || alignment == 0u)
        return -1;
    if (width == 0u || height == 0u) {
        *bytes_out = 0u;
        return 0;
    }

    row_bytes = (uint64_t)width * texture_external_texel_bytes(format, type);
    if (row_bytes > UINT64_MAX - ((uint64_t)alignment - 1u))
        return -1;
    row_pitch = texture_source_row_pitch(width, format, type, alignment);
    if ((uint64_t)(height - 1u) > (UINT64_MAX - row_bytes) / row_pitch)
        return -1;

    *bytes_out = (uint64_t)(height - 1u) * row_pitch + row_bytes;
    return 0;
}

static void texture_copy_color_texels(uint8_t* destination,
                                      const uint8_t* source,
                                      uint32_t storage_format,
                                      uint32_t source_format, uint32_t type,
                                      uint32_t texel_count)
{
    uint32_t index;
    uint32_t source_texel_bytes = texture_external_texel_bytes(source_format,
                                                                type);

    if (texture_packed_color_format(storage_format)) {
        memcpy(destination, source, (size_t)texel_count * sizeof(uint16_t));
        return;
    }

    for (index = 0u; index < texel_count; ++index) {
        const uint8_t* source_texel =
            source + (uint64_t)index * source_texel_bytes;
        uint8_t* destination_texel = destination + (uint64_t)index * 4u;

        switch (source_format) {
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
    if (format == RINGL_DEPTH24_STENCIL8)
        return 8u;
    return texture_packed_color_format(format) ? 2u : 4u;
}

/* Packed formats retain their native component precision in the shadow image.
 * GenerateMipmap therefore averages the stored component values and quantizes
 * only at the destination level. This is equivalent to averaging normalized
 * values followed by conversion back to the same UNORM format, without first
 * expanding a 5/6/4-bit source through an avoidable 8-bit intermediate. */
static void texture_unpack_packed_color(uint32_t format, const uint8_t* source,
                                        uint8_t components[4])
{
    uint16_t packed;

    memcpy(&packed, source, sizeof(packed));
    if (format == RINGL_RGB565) {
        components[0] = (uint8_t)((packed >> 11u) & 0x1fu);
        components[1] = (uint8_t)((packed >> 5u) & 0x3fu);
        components[2] = (uint8_t)(packed & 0x1fu);
        components[3] = 1u;
        return;
    }
    if (format == RINGL_RGBA4) {
        components[0] = (uint8_t)((packed >> 12u) & 0xfu);
        components[1] = (uint8_t)((packed >> 8u) & 0xfu);
        components[2] = (uint8_t)((packed >> 4u) & 0xfu);
        components[3] = (uint8_t)(packed & 0xfu);
        return;
    }
    components[0] = (uint8_t)((packed >> 11u) & 0x1fu);
    components[1] = (uint8_t)((packed >> 6u) & 0x1fu);
    components[2] = (uint8_t)((packed >> 1u) & 0x1fu);
    components[3] = (uint8_t)(packed & 0x1u);
}

static uint16_t texture_pack_packed_color(uint32_t format,
                                          const uint8_t components[4])
{
    if (format == RINGL_RGB565) {
        return (uint16_t)(((uint16_t)components[0] << 11u) |
                          ((uint16_t)components[1] << 5u) |
                          components[2]);
    }
    if (format == RINGL_RGBA4) {
        return (uint16_t)(((uint16_t)components[0] << 12u) |
                          ((uint16_t)components[1] << 8u) |
                          ((uint16_t)components[2] << 4u) |
                          components[3]);
    }
    return (uint16_t)(((uint16_t)components[0] << 11u) |
                      ((uint16_t)components[1] << 6u) |
                      ((uint16_t)components[2] << 1u) |
                      components[3]);
}

/* copyTexSubImage2D reads the source color target as canonical RGBA8. Keep
 * packed texture shadows in their native precision by quantizing that
 * snapshot directly into the destination's stored components. */
static uint8_t texture_unorm8_to_packed_component(uint8_t value,
                                                   uint32_t maximum)
{
    return (uint8_t)(((uint32_t)value * maximum + UINT8_MAX / 2u) /
                     UINT8_MAX);
}

static void texture_copy_rgba_to_color_storage_texels(
    uint8_t* destination, const uint8_t* source, uint32_t storage_format,
    uint32_t texel_count)
{
    uint32_t index;

    if (!texture_packed_color_format(storage_format)) {
        memcpy(destination, source, (size_t)texel_count * 4u);
        return;
    }

    for (index = 0u; index < texel_count; ++index) {
        const uint8_t* source_texel = source + (uint64_t)index * 4u;
        uint8_t components[4];
        uint16_t packed;

        if (storage_format == RINGL_RGB565) {
            components[0] = texture_unorm8_to_packed_component(
                source_texel[0], 31u);
            components[1] = texture_unorm8_to_packed_component(
                source_texel[1], 63u);
            components[2] = texture_unorm8_to_packed_component(
                source_texel[2], 31u);
            components[3] = 0u;
        } else if (storage_format == RINGL_RGBA4) {
            components[0] = texture_unorm8_to_packed_component(
                source_texel[0], 15u);
            components[1] = texture_unorm8_to_packed_component(
                source_texel[1], 15u);
            components[2] = texture_unorm8_to_packed_component(
                source_texel[2], 15u);
            components[3] = texture_unorm8_to_packed_component(
                source_texel[3], 15u);
        } else {
            components[0] = texture_unorm8_to_packed_component(
                source_texel[0], 31u);
            components[1] = texture_unorm8_to_packed_component(
                source_texel[1], 31u);
            components[2] = texture_unorm8_to_packed_component(
                source_texel[2], 31u);
            components[3] = texture_unorm8_to_packed_component(
                source_texel[3], 1u);
        }
        packed = texture_pack_packed_color(storage_format, components);
        memcpy(destination + (uint64_t)index * sizeof(packed), &packed,
               sizeof(packed));
    }
}

/* copyTexImage2D snapshots canonical RGBA from its source color target. RGB
 * uses the ordinary four-byte shadow layout but preserves its implicit alpha
 * one, while packed destinations use the same direct quantization path as
 * copyTexSubImage2D. */
static void texture_copy_rgba_to_copy_image_storage(
    uint8_t* destination, const uint8_t* source, uint32_t storage_format,
    uint32_t texel_count)
{
    uint32_t index;

    texture_copy_rgba_to_color_storage_texels(destination, source,
                                               storage_format, texel_count);
    if (storage_format != RINGL_RGB)
        return;
    for (index = 0u; index < texel_count; ++index)
        destination[(uint64_t)index * 4u + 3u] = UINT8_MAX;
}

static uint32_t texture_ringpu_format(uint32_t format)
{
    if (format == RINGL_RGB565)
        return RINGL_RIN_GPU_FORMAT_RGB565_UNORM;
    if (format == RINGL_RGBA4)
        return RINGL_RIN_GPU_FORMAT_RGBA4_UNORM;
    if (format == RINGL_RGB5_A1)
        return RINGL_RIN_GPU_FORMAT_RGB5_A1_UNORM;
    return RINGL_RIN_GPU_FORMAT_RGBA8_UNORM;
}

static uint32_t texture_storage_format(uint32_t internal_format,
                                       uint32_t format, uint32_t type)
{
    if (internal_format == RINGL_RGB && format == RINGL_RGB &&
        type == RINGL_UNSIGNED_SHORT_5_6_5)
        return RINGL_RGB565;
    if (internal_format == RINGL_RGBA && format == RINGL_RGBA &&
        type == RINGL_UNSIGNED_SHORT_4_4_4_4)
        return RINGL_RGBA4;
    if (internal_format == RINGL_RGBA && format == RINGL_RGBA &&
        type == RINGL_UNSIGNED_SHORT_5_5_5_1)
        return RINGL_RGB5_A1;
    if (texture_color_format(internal_format) && internal_format == format &&
        type == RINGL_UNSIGNED_BYTE)
        return internal_format;
    return 0u;
}

static int texture_upload_format_valid(uint32_t storage_format,
                                       uint32_t format, uint32_t type)
{
    if (storage_format == RINGL_RGB565)
        return format == RINGL_RGB && type == RINGL_UNSIGNED_SHORT_5_6_5;
    if (storage_format == RINGL_RGBA4)
        return format == RINGL_RGBA && type == RINGL_UNSIGNED_SHORT_4_4_4_4;
    if (storage_format == RINGL_RGB5_A1)
        return format == RINGL_RGBA && type == RINGL_UNSIGNED_SHORT_5_5_5_1;
    return texture_color_format(storage_format) &&
           !texture_packed_color_format(storage_format) &&
           storage_format == format && type == RINGL_UNSIGNED_BYTE;
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
    if (value == RINGL_NEAREST_MIPMAP_NEAREST ||
        value == RINGL_LINEAR_MIPMAP_NEAREST) {
        return RINGL_RIN_GPU_SAMPLER_NEAREST;
    }
    return RINGL_RIN_GPU_SAMPLER_MIP_NONE;
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
    memset(texture->ringpu_image_state, RINGL_RIN_GPU_IMAGE_UNDEFINED,
           sizeof(texture->ringpu_image_state));
}

static int texture_realize_image(RinGLContext* context,
                                 RinGLTextureObject* texture)
{
    RinGLRinGpuSampledImage2DV1 desc;
    RinGLRinGpuImage2DV1 color_target_desc;
    RinGLRinGpuImage2DMipV2 mip_desc;
    RinGLRinGpuImageUpload2DV1 upload;
    RinGLRinGpuImageUpload2DMipV2 mip_upload;
    uint64_t image = 0u;
    uint32_t mip_count;
    uint32_t level;

    if (texture->ringpu_image != 0u)
        return 0;
    if ((texture->requires_color_target ||
         texture->format == RINGL_DEPTH_COMPONENT32F ||
         texture->format == RINGL_DEPTH24_STENCIL8)
            ? !texture_level0_storage_defined(texture)
            : !texture_level0_complete(texture))
        return -1;

    mip_count = (texture->requires_color_target != 0u ||
                 texture->format == RINGL_DEPTH_COMPONENT32F ||
                 texture->format == RINGL_DEPTH24_STENCIL8)
        ? texture_highest_defined_mip_count(texture)
        : ringl_texture_sampled_mip_count(texture);
    if (mip_count == 0u)
        return -1;

    if (mip_count > 1u) {
        if ((!texture_color_format(texture->format) &&
             texture->format != RINGL_DEPTH_COMPONENT32F &&
             texture->format != RINGL_DEPTH24_STENCIL8) ||
            (texture->requires_color_target != 0u &&
             texture->format != RINGL_RGBA &&
             !texture_packed_color_format(texture->format))) {
            return -1;
        }
        memset(&mip_desc, 0, sizeof(mip_desc));
        mip_desc.width = texture->width;
        mip_desc.height = texture->height;
        mip_desc.format = texture->format == RINGL_DEPTH24_STENCIL8
            ? RINGL_RIN_GPU_FORMAT_D32_FLOAT_S8_UINT
            : texture->format == RINGL_DEPTH_COMPONENT32F
                ? RINGL_RIN_GPU_FORMAT_D32_FLOAT
                : texture_ringpu_format(texture->format);
        mip_desc.mip_levels = mip_count;
        mip_desc.usage = RINGL_RIN_GPU_IMAGE_USAGE_COPY_DESTINATION |
                         RINGL_RIN_GPU_IMAGE_USAGE_SAMPLED;
        if (texture->requires_color_target != 0u) {
            mip_desc.usage |= RINGL_RIN_GPU_IMAGE_USAGE_COLOR_TARGET |
                              RINGL_RIN_GPU_IMAGE_USAGE_COPY_SOURCE;
        }
        if (texture->format == RINGL_DEPTH_COMPONENT32F ||
            texture->format == RINGL_DEPTH24_STENCIL8) {
            mip_desc.usage |= RINGL_RIN_GPU_IMAGE_USAGE_DEPTH_STENCIL;
        }
        if (ringl_backend_create_image_2d_mip_v2(context, &mip_desc,
                                                 &image) != 0 ||
            image == 0u) {
            return -1;
        }
    } else if (texture->format == RINGL_DEPTH_COMPONENT32F ||
               texture->format == RINGL_DEPTH24_STENCIL8) {
        memset(&color_target_desc, 0, sizeof(color_target_desc));
        color_target_desc.width = texture->width;
        color_target_desc.height = texture->height;
        color_target_desc.format = texture->format == RINGL_DEPTH24_STENCIL8
            ? RINGL_RIN_GPU_FORMAT_D32_FLOAT_S8_UINT
            : RINGL_RIN_GPU_FORMAT_D32_FLOAT;
        color_target_desc.usage =
            RINGL_RIN_GPU_IMAGE_USAGE_COPY_DESTINATION |
            RINGL_RIN_GPU_IMAGE_USAGE_DEPTH_STENCIL |
            RINGL_RIN_GPU_IMAGE_USAGE_SAMPLED;
        if (ringl_backend_create_image_2d(context, &color_target_desc,
                                          &image) != 0 || image == 0u) {
            return -1;
        }
    } else if ((texture->format == RINGL_RGBA ||
                texture_packed_color_format(texture->format)) &&
               texture->requires_color_target) {
        memset(&color_target_desc, 0, sizeof(color_target_desc));
        color_target_desc.width = texture->width;
        color_target_desc.height = texture->height;
        color_target_desc.format = texture_ringpu_format(texture->format);
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
        desc.format = texture_ringpu_format(texture->format);
        if (ringl_backend_create_sampled_image_2d(context, &desc, &image) != 0 ||
            image == 0u) {
            return -1;
        }
    } else {
        return -1;
    }

    memset(texture->ringpu_image_state, RINGL_RIN_GPU_IMAGE_UNDEFINED,
           sizeof(texture->ringpu_image_state));
    if (mip_count == 1u) {
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
        texture->ringpu_image_state[0] =
            RINGL_RIN_GPU_IMAGE_COPY_DESTINATION;
    } else {
        for (level = 0u; level < mip_count; ++level) {
            const uint8_t* bytes;
            uint64_t size;
            uint32_t width;
            uint32_t height;

            if (level == 0u) {
                bytes = texture->shadow_bytes;
                size = texture->shadow_size;
                width = texture->width;
                height = texture->height;
            } else {
                const RinGLTextureMipStorage* storage =
                    texture_mip_storage_const(texture, level);

                if (storage == NULL || !texture_level_storage_defined(texture, level))
                    continue;
                bytes = storage->shadow_bytes;
                size = storage->shadow_size;
                width = storage->width;
                height = storage->height;
            }
            memset(&mip_upload, 0, sizeof(mip_upload));
            mip_upload.mip_level = level;
            mip_upload.width = width;
            mip_upload.height = height;
            mip_upload.source_row_pitch_bytes = (uint64_t)width *
                texture_storage_texel_bytes(texture->format);
            if (ringl_backend_upload_image_2d_mip_v2(context, image,
                                                      &mip_upload, bytes,
                                                      size) != 0) {
                ringl_backend_destroy_object(context, image);
                return -1;
            }
            texture->ringpu_image_state[level] =
                RINGL_RIN_GPU_IMAGE_COPY_DESTINATION;
        }
    }

    texture->ringpu_image = image;
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
          texture->format == RINGL_DEPTH_COMPONENT32F ||
          texture->format == RINGL_DEPTH24_STENCIL8) ||
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
            texture_drop_mip_storage_from(&context->textures[slot_index], 1u);
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

static void texture_free_generated_mips(
    RinGLTextureMipStorage generated[RINGL_MAX_TEXTURE_MIP_LEVELS - 1u])
{
    uint32_t index;

    for (index = 0u; index < RINGL_MAX_TEXTURE_MIP_LEVELS - 1u; ++index) {
        free(generated[index].shadow_bytes);
        memset(&generated[index], 0, sizeof(generated[index]));
    }
}

static int texture_generate_color_mips(
    const RinGLTextureObject* texture,
    RinGLTextureMipStorage generated[RINGL_MAX_TEXTURE_MIP_LEVELS - 1u])
{
    const uint8_t* source = texture->shadow_bytes;
    uint32_t source_width = texture->width;
    uint32_t source_height = texture->height;
    uint32_t texel_bytes = texture_storage_texel_bytes(texture->format);
    uint32_t level_count = texture_mip_level_count(texture->width,
                                                    texture->height);
    uint32_t level;

    memset(generated, 0, sizeof(RinGLTextureMipStorage) *
                         (RINGL_MAX_TEXTURE_MIP_LEVELS - 1u));
    for (level = 1u; level < level_count; ++level) {
        RinGLTextureMipStorage* destination = &generated[level - 1u];
        uint32_t x;
        uint32_t y;
        uint64_t size;

        destination->width = source_width > 1u ? source_width >> 1u : 1u;
        destination->height = source_height > 1u ? source_height >> 1u : 1u;
        size = (uint64_t)destination->width * destination->height *
               texel_bytes;
        if (size > SIZE_MAX) {
            texture_free_generated_mips(generated);
            return -1;
        }
        destination->shadow_bytes = malloc((size_t)size);
        if (destination->shadow_bytes == NULL) {
            texture_free_generated_mips(generated);
            return -1;
        }
        destination->shadow_size = size;
        destination->defined = RINGL_TRUE;
        destination->generated = RINGL_TRUE;
        for (y = 0u; y < destination->height; ++y) {
            uint32_t source_y0 = y * 2u;
            uint32_t source_y1 = source_y0 + 1u < source_height
                ? source_y0 + 1u : source_y0;

            for (x = 0u; x < destination->width; ++x) {
                uint32_t source_x0 = x * 2u;
                uint32_t source_x1 = source_x0 + 1u < source_width
                    ? source_x0 + 1u : source_x0;
                const uint8_t* a = source +
                    ((uint64_t)source_y0 * source_width + source_x0) *
                        texel_bytes;
                const uint8_t* b = source +
                    ((uint64_t)source_y0 * source_width + source_x1) *
                        texel_bytes;
                const uint8_t* c = source +
                    ((uint64_t)source_y1 * source_width + source_x0) *
                        texel_bytes;
                const uint8_t* d = source +
                    ((uint64_t)source_y1 * source_width + source_x1) *
                        texel_bytes;
                uint8_t* output = destination->shadow_bytes +
                    ((uint64_t)y * destination->width + x) * texel_bytes;
                uint32_t component;

                if (texture_packed_color_format(texture->format)) {
                    uint8_t a_components[4];
                    uint8_t b_components[4];
                    uint8_t c_components[4];
                    uint8_t d_components[4];
                    uint8_t output_components[4];
                    uint16_t packed;

                    texture_unpack_packed_color(texture->format, a,
                                                a_components);
                    texture_unpack_packed_color(texture->format, b,
                                                b_components);
                    texture_unpack_packed_color(texture->format, c,
                                                c_components);
                    texture_unpack_packed_color(texture->format, d,
                                                d_components);
                    for (component = 0u; component < 4u; ++component) {
                        output_components[component] = (uint8_t)(
                            ((uint32_t)a_components[component] +
                             b_components[component] + c_components[component] +
                             d_components[component] + 2u) / 4u);
                    }
                    packed = texture_pack_packed_color(texture->format,
                                                        output_components);
                    memcpy(output, &packed, sizeof(packed));
                } else {
                    for (component = 0u; component < 4u; ++component) {
                        output[component] = (uint8_t)(
                            ((uint32_t)a[component] + b[component] +
                             c[component] + d[component] + 2u) / 4u);
                    }
                }
            }
        }
        source = destination->shadow_bytes;
        source_width = destination->width;
        source_height = destination->height;
    }
    return 0;
}

void ringl_generate_mipmap(uint32_t target)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLTextureObject* texture;
    RinGLTextureMipStorage generated[RINGL_MAX_TEXTURE_MIP_LEVELS - 1u];
    uint32_t level_count;
    uint32_t level;

    if (context == NULL)
        return;
    if (!texture_target_valid(target)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    texture = bound_texture_2d(context);
    if (texture == NULL || !texture_level0_storage_defined(texture) ||
        !texture_color_format(texture->format)) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    level_count = texture_mip_level_count(texture->width, texture->height);
    if (level_count > 1u &&
        (!context->has_ringpu_ops ||
         context->ringpu_ops.create_image_2d_mip_v2 == NULL ||
         context->ringpu_ops.upload_image_2d_mip_v2 == NULL)) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if (texture_generate_color_mips(texture, generated) != 0) {
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return;
    }

    texture_discard_image(context, texture);
    texture_drop_mip_storage_from(texture, 1u);
    for (level = 1u; level < level_count; ++level) {
        texture->mip_storage[level - 1u] = generated[level - 1u];
        memset(&generated[level - 1u], 0, sizeof(generated[level - 1u]));
    }
    texture_free_generated_mips(generated);
    ringl_context_mark_dirty(context, RINGL_DIRTY_BINDINGS);
}

static void ringl_tex_image_2d_impl(uint32_t target, int32_t level,
                                    uint32_t internal_format, int32_t width,
                                    int32_t height, int32_t border,
                                    uint32_t format, uint32_t type,
                                    const void* pixels, uint64_t pixels_size)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLTextureObject* texture;
    RinGLTextureMipStorage* mip_storage = NULL;
    uint8_t* replacement = NULL;
    uint64_t size;
    uint32_t storage_format;

    if (context == NULL)
        return;
    storage_format = texture_storage_format(internal_format, format, type);
    if (!texture_target_valid(target) ||
        !(storage_format != 0u ||
          (internal_format == RINGL_DEPTH_COMPONENT32F &&
           format == RINGL_DEPTH_COMPONENT && type == RINGL_FLOAT) ||
          (internal_format == RINGL_DEPTH24_STENCIL8 &&
           format == RINGL_DEPTH_STENCIL && type == RINGL_UNSIGNED_INT_24_8))) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (level < 0 || (uint32_t)level >= RINGL_MAX_TEXTURE_MIP_LEVELS ||
        border != 0 || width < 0 || height < 0 ||
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
    if (level != 0) {
        uint32_t mip_level = (uint32_t)level;
        uint32_t requested_format = storage_format != 0u
            ? storage_format : internal_format;

        if (!texture_level0_storage_defined(texture) ||
            !((storage_format != 0u && texture_color_format(storage_format)) ||
              requested_format == RINGL_DEPTH_COMPONENT32F ||
              requested_format == RINGL_DEPTH24_STENCIL8) ||
            texture->format != requested_format) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return;
        }
        if (mip_level >= texture_mip_level_count(texture->width, texture->height) ||
            (uint32_t)width != texture_expected_mip_width(texture, mip_level) ||
            (uint32_t)height != texture_expected_mip_height(texture, mip_level)) {
            ringl_context_record_error(context, RINGL_INVALID_VALUE);
            return;
        }
        mip_storage = texture_mip_storage(texture, mip_level);
        if (mip_storage == NULL) {
            ringl_context_record_error(context, RINGL_INVALID_VALUE);
            return;
        }
    }
    if (pixels != NULL) {
        uint64_t required_source_bytes;

        if (texture_required_source_bytes((uint32_t)width, (uint32_t)height,
                                          format, type, context->unpack_alignment,
                                          &required_source_bytes) != 0 ||
            pixels_size < required_source_bytes) {
            ringl_context_record_error(context, RINGL_INVALID_VALUE);
            return;
        }
    }

    size = (uint64_t)(uint32_t)width * (uint64_t)(uint32_t)height *
           texture_storage_texel_bytes(storage_format != 0u ? storage_format
                                                             : internal_format);
    if (size > SIZE_MAX) {
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return;
    }
    if (size != 0u) {
        replacement = malloc((size_t)size);
        if (replacement == NULL) {
            ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
            return;
        }
        if (pixels != NULL) {
            if (storage_format != 0u) {
                uint32_t row;
                uint64_t source_row_pitch = texture_source_row_pitch(
                    (uint32_t)width, format, type,
                    context->unpack_alignment);

                for (row = 0u; row < (uint32_t)height; ++row) {
                    texture_copy_color_texels(
                        replacement + (uint64_t)row * (uint32_t)width *
                            texture_storage_texel_bytes(storage_format),
                        (const uint8_t*)pixels + row * source_row_pitch,
                        storage_format, format, type, (uint32_t)width);
                }
            } else if (internal_format == RINGL_DEPTH24_STENCIL8) {
                uint32_t row;
                uint64_t source_row_pitch = texture_source_row_pitch(
                    (uint32_t)width, format, type,
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
                    (uint32_t)width, format, type,
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
    if (level == 0) {
        /* A new base definition can change the entire level hierarchy. Drop
         * every old level only after allocation and conversion succeeded. */
        texture_drop_mip_storage_from(texture, 1u);
        free(texture->shadow_bytes);
        texture->shadow_bytes = replacement;
        texture->shadow_size = size;
        texture->width = (uint32_t)width;
        texture->height = (uint32_t)height;
        texture->format = storage_format != 0u ? storage_format : internal_format;
        texture->defined = RINGL_TRUE;
    } else {
        free(mip_storage->shadow_bytes);
        mip_storage->shadow_bytes = replacement;
        mip_storage->shadow_size = size;
        mip_storage->width = (uint32_t)width;
        mip_storage->height = (uint32_t)height;
        mip_storage->defined = RINGL_TRUE;
        mip_storage->generated = RINGL_FALSE;
    }
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
    RinGLTextureMipStorage* mip_storage = NULL;
    uint8_t* level_bytes;
    uint32_t level_width;
    uint32_t level_height;
    uint32_t row;

    if (context == NULL)
        return;
    if (!texture_target_valid(target)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (level < 0 || (uint32_t)level >= RINGL_MAX_TEXTURE_MIP_LEVELS ||
        xoffset < 0 || yoffset < 0 || width < 0 || height < 0) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }

    texture = bound_texture_2d(context);
    if (texture == NULL || !texture_level0_storage_defined(texture)) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if (level == 0) {
        level_bytes = texture->shadow_bytes;
        level_width = texture->width;
        level_height = texture->height;
    } else {
        mip_storage = texture_mip_storage(texture, (uint32_t)level);
        if (!texture_level_storage_defined(texture, (uint32_t)level) ||
            mip_storage == NULL) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return;
        }
        level_bytes = mip_storage->shadow_bytes;
        level_width = mip_storage->width;
        level_height = mip_storage->height;
    }
    if (!(texture_upload_format_valid(texture->format, format, type) ||
          (texture->format == RINGL_DEPTH_COMPONENT32F &&
           format == RINGL_DEPTH_COMPONENT && type == RINGL_FLOAT) ||
          (texture->format == RINGL_DEPTH24_STENCIL8 &&
           format == RINGL_DEPTH_STENCIL && type == RINGL_UNSIGNED_INT_24_8))) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if ((uint32_t)xoffset > level_width || (uint32_t)yoffset > level_height ||
        (uint32_t)width > level_width - (uint32_t)xoffset ||
        (uint32_t)height > level_height - (uint32_t)yoffset) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (width == 0 || height == 0)
        return;
    if (pixels == NULL || level_bytes == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    {
        uint64_t required_source_bytes;

        if (texture_required_source_bytes((uint32_t)width, (uint32_t)height,
                                          format, type, context->unpack_alignment,
                                          &required_source_bytes) != 0 ||
            pixels_size < required_source_bytes) {
            ringl_context_record_error(context, RINGL_INVALID_VALUE);
            return;
        }
    }

    for (row = 0u; row < (uint32_t)height; ++row) {
        uint64_t destination_offset =
            ((uint64_t)((uint32_t)yoffset + row) * level_width +
             (uint32_t)xoffset) * texture_storage_texel_bytes(texture->format);
        uint64_t source_offset = (uint64_t)row *
            texture_source_row_pitch((uint32_t)width, format, type,
                                     context->unpack_alignment);

        if (texture_color_format(texture->format)) {
            texture_copy_color_texels(
                level_bytes + destination_offset,
                (const uint8_t*)pixels + source_offset, texture->format,
                format, type, (uint32_t)width);
        } else if (texture->format == RINGL_DEPTH24_STENCIL8) {
            texture_copy_depth_stencil_texels(
                level_bytes + destination_offset,
                (const uint8_t*)pixels + source_offset, (uint32_t)width);
        } else {
            memcpy(level_bytes + destination_offset,
                   (const uint8_t*)pixels + source_offset,
                   (size_t)(uint32_t)width * 4u);
        }
    }

    /* Drop the realized image and rebuild the complete chain lazily. A base
     * update invalidates only derived levels; explicitly defined levels retain
     * their WebGL image contents. */
    texture_discard_image(context, texture);
    if (level == 0)
        texture_drop_generated_mips(texture);
    else
        mip_storage->generated = RINGL_FALSE;
    ringl_context_mark_dirty(context, RINGL_DIRTY_BINDINGS);
}

void ringl_copy_tex_sub_image_2d(uint32_t target, int32_t level,
                                 int32_t xoffset, int32_t yoffset,
                                 int32_t x, int32_t y,
                                 int32_t width, int32_t height)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLTextureObject* texture;
    RinGLTextureMipStorage* mip_storage = NULL;
    RinGLColorTarget source;
    uint64_t size;
    uint8_t* replacement;
    uint8_t* level_bytes;
    uint32_t level_width;
    uint32_t level_height;
    uint32_t row;

    if (context == NULL)
        return;
    if (!texture_target_valid(target)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (level < 0 || (uint32_t)level >= RINGL_MAX_TEXTURE_MIP_LEVELS ||
        xoffset < 0 || yoffset < 0 || x < 0 || y < 0 || width < 0 ||
        height < 0) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    texture = bound_texture_2d(context);
    if (texture == NULL || !texture_level0_storage_defined(texture) ||
        (texture->format != RINGL_RGBA &&
         !texture_packed_color_format(texture->format))) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if (level == 0) {
        level_bytes = texture->shadow_bytes;
        level_width = texture->width;
        level_height = texture->height;
    } else {
        mip_storage = texture_mip_storage(texture, (uint32_t)level);
        if (!texture_level_storage_defined(texture, (uint32_t)level) ||
            mip_storage == NULL) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return;
        }
        level_bytes = mip_storage->shadow_bytes;
        level_width = mip_storage->width;
        level_height = mip_storage->height;
    }
    if (level_bytes == NULL || (uint32_t)xoffset > level_width ||
        (uint32_t)yoffset > level_height ||
        (uint32_t)width > level_width - (uint32_t)xoffset ||
        (uint32_t)height > level_height - (uint32_t)yoffset) {
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
            ((uint64_t)((uint32_t)yoffset + row) * level_width +
             (uint32_t)xoffset) * texture_storage_texel_bytes(texture->format);

        texture_copy_rgba_to_color_storage_texels(
            level_bytes + destination_offset,
            replacement + (uint64_t)row * (uint32_t)width * 4u,
            texture->format, (uint32_t)width);
    }
    free(replacement);
    texture_discard_image(context, texture);
    if (level == 0)
        texture_drop_generated_mips(texture);
    else
        mip_storage->generated = RINGL_FALSE;
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
    uint64_t snapshot_size;
    uint64_t replacement_size;
    uint8_t* snapshot;
    uint8_t* replacement;

    if (context == NULL)
        return;
    if (!texture_target_valid(target) ||
        (internal_format != RINGL_RGBA && internal_format != RINGL_RGB &&
         !texture_packed_color_format(internal_format))) {
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
    snapshot_size = (uint64_t)(uint32_t)width * (uint64_t)(uint32_t)height *
                    4u;
    replacement_size = (uint64_t)(uint32_t)width * (uint64_t)(uint32_t)height *
                       texture_storage_texel_bytes(internal_format);
    if (snapshot_size > SIZE_MAX || replacement_size > SIZE_MAX) {
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return;
    }
    snapshot = malloc((size_t)snapshot_size);
    if (snapshot == NULL) {
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return;
    }
    if (ringl_read_color_target_rgba(context, x, y, width, height,
                                     snapshot) != 0) {
        free(snapshot);
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    replacement = malloc((size_t)replacement_size);
    if (replacement == NULL) {
        free(snapshot);
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return;
    }
    texture_copy_rgba_to_copy_image_storage(
        replacement, snapshot, internal_format,
        (uint32_t)width * (uint32_t)height);
    free(snapshot);

    texture_discard_image(context, texture);
    texture_drop_mip_storage_from(texture, 1u);
    free(texture->shadow_bytes);
    texture->shadow_bytes = replacement;
    texture->shadow_size = replacement_size;
    texture->width = (uint32_t)width;
    texture->height = (uint32_t)height;
    texture->format = internal_format;
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
        texture_drop_mip_storage_from(&context->textures[index], 1u);
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
                                       uint32_t mip_level,
                                       uint64_t* image_out,
                                       uint32_t** image_state_out,
                                       uint32_t* width_out,
                                       uint32_t* height_out)
{
    uint32_t index;
    RinGLTextureObject* object;

    if (context == NULL || mip_level >= RINGL_MAX_TEXTURE_MIP_LEVELS ||
        image_out == NULL || image_state_out == NULL ||
        width_out == NULL || height_out == NULL ||
        ringl_texture_require_color_target(context, texture) != 0)
        return -1;
    index = ringl_object_slot_index(texture);
    if (index >= RINGL_OBJECT_SLOT_COUNT)
        return -1;
    object = &context->textures[index];
    if (!texture_level_storage_defined(object, mip_level) ||
        texture_realize_image(context, object) != 0 ||
        object->ringpu_image == 0u) {
        return -1;
    }
    *image_out = object->ringpu_image;
    *image_state_out = &object->ringpu_image_state[mip_level];
    if (mip_level == 0u) {
        *width_out = object->width;
        *height_out = object->height;
    } else {
        const RinGLTextureMipStorage* storage =
            texture_mip_storage_const(object, mip_level);

        if (storage == NULL)
            return -1;
        *width_out = storage->width;
        *height_out = storage->height;
    }
    return 0;
}

int ringl_texture_realize_depth_target(RinGLContext* context, uint32_t texture,
                                       uint32_t mip_level,
                                       uint64_t* image_out,
                                       uint32_t** image_state_out,
                                       uint32_t* width_out,
                                       uint32_t* height_out)
{
    uint32_t index;
    RinGLTextureObject* object;

    if (context == NULL || texture == 0u ||
        mip_level >= RINGL_MAX_TEXTURE_MIP_LEVELS || image_out == NULL ||
        image_state_out == NULL || width_out == NULL || height_out == NULL ||
        ringl_object_lookup(context, texture, RINGL_OBJECT_TEXTURE) == NULL)
        return -1;
    index = ringl_object_slot_index(texture);
    if (index >= RINGL_OBJECT_SLOT_COUNT)
        return -1;
    object = &context->textures[index];
    if (!texture_level_storage_defined(object, mip_level) ||
        (object->format != RINGL_DEPTH_COMPONENT32F &&
         object->format != RINGL_DEPTH24_STENCIL8) ||
        texture_realize_image(context, object) != 0 ||
        object->ringpu_image == 0u) {
        return -1;
    }
    *image_out = object->ringpu_image;
    *image_state_out = &object->ringpu_image_state[mip_level];
    if (mip_level == 0u) {
        *width_out = object->width;
        *height_out = object->height;
    } else {
        const RinGLTextureMipStorage* storage =
            texture_mip_storage_const(object, mip_level);

        if (storage == NULL)
            return -1;
        *width_out = storage->width;
        *height_out = storage->height;
    }
    return 0;
}
