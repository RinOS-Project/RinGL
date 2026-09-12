/* SPDX-License-Identifier: MIT */
#include "../ringl_internal.h"

#include <float.h>
#include <stdlib.h>
#include <string.h>

static int texture_target_valid(uint32_t target)
{
    return target == RINGL_TEXTURE_2D || target == RINGL_TEXTURE_CUBE_MAP;
}

static int texture_cube_face_index(uint32_t target, uint32_t* index_out)
{
    if (index_out == NULL || target < RINGL_TEXTURE_CUBE_MAP_POSITIVE_X ||
        target > RINGL_TEXTURE_CUBE_MAP_NEGATIVE_Z)
        return 0;
    *index_out = target - RINGL_TEXTURE_CUBE_MAP_POSITIVE_X;
    return 1;
}

static int texture_image_target_valid(uint32_t target)
{
    uint32_t face;

    return target == RINGL_TEXTURE_2D ||
           texture_cube_face_index(target, &face);
}

static int texture_dimension_is_power_of_two(uint32_t dimension);
static const RinGLTextureMipStorage* texture_cube_mip_storage_const(
    const RinGLTextureCubeFaceStorage* face, uint32_t level);
static int texture_cube_level_storage_defined(
    const RinGLTextureCubeFaceStorage* face, uint32_t level);
static uint32_t texture_cube_expected_mip_width(
    const RinGLTextureCubeFaceStorage* face, uint32_t level);
static uint32_t texture_cube_expected_mip_height(
    const RinGLTextureCubeFaceStorage* face, uint32_t level);
static int texture_cube_base_images_complete(
    const RinGLTextureObject* texture);

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
    texture->max_anisotropy = 1.0f;
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

static RinGLTextureObject* bound_texture_cube(RinGLContext* context)
{
    uint32_t name;
    uint32_t slot_index;

    if (context == NULL || context->active_texture_unit >= RINGL_MAX_TEXTURE_UNITS)
        return NULL;
    name = context->bound_texture_cube[context->active_texture_unit];
    if (name == 0u ||
        ringl_object_lookup(context, name, RINGL_OBJECT_TEXTURE) == NULL)
        return NULL;
    slot_index = ringl_object_slot_index(name);
    if (slot_index >= RINGL_OBJECT_SLOT_COUNT)
        return NULL;
    return &context->textures[slot_index];
}

static RinGLTextureObject* bound_texture_for_target(RinGLContext* context,
                                                    uint32_t target)
{
    return target == RINGL_TEXTURE_CUBE_MAP
        ? bound_texture_cube(context) : bound_texture_2d(context);
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

    if (texture != NULL && texture->target == RINGL_TEXTURE_CUBE_MAP) {
        const RinGLTextureCubeFaceStorage* first =
            &texture->cube_faces[0];

        if (!texture_cube_base_images_complete(texture))
            return 0u;
        level_count = texture_mip_level_count(first->width, first->height);
        for (level = 1u; level < level_count; ++level) {
            uint32_t face_index;

            for (face_index = 0u; face_index < RINGL_CUBE_FACE_COUNT;
                 ++face_index) {
                const RinGLTextureCubeFaceStorage* face =
                    &texture->cube_faces[face_index];
                const RinGLTextureMipStorage* storage =
                    texture_cube_mip_storage_const(face, level);

                if (!texture_cube_level_storage_defined(face, level) ||
                    storage == NULL ||
                    storage->width != texture_cube_expected_mip_width(face, level) ||
                    storage->height != texture_cube_expected_mip_height(face, level))
                    return level;
            }
        }
        return level_count;
    }
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

static void texture_release_storage(RinGLContext* context, uint8_t** bytes,
                                    uint64_t* size)
{
    uint64_t owned_size = size != NULL ? *size : 0u;

    if (context != NULL)
        ringl_context_release_shadow_bytes(context, owned_size);
    if (bytes != NULL)
        free(*bytes);
    if (bytes != NULL)
        *bytes = NULL;
    if (size != NULL)
        *size = 0u;
}

static RinGLTextureMipStorage* texture_cube_mip_storage(
    RinGLTextureCubeFaceStorage* face, uint32_t level)
{
    if (face == NULL || level == 0u ||
        level >= RINGL_MAX_TEXTURE_MIP_LEVELS)
        return NULL;
    return &face->mip_storage[level - 1u];
}

static const RinGLTextureMipStorage* texture_cube_mip_storage_const(
    const RinGLTextureCubeFaceStorage* face, uint32_t level)
{
    if (face == NULL || level == 0u ||
        level >= RINGL_MAX_TEXTURE_MIP_LEVELS)
        return NULL;
    return &face->mip_storage[level - 1u];
}

static int texture_cube_level_storage_defined(
    const RinGLTextureCubeFaceStorage* face, uint32_t level)
{
    const RinGLTextureMipStorage* storage;

    if (face == NULL)
        return 0;
    if (level == 0u)
        return face->defined != 0u && face->width != 0u &&
               face->height != 0u && face->shadow_bytes != NULL &&
               face->shadow_size != 0u;
    storage = texture_cube_mip_storage_const(face, level);
    return storage != NULL && storage->defined != 0u &&
           storage->width != 0u && storage->height != 0u &&
           storage->shadow_bytes != NULL && storage->shadow_size != 0u;
}

static uint32_t texture_cube_expected_mip_width(
    const RinGLTextureCubeFaceStorage* face, uint32_t level)
{
    uint32_t width;

    if (face == NULL || level >= RINGL_MAX_TEXTURE_MIP_LEVELS)
        return 0u;
    width = face->width;
    while (level != 0u && width > 1u) {
        width >>= 1u;
        --level;
    }
    return width;
}

static uint32_t texture_cube_expected_mip_height(
    const RinGLTextureCubeFaceStorage* face, uint32_t level)
{
    uint32_t height;

    if (face == NULL || level >= RINGL_MAX_TEXTURE_MIP_LEVELS)
        return 0u;
    height = face->height;
    while (level != 0u && height > 1u) {
        height >>= 1u;
        --level;
    }
    return height;
}

static int texture_cube_base_compatible(
    const RinGLTextureObject* texture, uint32_t face_index,
    uint32_t width, uint32_t height, uint32_t format,
    uint32_t color_component_type, uint32_t compressed_format,
    uint32_t srgb_encoding)
{
    uint32_t index;

    if (texture == NULL || face_index >= RINGL_CUBE_FACE_COUNT)
        return 0;
    for (index = 0u; index < RINGL_CUBE_FACE_COUNT; ++index) {
        const RinGLTextureCubeFaceStorage* face =
            &texture->cube_faces[index];

        if (index == face_index || face->defined == 0u)
            continue;
        if (face->width != width || face->height != height ||
            face->format != format ||
            face->color_component_type != color_component_type ||
            face->compressed_format != compressed_format ||
            face->srgb_encoding != srgb_encoding)
            return 0;
    }
    return 1;
}

static void texture_cube_update_summary(RinGLTextureObject* texture)
{
    uint32_t index;

    if (texture == NULL)
        return;
    texture->shadow_bytes = NULL;
    texture->shadow_size = 0u;
    memset(texture->mip_storage, 0, sizeof(texture->mip_storage));
    texture->width = 0u;
    texture->height = 0u;
    texture->format = 0u;
    texture->color_component_type = 0u;
    texture->compressed_format = 0u;
    texture->srgb_encoding = 0u;
    texture->defined = RINGL_FALSE;
    for (index = 0u; index < RINGL_CUBE_FACE_COUNT; ++index) {
        const RinGLTextureCubeFaceStorage* face =
            &texture->cube_faces[index];

        if (face->defined == 0u)
            continue;
        texture->width = face->width;
        texture->height = face->height;
        texture->format = face->format;
        texture->color_component_type = face->color_component_type;
        texture->compressed_format = face->compressed_format;
        texture->srgb_encoding = face->srgb_encoding;
        texture->defined = RINGL_TRUE;
        break;
    }
}

static void texture_cube_face_enter(
    RinGLTextureObject* texture, RinGLTextureCubeFaceStorage* face,
    RinGLTextureCubeFaceStorage* saved)
{
    if (texture == NULL || face == NULL || saved == NULL)
        return;
    saved->shadow_bytes = texture->shadow_bytes;
    saved->shadow_size = texture->shadow_size;
    saved->width = texture->width;
    saved->height = texture->height;
    saved->format = texture->format;
    saved->color_component_type = texture->color_component_type;
    saved->compressed_format = texture->compressed_format;
    saved->srgb_encoding = texture->srgb_encoding;
    saved->defined = texture->defined;
    memcpy(saved->mip_storage, texture->mip_storage,
           sizeof(saved->mip_storage));
    texture->shadow_bytes = face->shadow_bytes;
    texture->shadow_size = face->shadow_size;
    texture->width = face->width;
    texture->height = face->height;
    texture->format = face->format;
    texture->color_component_type = face->color_component_type;
    texture->compressed_format = face->compressed_format;
    texture->srgb_encoding = face->srgb_encoding;
    texture->defined = face->defined;
    memcpy(texture->mip_storage, face->mip_storage,
           sizeof(texture->mip_storage));
}

static void texture_cube_face_leave(
    RinGLTextureObject* texture, RinGLTextureCubeFaceStorage* face,
    const RinGLTextureCubeFaceStorage* saved)
{
    if (texture == NULL || face == NULL || saved == NULL)
        return;
    face->shadow_bytes = texture->shadow_bytes;
    face->shadow_size = texture->shadow_size;
    face->width = texture->width;
    face->height = texture->height;
    face->format = texture->format;
    face->color_component_type = texture->color_component_type;
    face->compressed_format = texture->compressed_format;
    face->srgb_encoding = texture->srgb_encoding;
    face->defined = texture->defined;
    memcpy(face->mip_storage, texture->mip_storage,
           sizeof(face->mip_storage));
    texture->shadow_bytes = saved->shadow_bytes;
    texture->shadow_size = saved->shadow_size;
    texture->width = saved->width;
    texture->height = saved->height;
    texture->format = saved->format;
    texture->color_component_type = saved->color_component_type;
    texture->compressed_format = saved->compressed_format;
    texture->srgb_encoding = saved->srgb_encoding;
    texture->defined = saved->defined;
    memcpy(texture->mip_storage, saved->mip_storage,
           sizeof(texture->mip_storage));
    texture_cube_update_summary(texture);
}

static int texture_cube_base_images_complete(
    const RinGLTextureObject* texture)
{
    const RinGLTextureCubeFaceStorage* first;
    uint32_t index;

    if (texture == NULL)
        return 0;
    first = &texture->cube_faces[0];
    if (!texture_cube_level_storage_defined(first, 0u) ||
        first->width != first->height)
        return 0;
    for (index = 1u; index < RINGL_CUBE_FACE_COUNT; ++index) {
        const RinGLTextureCubeFaceStorage* face =
            &texture->cube_faces[index];

        if (!texture_cube_level_storage_defined(face, 0u) ||
            face->width != first->width || face->height != first->height ||
            face->format != first->format ||
            face->color_component_type != first->color_component_type ||
            face->compressed_format != first->compressed_format ||
            face->srgb_encoding != first->srgb_encoding)
            return 0;
    }
    return 1;
}

static int texture_cube_level0_complete(const RinGLContext* context,
                                        const RinGLTextureObject* texture)
{
    const RinGLTextureCubeFaceStorage* first;
    uint32_t index;
    uint32_t level;
    uint32_t level_count;

    if (context == NULL || texture == NULL ||
        texture->wrap_s != RINGL_CLAMP_TO_EDGE ||
        texture->wrap_t != RINGL_CLAMP_TO_EDGE ||
        !texture_cube_base_images_complete(texture))
        return 0;
    first = &texture->cube_faces[0];
    if (((first->color_component_type == RINGL_FLOAT &&
          first->compressed_format == 0u && first->srgb_encoding == 0u &&
          context->webgl_float_texture_linear_enabled == RINGL_FALSE) ||
         (first->color_component_type == RINGL_HALF_FLOAT_OES &&
          context->webgl_half_float_texture_linear_enabled == RINGL_FALSE)) &&
        (texture->mag_filter != RINGL_NEAREST ||
         (texture->min_filter != RINGL_NEAREST &&
          texture->min_filter != RINGL_NEAREST_MIPMAP_NEAREST)))
        return 0;
    level_count = texture_mip_level_count(first->width, first->height);
    if (texture->min_filter == RINGL_NEAREST ||
        texture->min_filter == RINGL_LINEAR)
        return 1;
    if (!texture_dimension_is_power_of_two(first->width))
        return 0;
    for (index = 0u; index < RINGL_CUBE_FACE_COUNT; ++index) {
        const RinGLTextureCubeFaceStorage* face =
            &texture->cube_faces[index];

        for (level = 1u; level < level_count; ++level) {
            const RinGLTextureMipStorage* storage =
                texture_cube_mip_storage_const(face, level);

            if (!texture_cube_level_storage_defined(face, level) ||
                storage == NULL ||
                storage->width != texture_cube_expected_mip_width(face, level) ||
                storage->height != texture_cube_expected_mip_height(face, level))
                return 0;
        }
    }
    return 1;
}

static void texture_drop_mip_storage_from(RinGLContext* context,
                                          RinGLTextureObject* texture,
                                          uint32_t first_level)
{
    uint32_t level;

    if (texture == NULL || first_level == 0u)
        return;
    for (level = first_level; level < RINGL_MAX_TEXTURE_MIP_LEVELS; ++level) {
        RinGLTextureMipStorage* storage = texture_mip_storage(texture, level);

        if (storage == NULL)
            continue;
        texture_release_storage(context, &storage->shadow_bytes,
                                &storage->shadow_size);
        memset(storage, 0, sizeof(*storage));
    }
}

static void texture_drop_generated_mips(RinGLContext* context,
                                        RinGLTextureObject* texture)
{
    uint32_t level;

    if (texture == NULL)
        return;
    for (level = 1u; level < RINGL_MAX_TEXTURE_MIP_LEVELS; ++level) {
        RinGLTextureMipStorage* storage = texture_mip_storage(texture, level);

        if (storage == NULL || storage->generated == 0u)
            continue;
        texture_release_storage(context, &storage->shadow_bytes,
                                &storage->shadow_size);
        memset(storage, 0, sizeof(*storage));
    }
}

static int texture_level0_storage_defined(const RinGLTextureObject* texture)
{
    return texture_level_storage_defined(texture, 0u);
}

static int texture_color_component_is_float(uint32_t type)
{
    return type == RINGL_FLOAT || type == RINGL_HALF_FLOAT_OES;
}

static int texture_dimension_is_power_of_two(uint32_t dimension)
{
    return dimension != 0u && (dimension & (dimension - 1u)) == 0u;
}

static int texture_is_npot(const RinGLTextureObject* texture)
{
    return texture != NULL &&
           (!texture_dimension_is_power_of_two(texture->width) ||
            !texture_dimension_is_power_of_two(texture->height));
}

static int texture_level0_complete(const RinGLContext* context,
                                   const RinGLTextureObject* texture)
{
    if (!context || !texture_level0_storage_defined(texture))
        return 0;

    /* WebGL 1 retains the GLES2 NPOT restrictions: an NPOT image may use
     * only clamp-to-edge addressing and a non-mipmapped minification filter.
     * Keep this in sampler completeness rather than silently rewriting the
     * requested state into a POT-compatible sampler. */
    if (texture_is_npot(texture) &&
        (texture->wrap_s != RINGL_CLAMP_TO_EDGE ||
         texture->wrap_t != RINGL_CLAMP_TO_EDGE ||
         (texture->min_filter != RINGL_NEAREST &&
          texture->min_filter != RINGL_LINEAR))) {
        return 0;
    }

    /* OES_texture_float and OES_texture_half_float guarantee nearest
     * filters only. The WebGL bridge flips the matching context-local bit
     * when, and only when, script has obtained its linear extension. Do not
     * expose a native sampler capability merely because RinGPU supports it. */
    if (((texture->color_component_type == RINGL_FLOAT &&
          texture->compressed_format == 0u && texture->srgb_encoding == 0u &&
          context->webgl_float_texture_linear_enabled == RINGL_FALSE) ||
         (texture->color_component_type == RINGL_HALF_FLOAT_OES &&
          context->webgl_half_float_texture_linear_enabled == RINGL_FALSE)) &&
        (texture->mag_filter != RINGL_NEAREST ||
         (texture->min_filter != RINGL_NEAREST &&
          texture->min_filter != RINGL_NEAREST_MIPMAP_NEAREST))) {
        return 0;
    }

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

static int texture_packed_component_type_valid(uint32_t format,
                                               uint32_t component_type)
{
    return (format == RINGL_RGB565 &&
            component_type == RINGL_UNSIGNED_SHORT_5_6_5) ||
           (format == RINGL_RGBA4 &&
            component_type == RINGL_UNSIGNED_SHORT_4_4_4_4) ||
           (format == RINGL_RGB5_A1 &&
            component_type == RINGL_UNSIGNED_SHORT_5_5_5_1);
}

static uint32_t texture_packed_component_type(uint32_t format)
{
    if (format == RINGL_RGB565)
        return RINGL_UNSIGNED_SHORT_5_6_5;
    if (format == RINGL_RGBA4)
        return RINGL_UNSIGNED_SHORT_4_4_4_4;
    if (format == RINGL_RGB5_A1)
        return RINGL_UNSIGNED_SHORT_5_5_5_1;
    return 0u;
}

static int texture_srgb_internal_format(uint32_t internal_format)
{
    return internal_format == RINGL_SRGB_EXT ||
           internal_format == RINGL_SRGB_ALPHA_EXT;
}

static uint32_t texture_srgb_storage_format(uint32_t internal_format)
{
    return internal_format == RINGL_SRGB_EXT ? RINGL_RGB : RINGL_RGBA;
}

static uint32_t texture_external_texel_bytes(uint32_t format, uint32_t type)
{
    if (format == RINGL_DEPTH_COMPONENT) {
        if (type == RINGL_UNSIGNED_SHORT)
            return 2u;
        if (type == RINGL_UNSIGNED_INT || type == RINGL_FLOAT)
            return 4u;
    }
    if (format == RINGL_DEPTH_STENCIL &&
        type == RINGL_UNSIGNED_INT_24_8) {
        return 4u;
    }
    if (type == RINGL_FLOAT) {
        switch (format) {
        case RINGL_RGBA:
            return 16u;
        case RINGL_RGB:
            return 12u;
        case RINGL_LUMINANCE_ALPHA:
            return 8u;
        case RINGL_ALPHA:
        case RINGL_LUMINANCE:
            return 4u;
        default:
            return 0u;
        }
    }
    if (type == RINGL_HALF_FLOAT_OES) {
        switch (format) {
        case RINGL_RGBA:
            return 8u;
        case RINGL_RGB:
            return 6u;
        case RINGL_LUMINANCE_ALPHA:
            return 4u;
        case RINGL_ALPHA:
        case RINGL_LUMINANCE:
            return 2u;
        default:
            return 0u;
        }
    }
    if (type == RINGL_UNSIGNED_SHORT_5_6_5 ||
        type == RINGL_UNSIGNED_SHORT_4_4_4_4 ||
        type == RINGL_UNSIGNED_SHORT_5_5_5_1) {
        return 2u;
    }
    switch (format) {
    case RINGL_RGBA:
    case RINGL_SRGB_ALPHA_EXT:
        return 4u;
    case RINGL_RGB:
    case RINGL_SRGB_EXT:
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

static float texture_read_float_component(const uint8_t* source,
                                          uint32_t type)
{
    if (type == RINGL_FLOAT) {
        float value;

        memcpy(&value, source, sizeof(value));
        return value;
    }
    {
        uint16_t half;
        uint32_t sign;
        uint32_t exponent;
        uint32_t mantissa;
        uint32_t bits;
        float value;

        memcpy(&half, source, sizeof(half));
        sign = ((uint32_t)half & 0x8000u) << 16u;
        exponent = ((uint32_t)half >> 10u) & 0x1fu;
        mantissa = (uint32_t)half & 0x03ffu;
        if (exponent == 0u) {
            if (mantissa == 0u) {
                bits = sign;
            } else {
                int32_t unbiased_exponent = -14;

                while ((mantissa & 0x0400u) == 0u) {
                    mantissa <<= 1u;
                    --unbiased_exponent;
                }
                mantissa &= 0x03ffu;
                bits = sign |
                    ((uint32_t)(unbiased_exponent + 127) << 23u) |
                    (mantissa << 13u);
            }
        } else if (exponent == 0x1fu) {
            bits = sign | 0x7f800000u | (mantissa << 13u);
        } else {
            bits = sign | ((exponent + 112u) << 23u) |
                   (mantissa << 13u);
        }
        memcpy(&value, &bits, sizeof(value));
        return value;
    }
}

static uint16_t texture_write_half_component(float value)
{
    uint32_t bits;
    uint32_t sign;
    uint32_t exponent;
    uint32_t mantissa;
    int32_t half_exponent;

    memcpy(&bits, &value, sizeof(bits));
    sign = (bits >> 16u) & 0x8000u;
    exponent = (bits >> 23u) & 0xffu;
    mantissa = bits & 0x007fffffu;
    if (exponent == 0xffu)
        return (uint16_t)(sign | 0x7bffu);
    half_exponent = (int32_t)exponent - 127 + 15;
    if (half_exponent >= 31)
        return (uint16_t)(sign | 0x7bffu);
    if (half_exponent <= 0) {
        uint32_t shifted;
        uint32_t round_bit;

        if (half_exponent < -10)
            return (uint16_t)sign;
        mantissa |= 0x00800000u;
        shifted = mantissa >> (uint32_t)(14 - half_exponent);
        round_bit = UINT32_C(1) << (uint32_t)(13 - half_exponent);
        if ((mantissa & round_bit) != 0u &&
            ((mantissa & (round_bit - 1u)) != 0u || (shifted & 1u) != 0u))
            ++shifted;
        return (uint16_t)(sign | shifted);
    }
    mantissa += 0x00001000u;
    if ((mantissa & 0x00800000u) != 0u) {
        mantissa = 0u;
        ++half_exponent;
        if (half_exponent >= 31)
            return (uint16_t)(sign | 0x7bffu);
    }
    return (uint16_t)(sign | ((uint32_t)half_exponent << 10u) |
                      (mantissa >> 13u));
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

    if (texture_color_component_is_float(type)) {
        for (index = 0u; index < texel_count; ++index) {
            const uint8_t* source_texel =
                source + (uint64_t)index * source_texel_bytes;
            float components[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            uint32_t component_bytes = type == RINGL_FLOAT
                ? (uint32_t)sizeof(float) : (uint32_t)sizeof(uint16_t);

            if (source_format == RINGL_RGBA) {
                components[0] = texture_read_float_component(source_texel,
                                                             type);
                components[1] = texture_read_float_component(
                    source_texel + component_bytes, type);
                components[2] = texture_read_float_component(
                    source_texel + 2u * component_bytes, type);
                components[3] = texture_read_float_component(
                    source_texel + 3u * component_bytes, type);
            } else if (source_format == RINGL_RGB) {
                components[0] = texture_read_float_component(source_texel,
                                                             type);
                components[1] = texture_read_float_component(
                    source_texel + component_bytes, type);
                components[2] = texture_read_float_component(
                    source_texel + 2u * component_bytes, type);
            } else if (source_format == RINGL_ALPHA) {
                components[3] = texture_read_float_component(source_texel,
                                                             type);
            } else if (source_format == RINGL_LUMINANCE) {
                components[0] = texture_read_float_component(source_texel,
                                                             type);
                components[1] = components[0];
                components[2] = components[0];
            } else if (source_format == RINGL_LUMINANCE_ALPHA) {
                components[0] = texture_read_float_component(source_texel,
                                                             type);
                components[1] = components[0];
                components[2] = components[0];
                components[3] = texture_read_float_component(
                    source_texel + component_bytes, type);
            }
            if (type == RINGL_HALF_FLOAT_OES) {
                uint16_t half_components[4];

                for (uint32_t component = 0u; component < 4u; ++component)
                    half_components[component] =
                        texture_write_half_component(components[component]);
                memcpy(destination + (uint64_t)index * sizeof(half_components),
                       half_components, sizeof(half_components));
            } else {
                memcpy(destination + (uint64_t)index * sizeof(components),
                       components, sizeof(components));
            }
        }
        return;
    }

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

static uint32_t texture_storage_texel_bytes(uint32_t format,
                                            uint32_t color_component_type)
{
    if (format == RINGL_DEPTH24_STENCIL8)
        return 8u;
    if (color_component_type == RINGL_FLOAT)
        return 4u * (uint32_t)sizeof(float);
    if (color_component_type == RINGL_HALF_FLOAT_OES)
        return 4u * (uint32_t)sizeof(uint16_t);
    return texture_packed_color_format(format) ? 2u : 4u;
}

/* A null TexImage definition is still a logical image, not an uninitialized
 * physical RGBA allocation. RGB and LUMINANCE omit alpha, so their expanded
 * shadow storage must start with the WebGL default alpha of one. This also
 * lets a later color-mask-limited clear or draw preserve the required value. */
static void texture_initialize_color_defaults(uint8_t* destination,
                                              uint64_t texel_count,
                                              uint32_t format,
                                              uint32_t color_component_type)
{
    uint64_t index;

    if (destination == NULL ||
        (format != RINGL_RGB && format != RINGL_LUMINANCE)) {
        return;
    }
    if (color_component_type == RINGL_FLOAT) {
        const float one = 1.0f;

        for (index = 0u; index < texel_count; ++index) {
            memcpy(destination + (index * 4u + 3u) * sizeof(one), &one,
                   sizeof(one));
        }
        return;
    }
    if (color_component_type == RINGL_HALF_FLOAT_OES) {
        const uint16_t one = UINT16_C(0x3c00);

        for (index = 0u; index < texel_count; ++index) {
            memcpy(destination + (index * 4u + 3u) * sizeof(one), &one,
                   sizeof(one));
        }
        return;
    }
    for (index = 0u; index < texel_count; ++index)
        destination[index * 4u + 3u] = UINT8_MAX;
}

/* EXT_sRGB accepts normalized byte source data, but texture sampling and
 * blending must see linear RGB. Keep alpha unmodified and always materialize
 * four Float32 components so the normal RinGPU RGBA32_FLOAT path owns the
 * physical image. */
static void texture_copy_srgb_texels(uint8_t* destination,
                                     const uint8_t* source,
                                     uint32_t source_format,
                                     uint32_t destination_has_alpha,
                                     uint32_t texel_count)
{
    uint32_t index;
    uint32_t source_texel_bytes = texture_external_texel_bytes(source_format,
                                                                RINGL_UNSIGNED_BYTE);

    for (index = 0u; index < texel_count; ++index) {
        const uint8_t* source_texel = source + (uint64_t)index * source_texel_bytes;
        float components[4] = {
            ringl_srgb_decode_u8(source_texel[0]),
            ringl_srgb_decode_u8(source_texel[1]),
            ringl_srgb_decode_u8(source_texel[2]),
            destination_has_alpha != RINGL_FALSE &&
                    (source_format == RINGL_RGBA ||
                     source_format == RINGL_SRGB_ALPHA_EXT)
                ? (float)source_texel[3] / 255.0f
                : 1.0f,
        };

        memcpy(destination + (uint64_t)index * sizeof(components), components,
               sizeof(components));
    }
}

static void texture_initialize_srgb_rgb_alpha(uint8_t* destination,
                                               uint64_t texel_count)
{
    float alpha = 1.0f;

    for (uint64_t index = 0u; index < texel_count; ++index) {
        memcpy(destination + (index * 4u + 3u) * sizeof(alpha), &alpha,
               sizeof(alpha));
    }
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

/* Copy-to-texture entry points snapshot canonical RGBA from their source color
 * target. Canonical color formats retain their GL component rules in the
 * ordinary four-byte shadow layout, while packed destinations use the same
 * direct quantization path as copyTexSubImage2D. */
static void texture_copy_rgba_to_copy_image_storage(
    uint8_t* destination, const uint8_t* source, uint32_t storage_format,
    uint32_t texel_count)
{
    uint32_t index;

    texture_copy_rgba_to_color_storage_texels(destination, source,
                                               storage_format, texel_count);
    for (index = 0u; index < texel_count; ++index) {
        uint8_t* destination_texel = destination + (uint64_t)index * 4u;
        const uint8_t* source_texel = source + (uint64_t)index * 4u;

        if (storage_format == RINGL_RGB) {
            destination_texel[3] = UINT8_MAX;
        } else if (storage_format == RINGL_ALPHA) {
            destination_texel[0] = 0u;
            destination_texel[1] = 0u;
            destination_texel[2] = 0u;
        } else if (storage_format == RINGL_LUMINANCE) {
            destination_texel[0] = source_texel[0];
            destination_texel[1] = source_texel[0];
            destination_texel[2] = source_texel[0];
            destination_texel[3] = UINT8_MAX;
        } else if (storage_format == RINGL_LUMINANCE_ALPHA) {
            destination_texel[0] = source_texel[0];
            destination_texel[1] = source_texel[0];
            destination_texel[2] = source_texel[0];
        }
    }
}

static uint32_t texture_storage_texel_bytes(uint32_t format,
                                            uint32_t color_component_type);

/* CopyTex snapshots are either normalized RGBA8 or finite canonical Float32
 * RGBA. Validate the complete Float32 snapshot before touching the destination
 * so a backend that reports a non-finite color cannot leave a partial texture
 * update behind. */
static int texture_canonical_rgba_float_snapshot_valid(const uint8_t* source,
                                                       uint32_t texel_count)
{
    uint32_t index;

    if (source == NULL)
        return 0;
    for (index = 0u; index < texel_count * 4u; ++index) {
        float component;

        memcpy(&component, source + (uint64_t)index * sizeof(component),
               sizeof(component));
        if (component != component || component > FLT_MAX ||
            component < -FLT_MAX) {
            return 0;
        }
    }
    return 1;
}

static uint8_t texture_float_to_unorm8(float value)
{
    if (!(value > 0.0f))
        return 0u;
    if (value >= 1.0f)
        return UINT8_MAX;
    return (uint8_t)(value * (float)UINT8_MAX + 0.5f);
}

static void texture_apply_copy_component_rules(float components[4],
                                               uint32_t storage_format)
{
    if (storage_format == RINGL_RGB) {
        components[3] = 1.0f;
    } else if (storage_format == RINGL_ALPHA) {
        components[0] = 0.0f;
        components[1] = 0.0f;
        components[2] = 0.0f;
    } else if (storage_format == RINGL_LUMINANCE) {
        components[1] = components[0];
        components[2] = components[0];
        components[3] = 1.0f;
    } else if (storage_format == RINGL_LUMINANCE_ALPHA) {
        components[1] = components[0];
        components[2] = components[0];
    }
}

/* Store a fully validated canonical CopyTex snapshot in the bound texture's
 * representation. U8 destinations use the normal UNORM/packed quantization;
 * Float32 retains finite values and binary16 follows the existing saturating
 * conversion used by Float texture uploads and generated mips. Logical sRGB
 * textures are represented as linear Float32 storage, so a Float source is
 * quantized through the same sRGB transfer function as byte source input. */
static int texture_copy_canonical_rgba_to_typed_copy_image_storage(
    uint8_t* destination, const uint8_t* source, uint32_t source_type,
    uint32_t storage_format, uint32_t storage_component_type,
    uint32_t srgb_encoding, uint32_t texel_count)
{
    uint32_t index;

    if (destination == NULL || source == NULL ||
        (source_type != RINGL_UNSIGNED_BYTE && source_type != RINGL_FLOAT) ||
        (!srgb_encoding &&
         ((texture_packed_color_format(storage_format) &&
           !texture_packed_component_type_valid(storage_format,
                                                storage_component_type)) ||
          (!texture_packed_color_format(storage_format) &&
           storage_component_type != RINGL_UNSIGNED_BYTE &&
           storage_component_type != RINGL_FLOAT &&
           storage_component_type != RINGL_HALF_FLOAT_OES)))) {
        return -1;
    }
    if (source_type == RINGL_FLOAT &&
        !texture_canonical_rgba_float_snapshot_valid(source, texel_count)) {
        return -1;
    }

    for (index = 0u; index < texel_count; ++index) {
        float components[4];
        uint8_t* destination_texel = destination +
            (uint64_t)index * texture_storage_texel_bytes(
                                  storage_format, storage_component_type);

        if (source_type == RINGL_FLOAT) {
            memcpy(components, source + (uint64_t)index * sizeof(components),
                   sizeof(components));
        } else {
            const uint8_t* source_texel = source + (uint64_t)index * 4u;

            components[0] = (float)source_texel[0] / (float)UINT8_MAX;
            components[1] = (float)source_texel[1] / (float)UINT8_MAX;
            components[2] = (float)source_texel[2] / (float)UINT8_MAX;
            components[3] = (float)source_texel[3] / (float)UINT8_MAX;
        }
        texture_apply_copy_component_rules(components, storage_format);

        if (srgb_encoding != 0u) {
            float linear_storage[4] = {
                ringl_srgb_decode_u8(ringl_srgb_encode_float(components[0])),
                ringl_srgb_decode_u8(ringl_srgb_encode_float(components[1])),
                ringl_srgb_decode_u8(ringl_srgb_encode_float(components[2])),
                storage_format == RINGL_RGBA
                    ? (components[3] <= 0.0f ? 0.0f
                       : components[3] >= 1.0f ? 1.0f : components[3])
                    : 1.0f,
            };

            memcpy(destination_texel, linear_storage, sizeof(linear_storage));
        } else if (storage_component_type == RINGL_FLOAT) {
            memcpy(destination_texel, components, sizeof(components));
        } else if (storage_component_type == RINGL_HALF_FLOAT_OES) {
            uint16_t half_components[4];
            uint32_t component;

            for (component = 0u; component < 4u; ++component)
                half_components[component] =
                    texture_write_half_component(components[component]);
            memcpy(destination_texel, half_components, sizeof(half_components));
        } else {
            uint8_t unorm_components[4] = {
                texture_float_to_unorm8(components[0]),
                texture_float_to_unorm8(components[1]),
                texture_float_to_unorm8(components[2]),
                texture_float_to_unorm8(components[3]),
            };

            texture_copy_rgba_to_copy_image_storage(destination_texel,
                                                     unorm_components,
                                                     storage_format, 1u);
        }
    }
    return 0;
}

static uint32_t texture_ringpu_format(uint32_t format,
                                      uint32_t color_component_type)
{
    if (color_component_type == RINGL_FLOAT)
        return RINGL_RIN_GPU_FORMAT_RGBA32_FLOAT;
    if (color_component_type == RINGL_HALF_FLOAT_OES)
        return RINGL_RIN_GPU_FORMAT_RGBA16_FLOAT;
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
    if (texture_color_format(internal_format) &&
        !texture_packed_color_format(internal_format) &&
        internal_format == format && texture_color_component_is_float(type))
        return internal_format;
    if (texture_color_format(internal_format) && internal_format == format &&
        type == RINGL_UNSIGNED_BYTE)
        return internal_format;
    return 0u;
}

/* WebGL 1's WEBGL_depth_texture extension has unsized DEPTH_COMPONENT and
 * DEPTH_STENCIL internal formats. Keep those public tokens at the browser
 * boundary, but normalize their bounded native storage to the D32/D32S8
 * representations consumed by the RinGPU image/FBO path. */
static uint32_t texture_depth_storage_format(uint32_t internal_format,
                                             uint32_t format, uint32_t type)
{
    if (format == RINGL_DEPTH_COMPONENT) {
        if (internal_format == RINGL_DEPTH_COMPONENT &&
            (type == RINGL_UNSIGNED_SHORT || type == RINGL_UNSIGNED_INT)) {
            return RINGL_DEPTH_COMPONENT32F;
        }
        if (internal_format == RINGL_DEPTH_COMPONENT32F &&
            type == RINGL_FLOAT) {
            return RINGL_DEPTH_COMPONENT32F;
        }
    }
    if (format == RINGL_DEPTH_STENCIL && type == RINGL_UNSIGNED_INT_24_8 &&
        (internal_format == RINGL_DEPTH_STENCIL ||
         internal_format == RINGL_DEPTH24_STENCIL8)) {
        return RINGL_DEPTH24_STENCIL8;
    }
    return 0u;
}

static int texture_upload_format_valid(const RinGLTextureObject* texture,
                                       uint32_t format, uint32_t type)
{
    uint32_t storage_format;

    if (texture == NULL)
        return 0;
    storage_format = texture->format;
    if (texture->srgb_encoding != 0u) {
        uint32_t expected_format = storage_format == RINGL_RGB
            ? RINGL_SRGB_EXT : RINGL_SRGB_ALPHA_EXT;

        return (storage_format == RINGL_RGB || storage_format == RINGL_RGBA) &&
               expected_format == format && type == RINGL_UNSIGNED_BYTE;
    }
    if (texture_color_component_is_float(texture->color_component_type))
        return texture_color_format(storage_format) &&
               !texture_packed_color_format(storage_format) &&
               storage_format == format &&
               type == texture->color_component_type;
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

static int texture_depth_upload_format_valid(uint32_t storage_format,
                                             uint32_t format, uint32_t type)
{
    if (storage_format == RINGL_DEPTH_COMPONENT32F) {
        return format == RINGL_DEPTH_COMPONENT &&
               (type == RINGL_UNSIGNED_SHORT || type == RINGL_UNSIGNED_INT ||
                type == RINGL_FLOAT);
    }
    return storage_format == RINGL_DEPTH24_STENCIL8 &&
           format == RINGL_DEPTH_STENCIL && type == RINGL_UNSIGNED_INT_24_8;
}

static void texture_copy_depth_texels(uint8_t* destination,
                                      const uint8_t* source, uint32_t type,
                                      uint32_t texel_count)
{
    uint32_t index;

    for (index = 0u; index < texel_count; ++index) {
        float depth;

        if (type == RINGL_UNSIGNED_SHORT) {
            uint16_t value;

            memcpy(&value, source + (uint64_t)index * sizeof(value),
                   sizeof(value));
            depth = (float)value / 65535.0f;
        } else if (type == RINGL_UNSIGNED_INT) {
            uint32_t value;

            memcpy(&value, source + (uint64_t)index * sizeof(value),
                   sizeof(value));
            depth = (float)value / 4294967295.0f;
        } else {
            memcpy(&depth, source + (uint64_t)index * sizeof(depth),
                   sizeof(depth));
        }
        memcpy(destination + (uint64_t)index * sizeof(depth), &depth,
               sizeof(depth));
    }
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
    memset(texture->ringpu_cube_image_state, RINGL_RIN_GPU_IMAGE_UNDEFINED,
           sizeof(texture->ringpu_cube_image_state));
}

static uint32_t texture_cube_highest_defined_mip_count(
    const RinGLTextureObject* texture)
{
    uint32_t face_index;
    uint32_t level;
    uint32_t count = 0u;

    if (texture == NULL)
        return 0u;
    for (face_index = 0u; face_index < RINGL_CUBE_FACE_COUNT; ++face_index) {
        const RinGLTextureCubeFaceStorage* face =
            &texture->cube_faces[face_index];
        uint32_t level_count;

        if (!texture_cube_level_storage_defined(face, 0u))
            continue;
        level_count = texture_mip_level_count(face->width, face->height);
        for (level = 0u; level < level_count; ++level) {
            const RinGLTextureMipStorage* storage;

            if (level == 0u) {
                count = count < 1u ? 1u : count;
                continue;
            }
            storage = texture_cube_mip_storage_const(face, level);
            if (texture_cube_level_storage_defined(face, level) &&
                storage != NULL &&
                storage->width == texture_cube_expected_mip_width(face, level) &&
                storage->height == texture_cube_expected_mip_height(face, level) &&
                count < level + 1u)
                count = level + 1u;
        }
    }
    return count;
}

static int texture_realize_cube_image(RinGLContext* context,
                                      RinGLTextureObject* texture)
{
    RinGLRinGpuImageArrayV1 desc;
    RinGLRinGpuImageUploadArrayV1 upload;
    uint64_t image = 0u;
    uint32_t mip_count;
    uint32_t face_index;
    uint32_t level;

    if (context == NULL || texture == NULL || texture->target !=
            RINGL_TEXTURE_CUBE_MAP)
        return -1;
    if (!texture->requires_color_target &&
        !texture_cube_level0_complete(context, texture))
        return -1;
    mip_count = texture->requires_color_target
        ? texture_cube_highest_defined_mip_count(texture)
        : texture_mip_level_count(texture->width, texture->height);
    if (mip_count == 0u || texture->format == 0u ||
        !texture_color_format(texture->format))
        return -1;
    if (!context->has_ringpu_ops ||
        context->ringpu_ops.create_image_array_v1 == NULL ||
        context->ringpu_ops.upload_image_array_v1 == NULL)
        return -1;

    memset(&desc, 0, sizeof(desc));
    desc.width = texture->width;
    desc.height = texture->height;
    desc.array_layers = RINGL_CUBE_FACE_COUNT;
    desc.mip_levels = mip_count;
    desc.format = texture_ringpu_format(texture->format,
                                        texture->color_component_type);
    desc.usage = RINGL_RIN_GPU_IMAGE_USAGE_COPY_DESTINATION |
                 RINGL_RIN_GPU_IMAGE_USAGE_SAMPLED;
    if (texture->requires_color_target != 0u)
        desc.usage |= RINGL_RIN_GPU_IMAGE_USAGE_COLOR_TARGET |
                      RINGL_RIN_GPU_IMAGE_USAGE_COPY_SOURCE;
    if (ringl_backend_create_image_array_v1(context, &desc, &image) != 0 ||
        image == 0u)
        return -1;
    memset(texture->ringpu_cube_image_state,
           RINGL_RIN_GPU_IMAGE_UNDEFINED,
           sizeof(texture->ringpu_cube_image_state));
    for (face_index = 0u; face_index < RINGL_CUBE_FACE_COUNT; ++face_index) {
        const RinGLTextureCubeFaceStorage* face =
            &texture->cube_faces[face_index];

        for (level = 0u; level < mip_count; ++level) {
            const uint8_t* bytes;
            uint64_t size;
            uint32_t width;
            uint32_t height;

            if (level == 0u) {
                if (!texture_cube_level_storage_defined(face, 0u))
                    continue;
                bytes = face->shadow_bytes;
                size = face->shadow_size;
                width = face->width;
                height = face->height;
            } else {
                const RinGLTextureMipStorage* storage =
                    texture_cube_mip_storage_const(face, level);

                if (!texture_cube_level_storage_defined(face, level) ||
                    storage == NULL)
                    continue;
                bytes = storage->shadow_bytes;
                size = storage->shadow_size;
                width = storage->width;
                height = storage->height;
            }
            memset(&upload, 0, sizeof(upload));
            upload.mip_level = level;
            upload.array_layer = face_index;
            upload.width = width;
            upload.height = height;
            upload.source_row_pitch_bytes = (uint64_t)width *
                texture_storage_texel_bytes(texture->format,
                                            texture->color_component_type);
            if (ringl_backend_upload_image_array_v1(context, image, &upload,
                                                     bytes, size) != 0) {
                ringl_backend_destroy_object(context, image);
                return -1;
            }
            texture->ringpu_cube_image_state[level][face_index] =
                RINGL_RIN_GPU_IMAGE_COPY_DESTINATION;
        }
    }
    for (level = 0u; level < RINGL_MAX_TEXTURE_MIP_LEVELS; ++level)
        texture->ringpu_image_state[level] =
            texture->ringpu_cube_image_state[level][0];
    texture->ringpu_image = image;
    return 0;
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
    if (texture->target == RINGL_TEXTURE_CUBE_MAP)
        return texture_realize_cube_image(context, texture);
    if ((texture->requires_color_target ||
         texture->format == RINGL_DEPTH_COMPONENT32F ||
         texture->format == RINGL_DEPTH24_STENCIL8)
            ? !texture_level0_storage_defined(texture)
            : !texture_level0_complete(context, texture))
        return -1;

    mip_count = (texture->requires_color_target != 0u ||
                 texture->format == RINGL_DEPTH_COMPONENT32F ||
                 texture->format == RINGL_DEPTH24_STENCIL8)
        ? texture_highest_defined_mip_count(texture)
        : ringl_texture_sampled_mip_count(texture);
    if (mip_count == 0u)
        return -1;

    if (mip_count > 1u) {
        if (!texture_color_format(texture->format) &&
            texture->format != RINGL_DEPTH_COMPONENT32F &&
            texture->format != RINGL_DEPTH24_STENCIL8) {
            return -1;
        }
        memset(&mip_desc, 0, sizeof(mip_desc));
        mip_desc.width = texture->width;
        mip_desc.height = texture->height;
        mip_desc.format = texture->format == RINGL_DEPTH24_STENCIL8
            ? RINGL_RIN_GPU_FORMAT_D32_FLOAT_S8_UINT
            : texture->format == RINGL_DEPTH_COMPONENT32F
                ? RINGL_RIN_GPU_FORMAT_D32_FLOAT
            : texture_ringpu_format(texture->format,
                                    texture->color_component_type);
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
    } else if (texture_color_format(texture->format) &&
               texture->requires_color_target) {
        memset(&color_target_desc, 0, sizeof(color_target_desc));
        color_target_desc.width = texture->width;
        color_target_desc.height = texture->height;
        color_target_desc.format = texture_ringpu_format(
            texture->format, texture->color_component_type);
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
        desc.format = texture_ringpu_format(texture->format,
                                            texture->color_component_type);
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
            texture_storage_texel_bytes(texture->format,
                                        texture->color_component_type);
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
                texture_storage_texel_bytes(texture->format,
                                            texture->color_component_type);
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
    if (texture->max_anisotropy < 1.0f ||
        texture->max_anisotropy > (float)RINGL_MAX_TEXTURE_ANISOTROPY) {
        return -1;
    }
    /* RinGPU's bounded sampler describes an integral number of real taps.
     * Fractional WebGL requests are rounded down, never beyond the requested
     * degree; query state retains the clamped GL value. */
    desc.max_anisotropy = (uint32_t)texture->max_anisotropy;
    if (desc.max_anisotropy == 0u)
        desc.max_anisotropy = 1u;
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
    if (name == 0u)
        name = context->bound_texture_cube[unit];
    if (name == 0u ||
        ringl_object_lookup(context, name, RINGL_OBJECT_TEXTURE) == NULL) {
        return -1;
    }
    slot_index = ringl_object_slot_index(name);
    if (slot_index >= RINGL_OBJECT_SLOT_COUNT)
        return -1;
    texture = &context->textures[slot_index];
    if (texture->target == RINGL_TEXTURE_CUBE_MAP
            ? (!texture_cube_level0_complete(context, texture) ||
               !texture_color_format(texture->format))
            : (!(texture_color_format(texture->format) ||
                 texture->format == RINGL_DEPTH_COMPONENT32F ||
                 texture->format == RINGL_DEPTH24_STENCIL8) ||
               !texture_level0_complete(context, texture)) ||
        texture_realize_image(context, texture) != 0 ||
        texture_realize_sampler(context, texture) != 0) {
        return -1;
    }
    *image_out = texture->ringpu_image;
    *sampler_out = texture->ringpu_sampler;
    return 0;
}

int ringl_enable_webgl_float_texture_linear(void)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL || context->lost != RINGL_FALSE)
        return -1;
    context->webgl_float_texture_linear_enabled = RINGL_TRUE;
    return 0;
}

int ringl_enable_webgl_half_float_texture_linear(void)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL || context->lost != RINGL_FALSE)
        return -1;
    context->webgl_half_float_texture_linear_enabled = RINGL_TRUE;
    return 0;
}

int ringl_enable_webgl_texture_filter_anisotropic(void)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL || context->lost != RINGL_FALSE)
        return -1;
    context->webgl_texture_filter_anisotropic_enabled = RINGL_TRUE;
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
            if (context->bound_texture_cube[unit] == name)
                context->bound_texture_cube[unit] = 0u;
        }

        slot_index = ringl_object_slot_index(name);
        if (slot_index < RINGL_OBJECT_SLOT_COUNT) {
            ringl_backend_destroy_object(context,
                                         context->textures[slot_index].ringpu_sampler);
            texture_discard_image(context, &context->textures[slot_index]);
            texture_drop_mip_storage_from(context,
                                          &context->textures[slot_index], 1u);
            texture_release_storage(context,
                                    &context->textures[slot_index].shadow_bytes,
                                    &context->textures[slot_index].shadow_size);
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
        {
            uint32_t slot_index = ringl_object_slot_index(texture);

            if (slot_index >= RINGL_OBJECT_SLOT_COUNT ||
                (context->textures[slot_index].target != 0u &&
                 context->textures[slot_index].target != target)) {
                ringl_context_record_error(context, RINGL_INVALID_OPERATION);
                return;
            }
            context->textures[slot_index].target = target;
        }
        ringl_object_promote(slot);
    }

    binding = target == RINGL_TEXTURE_CUBE_MAP
        ? &context->bound_texture_cube[context->active_texture_unit]
        : &context->bound_texture_2d[context->active_texture_unit];
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
    return target == RINGL_TEXTURE_CUBE_MAP
        ? context->bound_texture_cube[context->active_texture_unit]
        : context->bound_texture_2d[context->active_texture_unit];
}

void ringl_tex_parameteri(uint32_t target, uint32_t pname, int32_t param)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLTextureObject* texture;
    uint32_t value = (uint32_t)param;
    uint32_t* field;

    if (pname == RINGL_TEXTURE_MAX_ANISOTROPY_EXT) {
        ringl_tex_parameterf(target, pname, (float)param);
        return;
    }
    if (context == NULL)
        return;
    if (!texture_target_valid(target)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    texture = bound_texture_for_target(context, target);
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

void ringl_tex_parameterf(uint32_t target, uint32_t pname, float param)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLTextureObject* texture;
    float clamped;

    if (context == NULL)
        return;
    if (!texture_target_valid(target) ||
        pname != RINGL_TEXTURE_MAX_ANISOTROPY_EXT ||
        context->webgl_texture_filter_anisotropic_enabled == RINGL_FALSE) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    texture = bound_texture_for_target(context, target);
    if (texture == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    /* The extension parameter must remain finite at this native boundary;
     * infinity is not silently converted into a maximum sampler degree. */
    if (param != param || param < 1.0f || param > FLT_MAX) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    clamped = param > (float)RINGL_MAX_TEXTURE_ANISOTROPY
        ? (float)RINGL_MAX_TEXTURE_ANISOTROPY : param;
    if (texture->max_anisotropy != clamped) {
        ringl_backend_destroy_object(context, texture->ringpu_sampler);
        texture->ringpu_sampler = 0u;
        texture->max_anisotropy = clamped;
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
    texture = bound_texture_for_target(context, target);
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

float ringl_get_tex_parameterf(uint32_t target, uint32_t pname)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLTextureObject* texture;

    if (context == NULL)
        return 0.0f;
    if (!texture_target_valid(target) ||
        pname != RINGL_TEXTURE_MAX_ANISOTROPY_EXT ||
        context->webgl_texture_filter_anisotropic_enabled == RINGL_FALSE) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return 0.0f;
    }
    texture = bound_texture_for_target(context, target);
    if (texture == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return 0.0f;
    }
    return texture->max_anisotropy;
}

int ringl_get_tex_parameteriv_bounded(uint32_t target, uint32_t pname,
                                      int32_t* value, size_t value_count)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLTextureObject* texture;
    int32_t result;

    if (context == NULL)
        return -1;
    if (value == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    if (value_count < 1u) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    if (!texture_target_valid(target)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return -1;
    }
    texture = bound_texture_for_target(context, target);
    if (texture == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }

    switch (pname) {
    case RINGL_TEXTURE_MIN_FILTER:
        result = (int32_t)texture->min_filter;
        break;
    case RINGL_TEXTURE_MAG_FILTER:
        result = (int32_t)texture->mag_filter;
        break;
    case RINGL_TEXTURE_WRAP_S:
        result = (int32_t)texture->wrap_s;
        break;
    case RINGL_TEXTURE_WRAP_T:
        result = (int32_t)texture->wrap_t;
        break;
    case RINGL_TEXTURE_MAX_ANISOTROPY_EXT:
        if (context->webgl_texture_filter_anisotropic_enabled == RINGL_FALSE) {
            ringl_context_record_error(context, RINGL_INVALID_ENUM);
            return -1;
        }
        result = (int32_t)texture->max_anisotropy;
        break;
    default:
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return -1;
    }
    *value = result;
    return 0;
}

int ringl_get_tex_parameterfv_bounded(uint32_t target, uint32_t pname,
                                      float* value, size_t value_count)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLTextureObject* texture;
    float result;

    if (context == NULL)
        return -1;
    if (value == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    if (value_count < 1u) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    if (!texture_target_valid(target) ||
        pname != RINGL_TEXTURE_MAX_ANISOTROPY_EXT ||
        context->webgl_texture_filter_anisotropic_enabled == RINGL_FALSE) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return -1;
    }
    texture = bound_texture_for_target(context, target);
    if (texture == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    result = texture->max_anisotropy;
    *value = result;
    return 0;
}

float ringl_get_max_texture_anisotropy(void)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL)
        return 0.0f;
    if (context->webgl_texture_filter_anisotropic_enabled == RINGL_FALSE) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return 0.0f;
    }
    return (float)RINGL_MAX_TEXTURE_ANISOTROPY;
}

static void texture_free_generated_mips(
    RinGLContext* context,
    RinGLTextureMipStorage generated[RINGL_MAX_TEXTURE_MIP_LEVELS - 1u])
{
    uint32_t index;

    for (index = 0u; index < RINGL_MAX_TEXTURE_MIP_LEVELS - 1u; ++index) {
        texture_release_storage(context, &generated[index].shadow_bytes,
                                &generated[index].shadow_size);
        memset(&generated[index], 0, sizeof(generated[index]));
    }
}

static int texture_generate_color_mips(
    RinGLContext* context, const RinGLTextureObject* texture,
    RinGLTextureMipStorage generated[RINGL_MAX_TEXTURE_MIP_LEVELS - 1u])
{
    const uint8_t* source = texture->shadow_bytes;
    uint32_t source_width = texture->width;
    uint32_t source_height = texture->height;
    uint32_t texel_bytes = texture_storage_texel_bytes(
        texture->format, texture->color_component_type);
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
            texture_free_generated_mips(context, generated);
            return -1;
        }
        if (!ringl_context_reserve_shadow_bytes(context, size)) {
            texture_free_generated_mips(context, generated);
            return -1;
        }
        destination->shadow_bytes = malloc((size_t)size);
        if (destination->shadow_bytes == NULL) {
            ringl_context_release_shadow_bytes(context, size);
            texture_free_generated_mips(context, generated);
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
                } else if (texture_color_component_is_float(
                               texture->color_component_type)) {
                    for (component = 0u; component < 4u; ++component) {
                        float a_component;
                        float b_component;
                        float c_component;
                        float d_component;
                        float result;
                        uint32_t component_bytes =
                            texture->color_component_type == RINGL_FLOAT
                            ? (uint32_t)sizeof(float)
                            : (uint32_t)sizeof(uint16_t);

                        a_component = texture_read_float_component(
                            a + component * component_bytes,
                            texture->color_component_type);
                        b_component = texture_read_float_component(
                            b + component * component_bytes,
                            texture->color_component_type);
                        c_component = texture_read_float_component(
                            c + component * component_bytes,
                            texture->color_component_type);
                        d_component = texture_read_float_component(
                            d + component * component_bytes,
                            texture->color_component_type);
                        /* Scaling before summation avoids an intermediate
                         * overflow for representable finite Float32 texels. */
                        result = a_component * 0.25f + b_component * 0.25f +
                                 c_component * 0.25f + d_component * 0.25f;
                        if (texture->color_component_type == RINGL_FLOAT) {
                            memcpy(output + component * sizeof(float), &result,
                                   sizeof(result));
                        } else {
                            uint16_t half = texture_write_half_component(result);

                            memcpy(output + component * sizeof(half), &half,
                                   sizeof(half));
                        }
                    }
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
    if (target == RINGL_TEXTURE_CUBE_MAP) {
        RinGLTextureObject* cube = bound_texture_cube(context);
        RinGLTextureMipStorage generated[RINGL_CUBE_FACE_COUNT]
            [RINGL_MAX_TEXTURE_MIP_LEVELS - 1u];
        uint32_t face_index;

        if (cube == NULL || cube->target != RINGL_TEXTURE_CUBE_MAP ||
            !texture_cube_base_images_complete(cube) ||
            !texture_color_format(cube->format) ||
            cube->compressed_format != 0u || cube->srgb_encoding != 0u ||
            !texture_dimension_is_power_of_two(cube->width) ||
            !context->has_ringpu_ops ||
            context->ringpu_ops.create_image_array_v1 == NULL ||
            context->ringpu_ops.upload_image_array_v1 == NULL) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return;
        }
        memset(generated, 0, sizeof(generated));
        for (face_index = 0u; face_index < RINGL_CUBE_FACE_COUNT;
             ++face_index) {
            RinGLTextureObject view;

            memset(&view, 0, sizeof(view));
            view.shadow_bytes = cube->cube_faces[face_index].shadow_bytes;
            view.shadow_size = cube->cube_faces[face_index].shadow_size;
            view.width = cube->cube_faces[face_index].width;
            view.height = cube->cube_faces[face_index].height;
            view.format = cube->cube_faces[face_index].format;
            view.color_component_type =
                cube->cube_faces[face_index].color_component_type;
            if (texture_generate_color_mips(context, &view,
                                             generated[face_index]) != 0) {
                uint32_t cleanup;

                for (cleanup = 0u; cleanup <= face_index; ++cleanup)
                    texture_free_generated_mips(context, generated[cleanup]);
                ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
                return;
            }
        }
        texture_discard_image(context, cube);
        for (face_index = 0u; face_index < RINGL_CUBE_FACE_COUNT;
             ++face_index) {
            uint32_t level;
            RinGLTextureCubeFaceStorage* face = &cube->cube_faces[face_index];

            for (level = 1u; level < RINGL_MAX_TEXTURE_MIP_LEVELS; ++level) {
                RinGLTextureMipStorage* storage =
                    texture_cube_mip_storage(face, level);

                if (storage == NULL)
                    continue;
                texture_release_storage(context, &storage->shadow_bytes,
                                        &storage->shadow_size);
                memset(storage, 0, sizeof(*storage));
            }
            for (level = 1u; level < texture_mip_level_count(
                         face->width, face->height); ++level) {
                face->mip_storage[level - 1u] = generated[face_index][level - 1u];
                memset(&generated[face_index][level - 1u], 0,
                       sizeof(generated[face_index][level - 1u]));
            }
            texture_free_generated_mips(context, generated[face_index]);
        }
        texture_cube_update_summary(cube);
        ringl_context_mark_dirty(context, RINGL_DIRTY_BINDINGS);
        return;
    }
    texture = bound_texture_for_target(context, target);
    if (texture == NULL || !texture_level0_storage_defined(texture) ||
        !texture_color_format(texture->format) ||
        texture->compressed_format != 0u || texture->srgb_encoding != 0u) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if (texture_is_npot(texture)) {
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
    if (texture_generate_color_mips(context, texture, generated) != 0) {
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return;
    }

    texture_discard_image(context, texture);
    texture_drop_mip_storage_from(context, texture, 1u);
    for (level = 1u; level < level_count; ++level) {
        texture->mip_storage[level - 1u] = generated[level - 1u];
        memset(&generated[level - 1u], 0, sizeof(generated[level - 1u]));
    }
    texture_free_generated_mips(context, generated);
    ringl_context_mark_dirty(context, RINGL_DIRTY_BINDINGS);
}

static int texture_cube_requested_format(uint32_t internal_format,
                                         uint32_t format, uint32_t type,
                                         uint32_t* format_out,
                                         uint32_t* component_type_out,
                                         uint32_t* srgb_out)
{
    uint32_t storage_format;

    if (format_out == NULL || component_type_out == NULL || srgb_out == NULL)
        return 0;
    *srgb_out = texture_srgb_internal_format(internal_format)
        ? RINGL_TRUE : RINGL_FALSE;
    if (*srgb_out != RINGL_FALSE) {
        storage_format = texture_srgb_storage_format(internal_format);
        if (type != RINGL_UNSIGNED_BYTE || format != internal_format)
            return 0;
        *format_out = storage_format;
        *component_type_out = RINGL_FLOAT;
        return 1;
    }
    storage_format = texture_storage_format(internal_format, format, type);
    *format_out = storage_format != 0u
        ? storage_format
        : texture_depth_storage_format(internal_format, format, type);
    *component_type_out = storage_format != 0u ? type : 0u;
    return *format_out != 0u;
}

static void ringl_tex_image_2d_impl(uint32_t target, int32_t level,
                                    uint32_t internal_format, int32_t width,
                                    int32_t height, int32_t border,
                                    uint32_t format, uint32_t type,
                                    const void* pixels, uint64_t pixels_size);

static void ringl_tex_sub_image_2d_impl(uint32_t target, int32_t level,
                                        int32_t xoffset, int32_t yoffset,
                                        int32_t width, int32_t height,
                                        uint32_t format, uint32_t type,
                                        const void* pixels,
                                        uint64_t pixels_size);

static int texture_cube_prepare_operation(RinGLContext* context,
                                           uint32_t face_index,
                                           RinGLTextureObject** texture_out,
                                           RinGLTextureCubeFaceStorage** face_out,
                                           RinGLTextureCubeFaceStorage* saved,
                                           uint32_t* old_2d_binding)
{
    RinGLTextureObject* texture;

    if (context == NULL || face_index >= RINGL_CUBE_FACE_COUNT ||
        texture_out == NULL || face_out == NULL || saved == NULL ||
        old_2d_binding == NULL || context->active_texture_unit >=
                                      RINGL_MAX_TEXTURE_UNITS)
        return 0;
    texture = bound_texture_cube(context);
    if (texture == NULL || texture->target != RINGL_TEXTURE_CUBE_MAP)
        return 0;
    *old_2d_binding = context->bound_texture_2d[context->active_texture_unit];
    context->bound_texture_2d[context->active_texture_unit] =
        context->bound_texture_cube[context->active_texture_unit];
    texture_discard_image(context, texture);
    *texture_out = texture;
    *face_out = &texture->cube_faces[face_index];
    texture_cube_face_enter(texture, *face_out, saved);
    return 1;
}

static void texture_cube_finish_operation(RinGLContext* context,
                                           RinGLTextureObject* texture,
                                           RinGLTextureCubeFaceStorage* face,
                                           const RinGLTextureCubeFaceStorage* saved,
                                           uint32_t old_2d_binding)
{
    if (context == NULL || texture == NULL || face == NULL || saved == NULL)
        return;
    texture_cube_face_leave(texture, face, saved);
    context->bound_texture_2d[context->active_texture_unit] = old_2d_binding;
}

static void texture_cube_tex_image_2d(uint32_t target, int32_t level,
                                      uint32_t internal_format, int32_t width,
                                      int32_t height, int32_t border,
                                      uint32_t format, uint32_t type,
                                      const void* pixels, uint64_t pixels_size)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLTextureObject* texture;
    RinGLTextureCubeFaceStorage* face;
    RinGLTextureCubeFaceStorage saved;
    uint32_t face_index;
    uint32_t old_2d_binding;
    uint32_t requested_format;
    uint32_t requested_component_type;
    uint32_t requested_srgb;

    if (context == NULL || !texture_cube_face_index(target, &face_index)) {
        if (context != NULL)
            ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (!texture_cube_requested_format(internal_format, format, type,
                                       &requested_format,
                                       &requested_component_type,
                                       &requested_srgb)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (level == 0 && width != height) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    texture = bound_texture_cube(context);
    if (texture == NULL || texture->target != RINGL_TEXTURE_CUBE_MAP) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if (level == 0 && !texture_cube_base_compatible(
            texture, face_index, (uint32_t)width, (uint32_t)height,
            requested_format, requested_component_type,
            context->pending_compressed_format, requested_srgb)) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if (!texture_cube_prepare_operation(context, face_index, &texture, &face,
                                        &saved, &old_2d_binding)) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    ringl_tex_image_2d_impl(RINGL_TEXTURE_2D, level, internal_format, width,
                            height, border, format, type, pixels, pixels_size);
    texture_cube_finish_operation(context, texture, face, &saved,
                                  old_2d_binding);
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
    uint32_t requested_format;
    uint32_t requested_color_component_type;
    uint32_t requested_srgb_encoding;

    if (context == NULL)
        return;
    if (target != RINGL_TEXTURE_2D) {
        texture_cube_tex_image_2d(target, level, internal_format, width, height,
                                  border, format, type, pixels, pixels_size);
        return;
    }
    requested_srgb_encoding = texture_srgb_internal_format(internal_format)
        ? RINGL_TRUE : RINGL_FALSE;
    if (requested_srgb_encoding != RINGL_FALSE) {
        storage_format = texture_srgb_storage_format(internal_format);
        requested_format = storage_format;
        requested_color_component_type = RINGL_FLOAT;
        if (type != RINGL_UNSIGNED_BYTE || format != internal_format) {
            ringl_context_record_error(context, RINGL_INVALID_ENUM);
            return;
        }
    } else {
        storage_format = texture_storage_format(internal_format, format, type);
        requested_format = storage_format != 0u
            ? storage_format
            : texture_depth_storage_format(internal_format, format, type);
        requested_color_component_type = storage_format != 0u ? type : 0u;
    }
    if (!texture_target_valid(target) || requested_format == 0u) {
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

        if (!texture_level0_storage_defined(texture) ||
            !(texture_color_format(requested_format) ||
              requested_format == RINGL_DEPTH_COMPONENT32F ||
              requested_format == RINGL_DEPTH24_STENCIL8) ||
            texture->format != requested_format ||
            texture->color_component_type != requested_color_component_type ||
            texture->srgb_encoding != requested_srgb_encoding ||
            (context->pending_compressed_format == 0u &&
             texture->compressed_format != 0u) ||
            (context->pending_compressed_format != 0u &&
             texture->compressed_format != context->pending_compressed_format)) {
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
            texture_storage_texel_bytes(
                requested_format, storage_format != 0u
                    ? requested_color_component_type : 0u);
    if (size > SIZE_MAX) {
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return;
    }
    if (size != 0u) {
        if (!ringl_context_reserve_shadow_bytes(context, size)) {
            ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
            return;
        }
        replacement = malloc((size_t)size);
        if (replacement == NULL) {
            ringl_context_release_shadow_bytes(context, size);
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
                    uint8_t* destination = replacement +
                        (uint64_t)row * (uint32_t)width *
                        texture_storage_texel_bytes(
                            storage_format, requested_color_component_type);
                    const uint8_t* source = (const uint8_t*)pixels +
                        row * source_row_pitch;

                    if (requested_srgb_encoding != RINGL_FALSE) {
                        texture_copy_srgb_texels(
                            destination, source, format,
                            storage_format == RINGL_RGBA, (uint32_t)width);
                    } else {
                        texture_copy_color_texels(destination, source,
                                                  storage_format, format, type,
                                                  (uint32_t)width);
                    }
                }
            } else if (requested_format == RINGL_DEPTH24_STENCIL8) {
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
                    texture_copy_depth_texels(
                        replacement + (uint64_t)row * (uint32_t)width *
                            sizeof(float),
                        (const uint8_t*)pixels + row * source_row_pitch, type,
                        (uint32_t)width);
                }
            }
    } else {
        memset(replacement, 0, (size_t)size);
        if (storage_format != 0u) {
            texture_initialize_color_defaults(
                replacement, (uint64_t)(uint32_t)width * (uint32_t)height,
                storage_format, requested_color_component_type);
        }
        if (requested_srgb_encoding != RINGL_FALSE &&
            storage_format == RINGL_RGB) {
            texture_initialize_srgb_rgb_alpha(
                replacement, (uint64_t)(uint32_t)width * (uint32_t)height);
        }
    }
    }

    texture_discard_image(context, texture);
    if (level == 0) {
        /* A new base definition can change the entire level hierarchy. Drop
         * every old level only after allocation and conversion succeeded. */
        texture_drop_mip_storage_from(context, texture, 1u);
        texture_release_storage(context, &texture->shadow_bytes,
                                &texture->shadow_size);
        texture->shadow_bytes = replacement;
        texture->shadow_size = size;
        texture->width = (uint32_t)width;
        texture->height = (uint32_t)height;
        texture->format = requested_format;
        texture->color_component_type = requested_color_component_type;
        texture->compressed_format = context->pending_compressed_format;
        texture->srgb_encoding = requested_srgb_encoding;
        texture->defined = RINGL_TRUE;
    } else {
        texture_release_storage(context, &mip_storage->shadow_bytes,
                                &mip_storage->shadow_size);
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
    RinGLContext* context = ringl_get_current_context();
    if (context != NULL)
        ringl_context_trace(context, RINGL_TRACE_TEXTURE_UPLOAD,
                            ((uint64_t)target << 32u) | (uint32_t)level,
                            ((uint64_t)(uint32_t)width << 32u) |
                                (uint32_t)height,
                            0);
    ringl_tex_image_2d_impl(target, level, internal_format, width, height,
                            border, format, type, pixels, UINT64_MAX);
}

void ringl_tex_image_2d_from_bytes(uint32_t target, int32_t level,
                                   uint32_t internal_format, int32_t width,
                                   int32_t height, int32_t border,
                                   uint32_t format, uint32_t type,
                                   const void* pixels, uint64_t pixels_size)
{
    RinGLContext* context = ringl_get_current_context();
    if (context != NULL)
        ringl_context_trace(context, RINGL_TRACE_TEXTURE_UPLOAD,
                            ((uint64_t)target << 32u) | (uint32_t)level,
                            ((uint64_t)(uint32_t)width << 32u) |
                                (uint32_t)height,
                            0);
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
    if (target != RINGL_TEXTURE_2D) {
        uint32_t face_index;

        if (!texture_cube_face_index(target, &face_index)) {
            ringl_context_record_error(context, RINGL_INVALID_ENUM);
            return;
        }
        {
            RinGLTextureObject* cube_texture;
            RinGLTextureCubeFaceStorage* cube_face;
            RinGLTextureCubeFaceStorage saved;
            uint32_t old_2d_binding;

            if (!texture_cube_prepare_operation(
                    context, face_index, &cube_texture, &cube_face, &saved,
                    &old_2d_binding)) {
                ringl_context_record_error(context, RINGL_INVALID_OPERATION);
                return;
            }
            ringl_tex_sub_image_2d_impl(
                RINGL_TEXTURE_2D, level, xoffset, yoffset, width, height,
                format, type, pixels, pixels_size);
            texture_cube_finish_operation(context, cube_texture, cube_face,
                                          &saved, old_2d_binding);
        }
        return;
    }
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
    if (texture == NULL || !texture_level0_storage_defined(texture) ||
        (context->pending_compressed_format == 0u &&
         texture->compressed_format != 0u) ||
        (context->pending_compressed_format != 0u &&
         texture->compressed_format != context->pending_compressed_format)) {
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
    if (!(texture_upload_format_valid(texture, format, type) ||
          texture_depth_upload_format_valid(texture->format, format, type))) {
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
             (uint32_t)xoffset) * texture_storage_texel_bytes(
                 texture->format, texture->color_component_type);
        uint64_t source_offset = (uint64_t)row *
            texture_source_row_pitch((uint32_t)width, format, type,
                                     context->unpack_alignment);

        if (texture_color_format(texture->format)) {
            if (texture->srgb_encoding != 0u) {
                texture_copy_srgb_texels(
                    level_bytes + destination_offset,
                    (const uint8_t*)pixels + source_offset, format,
                    texture->format == RINGL_RGBA, (uint32_t)width);
            } else {
                texture_copy_color_texels(
                    level_bytes + destination_offset,
                    (const uint8_t*)pixels + source_offset, texture->format,
                    format, type, (uint32_t)width);
            }
        } else if (texture->format == RINGL_DEPTH24_STENCIL8) {
            texture_copy_depth_stencil_texels(
                level_bytes + destination_offset,
                (const uint8_t*)pixels + source_offset, (uint32_t)width);
        } else {
            texture_copy_depth_texels(level_bytes + destination_offset,
                                      (const uint8_t*)pixels + source_offset,
                                      type, (uint32_t)width);
        }
    }

    /* Drop the realized image and rebuild the complete chain lazily. A base
     * update invalidates only derived levels; explicitly defined levels retain
     * their WebGL image contents. */
    texture_discard_image(context, texture);
    if (level == 0)
        texture_drop_generated_mips(context, texture);
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
    uint64_t snapshot_size;
    uint8_t* snapshot;
    uint8_t* level_bytes;
    uint32_t level_width;
    uint32_t level_height;
    uint32_t snapshot_component_type;
    uint32_t row;

    if (context == NULL)
        return;
    if (!texture_image_target_valid(target)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (level < 0 || (uint32_t)level >= RINGL_MAX_TEXTURE_MIP_LEVELS ||
        xoffset < 0 || yoffset < 0 || x < 0 || y < 0 || width < 0 ||
        height < 0) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (target != RINGL_TEXTURE_2D) {
        uint32_t face_index;
        RinGLTextureObject* cube_texture;
        RinGLTextureCubeFaceStorage* cube_face;
        RinGLTextureCubeFaceStorage saved;
        uint32_t old_2d_binding;

        (void)texture_cube_face_index(target, &face_index);
        cube_texture = bound_texture_cube(context);
        if (cube_texture == NULL ||
            !texture_cube_prepare_operation(context, face_index,
                                            &cube_texture, &cube_face,
                                            &saved, &old_2d_binding)) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return;
        }
        ringl_copy_tex_sub_image_2d(
            RINGL_TEXTURE_2D, level, xoffset, yoffset, x, y, width, height);
        texture_cube_finish_operation(context, cube_texture, cube_face,
                                      &saved, old_2d_binding);
        return;
    }
    texture = bound_texture_for_target(context, target);
    if (texture == NULL || !texture_level0_storage_defined(texture) ||
        !texture_color_format(texture->format) ||
        texture->compressed_format != 0u) {
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
        ringl_context_record_error(context,
                                   ringl_framebuffer_operation_error(context));
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
    snapshot_component_type = source.srgb_encoding == 0u &&
            (source.format == RINGL_RIN_GPU_FORMAT_RGBA32_FLOAT ||
             source.format == RINGL_RIN_GPU_FORMAT_RGBA16_FLOAT)
        ? RINGL_FLOAT : RINGL_UNSIGNED_BYTE;
    snapshot_size = (uint64_t)(uint32_t)width * (uint64_t)(uint32_t)height *
        4u * (snapshot_component_type == RINGL_FLOAT
                  ? (uint64_t)sizeof(float) : 1u);
    if (snapshot_size > SIZE_MAX) {
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return;
    }
    snapshot = ringl_context_alloc_temporary(context, snapshot_size);
    if (snapshot == NULL) {
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return;
    }
    if ((snapshot_component_type == RINGL_FLOAT
             ? ringl_read_color_target_rgba_float(context, x, y, width,
                                                   height, snapshot)
             : ringl_read_color_target_rgba(context, x, y, width, height,
                                             snapshot)) != 0 ||
        (snapshot_component_type == RINGL_FLOAT &&
         !texture_canonical_rgba_float_snapshot_valid(
             snapshot, (uint32_t)width * (uint32_t)height))) {
        ringl_context_free_temporary(context, snapshot, snapshot_size);
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    for (row = 0u; row < (uint32_t)height; ++row) {
        uint64_t destination_offset =
            ((uint64_t)((uint32_t)yoffset + row) * level_width +
             (uint32_t)xoffset) * texture_storage_texel_bytes(
                 texture->format, texture->color_component_type);

        if (texture_copy_canonical_rgba_to_typed_copy_image_storage(
                level_bytes + destination_offset,
                snapshot + (uint64_t)row * (uint32_t)width * 4u *
                    (snapshot_component_type == RINGL_FLOAT
                         ? (uint64_t)sizeof(float) : 1u),
                snapshot_component_type, texture->format,
                texture->color_component_type, texture->srgb_encoding,
                (uint32_t)width) != 0) {
            ringl_context_free_temporary(context, snapshot, snapshot_size);
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return;
        }
    }
    ringl_context_free_temporary(context, snapshot, snapshot_size);
    texture_discard_image(context, texture);
    if (level == 0)
        texture_drop_generated_mips(context, texture);
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
    RinGLContext* context = ringl_get_current_context();
    if (context != NULL)
        ringl_context_trace(context, RINGL_TRACE_TEXTURE_UPLOAD,
                            ((uint64_t)target << 32u) | (uint32_t)level,
                            ((uint64_t)(uint32_t)width << 32u) |
                                (uint32_t)height,
                            0);
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
    RinGLContext* context = ringl_get_current_context();
    if (context != NULL)
        ringl_context_trace(context, RINGL_TRACE_TEXTURE_UPLOAD,
                            ((uint64_t)target << 32u) | (uint32_t)level,
                            ((uint64_t)(uint32_t)width << 32u) |
                                (uint32_t)height,
                            0);
    ringl_tex_sub_image_2d_impl(target, level, xoffset, yoffset, width,
                                height, format, type, pixels, pixels_size);
}

static uint32_t etc1_read_be32(const uint8_t* bytes)
{
    return ((uint32_t)bytes[0] << 24u) | ((uint32_t)bytes[1] << 16u) |
           ((uint32_t)bytes[2] << 8u) | (uint32_t)bytes[3];
}

static int32_t etc1_sign_extend3(uint32_t value)
{
    return (value & 4u) != 0u ? (int32_t)value - 8 : (int32_t)value;
}

static uint8_t etc1_expand4(uint32_t value)
{
    return (uint8_t)((value << 4u) | value);
}

static uint8_t etc1_expand5(uint32_t value)
{
    return (uint8_t)((value << 3u) | (value >> 2u));
}

static uint8_t etc1_clamp_component(int32_t value)
{
    if (value < 0)
        return 0u;
    if (value > 255)
        return UINT8_MAX;
    return (uint8_t)value;
}

static int etc1_required_bytes(uint32_t width, uint32_t height,
                               uint64_t* bytes_out)
{
    uint64_t block_count;

    if (bytes_out == NULL)
        return -1;
    if (width == 0u || height == 0u) {
        *bytes_out = 0u;
        return 0;
    }
    block_count = ((uint64_t)width + 3u) / 4u;
    block_count *= ((uint64_t)height + 3u) / 4u;
    if (block_count > UINT64_MAX / 8u)
        return -1;
    *bytes_out = block_count * 8u;
    return 0;
}

/* Decode one ETC1 RGB8 block according to the Khronos ETC1 bit layout. The
 * low-word selector plane is column-major (x * 4 + y); keeping that detail
 * local avoids transposing the visible texture during conversion. */
static int etc1_decode_block(const uint8_t* source, uint8_t* destination,
                             uint32_t destination_width,
                             uint32_t destination_height,
                             uint32_t origin_x, uint32_t origin_y)
{
    static const int16_t modifiers[8][4] = {
        { 2, 8, -2, -8 }, { 5, 17, -5, -17 },
        { 9, 29, -9, -29 }, { 13, 42, -13, -42 },
        { 18, 60, -18, -60 }, { 24, 80, -24, -80 },
        { 33, 106, -33, -106 }, { 47, 183, -47, -183 },
    };
    uint32_t high = etc1_read_be32(source);
    uint32_t low = etc1_read_be32(source + 4u);
    uint8_t base[2][3];
    uint32_t tables[2];
    uint32_t flip = high & 1u;
    uint32_t x;
    uint32_t y;

    tables[0] = (high >> 5u) & 7u;
    tables[1] = (high >> 2u) & 7u;
    if ((high & 2u) != 0u) {
        int32_t red0 = (int32_t)((high >> 27u) & 31u);
        int32_t green0 = (int32_t)((high >> 19u) & 31u);
        int32_t blue0 = (int32_t)((high >> 11u) & 31u);
        int32_t red1 = red0 + etc1_sign_extend3((high >> 24u) & 7u);
        int32_t green1 = green0 + etc1_sign_extend3((high >> 16u) & 7u);
        int32_t blue1 = blue0 + etc1_sign_extend3((high >> 8u) & 7u);

        if (red1 < 0 || red1 > 31 || green1 < 0 || green1 > 31 ||
            blue1 < 0 || blue1 > 31) {
            return -1;
        }
        base[0][0] = etc1_expand5((uint32_t)red0);
        base[0][1] = etc1_expand5((uint32_t)green0);
        base[0][2] = etc1_expand5((uint32_t)blue0);
        base[1][0] = etc1_expand5((uint32_t)red1);
        base[1][1] = etc1_expand5((uint32_t)green1);
        base[1][2] = etc1_expand5((uint32_t)blue1);
    } else {
        base[0][0] = etc1_expand4((high >> 28u) & 15u);
        base[1][0] = etc1_expand4((high >> 24u) & 15u);
        base[0][1] = etc1_expand4((high >> 20u) & 15u);
        base[1][1] = etc1_expand4((high >> 16u) & 15u);
        base[0][2] = etc1_expand4((high >> 12u) & 15u);
        base[1][2] = etc1_expand4((high >> 8u) & 15u);
    }

    for (y = 0u; y < 4u; ++y) {
        for (x = 0u; x < 4u; ++x) {
            uint32_t selector_bit = x * 4u + y;
            uint32_t selector = ((low >> selector_bit) & 1u) |
                                (((low >> (selector_bit + 16u)) & 1u) << 1u);
            uint32_t subblock = flip != 0u ? (y >= 2u) : (x >= 2u);
            int32_t modifier = modifiers[tables[subblock]][selector];
            uint8_t* pixel;

            if (origin_x + x >= destination_width ||
                origin_y + y >= destination_height) {
                continue;
            }
            pixel = destination + ((uint64_t)(origin_y + y) *
                                   destination_width + origin_x + x) * 3u;
            pixel[0] = etc1_clamp_component((int32_t)base[subblock][0] +
                                             modifier);
            pixel[1] = etc1_clamp_component((int32_t)base[subblock][1] +
                                             modifier);
            pixel[2] = etc1_clamp_component((int32_t)base[subblock][2] +
                                             modifier);
        }
    }
    return 0;
}

/* Returns 0 on success, -1 for malformed/short data, and -2 when the bounded
 * RGB expansion cannot be allocated. `decoded_out` is owned by the caller. */
static int etc1_decode_image(RinGLContext* context, uint32_t width,
                              uint32_t height,
                              const void* data, uint64_t data_size,
                              uint8_t** decoded_out)
{
    const uint8_t* source = data;
    uint64_t required_bytes;
    uint64_t decoded_size;
    uint8_t* decoded;
    uint32_t block_y;
    uint32_t block_x;
    uint32_t blocks_wide;

    if (decoded_out == NULL || etc1_required_bytes(width, height,
                                                   &required_bytes) != 0 ||
        data_size != required_bytes || (required_bytes != 0u && data == NULL)) {
        return -1;
    }
    *decoded_out = NULL;
    if (required_bytes == 0u)
        return 0;
    decoded_size = (uint64_t)width * (uint64_t)height * 3u;
    if (decoded_size > SIZE_MAX)
        return -2;
    decoded = ringl_context_alloc_temporary(context, decoded_size);
    if (decoded == NULL)
        return -2;
    blocks_wide = (width + 3u) / 4u;
    for (block_y = 0u; block_y < (height + 3u) / 4u; ++block_y) {
        for (block_x = 0u; block_x < blocks_wide; ++block_x) {
            uint64_t block_index = (uint64_t)block_y * blocks_wide + block_x;

            if (etc1_decode_block(source + block_index * 8u, decoded, width,
                                  height, block_x * 4u, block_y * 4u) != 0) {
                ringl_context_free_temporary(context, decoded, decoded_size);
                return -1;
            }
        }
    }
    *decoded_out = decoded;
    return 0;
}

static uint16_t dxt_read_le16(const uint8_t* bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8u));
}

static uint8_t dxt_expand5(uint32_t value)
{
    return (uint8_t)((value << 3u) | (value >> 2u));
}

static uint8_t dxt_expand6(uint32_t value)
{
    return (uint8_t)((value << 2u) | (value >> 4u));
}

static int dxt_format_info(uint32_t format, uint32_t* block_bytes_out,
                           uint32_t* output_format_out,
                           uint32_t* srgb_out)
{
    if (block_bytes_out == NULL || output_format_out == NULL)
        return -1;
    if (srgb_out != NULL)
        *srgb_out = RINGL_FALSE;
    switch (format) {
    case RINGL_COMPRESSED_RGB_S3TC_DXT1_EXT:
        *block_bytes_out = 8u;
        *output_format_out = RINGL_RGB;
        return 0;
    case RINGL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
        *block_bytes_out = 8u;
        *output_format_out = RINGL_RGBA;
        return 0;
    case RINGL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
    case RINGL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
        *block_bytes_out = 16u;
        *output_format_out = RINGL_RGBA;
        return 0;
    case RINGL_COMPRESSED_SRGB_S3TC_DXT1_EXT:
        *block_bytes_out = 8u;
        *output_format_out = RINGL_RGB;
        if (srgb_out != NULL)
            *srgb_out = RINGL_TRUE;
        return 0;
    case RINGL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT:
        *block_bytes_out = 8u;
        *output_format_out = RINGL_RGBA;
        if (srgb_out != NULL)
            *srgb_out = RINGL_TRUE;
        return 0;
    case RINGL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT:
    case RINGL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
        *block_bytes_out = 16u;
        *output_format_out = RINGL_RGBA;
        if (srgb_out != NULL)
            *srgb_out = RINGL_TRUE;
        return 0;
    default:
        return -1;
    }
}

static int dxt_required_bytes(uint32_t width, uint32_t height,
                              uint32_t block_bytes, uint64_t* bytes_out)
{
    uint64_t blocks;

    if (bytes_out == NULL || (block_bytes != 8u && block_bytes != 16u))
        return -1;
    if (width == 0u || height == 0u) {
        *bytes_out = 0u;
        return 0;
    }
    blocks = ((uint64_t)width + 3u) / 4u;
    blocks *= ((uint64_t)height + 3u) / 4u;
    if (blocks > UINT64_MAX / block_bytes)
        return -1;
    *bytes_out = blocks * block_bytes;
    return 0;
}

static void dxt_decode_color_block(const uint8_t* source, uint8_t palette[4][4],
                                   uint32_t rgba_dxt1)
{
    uint16_t color0 = dxt_read_le16(source);
    uint16_t color1 = dxt_read_le16(source + 2u);
    uint32_t index;

    for (index = 0u; index < 2u; ++index) {
        uint16_t color = index == 0u ? color0 : color1;

        palette[index][0] = dxt_expand5((color >> 11u) & 31u);
        palette[index][1] = dxt_expand6((color >> 5u) & 63u);
        palette[index][2] = dxt_expand5(color & 31u);
        palette[index][3] = UINT8_MAX;
    }
    if (color0 > color1 || rgba_dxt1 == 0u) {
        for (index = 0u; index < 3u; ++index) {
            palette[2][index] = (uint8_t)((2u * palette[0][index] +
                                            palette[1][index]) / 3u);
            palette[3][index] = (uint8_t)((palette[0][index] +
                                            2u * palette[1][index]) / 3u);
        }
        palette[2][3] = UINT8_MAX;
        palette[3][3] = UINT8_MAX;
    } else {
        for (index = 0u; index < 3u; ++index)
            palette[2][index] = (uint8_t)((palette[0][index] + palette[1][index]) / 2u);
        palette[2][3] = UINT8_MAX;
        palette[3][0] = 0u;
        palette[3][1] = 0u;
        palette[3][2] = 0u;
        palette[3][3] = 0u;
    }
}

static void dxt_decode_block(const uint8_t* source, uint32_t format,
                             uint8_t* destination, uint32_t destination_width,
                             uint32_t destination_height, uint32_t origin_x,
                             uint32_t origin_y)
{
    uint8_t palette[4][4];
    uint8_t alpha[16];
    const uint8_t* color_source;
    uint32_t selectors;
    uint32_t x;
    uint32_t y;

    for (y = 0u; y < 16u; ++y)
        alpha[y] = UINT8_MAX;
    if (format == RINGL_COMPRESSED_RGBA_S3TC_DXT3_EXT ||
        format == RINGL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT) {
        for (y = 0u; y < 4u; ++y) {
            uint16_t row = dxt_read_le16(source + y * 2u);
            for (x = 0u; x < 4u; ++x)
                alpha[y * 4u + x] = (uint8_t)(((row >> (x * 4u)) & 15u) * 17u);
        }
        color_source = source + 8u;
    } else if (format == RINGL_COMPRESSED_RGBA_S3TC_DXT5_EXT ||
               format == RINGL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT) {
        uint8_t alpha_palette[8];
        uint64_t alpha_bits = 0u;

        alpha_palette[0] = source[0];
        alpha_palette[1] = source[1];
        if (alpha_palette[0] > alpha_palette[1]) {
            for (x = 1u; x < 7u; ++x)
                alpha_palette[x + 1u] = (uint8_t)(((7u - x) * alpha_palette[0] +
                                                    x * alpha_palette[1]) / 7u);
        } else {
            for (x = 1u; x < 5u; ++x)
                alpha_palette[x + 1u] = (uint8_t)(((5u - x) * alpha_palette[0] +
                                                    x * alpha_palette[1]) / 5u);
            alpha_palette[6] = 0u;
            alpha_palette[7] = UINT8_MAX;
        }
        for (x = 0u; x < 6u; ++x)
            alpha_bits |= (uint64_t)source[2u + x] << (x * 8u);
        for (x = 0u; x < 16u; ++x)
            alpha[x] = alpha_palette[(alpha_bits >> (x * 3u)) & 7u];
        color_source = source + 8u;
    } else {
        color_source = source;
    }
    dxt_decode_color_block(
        color_source, palette,
        format == RINGL_COMPRESSED_RGBA_S3TC_DXT1_EXT ||
            format == RINGL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT);
    selectors = (uint32_t)color_source[4] | ((uint32_t)color_source[5] << 8u) |
                ((uint32_t)color_source[6] << 16u) | ((uint32_t)color_source[7] << 24u);
    for (y = 0u; y < 4u; ++y) {
        for (x = 0u; x < 4u; ++x) {
            uint32_t pixel_index = y * 4u + x;
            uint8_t* pixel;
            uint32_t selector;

            if (origin_x + x >= destination_width || origin_y + y >= destination_height)
                continue;
            selector = (selectors >> (pixel_index * 2u)) & 3u;
            pixel = destination + ((uint64_t)(origin_y + y) * destination_width +
                                   origin_x + x) * 4u;
            pixel[0] = palette[selector][0];
            pixel[1] = palette[selector][1];
            pixel[2] = palette[selector][2];
            pixel[3] = ((format == RINGL_COMPRESSED_RGBA_S3TC_DXT1_EXT ||
                         format == RINGL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT) &&
                        palette[selector][3] == 0u) ? 0u : alpha[pixel_index];
        }
    }
}

static int dxt_decode_image(RinGLContext* context, uint32_t width,
                            uint32_t height, uint32_t format,
                            const void* data, uint64_t data_size,
                            uint8_t** decoded_out)
{
    uint32_t block_bytes;
    uint32_t output_format;
    uint64_t required_bytes;
    uint64_t decoded_size;
    uint8_t* decoded;
    uint32_t blocks_wide;
    uint32_t block_y;
    uint32_t block_x;

    if (decoded_out == NULL || dxt_format_info(format, &block_bytes, &output_format,
                                                NULL) != 0 ||
        dxt_required_bytes(width, height, block_bytes, &required_bytes) != 0 ||
        data_size != required_bytes || (required_bytes != 0u && data == NULL))
        return -1;
    *decoded_out = NULL;
    if (required_bytes == 0u)
        return 0;
    decoded_size = (uint64_t)width * height * 4u;
    if (decoded_size > SIZE_MAX)
        return -2;
    decoded = ringl_context_alloc_temporary(context, decoded_size);
    if (decoded == NULL)
        return -2;
    blocks_wide = (width + 3u) / 4u;
    for (block_y = 0u; block_y < (height + 3u) / 4u; ++block_y) {
        for (block_x = 0u; block_x < blocks_wide; ++block_x) {
            uint64_t index = (uint64_t)block_y * blocks_wide + block_x;

            dxt_decode_block((const uint8_t*)data + index * block_bytes, format,
                             decoded, width, height, block_x * 4u, block_y * 4u);
        }
    }
    if (output_format == RINGL_RGB) {
        uint64_t rgb_size = (uint64_t)width * height * 3u;
        uint8_t* rgb;
        uint64_t pixel;

        rgb = ringl_context_alloc_temporary(context, rgb_size);
        if (rgb == NULL) {
            ringl_context_free_temporary(context, decoded, decoded_size);
            return -2;
        }
        for (pixel = 0u; pixel < (uint64_t)width * height; ++pixel) {
            rgb[pixel * 3u] = decoded[pixel * 4u];
            rgb[pixel * 3u + 1u] = decoded[pixel * 4u + 1u];
            rgb[pixel * 3u + 2u] = decoded[pixel * 4u + 2u];
        }
        ringl_context_free_temporary(context, decoded, decoded_size);
        decoded = rgb;
    }
    *decoded_out = decoded;
    return 0;
}

/* The IEC 61966-2-1 EOTF is stored as a 16-bit normalized lookup table so
 * compressed sRGB uploads have deterministic linear values without linking a
 * freestanding RinGL build against libm. */
float ringl_srgb_decode_u8(uint8_t value)
{
    static const uint16_t srgb_to_linear[256] = {
        0, 20, 40, 60, 80, 99, 119, 139,
        159, 179, 199, 219, 241, 264, 288, 313,
        340, 367, 396, 427, 458, 491, 526, 562,
        599, 637, 677, 718, 761, 805, 851, 898,
        947, 997, 1048, 1101, 1156, 1212, 1270, 1330,
        1391, 1453, 1517, 1583, 1651, 1720, 1790, 1863,
        1937, 2013, 2090, 2170, 2250, 2333, 2418, 2504,
        2592, 2681, 2773, 2866, 2961, 3058, 3157, 3258,
        3360, 3464, 3570, 3678, 3788, 3900, 4014, 4129,
        4247, 4366, 4488, 4611, 4736, 4864, 4993, 5124,
        5257, 5392, 5530, 5669, 5810, 5953, 6099, 6246,
        6395, 6547, 6700, 6856, 7014, 7174, 7335, 7500,
        7666, 7834, 8004, 8177, 8352, 8528, 8708, 8889,
        9072, 9258, 9445, 9635, 9828, 10022, 10219, 10417,
        10619, 10822, 11028, 11235, 11446, 11658, 11873, 12090,
        12309, 12530, 12754, 12980, 13209, 13440, 13673, 13909,
        14146, 14387, 14629, 14874, 15122, 15371, 15623, 15878,
        16135, 16394, 16656, 16920, 17187, 17456, 17727, 18001,
        18277, 18556, 18837, 19121, 19407, 19696, 19987, 20281,
        20577, 20876, 21177, 21481, 21787, 22096, 22407, 22721,
        23038, 23357, 23678, 24002, 24329, 24658, 24990, 25325,
        25662, 26001, 26344, 26688, 27036, 27386, 27739, 28094,
        28452, 28813, 29176, 29542, 29911, 30282, 30656, 31033,
        31412, 31794, 32179, 32567, 32957, 33350, 33745, 34143,
        34544, 34948, 35355, 35764, 36176, 36591, 37008, 37429,
        37852, 38278, 38706, 39138, 39572, 40009, 40449, 40891,
        41337, 41785, 42236, 42690, 43147, 43606, 44069, 44534,
        45002, 45473, 45947, 46423, 46903, 47385, 47871, 48359,
        48850, 49344, 49841, 50341, 50844, 51349, 51858, 52369,
        52884, 53401, 53921, 54445, 54971, 55500, 56032, 56567,
        57105, 57646, 58190, 58737, 59287, 59840, 60396, 60955,
        61517, 62082, 62650, 63221, 63795, 64372, 64952, 65535,
    };

    return (float)srgb_to_linear[value] / 65535.0f;
}

uint8_t ringl_srgb_encode_float(float value)
{
    uint32_t low = 0u;
    uint32_t high = UINT8_MAX;

    if (!(value > 0.0f))
        return 0u;
    if (value >= 1.0f)
        return UINT8_MAX;
    /* The decode table is monotonic. Search it rather than linking libm into
     * freestanding RinGL; choose the closest representable sRGB byte. */
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;

        if (ringl_srgb_decode_u8((uint8_t)middle) < value)
            low = middle + 1u;
        else
            high = middle;
    }
    if (low != 0u) {
        float upper = ringl_srgb_decode_u8((uint8_t)low);
        float lower = ringl_srgb_decode_u8((uint8_t)(low - 1u));

        if (value - lower < upper - value)
            return (uint8_t)(low - 1u);
    }
    return (uint8_t)low;
}

static int dxt_srgb_decode_image(RinGLContext* context, uint32_t width,
                                 uint32_t height,
                                 uint32_t output_format,
                                 const uint8_t* encoded,
                                 uint8_t** decoded_out)
{
    uint32_t components;
    uint64_t pixel_count;
    uint64_t decoded_size;
    uint8_t* decoded;
    uint64_t pixel;

    if (decoded_out == NULL ||
        (output_format != RINGL_RGB && output_format != RINGL_RGBA))
        return -1;
    *decoded_out = NULL;
    components = output_format == RINGL_RGB ? 3u : 4u;
    pixel_count = (uint64_t)width * height;
    if (pixel_count == 0u)
        return 0;
    if (encoded == NULL || pixel_count > UINT64_MAX / components ||
        pixel_count * components > SIZE_MAX / sizeof(float))
        return -2;
    decoded_size = pixel_count * components * sizeof(float);
    decoded = ringl_context_alloc_temporary(context, decoded_size);
    if (decoded == NULL)
        return -2;
    for (pixel = 0u; pixel < pixel_count; ++pixel) {
        uint8_t* destination = decoded + pixel * components * sizeof(float);
        float red = ringl_srgb_decode_u8(encoded[pixel * components]);
        float green = ringl_srgb_decode_u8(encoded[pixel * components + 1u]);
        float blue = ringl_srgb_decode_u8(encoded[pixel * components + 2u]);

        memcpy(destination, &red, sizeof(red));
        memcpy(destination + sizeof(float), &green, sizeof(green));
        memcpy(destination + 2u * sizeof(float), &blue, sizeof(blue));
        if (components == 4u) {
            float alpha = (float)encoded[pixel * components + 3u] / 255.0f;

            memcpy(destination + 3u * sizeof(float), &alpha, sizeof(alpha));
        }
    }
    *decoded_out = decoded;
    return 0;
}

void ringl_compressed_tex_image_2d_from_bytes(uint32_t target, int32_t level,
                                               uint32_t internal_format,
                                              int32_t width, int32_t height,
                                              int32_t border,
                                              const void* data,
                                              uint64_t data_size)
{
    RinGLContext* context = ringl_get_current_context();
    uint8_t* decoded = NULL;
    uint32_t block_bytes;
    uint32_t output_format;
    uint32_t output_type = RINGL_UNSIGNED_BYTE;
    uint32_t srgb = RINGL_FALSE;
    int decode_result;

    if (context == NULL)
        return;
    if (!texture_image_target_valid(target)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (internal_format == RINGL_ETC1_RGB8_OES)
        output_format = RINGL_RGB;
    else if (dxt_format_info(internal_format, &block_bytes, &output_format,
                             &srgb) != 0) {
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
    if (target != RINGL_TEXTURE_2D) {
        uint32_t face_index;
        RinGLTextureObject* cube_texture;
        RinGLTextureCubeFaceStorage* cube_face;
        RinGLTextureCubeFaceStorage saved;
        uint32_t old_2d_binding;

        (void)texture_cube_face_index(target, &face_index);
        if (level == 0 && width != height) {
            ringl_context_record_error(context, RINGL_INVALID_VALUE);
            return;
        }
        cube_texture = bound_texture_cube(context);
        if (cube_texture == NULL ||
            (level == 0 && !texture_cube_base_compatible(
                cube_texture, face_index, (uint32_t)width, (uint32_t)height,
                output_format,
                srgb != RINGL_FALSE ? RINGL_FLOAT : RINGL_UNSIGNED_BYTE,
                internal_format, srgb)) ||
            !texture_cube_prepare_operation(context, face_index,
                                            &cube_texture, &cube_face,
                                            &saved, &old_2d_binding)) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return;
        }
        ringl_compressed_tex_image_2d_from_bytes(
            RINGL_TEXTURE_2D, level, internal_format, width, height, border,
            data, data_size);
        texture_cube_finish_operation(context, cube_texture, cube_face,
                                      &saved, old_2d_binding);
        return;
    }
    decode_result = internal_format == RINGL_ETC1_RGB8_OES
                      ? etc1_decode_image(context, (uint32_t)width,
                                          (uint32_t)height,
                                          data, data_size, &decoded)
                      : dxt_decode_image(context, (uint32_t)width,
                                         (uint32_t)height,
                                         internal_format, data, data_size,
                                         &decoded);
    if (decode_result != 0) {
        ringl_context_record_error(context, decode_result == -2
                                               ? RINGL_OUT_OF_MEMORY
                                               : RINGL_INVALID_VALUE);
        return;
    }
    if (srgb != RINGL_FALSE) {
        uint8_t* linear = NULL;

        decode_result = dxt_srgb_decode_image(context, (uint32_t)width,
                                               (uint32_t)height,
                                               output_format, decoded, &linear);
        ringl_context_free_temporary(
            context, decoded,
            (uint64_t)(uint32_t)width * (uint32_t)height *
                (output_format == RINGL_RGB ? 3u : 4u));
        decoded = NULL;
        if (decode_result != 0) {
            ringl_context_record_error(context, decode_result == -2
                                                   ? RINGL_OUT_OF_MEMORY
                                                   : RINGL_INVALID_VALUE);
            return;
        }
        decoded = linear;
        output_type = RINGL_FLOAT;
    }
    context->pending_compressed_format = internal_format;
    ringl_tex_image_2d_from_bytes(target, level, output_format, width, height,
                                  border, output_format, output_type,
                                  decoded, (uint64_t)(uint32_t)width *
                                               (uint32_t)height *
                                               (output_format == RINGL_RGB ? 3u : 4u) *
                                               (output_type == RINGL_FLOAT
                                                    ? sizeof(float) : 1u));
    context->pending_compressed_format = 0u;
    ringl_context_free_temporary(
        context, decoded,
        (uint64_t)(uint32_t)width * (uint32_t)height *
            (output_format == RINGL_RGB ? 3u : 4u) *
            (output_type == RINGL_FLOAT ? sizeof(float) : 1u));
}

void ringl_compressed_tex_sub_image_2d_from_bytes(
    uint32_t target, int32_t level, int32_t xoffset, int32_t yoffset,
    int32_t width, int32_t height, uint32_t format, const void* data,
    uint64_t data_size)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLTextureObject* texture;
    RinGLTextureMipStorage* mip_storage;
    uint32_t level_width;
    uint32_t level_height;
    uint32_t block_bytes;
    uint32_t output_format;
    uint32_t output_type = RINGL_UNSIGNED_BYTE;
    uint32_t srgb = RINGL_FALSE;
    uint8_t* decoded = NULL;
    int decode_result;

    if (context == NULL)
        return;
    if (!texture_image_target_valid(target)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (format == RINGL_ETC1_RGB8_OES)
        output_format = RINGL_RGB;
    else if (dxt_format_info(format, &block_bytes, &output_format, &srgb) != 0) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (level < 0 || (uint32_t)level >= RINGL_MAX_TEXTURE_MIP_LEVELS ||
        xoffset < 0 || yoffset < 0 || width < 0 || height < 0) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (target != RINGL_TEXTURE_2D) {
        uint32_t face_index;
        RinGLTextureObject* cube_texture;
        RinGLTextureCubeFaceStorage* cube_face;
        RinGLTextureCubeFaceStorage saved;
        uint32_t old_2d_binding;

        (void)texture_cube_face_index(target, &face_index);
        cube_texture = bound_texture_cube(context);
        if (cube_texture == NULL ||
            !texture_cube_prepare_operation(context, face_index,
                                            &cube_texture, &cube_face,
                                            &saved, &old_2d_binding)) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return;
        }
        ringl_compressed_tex_sub_image_2d_from_bytes(
            RINGL_TEXTURE_2D, level, xoffset, yoffset, width, height, format,
            data, data_size);
        texture_cube_finish_operation(context, cube_texture, cube_face,
                                      &saved, old_2d_binding);
        return;
    }
    texture = bound_texture_2d(context);
    if (texture == NULL || !texture_level0_storage_defined(texture) ||
        texture->format != output_format ||
        texture->color_component_type != (srgb != RINGL_FALSE
                                              ? RINGL_FLOAT
                                              : RINGL_UNSIGNED_BYTE) ||
        texture->compressed_format != format) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if (level == 0) {
        level_width = texture->width;
        level_height = texture->height;
    } else {
        mip_storage = texture_mip_storage(texture, (uint32_t)level);
        if (!texture_level_storage_defined(texture, (uint32_t)level) ||
            mip_storage == NULL) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return;
        }
        level_width = mip_storage->width;
        level_height = mip_storage->height;
    }
    if ((uint32_t)xoffset > level_width || (uint32_t)yoffset > level_height ||
        (uint32_t)width > level_width - (uint32_t)xoffset ||
        (uint32_t)height > level_height - (uint32_t)yoffset) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if ((((uint32_t)xoffset | (uint32_t)yoffset) & 3u) != 0u ||
        (((uint32_t)width & 3u) != 0u &&
         (uint32_t)width != level_width - (uint32_t)xoffset) ||
        (((uint32_t)height & 3u) != 0u &&
         (uint32_t)height != level_height - (uint32_t)yoffset)) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    decode_result = format == RINGL_ETC1_RGB8_OES
                      ? etc1_decode_image(context, (uint32_t)width,
                                          (uint32_t)height,
                                          data, data_size, &decoded)
                      : dxt_decode_image(context, (uint32_t)width,
                                         (uint32_t)height,
                                         format, data, data_size, &decoded);
    if (decode_result != 0) {
        ringl_context_record_error(context, decode_result == -2
                                               ? RINGL_OUT_OF_MEMORY
                                               : RINGL_INVALID_VALUE);
        return;
    }
    if (srgb != RINGL_FALSE) {
        uint8_t* linear = NULL;

        decode_result = dxt_srgb_decode_image(context, (uint32_t)width,
                                               (uint32_t)height,
                                               output_format, decoded, &linear);
        ringl_context_free_temporary(
            context, decoded,
            (uint64_t)(uint32_t)width * (uint32_t)height *
                (output_format == RINGL_RGB ? 3u : 4u));
        decoded = NULL;
        if (decode_result != 0) {
            ringl_context_record_error(context, decode_result == -2
                                                   ? RINGL_OUT_OF_MEMORY
                                                   : RINGL_INVALID_VALUE);
            return;
        }
        decoded = linear;
        output_type = RINGL_FLOAT;
    }
    context->pending_compressed_format = format;
    ringl_tex_sub_image_2d_from_bytes(target, level, xoffset, yoffset, width,
                                      height, output_format,
                                      output_type, decoded,
                                      (uint64_t)(uint32_t)width *
                                          (uint32_t)height *
                                          (output_format == RINGL_RGB ? 3u : 4u) *
                                          (output_type == RINGL_FLOAT
                                               ? sizeof(float) : 1u));
    context->pending_compressed_format = 0u;
    ringl_context_free_temporary(
        context, decoded,
        (uint64_t)(uint32_t)width * (uint32_t)height *
            (output_format == RINGL_RGB ? 3u : 4u) *
            (output_type == RINGL_FLOAT ? sizeof(float) : 1u));
}

void ringl_copy_tex_image_2d(uint32_t target, int32_t level,
                             uint32_t internal_format, int32_t x, int32_t y,
                             int32_t width, int32_t height, int32_t border)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLTextureObject* texture;
    RinGLTextureMipStorage* mip_storage = NULL;
    RinGLColorTarget source;
    uint64_t snapshot_size;
    uint64_t replacement_size;
    uint8_t* snapshot;
    uint8_t* replacement;
    uint32_t storage_format;
    uint32_t srgb_encoding;
    uint32_t snapshot_component_type;
    uint32_t storage_component_type;

    if (context == NULL)
        return;
    srgb_encoding = texture_srgb_internal_format(internal_format)
        ? RINGL_TRUE : RINGL_FALSE;
    storage_format = srgb_encoding != RINGL_FALSE
        ? texture_srgb_storage_format(internal_format) : internal_format;
    if (!texture_image_target_valid(target) || !texture_color_format(storage_format)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (level < 0 || (uint32_t)level >= RINGL_MAX_TEXTURE_MIP_LEVELS ||
        border != 0 || x < 0 || y < 0 || width <= 0 || height <= 0 ||
        (uint32_t)width > RINGL_MAX_TEXTURE_SIZE ||
        (uint32_t)height > RINGL_MAX_TEXTURE_SIZE) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    texture = target == RINGL_TEXTURE_2D
        ? bound_texture_for_target(context, target) : bound_texture_cube(context);
    if (texture == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if (target != RINGL_TEXTURE_2D && level == 0 && width != height) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (context->framebuffer_binding != 0u &&
        ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) !=
            RINGL_FRAMEBUFFER_COMPLETE) {
        ringl_context_record_error(context,
                                   ringl_framebuffer_operation_error(context));
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
    snapshot_component_type = source.srgb_encoding == 0u &&
            (source.format == RINGL_RIN_GPU_FORMAT_RGBA32_FLOAT ||
             source.format == RINGL_RIN_GPU_FORMAT_RGBA16_FLOAT)
        ? RINGL_FLOAT : RINGL_UNSIGNED_BYTE;
    if (target != RINGL_TEXTURE_2D) {
        uint32_t face_index;
        RinGLTextureObject* cube_texture = texture;
        RinGLTextureCubeFaceStorage* cube_face;
        RinGLTextureCubeFaceStorage saved;
        uint32_t old_2d_binding;
        uint32_t copy_component_type = srgb_encoding != RINGL_FALSE
            ? RINGL_FLOAT
            : texture_packed_color_format(storage_format)
                ? texture_packed_component_type(storage_format)
                : snapshot_component_type;

        (void)texture_cube_face_index(target, &face_index);
        if (!texture_cube_base_compatible(
                cube_texture, face_index, (uint32_t)width, (uint32_t)height,
                storage_format, copy_component_type, 0u, srgb_encoding) ||
            !texture_cube_prepare_operation(context, face_index, &cube_texture,
                                            &cube_face, &saved,
                                            &old_2d_binding)) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return;
        }
        ringl_copy_tex_image_2d(
            RINGL_TEXTURE_2D, level, internal_format, x, y, width, height,
            border);
        texture_cube_finish_operation(context, cube_texture, cube_face,
                                      &saved, old_2d_binding);
        return;
    }
    storage_component_type = srgb_encoding != RINGL_FALSE
        ? RINGL_FLOAT
        : texture_packed_color_format(storage_format)
            ? texture_packed_component_type(storage_format)
            : snapshot_component_type;
    if (level != 0) {
        uint32_t mip_level = (uint32_t)level;

        /* A packed base image retains its packed upload type so later
         * sub-image validation can preserve its native representation.  A
         * same-format copy-defined mip is nevertheless valid: it snapshots
         * RGBA and packs back into that exact storage rather than requiring a
         * fabricated UNSIGNED_BYTE base type. */
        if (!texture_level0_storage_defined(texture) ||
            texture->format != storage_format ||
            texture->srgb_encoding != srgb_encoding ||
            (srgb_encoding != RINGL_FALSE &&
             texture->color_component_type != RINGL_FLOAT) ||
            (srgb_encoding == RINGL_FALSE &&
             ((!texture_packed_color_format(texture->format) &&
               texture->color_component_type != RINGL_UNSIGNED_BYTE &&
               texture->color_component_type != RINGL_FLOAT &&
               texture->color_component_type != RINGL_HALF_FLOAT_OES) ||
              (texture_packed_color_format(texture->format) &&
               !texture_packed_component_type_valid(
                   texture->format, texture->color_component_type)))) ||
            texture->compressed_format != 0u) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return;
        }
        /* A nonzero definition keeps its existing base representation. An
         * RGBA8 source may therefore define a Float/half mip through the
         * same normalized canonical conversion as copyTexSubImage2D. */
        storage_component_type = texture->color_component_type;
        if (mip_level >= texture_mip_level_count(texture->width,
                                                  texture->height) ||
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
    snapshot_size = (uint64_t)(uint32_t)width * (uint64_t)(uint32_t)height *
                    4u * (snapshot_component_type == RINGL_FLOAT
                              ? (uint64_t)sizeof(float) : 1u);
    replacement_size = (uint64_t)(uint32_t)width * (uint64_t)(uint32_t)height *
                       texture_storage_texel_bytes(storage_format,
                                                    storage_component_type);
    if (snapshot_size > SIZE_MAX || replacement_size > SIZE_MAX) {
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return;
    }
    snapshot = ringl_context_alloc_temporary(context, snapshot_size);
    if (snapshot == NULL) {
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return;
    }
    if ((snapshot_component_type == RINGL_FLOAT
             ? ringl_read_color_target_rgba_float(context, x, y, width,
                                                   height, snapshot)
             : ringl_read_color_target_rgba(context, x, y, width, height,
                                             snapshot)) != 0 ||
        (snapshot_component_type == RINGL_FLOAT &&
         !texture_canonical_rgba_float_snapshot_valid(
             snapshot, (uint32_t)width * (uint32_t)height))) {
        ringl_context_free_temporary(context, snapshot, snapshot_size);
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if (!ringl_context_reserve_shadow_bytes(context, replacement_size)) {
        ringl_context_free_temporary(context, snapshot, snapshot_size);
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return;
    }
    replacement = malloc((size_t)replacement_size);
    if (replacement == NULL) {
        ringl_context_release_shadow_bytes(context, replacement_size);
        ringl_context_free_temporary(context, snapshot, snapshot_size);
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return;
    }
    if (texture_copy_canonical_rgba_to_typed_copy_image_storage(
            replacement, snapshot, snapshot_component_type, storage_format,
            storage_component_type, srgb_encoding,
            (uint32_t)width * (uint32_t)height) != 0) {
        ringl_context_release_shadow_bytes(context, replacement_size);
        free(replacement);
        ringl_context_free_temporary(context, snapshot, snapshot_size);
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    ringl_context_free_temporary(context, snapshot, snapshot_size);

    texture_discard_image(context, texture);
    if (level == 0) {
        texture_drop_mip_storage_from(context, texture, 1u);
        texture_release_storage(context, &texture->shadow_bytes,
                                &texture->shadow_size);
        texture->shadow_bytes = replacement;
        texture->shadow_size = replacement_size;
        texture->width = (uint32_t)width;
        texture->height = (uint32_t)height;
        texture->format = storage_format;
        texture->color_component_type = storage_component_type;
        texture->compressed_format = 0u;
        texture->srgb_encoding = srgb_encoding;
        texture->defined = RINGL_TRUE;
    } else {
        texture_release_storage(context, &mip_storage->shadow_bytes,
                                &mip_storage->shadow_size);
        mip_storage->shadow_bytes = replacement;
        mip_storage->shadow_size = replacement_size;
        mip_storage->width = (uint32_t)width;
        mip_storage->height = (uint32_t)height;
        mip_storage->defined = RINGL_TRUE;
        mip_storage->generated = RINGL_FALSE;
    }
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
        texture_drop_mip_storage_from(context, &context->textures[index], 1u);
        texture_release_storage(context, &context->textures[index].shadow_bytes,
                                &context->textures[index].shadow_size);
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

int ringl_texture_realize_color_target_layer(
    RinGLContext* context, uint32_t texture, uint32_t mip_level,
    uint32_t array_layer, uint64_t* image_out, uint32_t** image_state_out,
    uint32_t* width_out, uint32_t* height_out)
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
    if (object->target == RINGL_TEXTURE_CUBE_MAP) {
        RinGLTextureCubeFaceStorage* face;
        const RinGLTextureMipStorage* storage;

        if (array_layer >= RINGL_CUBE_FACE_COUNT)
            return -1;
        face = &object->cube_faces[array_layer];
        if (!texture_cube_level_storage_defined(face, mip_level) ||
            texture_realize_image(context, object) != 0 ||
            object->ringpu_image == 0u)
            return -1;
        *image_out = object->ringpu_image;
        *image_state_out = &object->ringpu_cube_image_state[mip_level]
            [array_layer];
        if (mip_level == 0u) {
            *width_out = face->width;
            *height_out = face->height;
        } else {
            storage = texture_cube_mip_storage_const(face, mip_level);
            if (storage == NULL)
                return -1;
            *width_out = storage->width;
            *height_out = storage->height;
        }
        return 0;
    }
    if (array_layer != 0u)
        return -1;
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

int ringl_texture_realize_color_target(RinGLContext* context, uint32_t texture,
                                       uint32_t mip_level,
                                       uint64_t* image_out,
                                       uint32_t** image_state_out,
                                       uint32_t* width_out,
                                       uint32_t* height_out)
{
    return ringl_texture_realize_color_target_layer(
        context, texture, mip_level, 0u, image_out, image_state_out,
        width_out, height_out);
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
