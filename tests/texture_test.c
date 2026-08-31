/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <float.h>
#include <stdint.h>

#include <ringl/ringl.h>
#include "../src/ringl_internal.h"

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
        int32_t pack_alignment = 0;

        ringl_get_integerv(RINGL_UNPACK_ALIGNMENT, &unpack_alignment);
        assert(unpack_alignment == 4);
        ringl_get_integerv(RINGL_PACK_ALIGNMENT, &pack_alignment);
        assert(pack_alignment == 4);
        ringl_pixel_storei(RINGL_UNPACK_ALIGNMENT, 1);
        ringl_get_integerv(RINGL_UNPACK_ALIGNMENT, &unpack_alignment);
        assert(unpack_alignment == 1);
        ringl_pixel_storei(RINGL_PACK_ALIGNMENT, 8);
        ringl_get_integerv(RINGL_PACK_ALIGNMENT, &pack_alignment);
        assert(pack_alignment == 8);
        ringl_pixel_storei(RINGL_UNPACK_ALIGNMENT, 3);
        assert(ringl_get_error() == RINGL_INVALID_VALUE);
        ringl_get_integerv(RINGL_UNPACK_ALIGNMENT, &unpack_alignment);
        assert(unpack_alignment == 1);
        ringl_pixel_storei(RINGL_PACK_ALIGNMENT, 3);
        assert(ringl_get_error() == RINGL_INVALID_VALUE);
        ringl_get_integerv(RINGL_PACK_ALIGNMENT, &pack_alignment);
        assert(pack_alignment == 8);
        ringl_pixel_storei(0x0cf6u, 4);
        assert(ringl_get_error() == RINGL_INVALID_ENUM);
        ringl_pixel_storei(RINGL_UNPACK_ALIGNMENT, 4);
        ringl_pixel_storei(RINGL_PACK_ALIGNMENT, 4);
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

    /* EXT_texture_filter_anisotropic is a context-local WebGL capability:
     * before its extension object is acquired, its tokens cannot leak into
     * native sampler state. */
    assert(ringl_get_tex_parameterf(
               RINGL_TEXTURE_2D, RINGL_TEXTURE_MAX_ANISOTROPY_EXT) == 0.0f);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);
    ringl_tex_parameterf(RINGL_TEXTURE_2D,
                         RINGL_TEXTURE_MAX_ANISOTROPY_EXT, 4.0f);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);
    assert(ringl_enable_webgl_texture_filter_anisotropic() == 0);
    assert(ringl_get_tex_parameterf(
               RINGL_TEXTURE_2D, RINGL_TEXTURE_MAX_ANISOTROPY_EXT) == 1.0f);
    ringl_tex_parameterf(RINGL_TEXTURE_2D,
                         RINGL_TEXTURE_MAX_ANISOTROPY_EXT, 4.5f);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_get_tex_parameterf(
               RINGL_TEXTURE_2D, RINGL_TEXTURE_MAX_ANISOTROPY_EXT) == 4.5f);
    ringl_tex_parameterf(RINGL_TEXTURE_2D,
                         RINGL_TEXTURE_MAX_ANISOTROPY_EXT, 0.5f);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    assert(ringl_get_tex_parameterf(
               RINGL_TEXTURE_2D, RINGL_TEXTURE_MAX_ANISOTROPY_EXT) == 4.5f);
    {
        float infinite_degree = FLT_MAX;

        infinite_degree *= 2.0f;
        ringl_tex_parameterf(RINGL_TEXTURE_2D,
                             RINGL_TEXTURE_MAX_ANISOTROPY_EXT,
                             infinite_degree);
        assert(ringl_get_error() == RINGL_INVALID_VALUE);
        assert(ringl_get_tex_parameterf(
                   RINGL_TEXTURE_2D, RINGL_TEXTURE_MAX_ANISOTROPY_EXT) ==
               4.5f);
    }
    ringl_tex_parameteri(RINGL_TEXTURE_2D,
                         RINGL_TEXTURE_MAX_ANISOTROPY_EXT, 32);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_get_tex_parameterf(
               RINGL_TEXTURE_2D, RINGL_TEXTURE_MAX_ANISOTROPY_EXT) ==
           (float)RINGL_MAX_TEXTURE_ANISOTROPY);

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
    ringl_tex_image_2d_from_bytes(RINGL_TEXTURE_2D, 0, RINGL_RGBA, 2, 2, 0,
                                  RINGL_RGBA, RINGL_UNSIGNED_BYTE, pixels,
                                  sizeof(pixels) - 1u);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    ringl_tex_sub_image_2d(RINGL_TEXTURE_2D, 0, 1, 1, 1, 1,
                           RINGL_RGBA, RINGL_UNSIGNED_BYTE, patch);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_tex_sub_image_2d_from_bytes(RINGL_TEXTURE_2D, 0, 0, 0, 1, 1,
                                      RINGL_RGBA, RINGL_UNSIGNED_BYTE, patch,
                                      sizeof(patch) - 1u);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
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

    /* A context-wide shadow budget must reject a new texture before malloc or
     * backend publication, and releasing the reservation must make the
     * context usable again.  This exercises the same accounting boundary as
     * large real uploads without allocating hundreds of megabytes in a unit
     * test. */
    assert(ringl_context_reserve_shadow_bytes(
               context, RINGL_MAX_CPU_SHADOW_BYTES) != 0);
    ringl_gen_textures(1, &recycled);
    assert(recycled != 0u);
    ringl_bind_texture(RINGL_TEXTURE_2D, recycled);
    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_RGBA, 1, 1, 0,
                       RINGL_RGBA, RINGL_UNSIGNED_BYTE, pixels);
    assert(ringl_get_error() == RINGL_OUT_OF_MEMORY);
    ringl_context_release_shadow_bytes(context, RINGL_MAX_CPU_SHADOW_BYTES);
    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_RGBA, 1, 1, 0,
                       RINGL_RGBA, RINGL_UNSIGNED_BYTE, pixels);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_delete_textures(1, &recycled);
    assert(context->cpu_shadow_bytes == 0u);

    /* Compressed uploads use a temporary decoded image before publishing a
     * persistent texture shadow.  The temporary must obey the same budget and
     * must be released on both the admission failure and success paths. */
    {
        const uint8_t etc1_block[8] = {0u};

        ringl_gen_textures(1, &recycled);
        ringl_bind_texture(RINGL_TEXTURE_2D, recycled);
        assert(ringl_context_reserve_shadow_bytes(
                   context, RINGL_MAX_CPU_SHADOW_BYTES) != 0);
        ringl_compressed_tex_image_2d_from_bytes(
            RINGL_TEXTURE_2D, 0, RINGL_ETC1_RGB8_OES, 4, 4, 0,
            etc1_block, sizeof(etc1_block));
        assert(ringl_get_error() == RINGL_OUT_OF_MEMORY);
        assert(context->cpu_shadow_bytes == RINGL_MAX_CPU_SHADOW_BYTES);
        ringl_context_release_shadow_bytes(context, RINGL_MAX_CPU_SHADOW_BYTES);
        ringl_compressed_tex_image_2d_from_bytes(
            RINGL_TEXTURE_2D, 0, RINGL_ETC1_RGB8_OES, 4, 4, 0,
            etc1_block, sizeof(etc1_block));
        assert(ringl_get_error() == RINGL_NO_ERROR);
        assert(context->cpu_shadow_bytes > 0u);
        ringl_delete_textures(1, &recycled);
        assert(context->cpu_shadow_bytes == 0u);
    }

    ringl_context_destroy(context);
    return 0;
}
