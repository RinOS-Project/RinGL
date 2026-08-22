/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <string.h>

#include <ringl/ringl.h>

#include "ringl_internal.h"

int main(void)
{
    RinGLContext* context = NULL;
    RinGLContextDescV1 desc = {
        .struct_size = sizeof(desc),
        .api_version = RINGL_API_VERSION,
    };
    uint32_t textures[2] = {0u, 0u};
    uint32_t recycled = 0u;
    uint32_t slot;
    const uint8_t pixels[16] = {
        1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u,
        9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u,
    };
    const uint8_t patch[4] = {99u, 98u, 97u, 96u};

    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_make_current(context) == 0);
    assert(ringl_get_active_texture() == RINGL_TEXTURE0);
    assert(ringl_get_bound_texture(RINGL_TEXTURE_2D) == 0u);

    ringl_gen_textures(2, textures);
    assert(textures[0] != 0u && textures[1] != 0u);
    assert(textures[0] != textures[1]);
    assert(!ringl_is_texture(textures[0]));

    ringl_bind_texture(RINGL_TEXTURE_2D, textures[0]);
    assert(ringl_is_texture(textures[0]));
    assert(ringl_get_bound_texture(RINGL_TEXTURE_2D) == textures[0]);
    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_RGBA, 2, 2, 0,
                       RINGL_RGBA, RINGL_UNSIGNED_BYTE, pixels);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    slot = ringl_object_slot_index(textures[0]);
    assert(context->textures[slot].defined == RINGL_TRUE);
    assert(context->textures[slot].width == 2u);
    assert(context->textures[slot].height == 2u);
    assert(context->textures[slot].shadow_size == sizeof(pixels));
    assert(memcmp(context->textures[slot].shadow_bytes, pixels,
                  sizeof(pixels)) == 0);

    ringl_tex_sub_image_2d(RINGL_TEXTURE_2D, 0, 1, 0, 1, 1,
                           RINGL_RGBA, RINGL_UNSIGNED_BYTE, patch);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(memcmp(context->textures[slot].shadow_bytes + 4u, patch,
                  sizeof(patch)) == 0);

    ringl_tex_sub_image_2d(RINGL_TEXTURE_2D, 0, 2, 0, 1, 1,
                           RINGL_RGBA, RINGL_UNSIGNED_BYTE, patch);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    ringl_tex_image_2d(RINGL_TEXTURE_2D, 1, RINGL_RGBA, 2, 2, 0,
                       RINGL_RGBA, RINGL_UNSIGNED_BYTE, pixels);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);

    ringl_active_texture(RINGL_TEXTURE0 + 1u);
    assert(ringl_get_active_texture() == RINGL_TEXTURE0 + 1u);
    assert(ringl_get_bound_texture(RINGL_TEXTURE_2D) == 0u);
    ringl_bind_texture(RINGL_TEXTURE_2D, textures[1]);
    assert(ringl_is_texture(textures[1]));
    assert(ringl_get_bound_texture(RINGL_TEXTURE_2D) == textures[1]);
    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_RGBA, 1, 1, 0,
                       RINGL_RGBA, RINGL_UNSIGNED_BYTE, NULL);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    slot = ringl_object_slot_index(textures[1]);
    assert(context->textures[slot].shadow_size == 4u);
    assert(context->textures[slot].shadow_bytes[0] == 0u);
    assert(context->textures[slot].shadow_bytes[3] == 0u);

    ringl_active_texture(RINGL_TEXTURE0);
    assert(ringl_get_bound_texture(RINGL_TEXTURE_2D) == textures[0]);

    ringl_delete_textures(1, &textures[0]);
    assert(!ringl_is_texture(textures[0]));
    assert(ringl_get_bound_texture(RINGL_TEXTURE_2D) == 0u);
    ringl_active_texture(RINGL_TEXTURE0 + 1u);
    assert(ringl_get_bound_texture(RINGL_TEXTURE_2D) == textures[1]);

    ringl_active_texture(RINGL_TEXTURE0 + RINGL_MAX_TEXTURE_UNITS);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);
    assert(ringl_get_active_texture() == RINGL_TEXTURE0 + 1u);

    ringl_bind_texture(0x1234u, textures[1]);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);

    ringl_gen_textures(1, &recycled);
    assert(recycled != 0u);
    assert(recycled != textures[0]);
    ringl_bind_texture(RINGL_TEXTURE_2D, recycled);
    assert(ringl_is_texture(recycled));

    ringl_delete_textures(1, &textures[1]);
    assert(ringl_get_bound_texture(RINGL_TEXTURE_2D) == recycled);
    ringl_delete_textures(1, &recycled);
    assert(ringl_get_bound_texture(RINGL_TEXTURE_2D) == 0u);

    ringl_context_destroy(context);
    return 0;
}
