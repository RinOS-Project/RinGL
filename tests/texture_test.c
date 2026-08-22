/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>

#include <ringl/ringl.h>

int main(void)
{
    RinGLContext* context = NULL;
    RinGLContextDescV1 desc = {
        .struct_size = sizeof(desc),
        .api_version = RINGL_API_VERSION,
    };
    uint32_t textures[2] = {0u, 0u};
    uint32_t recycled = 0u;
    const uint8_t pixels[16] = {
        1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u,
        9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u,
    };
    const uint8_t patch[4] = {21u, 22u, 23u, 24u};

    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_make_current(context) == 0);
    assert(ringl_get_active_texture() == RINGL_TEXTURE0);
    assert(ringl_get_bound_texture(RINGL_TEXTURE_2D) == 0u);
    {
        int32_t unpack_alignment = 0;

        ringl_get_integerv(RINGL_UNPACK_ALIGNMENT, &unpack_alignment);
        assert(unpack_alignment == 4);
        ringl_pixel_storei(RINGL_UNPACK_ALIGNMENT, 1);
        ringl_get_integerv(RINGL_UNPACK_ALIGNMENT, &unpack_alignment);
        assert(unpack_alignment == 1);
        ringl_pixel_storei(RINGL_UNPACK_ALIGNMENT, 3);
        assert(ringl_get_error() == RINGL_INVALID_VALUE);
        ringl_get_integerv(RINGL_UNPACK_ALIGNMENT, &unpack_alignment);
        assert(unpack_alignment == 1);
        ringl_pixel_storei(0x0cf6u, 4);
        assert(ringl_get_error() == RINGL_INVALID_ENUM);
        ringl_pixel_storei(RINGL_UNPACK_ALIGNMENT, 4);
    }

    ringl_gen_textures(2, textures);
    assert(textures[0] != 0u && textures[1] != 0u);
    assert(textures[0] != textures[1]);
    assert(!ringl_is_texture(textures[0]));

    ringl_bind_texture(RINGL_TEXTURE_2D, textures[0]);
    assert(ringl_is_texture(textures[0]));
    assert(ringl_get_bound_texture(RINGL_TEXTURE_2D) == textures[0]);
    assert(ringl_get_tex_parameteri(RINGL_TEXTURE_2D,
                                    RINGL_TEXTURE_MIN_FILTER) ==
           (int32_t)RINGL_NEAREST_MIPMAP_LINEAR);
    assert(ringl_get_tex_parameteri(RINGL_TEXTURE_2D,
                                    RINGL_TEXTURE_MAG_FILTER) ==
           (int32_t)RINGL_LINEAR);
    assert(ringl_get_tex_parameteri(RINGL_TEXTURE_2D,
                                    RINGL_TEXTURE_WRAP_S) ==
           (int32_t)RINGL_REPEAT);
    assert(ringl_get_tex_parameteri(RINGL_TEXTURE_2D,
                                    RINGL_TEXTURE_WRAP_T) ==
           (int32_t)RINGL_REPEAT);

    ringl_tex_parameteri(RINGL_TEXTURE_2D, RINGL_TEXTURE_MIN_FILTER,
                         RINGL_LINEAR);
    ringl_tex_parameteri(RINGL_TEXTURE_2D, RINGL_TEXTURE_MAG_FILTER,
                         RINGL_NEAREST);
    ringl_tex_parameteri(RINGL_TEXTURE_2D, RINGL_TEXTURE_WRAP_S,
                         RINGL_CLAMP_TO_EDGE);
    ringl_tex_parameteri(RINGL_TEXTURE_2D, RINGL_TEXTURE_WRAP_T,
                         RINGL_MIRRORED_REPEAT);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_get_tex_parameteri(RINGL_TEXTURE_2D,
                                    RINGL_TEXTURE_MIN_FILTER) ==
           (int32_t)RINGL_LINEAR);
    assert(ringl_get_tex_parameteri(RINGL_TEXTURE_2D,
                                    RINGL_TEXTURE_MAG_FILTER) ==
           (int32_t)RINGL_NEAREST);
    assert(ringl_get_tex_parameteri(RINGL_TEXTURE_2D,
                                    RINGL_TEXTURE_WRAP_S) ==
           (int32_t)RINGL_CLAMP_TO_EDGE);
    assert(ringl_get_tex_parameteri(RINGL_TEXTURE_2D,
                                    RINGL_TEXTURE_WRAP_T) ==
           (int32_t)RINGL_MIRRORED_REPEAT);

    ringl_tex_parameteri(RINGL_TEXTURE_2D, RINGL_TEXTURE_MAG_FILTER,
                         RINGL_LINEAR_MIPMAP_LINEAR);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);
    assert(ringl_get_tex_parameteri(RINGL_TEXTURE_2D,
                                    RINGL_TEXTURE_MAG_FILTER) ==
           (int32_t)RINGL_NEAREST);

    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_RGBA, 2, 2, 0,
                       RINGL_RGBA, RINGL_UNSIGNED_BYTE, pixels);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_tex_sub_image_2d(RINGL_TEXTURE_2D, 0, 1, 1, 1, 1,
                           RINGL_RGBA, RINGL_UNSIGNED_BYTE, patch);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_tex_sub_image_2d(RINGL_TEXTURE_2D, 0, 2, 1, 1, 1,
                           RINGL_RGBA, RINGL_UNSIGNED_BYTE, patch);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);

    ringl_active_texture(RINGL_TEXTURE0 + 1u);
    assert(ringl_get_active_texture() == RINGL_TEXTURE0 + 1u);
    assert(ringl_get_bound_texture(RINGL_TEXTURE_2D) == 0u);
    ringl_bind_texture(RINGL_TEXTURE_2D, textures[1]);
    assert(ringl_is_texture(textures[1]));
    assert(ringl_get_bound_texture(RINGL_TEXTURE_2D) == textures[1]);
    assert(ringl_get_tex_parameteri(RINGL_TEXTURE_2D,
                                    RINGL_TEXTURE_MIN_FILTER) ==
           (int32_t)RINGL_NEAREST_MIPMAP_LINEAR);

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
