/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <ringl/ringl.h>

#include "../src/ringl_internal.h"

typedef struct FakeBackend {
    uint64_t next_handle;
    uint32_t image_creates;
    uint32_t image_uploads;
    uint32_t sampler_creates;
    uint32_t destroys;
    uint32_t last_format;
    uint64_t last_upload_size;
    uint8_t last_upload[64];
    uint32_t mip_image_creates;
    uint32_t mip_uploads;
    uint32_t mip_level_count;
    uint32_t mip_upload_level[3];
    uint32_t mip_upload_width[3];
    uint32_t mip_upload_height[3];
    uint64_t mip_upload_size[3];
    uint8_t mip_upload[3][64];
    uint32_t last_sampler_min_filter;
    uint32_t last_sampler_mag_filter;
    uint32_t last_sampler_mip_filter;
    uint32_t last_sampler_max_anisotropy;
} FakeBackend;

static int fake_create_buffer(void* session, uint64_t size_bytes,
                              uint64_t* buffer_out)
{
    FakeBackend* backend = session;
    assert(size_bytes > 0u);
    *buffer_out = ++backend->next_handle;
    return 0;
}

static int fake_upload_buffer(void* session, uint64_t buffer, uint64_t offset,
                              const void* data, uint64_t size_bytes)
{
    (void)session;
    assert(buffer != 0u && offset == 0u && data != NULL && size_bytes > 0u);
    return 0;
}

static int fake_destroy(void* session, uint64_t object)
{
    FakeBackend* backend = session;
    assert(object != 0u);
    backend->destroys++;
    return 0;
}

static int fake_create_image(void* session,
                             const RinGLRinGpuSampledImage2DV1* desc,
                             uint64_t* image_out)
{
    FakeBackend* backend = session;
    assert(desc != NULL && image_out != NULL);
    assert(desc->width == 2u && desc->height == 2u);
    assert(desc->format == RINGL_RIN_GPU_FORMAT_RGBA8_UNORM ||
           desc->format == RINGL_RIN_GPU_FORMAT_RGBA16_FLOAT ||
           desc->format == RINGL_RIN_GPU_FORMAT_RGBA32_FLOAT ||
           desc->format == RINGL_RIN_GPU_FORMAT_RGB565_UNORM ||
           desc->format == RINGL_RIN_GPU_FORMAT_RGBA4_UNORM ||
           desc->format == RINGL_RIN_GPU_FORMAT_RGB5_A1_UNORM);
    assert(desc->reserved0 == 0u);
    backend->last_format = desc->format;
    backend->image_creates++;
    *image_out = ++backend->next_handle;
    return 0;
}

static int fake_upload_image(void* session, uint64_t image,
                             const RinGLRinGpuImageUpload2DV1* upload,
                             const void* data, uint64_t size_bytes)
{
    FakeBackend* backend = session;
    assert(image != 0u && upload != NULL && data != NULL);
    assert(upload->x == 0u && upload->y == 0u);
    assert(upload->width == 2u && upload->height == 2u);
    uint32_t texel_bytes = backend->last_format ==
            RINGL_RIN_GPU_FORMAT_RGBA32_FLOAT
        ? 16u
        : backend->last_format == RINGL_RIN_GPU_FORMAT_RGBA16_FLOAT
            ? 8u
        : backend->last_format == RINGL_RIN_GPU_FORMAT_RGBA8_UNORM
            ? 4u : 2u;

    assert(upload->source_row_pitch_bytes == 2u * texel_bytes);
    assert(size_bytes == 4u * texel_bytes);
    memcpy(backend->last_upload, data, (size_t)size_bytes);
    backend->last_upload_size = size_bytes;
    backend->image_uploads++;
    return 0;
}

static int fake_create_mip_image(void* session,
                                 const RinGLRinGpuImage2DMipV2* desc,
                                 uint64_t* image_out)
{
    FakeBackend* backend = session;

    assert(desc != NULL && image_out != NULL);
    assert((desc->width == 3u && desc->height == 2u &&
            desc->format == RINGL_RIN_GPU_FORMAT_RGBA8_UNORM) ||
           (desc->width == 2u && desc->height == 2u &&
            desc->format == RINGL_RIN_GPU_FORMAT_RGBA32_FLOAT) ||
           (desc->width == 4u && desc->height == 4u &&
            (desc->format == RINGL_RIN_GPU_FORMAT_RGB565_UNORM ||
             desc->format == RINGL_RIN_GPU_FORMAT_RGBA4_UNORM ||
             desc->format == RINGL_RIN_GPU_FORMAT_RGB5_A1_UNORM)));
    assert(desc->usage == (RINGL_RIN_GPU_IMAGE_USAGE_COPY_DESTINATION |
                           RINGL_RIN_GPU_IMAGE_USAGE_SAMPLED));
    assert(desc->mip_levels ==
           ((desc->format == RINGL_RIN_GPU_FORMAT_RGBA8_UNORM ||
             desc->format == RINGL_RIN_GPU_FORMAT_RGBA32_FLOAT) ? 2u : 3u));
    assert(desc->reserved0 == 0u);
    backend->mip_image_creates++;
    backend->mip_level_count = desc->mip_levels;
    backend->last_format = desc->format;
    *image_out = ++backend->next_handle;
    return 0;
}

static int fake_upload_mip_image(void* session, uint64_t image,
                                 const RinGLRinGpuImageUpload2DMipV2* upload,
                                 const void* data, uint64_t size_bytes)
{
    FakeBackend* backend = session;
    uint32_t index = backend->mip_uploads;

    assert(image != 0u && upload != NULL && data != NULL);
    assert(index < 3u && upload->reserved0 == 0u);
    assert(upload->mip_level == index);
    assert(upload->x == 0u && upload->y == 0u);
    {
        uint32_t texel_bytes = backend->last_format ==
                RINGL_RIN_GPU_FORMAT_RGBA8_UNORM
            ? 4u : backend->last_format == RINGL_RIN_GPU_FORMAT_RGBA32_FLOAT
                ? 16u : 2u;

        assert(upload->source_row_pitch_bytes ==
               (uint64_t)upload->width * texel_bytes);
        assert(size_bytes == (uint64_t)upload->width * upload->height *
               texel_bytes);
    }
    backend->mip_upload_level[index] = upload->mip_level;
    backend->mip_upload_width[index] = upload->width;
    backend->mip_upload_height[index] = upload->height;
    backend->mip_upload_size[index] = size_bytes;
    memcpy(backend->mip_upload[index], data, (size_t)size_bytes);
    backend->mip_uploads++;
    return 0;
}

static int fake_create_sampler(void* session,
                               const RinGLRinGpuSamplerV1* desc,
                               uint64_t* sampler_out)
{
    FakeBackend* backend = session;
    assert(desc != NULL && sampler_out != NULL);
    assert(desc->min_filter == RINGL_RIN_GPU_SAMPLER_LINEAR ||
           desc->min_filter == RINGL_RIN_GPU_SAMPLER_NEAREST);
    assert(desc->mag_filter == RINGL_RIN_GPU_SAMPLER_LINEAR ||
           desc->mag_filter == RINGL_RIN_GPU_SAMPLER_NEAREST);
    assert(desc->mip_filter == RINGL_RIN_GPU_SAMPLER_MIP_NONE ||
           desc->mip_filter == RINGL_RIN_GPU_SAMPLER_NEAREST ||
           desc->mip_filter == RINGL_RIN_GPU_SAMPLER_LINEAR);
    assert(desc->address_u == RINGL_RIN_GPU_ADDRESS_REPEAT ||
           desc->address_u == RINGL_RIN_GPU_ADDRESS_CLAMP);
    assert(desc->address_v == RINGL_RIN_GPU_ADDRESS_REPEAT);
    assert(desc->reserved0 == 0u);
    assert(desc->max_anisotropy >= 1u &&
           desc->max_anisotropy <= RINGL_MAX_TEXTURE_ANISOTROPY);
    backend->last_sampler_min_filter = desc->min_filter;
    backend->last_sampler_mag_filter = desc->mag_filter;
    backend->last_sampler_mip_filter = desc->mip_filter;
    backend->last_sampler_max_anisotropy = desc->max_anisotropy;
    backend->sampler_creates++;
    *sampler_out = ++backend->next_handle;
    return 0;
}

int main(void)
{
    FakeBackend backend = {0};
    RinGLRinGpuOpsV1 ops = {
        .struct_size = sizeof(ops),
        .api_version = RINGL_API_VERSION,
        .create_buffer = fake_create_buffer,
        .upload_buffer = fake_upload_buffer,
        .destroy_object = fake_destroy,
        .create_sampled_image_2d = fake_create_image,
        .upload_image_2d = fake_upload_image,
        .create_image_2d_mip_v2 = fake_create_mip_image,
        .upload_image_2d_mip_v2 = fake_upload_mip_image,
        .create_sampler = fake_create_sampler,
    };
    RinGLRinGpuBindingV1 binding = {
        .struct_size = sizeof(binding),
        .api_version = RINGL_API_VERSION,
        .session = &backend,
        .ops = &ops,
    };
    RinGLContextDescV1 desc = {
        .struct_size = sizeof(desc),
        .api_version = RINGL_API_VERSION,
        .ringpu = &binding,
    };
    RinGLContext* context = NULL;
    uint32_t texture;
    uint32_t incomplete;
    uint32_t packed_texture;
    uint32_t packed_mip_texture;
    uint32_t mip_texture;
    uint32_t float_texture;
    uint32_t float_mip_texture;
    uint32_t float_framebuffer;
    uint32_t half_float_texture;
    uint32_t half_float_framebuffer;
    uint32_t webgl_depth_texture;
    uint32_t webgl_depth_stencil_texture;
    uint32_t webgl_depth_framebuffer;
    uint32_t color_attachment_is_float;
    uint32_t color_attachment_component_type;
    uint64_t image;
    uint64_t sampler;
    const uint8_t pixels[16] = {
        1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u,
        9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u,
    };
    const uint8_t patch[4] = {21u, 22u, 23u, 24u};
    const uint8_t rgb_pixels[16] = {
        1u, 2u, 3u, 4u, 5u, 6u, 0xffu, 0xffu,
        7u, 8u, 9u, 10u, 11u, 12u, 0xffu, 0xffu,
    };
    const uint8_t rgb_expected[16] = {
        1u, 2u, 3u, 0xffu, 4u, 5u, 6u, 0xffu,
        7u, 8u, 9u, 0xffu, 10u, 11u, 12u, 0xffu,
    };
    const uint8_t rgb_patch[3] = {21u, 22u, 23u};
    const uint8_t tightly_packed_rgb_pixels[12] = {
        31u, 32u, 33u, 34u, 35u, 36u,
        37u, 38u, 39u, 40u, 41u, 42u,
    };
    const uint8_t tightly_packed_rgb_expected[16] = {
        31u, 32u, 33u, 0xffu, 34u, 35u, 36u, 0xffu,
        37u, 38u, 39u, 0xffu, 40u, 41u, 42u, 0xffu,
    };
    const uint8_t aligned_depth_pixels[12] = {
        0u, 0u, 0x80u, 0x3fu, 0xa5u, 0xa5u, 0xa5u, 0xa5u,
        0u, 0u, 0u, 0x3fu,
    };
    const uint8_t luminance_alpha_pixels[8] = {
        1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u,
    };
    const uint8_t luminance_alpha_expected[16] = {
        1u, 1u, 1u, 2u, 3u, 3u, 3u, 4u,
        5u, 5u, 5u, 6u, 7u, 7u, 7u, 8u,
    };
    const uint8_t alpha_pixels[8] = {
        9u, 10u, 0xffu, 0xffu, 11u, 12u, 0xffu, 0xffu,
    };
    const uint8_t alpha_expected[16] = {
        0u, 0u, 0u, 9u, 0u, 0u, 0u, 10u,
        0u, 0u, 0u, 11u, 0u, 0u, 0u, 12u,
    };
    const uint8_t luminance_expected[16] = {
        9u, 9u, 9u, 0xffu, 10u, 10u, 10u, 0xffu,
        11u, 11u, 11u, 0xffu, 12u, 12u, 12u, 0xffu,
    };
    const uint16_t rgb565_pixels[4] = {
        UINT16_C(0xf800), UINT16_C(0x07e0), UINT16_C(0x001f), UINT16_C(0xffff),
    };
    const uint16_t rgba4_pixels[4] = {
        UINT16_C(0xf00f), UINT16_C(0x0f08), UINT16_C(0x00f0), UINT16_C(0xffff),
    };
    const uint16_t rgb5_a1_pixels[4] = {
        UINT16_C(0xf801), UINT16_C(0x07c0), UINT16_C(0x003f), UINT16_C(0xffff),
    };
    const uint16_t packed_patch = UINT16_C(0x07e0);
    const uint8_t mip_pixels[24] = {
        0u, 10u, 20u, 30u, 40u, 50u, 60u, 70u,
        80u, 90u, 100u, 110u, 120u, 130u, 140u, 150u,
        160u, 170u, 180u, 190u, 200u, 210u, 220u, 230u,
    };
    const uint8_t expected_mip_level1[4] = {80u, 90u, 100u, 110u};
    const uint8_t manual_mip_level1[4] = {31u, 41u, 51u, 61u};
    const uint16_t packed_mip_pixels[16] = {
        UINT16_C(0xf800), UINT16_C(0xf800), UINT16_C(0x07e0), UINT16_C(0x07e0),
        UINT16_C(0xf800), UINT16_C(0xf800), UINT16_C(0x07e0), UINT16_C(0x07e0),
        UINT16_C(0x001f), UINT16_C(0x001f), UINT16_C(0xffff), UINT16_C(0xffff),
        UINT16_C(0x001f), UINT16_C(0x001f), UINT16_C(0xffff), UINT16_C(0xffff),
    };
    const uint16_t expected_packed_mip_level1[4] = {
        UINT16_C(0xf800), UINT16_C(0x07e0), UINT16_C(0x001f), UINT16_C(0xffff),
    };
    const uint16_t expected_packed_mip_level2 = UINT16_C(0x8410);
    const uint16_t manual_packed_mip_level1[4] = {
        UINT16_C(0x07ff), UINT16_C(0x07ff), UINT16_C(0x07ff), UINT16_C(0x07ff),
    };
    const uint16_t rgba4_mip_pixels[16] = {
        UINT16_C(0xf00f), UINT16_C(0x0f0f), UINT16_C(0xf00f), UINT16_C(0x0f0f),
        UINT16_C(0x00ff), UINT16_C(0xffff), UINT16_C(0x00ff), UINT16_C(0xffff),
        UINT16_C(0xf00f), UINT16_C(0x0f0f), UINT16_C(0xf00f), UINT16_C(0x0f0f),
        UINT16_C(0x00ff), UINT16_C(0xffff), UINT16_C(0x00ff), UINT16_C(0xffff),
    };
    const uint16_t expected_rgba4_mip_level1[4] = {
        UINT16_C(0x888f), UINT16_C(0x888f), UINT16_C(0x888f), UINT16_C(0x888f),
    };
    const uint16_t expected_rgba4_mip_level2 = UINT16_C(0x888f);
    const uint16_t rgb5_a1_mip_pixels[16] = {
        UINT16_C(0xf801), UINT16_C(0x07c1), UINT16_C(0xf801), UINT16_C(0x07c1),
        UINT16_C(0x003f), UINT16_C(0xffff), UINT16_C(0x003f), UINT16_C(0xffff),
        UINT16_C(0xf801), UINT16_C(0x07c1), UINT16_C(0xf801), UINT16_C(0x07c1),
        UINT16_C(0x003f), UINT16_C(0xffff), UINT16_C(0x003f), UINT16_C(0xffff),
    };
    const uint16_t expected_rgb5_a1_mip_level1[4] = {
        UINT16_C(0x8421), UINT16_C(0x8421), UINT16_C(0x8421), UINT16_C(0x8421),
    };
    const float float_rgba_pixels[16] = {
        -0.25f, 0.50f, 1.25f, 0.75f,
        0.00f, 1.00f, 0.25f, 1.00f,
        2.00f, -1.00f, 0.125f, 0.50f,
        0.75f, 0.625f, 0.375f, 0.25f,
    };
    const float float_luminance_alpha_pixels[8] = {
        -0.5f, 0.25f, 1.5f, 0.75f,
        0.125f, 1.0f, 0.875f, 0.5f,
    };
    const float float_patch[4] = { 0.50f, 0.25f, 0.75f, 1.25f };
    const uint16_t half_float_rgba_pixels[16] = {
        UINT16_C(0xbc00), UINT16_C(0x3800), UINT16_C(0x3d00), UINT16_C(0x3a00),
        UINT16_C(0x0000), UINT16_C(0x3c00), UINT16_C(0x3400), UINT16_C(0x3c00),
        UINT16_C(0x4000), UINT16_C(0xc000), UINT16_C(0x3000), UINT16_C(0x3800),
        UINT16_C(0x3a00), UINT16_C(0x3900), UINT16_C(0x3600), UINT16_C(0x3400),
    };
    const uint16_t half_float_patch[4] = {
        UINT16_C(0x3800), UINT16_C(0x3400), UINT16_C(0x3a00), UINT16_C(0x3d00),
    };
    const uint16_t expected_rgb5_a1_mip_level2 = UINT16_C(0x8421);
    const uint16_t webgl_depth_u16[4] = {
        0u, UINT16_C(0x8000), UINT16_MAX, UINT16_C(0x4000),
    };
    const uint32_t webgl_depth_u32[4] = {
        UINT32_C(0x80000000), UINT32_MAX, 0u, UINT32_C(0x40000000),
    };
    const uint32_t webgl_depth_stencil[4] = {
        UINT32_C(0x0000002a), UINT32_C(0x8000004b),
        UINT32_C(0xffffff7c), UINT32_C(0x4000009d),
    };

    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_make_current(context) == 0);

    ringl_gen_textures(1, &texture);
    ringl_bind_texture(RINGL_TEXTURE_2D, texture);
    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_RGBA, 2, 2, 0,
                       RINGL_RGBA, RINGL_UNSIGNED_BYTE, pixels);

    /* Level zero alone is incomplete with the GLES mipmapped default filter. */
    assert(ringl_texture_realize_unit(context, 0u, &image, &sampler) != 0);
    assert(backend.image_creates == 0u && backend.sampler_creates == 0u);

    ringl_tex_parameteri(RINGL_TEXTURE_2D, RINGL_TEXTURE_MIN_FILTER,
                         RINGL_LINEAR);
    assert(ringl_texture_realize_unit(context, 0u, &image, &sampler) == 0);
    assert(image != 0u && sampler != 0u);
    assert(backend.image_creates == 1u);
    assert(backend.image_uploads == 1u);
    assert(backend.sampler_creates == 1u);
    assert(memcmp(backend.last_upload, pixels, sizeof(pixels)) == 0);

    assert(ringl_texture_realize_unit(context, 0u, &image, &sampler) == 0);
    assert(backend.image_creates == 1u && backend.sampler_creates == 1u);

    ringl_tex_parameteri(RINGL_TEXTURE_2D, RINGL_TEXTURE_WRAP_S,
                         RINGL_CLAMP_TO_EDGE);
    assert(backend.destroys == 1u);
    assert(ringl_texture_realize_unit(context, 0u, &image, &sampler) == 0);
    assert(backend.image_creates == 1u && backend.sampler_creates == 2u);

    ringl_tex_sub_image_2d(RINGL_TEXTURE_2D, 0, 1, 0, 1, 1,
                           RINGL_RGBA, RINGL_UNSIGNED_BYTE, patch);
    assert(backend.destroys == 2u);
    assert(ringl_texture_realize_unit(context, 0u, &image, &sampler) == 0);
    assert(backend.image_creates == 2u && backend.image_uploads == 2u);
    assert(backend.last_upload[4] == 21u);
    assert(backend.last_upload[5] == 22u);
    assert(backend.last_upload[6] == 23u);
    assert(backend.last_upload[7] == 24u);

    ringl_gen_textures(1, &incomplete);
    ringl_active_texture(RINGL_TEXTURE0 + 1u);
    ringl_bind_texture(RINGL_TEXTURE_2D, incomplete);
    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_RGBA, 2, 2, 0,
                       RINGL_RGBA, RINGL_UNSIGNED_BYTE, pixels);
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) != 0);

    ringl_tex_parameteri(RINGL_TEXTURE_2D, RINGL_TEXTURE_MIN_FILTER,
                         RINGL_LINEAR);
    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_RGB, 2, 2, 0, RINGL_RGB,
                       RINGL_UNSIGNED_BYTE, rgb_pixels);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) == 0);
    assert(memcmp(backend.last_upload, rgb_expected, sizeof(rgb_expected)) == 0);
    ringl_pixel_storei(RINGL_UNPACK_ALIGNMENT, 1);
    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_RGB, 2, 2, 0, RINGL_RGB,
                       RINGL_UNSIGNED_BYTE, tightly_packed_rgb_pixels);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) == 0);
    assert(memcmp(backend.last_upload, tightly_packed_rgb_expected,
                  sizeof(tightly_packed_rgb_expected)) == 0);
    ringl_tex_sub_image_2d(RINGL_TEXTURE_2D, 0, 0, 0, 2, 2, RINGL_RGB,
                           RINGL_UNSIGNED_BYTE, tightly_packed_rgb_pixels);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) == 0);
    assert(memcmp(backend.last_upload, tightly_packed_rgb_expected,
                  sizeof(tightly_packed_rgb_expected)) == 0);
    ringl_pixel_storei(RINGL_UNPACK_ALIGNMENT, 4);
    ringl_tex_sub_image_2d(RINGL_TEXTURE_2D, 0, 1, 0, 1, 1, RINGL_RGB,
                           RINGL_UNSIGNED_BYTE, rgb_patch);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) == 0);
    assert(backend.last_upload[4] == 21u);
    assert(backend.last_upload[5] == 22u);
    assert(backend.last_upload[6] == 23u);
    assert(backend.last_upload[7] == UINT8_MAX);

    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_LUMINANCE_ALPHA, 2, 2, 0,
                       RINGL_LUMINANCE_ALPHA, RINGL_UNSIGNED_BYTE,
                       luminance_alpha_pixels);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) == 0);
    assert(memcmp(backend.last_upload, luminance_alpha_expected,
                  sizeof(luminance_alpha_expected)) == 0);

    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_ALPHA, 2, 2, 0,
                       RINGL_ALPHA, RINGL_UNSIGNED_BYTE, alpha_pixels);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) == 0);
    assert(memcmp(backend.last_upload, alpha_expected, sizeof(alpha_expected)) ==
           0);

    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_LUMINANCE, 2, 2, 0,
                       RINGL_LUMINANCE, RINGL_UNSIGNED_BYTE, alpha_pixels);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) == 0);
    assert(memcmp(backend.last_upload, luminance_expected,
                  sizeof(luminance_expected)) == 0);

    ringl_gen_textures(1, &packed_texture);
    ringl_bind_texture(RINGL_TEXTURE_2D, packed_texture);
    ringl_tex_parameteri(RINGL_TEXTURE_2D, RINGL_TEXTURE_MIN_FILTER,
                         RINGL_LINEAR);
    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_RGB, 2, 2, 0, RINGL_RGB,
                       RINGL_UNSIGNED_SHORT_5_6_5, rgb565_pixels);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) == 0);
    assert(backend.last_format == RINGL_RIN_GPU_FORMAT_RGB565_UNORM);
    assert(backend.last_upload_size == sizeof(rgb565_pixels));
    assert(memcmp(backend.last_upload, rgb565_pixels, sizeof(rgb565_pixels)) ==
           0);
    ringl_tex_sub_image_2d(RINGL_TEXTURE_2D, 0, 1, 0, 1, 1, RINGL_RGB,
                           RINGL_UNSIGNED_SHORT_5_6_5, &packed_patch);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) == 0);
    assert(memcmp(backend.last_upload + sizeof(uint16_t), &packed_patch,
                  sizeof(packed_patch)) == 0);

    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_RGBA, 2, 2, 0, RINGL_RGBA,
                       RINGL_UNSIGNED_SHORT_4_4_4_4, rgba4_pixels);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) == 0);
    assert(backend.last_format == RINGL_RIN_GPU_FORMAT_RGBA4_UNORM);
    assert(backend.last_upload_size == sizeof(rgba4_pixels));
    assert(memcmp(backend.last_upload, rgba4_pixels, sizeof(rgba4_pixels)) ==
           0);

    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_RGBA, 2, 2, 0, RINGL_RGBA,
                       RINGL_UNSIGNED_SHORT_5_5_5_1, rgb5_a1_pixels);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) == 0);
    assert(backend.last_format == RINGL_RIN_GPU_FORMAT_RGB5_A1_UNORM);
    assert(backend.last_upload_size == sizeof(rgb5_a1_pixels));
    assert(memcmp(backend.last_upload, rgb5_a1_pixels,
                  sizeof(rgb5_a1_pixels)) == 0);

    /* OES_texture_float storage is native RGBA32F, not a clamped RGBA8
     * emulation. The base extension permits only nearest filters, so the
     * GLES defaults remain incomplete until both filters are made nearest. */
    ringl_gen_textures(1, &float_texture);
    ringl_bind_texture(RINGL_TEXTURE_2D, float_texture);
    ringl_tex_image_2d_from_bytes(
        RINGL_TEXTURE_2D, 0, RINGL_RGBA, 2, 2, 0, RINGL_RGBA, RINGL_FLOAT,
        float_rgba_pixels, sizeof(float_rgba_pixels));
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) != 0);
    ringl_tex_parameteri(RINGL_TEXTURE_2D, RINGL_TEXTURE_MIN_FILTER,
                         RINGL_NEAREST);
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) != 0);
    ringl_tex_parameteri(RINGL_TEXTURE_2D, RINGL_TEXTURE_MAG_FILTER,
                         RINGL_NEAREST);
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) == 0);
    assert(backend.last_format == RINGL_RIN_GPU_FORMAT_RGBA32_FLOAT);
    assert(backend.last_upload_size == sizeof(float_rgba_pixels));
    assert(memcmp(backend.last_upload, float_rgba_pixels,
                  sizeof(float_rgba_pixels)) == 0);
    /* A native sampler must not bypass the WebGL extension gate. Once the
     * browser has acquired OES_texture_float_linear, the same Float texture
     * realizes the real RinGPU linear sampler without reuploading as UNORM. */
    ringl_tex_parameteri(RINGL_TEXTURE_2D, RINGL_TEXTURE_MIN_FILTER,
                         RINGL_LINEAR);
    ringl_tex_parameteri(RINGL_TEXTURE_2D, RINGL_TEXTURE_MAG_FILTER,
                         RINGL_LINEAR);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) != 0);
    assert(ringl_enable_webgl_float_texture_linear() == 0);
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) == 0);
    assert(backend.last_format == RINGL_RIN_GPU_FORMAT_RGBA32_FLOAT);
    ringl_tex_sub_image_2d_from_bytes(
        RINGL_TEXTURE_2D, 0, 1, 0, 1, 1, RINGL_RGBA, RINGL_FLOAT,
        float_patch, sizeof(float_patch) - 1u);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    assert(memcmp(&context->textures[ringl_object_slot_index(float_texture)]
                      .shadow_bytes[4u * sizeof(float)],
                  &float_rgba_pixels[4], 4u * sizeof(float)) == 0);
    ringl_tex_sub_image_2d(RINGL_TEXTURE_2D, 0, 1, 0, 1, 1, RINGL_RGBA,
                           RINGL_FLOAT, float_patch);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) == 0);
    assert(memcmp(backend.last_upload + 4u * sizeof(float), float_patch,
                  sizeof(float_patch)) == 0);
    /* Float textures are now genuine offscreen color attachments. The bridge
     * exercises the rendered/readback path; this unit assertion catches a
     * regression before the texture is rebound for later upload coverage. */
    assert(ringl_texture_require_color_target(context, float_texture) == 0);
    /* The browser must be able to distinguish a real Float attachment from
     * the normalized default target before enabling WebGL's FBO extension. */
    color_attachment_is_float = UINT32_MAX;
    assert(ringl_framebuffer_color_attachment_is_float(
               &color_attachment_is_float) == 0);
    assert(color_attachment_is_float == RINGL_FALSE);
    ringl_gen_framebuffers(1, &float_framebuffer);
    ringl_bind_framebuffer(RINGL_FRAMEBUFFER, float_framebuffer);
    ringl_framebuffer_texture_2d(RINGL_FRAMEBUFFER, RINGL_COLOR_ATTACHMENT0,
                                 RINGL_TEXTURE_2D, float_texture, 0);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    color_attachment_is_float = RINGL_FALSE;
    assert(ringl_framebuffer_color_attachment_is_float(
               &color_attachment_is_float) == 0);
    assert(color_attachment_is_float == RINGL_TRUE);
    ringl_bind_framebuffer(RINGL_FRAMEBUFFER, 0u);

    /* HALF_FLOAT_OES remains native binary16 RinGPU storage. Its linear
     * completeness gate is independent from Float32 texture filtering. */
    ringl_gen_textures(1, &half_float_texture);
    ringl_bind_texture(RINGL_TEXTURE_2D, half_float_texture);
    ringl_tex_image_2d_from_bytes(
        RINGL_TEXTURE_2D, 0, RINGL_RGBA, 2, 2, 0, RINGL_RGBA,
        RINGL_HALF_FLOAT_OES, half_float_rgba_pixels,
        sizeof(half_float_rgba_pixels));
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) != 0);
    ringl_tex_parameteri(RINGL_TEXTURE_2D, RINGL_TEXTURE_MIN_FILTER,
                         RINGL_NEAREST);
    ringl_tex_parameteri(RINGL_TEXTURE_2D, RINGL_TEXTURE_MAG_FILTER,
                         RINGL_NEAREST);
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) == 0);
    assert(backend.last_format == RINGL_RIN_GPU_FORMAT_RGBA16_FLOAT);
    assert(backend.last_upload_size == sizeof(half_float_rgba_pixels));
    assert(memcmp(backend.last_upload, half_float_rgba_pixels,
                  sizeof(half_float_rgba_pixels)) == 0);
    ringl_tex_parameteri(RINGL_TEXTURE_2D, RINGL_TEXTURE_MIN_FILTER,
                         RINGL_LINEAR);
    ringl_tex_parameteri(RINGL_TEXTURE_2D, RINGL_TEXTURE_MAG_FILTER,
                         RINGL_LINEAR);
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) != 0);
    assert(ringl_enable_webgl_half_float_texture_linear() == 0);
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) == 0);
    ringl_tex_sub_image_2d_from_bytes(
        RINGL_TEXTURE_2D, 0, 1, 0, 1, 1, RINGL_RGBA,
        RINGL_HALF_FLOAT_OES, half_float_patch,
        sizeof(half_float_patch) - 1u);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    assert(memcmp(&context->textures[ringl_object_slot_index(half_float_texture)]
                      .shadow_bytes[4u * sizeof(uint16_t)],
                  &half_float_rgba_pixels[4], 4u * sizeof(uint16_t)) == 0);
    ringl_tex_sub_image_2d(RINGL_TEXTURE_2D, 0, 1, 0, 1, 1, RINGL_RGBA,
                           RINGL_HALF_FLOAT_OES, half_float_patch);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) == 0);
    assert(memcmp(backend.last_upload + 4u * sizeof(uint16_t),
                  half_float_patch, sizeof(half_float_patch)) == 0);
    assert(ringl_texture_require_color_target(context, half_float_texture) ==
           0);
    ringl_gen_framebuffers(1, &half_float_framebuffer);
    ringl_bind_framebuffer(RINGL_FRAMEBUFFER, half_float_framebuffer);
    ringl_framebuffer_texture_2d(RINGL_FRAMEBUFFER, RINGL_COLOR_ATTACHMENT0,
                                 RINGL_TEXTURE_2D, half_float_texture, 0);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    color_attachment_component_type = RINGL_UNSIGNED_BYTE;
    assert(ringl_framebuffer_color_attachment_component_type(
               &color_attachment_component_type) == 0);
    assert(color_attachment_component_type == RINGL_HALF_FLOAT_OES);
    color_attachment_is_float = RINGL_TRUE;
    assert(ringl_framebuffer_color_attachment_is_float(
               &color_attachment_is_float) == 0);
    assert(color_attachment_is_float == RINGL_FALSE);
    ringl_bind_framebuffer(RINGL_FRAMEBUFFER, 0u);

    /* OES_texture_float_linear extends the base nearest-only profile with
     * every linear minification variant. Generated Float mips remain Float
     * through the V2 RinGPU image descriptor; the recorded sampler proves
     * that no mode was collapsed to a nearest-only substitute. */
    ringl_gen_textures(1, &float_mip_texture);
    ringl_bind_texture(RINGL_TEXTURE_2D, float_mip_texture);
    ringl_tex_parameteri(RINGL_TEXTURE_2D, RINGL_TEXTURE_MIN_FILTER,
                         RINGL_NEAREST_MIPMAP_LINEAR);
    ringl_tex_parameteri(RINGL_TEXTURE_2D, RINGL_TEXTURE_MAG_FILTER,
                         RINGL_LINEAR);
    ringl_tex_image_2d_from_bytes(
        RINGL_TEXTURE_2D, 0, RINGL_RGBA, 2, 2, 0, RINGL_RGBA, RINGL_FLOAT,
        float_rgba_pixels, sizeof(float_rgba_pixels));
    ringl_generate_mipmap(RINGL_TEXTURE_2D);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    backend.mip_image_creates = 0u;
    backend.mip_uploads = 0u;
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) == 0);
    assert(backend.last_format == RINGL_RIN_GPU_FORMAT_RGBA32_FLOAT);
    assert(backend.mip_image_creates == 1u && backend.mip_level_count == 2u &&
           backend.mip_uploads == 2u);
    assert(backend.last_sampler_min_filter == RINGL_RIN_GPU_SAMPLER_NEAREST);
    assert(backend.last_sampler_mag_filter == RINGL_RIN_GPU_SAMPLER_LINEAR);
    assert(backend.last_sampler_mip_filter == RINGL_RIN_GPU_SAMPLER_LINEAR);

    ringl_tex_parameteri(RINGL_TEXTURE_2D, RINGL_TEXTURE_MIN_FILTER,
                         RINGL_LINEAR_MIPMAP_NEAREST);
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) == 0);
    assert(backend.last_sampler_min_filter == RINGL_RIN_GPU_SAMPLER_LINEAR);
    assert(backend.last_sampler_mag_filter == RINGL_RIN_GPU_SAMPLER_LINEAR);
    assert(backend.last_sampler_mip_filter == RINGL_RIN_GPU_SAMPLER_NEAREST);

    ringl_tex_parameteri(RINGL_TEXTURE_2D, RINGL_TEXTURE_MIN_FILTER,
                         RINGL_LINEAR_MIPMAP_LINEAR);
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) == 0);
    assert(backend.last_sampler_min_filter == RINGL_RIN_GPU_SAMPLER_LINEAR);
    assert(backend.last_sampler_mag_filter == RINGL_RIN_GPU_SAMPLER_LINEAR);
    assert(backend.last_sampler_mip_filter == RINGL_RIN_GPU_SAMPLER_LINEAR);
    /* The WebGL-facing floating state is quantized only at the RinGPU
     * adapter boundary. The software backend receives four real taps rather
     * than silently retaining the default degree one sampler. */
    assert(ringl_enable_webgl_texture_filter_anisotropic() == 0);
    ringl_tex_parameterf(RINGL_TEXTURE_2D,
                         RINGL_TEXTURE_MAX_ANISOTROPY_EXT, 4.5f);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) == 0);
    assert(backend.last_sampler_max_anisotropy == 4u);
    // The existing later mip-chain cases use the fake backend's counters as
    // per-case evidence, so restore their initially-zero fixture state.
    backend.mip_image_creates = 0u;
    backend.mip_uploads = 0u;

    /* Float inputs apply the same ALPHA/LUMINANCE expansion as U8 input but
     * preserve components outside the normalized range. */
    ringl_bind_texture(RINGL_TEXTURE_2D, float_texture);
    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_LUMINANCE_ALPHA, 2, 2, 0,
                       RINGL_LUMINANCE_ALPHA, RINGL_FLOAT,
                       float_luminance_alpha_pixels);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) == 0);
    {
        const float expected_float_luminance_alpha[16] = {
            -0.5f, -0.5f, -0.5f, 0.25f,
            1.5f, 1.5f, 1.5f, 0.75f,
            0.125f, 0.125f, 0.125f, 1.0f,
            0.875f, 0.875f, 0.875f, 0.5f,
        };

        assert(backend.last_format == RINGL_RIN_GPU_FORMAT_RGBA32_FLOAT);
        assert(memcmp(backend.last_upload, expected_float_luminance_alpha,
                      sizeof(expected_float_luminance_alpha)) == 0);
    }

    ringl_gen_textures(1, &incomplete);
    ringl_bind_texture(RINGL_TEXTURE_2D, incomplete);
    ringl_pixel_storei(RINGL_UNPACK_ALIGNMENT, 8);
    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_DEPTH_COMPONENT32F, 1, 2,
                       0, RINGL_DEPTH_COMPONENT, RINGL_FLOAT,
                       aligned_depth_pixels);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    {
        RinGLTextureObject* depth_texture =
            &context->textures[ringl_object_slot_index(incomplete)];
        float depth0;
        float depth1;

        assert(depth_texture->shadow_size == 8u);
        memcpy(&depth0, depth_texture->shadow_bytes, sizeof(depth0));
        memcpy(&depth1, depth_texture->shadow_bytes + sizeof(depth0),
               sizeof(depth1));
        assert(depth0 == 1.0f && depth1 == 0.5f);
    }
    ringl_pixel_storei(RINGL_UNPACK_ALIGNMENT, 4);

    /* WEBGL_depth_texture uses WebGL's unsized DEPTH_COMPONENT token with
     * unsigned source data. RinGL stores it as D32, so this is also a
     * conversion boundary rather than an extension-name-only test. */
    ringl_gen_textures(1, &webgl_depth_texture);
    ringl_bind_texture(RINGL_TEXTURE_2D, webgl_depth_texture);
    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_DEPTH_COMPONENT, 2, 2, 0,
                       RINGL_DEPTH_COMPONENT, RINGL_UNSIGNED_SHORT,
                       webgl_depth_u16);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    {
        RinGLTextureObject* depth_texture =
            &context->textures[ringl_object_slot_index(webgl_depth_texture)];
        uint8_t before_short_upload[sizeof(float) * 4u];
        float values[4];

        assert(depth_texture->format == RINGL_DEPTH_COMPONENT32F);
        assert(depth_texture->shadow_size == sizeof(values));
        memcpy(values, depth_texture->shadow_bytes, sizeof(values));
        assert(values[0] == 0.0f && values[2] == 1.0f);
        assert(values[1] > 0.49f && values[1] < 0.51f);
        assert(values[3] > 0.24f && values[3] < 0.26f);

        memcpy(before_short_upload, depth_texture->shadow_bytes,
               sizeof(before_short_upload));
        ringl_tex_image_2d_from_bytes(
            RINGL_TEXTURE_2D, 0, RINGL_DEPTH_COMPONENT, 2, 2, 0,
            RINGL_DEPTH_COMPONENT, RINGL_UNSIGNED_SHORT, webgl_depth_u16,
            sizeof(webgl_depth_u16) - 1u);
        assert(ringl_get_error() == RINGL_INVALID_VALUE);
        assert(memcmp(depth_texture->shadow_bytes, before_short_upload,
                      sizeof(before_short_upload)) == 0);
    }
    ringl_tex_sub_image_2d(RINGL_TEXTURE_2D, 0, 0, 0, 2, 2,
                           RINGL_DEPTH_COMPONENT, RINGL_UNSIGNED_INT,
                           webgl_depth_u32);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    {
        RinGLTextureObject* depth_texture =
            &context->textures[ringl_object_slot_index(webgl_depth_texture)];
        float values[4];

        memcpy(values, depth_texture->shadow_bytes, sizeof(values));
        assert(values[0] > 0.49f && values[0] < 0.51f);
        assert(values[1] == 1.0f && values[2] == 0.0f);
        assert(values[3] > 0.24f && values[3] < 0.26f);
    }

    /* The packed extension form keeps the exact 24-bit depth and stencil
     * components in the existing D32/S8 split storage and can be attached to
     * a same-size WebGL framebuffer. */
    ringl_gen_textures(1, &webgl_depth_stencil_texture);
    ringl_bind_texture(RINGL_TEXTURE_2D, webgl_depth_stencil_texture);
    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_DEPTH_STENCIL, 2, 2, 0,
                       RINGL_DEPTH_STENCIL, RINGL_UNSIGNED_INT_24_8,
                       webgl_depth_stencil);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    {
        RinGLTextureObject* depth_stencil_texture =
            &context->textures[ringl_object_slot_index(
                webgl_depth_stencil_texture)];
        float values[4];
        uint32_t depth_index;

        assert(depth_stencil_texture->format == RINGL_DEPTH24_STENCIL8);
        assert(depth_stencil_texture->shadow_size == sizeof(values) * 2u);
        for (depth_index = 0u; depth_index < 4u; ++depth_index) {
            memcpy(&values[depth_index],
                   depth_stencil_texture->shadow_bytes +
                       (uint64_t)depth_index * 8u,
                   sizeof(values[depth_index]));
        }
        assert(values[0] == 0.0f && values[2] == 1.0f);
        assert(values[1] > 0.49f && values[1] < 0.51f);
        assert(values[3] > 0.24f && values[3] < 0.26f);
        assert(depth_stencil_texture->shadow_bytes[4] == 0x2au);
        assert(depth_stencil_texture->shadow_bytes[12] == 0x4bu);
        assert(depth_stencil_texture->shadow_bytes[20] == 0x7cu);
        assert(depth_stencil_texture->shadow_bytes[28] == 0x9du);
    }
    ringl_gen_framebuffers(1, &webgl_depth_framebuffer);
    ringl_bind_framebuffer(RINGL_FRAMEBUFFER, webgl_depth_framebuffer);
    ringl_framebuffer_texture_2d(RINGL_FRAMEBUFFER, RINGL_COLOR_ATTACHMENT0,
                                 RINGL_TEXTURE_2D, texture, 0);
    ringl_framebuffer_texture_2d(RINGL_FRAMEBUFFER,
                                 RINGL_DEPTH_STENCIL_ATTACHMENT,
                                 RINGL_TEXTURE_2D,
                                 webgl_depth_stencil_texture, 0);
    assert(ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) ==
           RINGL_FRAMEBUFFER_COMPLETE);
    ringl_bind_framebuffer(RINGL_FRAMEBUFFER, 0u);

    ringl_gen_textures(1, &mip_texture);
    ringl_bind_texture(RINGL_TEXTURE_2D, mip_texture);
    ringl_tex_parameteri(RINGL_TEXTURE_2D, RINGL_TEXTURE_MIN_FILTER,
                         RINGL_LINEAR_MIPMAP_NEAREST);
    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_RGBA, 3, 2, 0,
                       RINGL_RGBA, RINGL_UNSIGNED_BYTE, mip_pixels);
    ringl_generate_mipmap(RINGL_TEXTURE_2D);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) == 0);
    assert(backend.mip_image_creates == 1u && backend.mip_level_count == 2u);
    assert(backend.mip_uploads == 2u);
    assert(backend.mip_upload_level[0] == 0u &&
           backend.mip_upload_width[0] == 3u &&
           backend.mip_upload_height[0] == 2u &&
           backend.mip_upload_size[0] == sizeof(mip_pixels));
    assert(memcmp(backend.mip_upload[0], mip_pixels, sizeof(mip_pixels)) == 0);
    assert(backend.mip_upload_level[1] == 1u &&
           backend.mip_upload_width[1] == 1u &&
           backend.mip_upload_height[1] == 1u &&
           backend.mip_upload_size[1] == sizeof(expected_mip_level1));
    assert(memcmp(backend.mip_upload[1], expected_mip_level1,
                  sizeof(expected_mip_level1)) == 0);

    /* Manual mip definitions are exact-size uploads into the same RinGPU
     * multi-mip image. Invalid level geometry cannot disturb that chain. */
    ringl_tex_image_2d_from_bytes(RINGL_TEXTURE_2D, 2, RINGL_RGBA, 1, 1, 0,
                                  RINGL_RGBA, RINGL_UNSIGNED_BYTE,
                                  manual_mip_level1,
                                  sizeof(manual_mip_level1));
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    ringl_tex_image_2d_from_bytes(RINGL_TEXTURE_2D, 1, RINGL_RGBA, 1, 1, 0,
                                  RINGL_RGBA, RINGL_UNSIGNED_BYTE,
                                  manual_mip_level1,
                                  sizeof(manual_mip_level1));
    assert(ringl_get_error() == RINGL_NO_ERROR);
    backend.mip_image_creates = 0u;
    backend.mip_uploads = 0u;
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) == 0);
    assert(backend.mip_image_creates == 1u && backend.mip_uploads == 2u);
    assert(memcmp(backend.mip_upload[1], manual_mip_level1,
                  sizeof(manual_mip_level1)) == 0);

    ringl_tex_sub_image_2d_from_bytes(RINGL_TEXTURE_2D, 1, 0, 0, 1, 1,
                                      RINGL_RGBA, RINGL_UNSIGNED_BYTE, patch,
                                      sizeof(patch));
    assert(ringl_get_error() == RINGL_NO_ERROR);
    backend.mip_image_creates = 0u;
    backend.mip_uploads = 0u;
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) == 0);
    assert(backend.mip_image_creates == 1u && backend.mip_uploads == 2u);
    assert(memcmp(backend.mip_upload[1], patch, sizeof(patch)) == 0);

    /* Packed UNORM formats use the same native multi-mip descriptor. Their
     * generated levels average native 5/6-bit components and retain their
     * packed byte layout through the backend upload boundary. */
    ringl_gen_textures(1, &packed_mip_texture);
    ringl_bind_texture(RINGL_TEXTURE_2D, packed_mip_texture);
    ringl_tex_parameteri(RINGL_TEXTURE_2D, RINGL_TEXTURE_MIN_FILTER,
                         RINGL_NEAREST_MIPMAP_NEAREST);
    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_RGB, 4, 4, 0, RINGL_RGB,
                       RINGL_UNSIGNED_SHORT_5_6_5, packed_mip_pixels);
    ringl_generate_mipmap(RINGL_TEXTURE_2D);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    backend.mip_image_creates = 0u;
    backend.mip_uploads = 0u;
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) == 0);
    assert(backend.mip_image_creates == 1u && backend.mip_level_count == 3u &&
           backend.mip_uploads == 3u);
    assert(backend.last_format == RINGL_RIN_GPU_FORMAT_RGB565_UNORM);
    assert(backend.mip_upload_size[0] == sizeof(packed_mip_pixels));
    assert(memcmp(backend.mip_upload[0], packed_mip_pixels,
                  sizeof(packed_mip_pixels)) == 0);
    assert(backend.mip_upload_level[1] == 1u &&
           backend.mip_upload_width[1] == 2u &&
           backend.mip_upload_height[1] == 2u &&
           backend.mip_upload_size[1] == sizeof(expected_packed_mip_level1));
    assert(memcmp(backend.mip_upload[1], expected_packed_mip_level1,
                  sizeof(expected_packed_mip_level1)) == 0);
    assert(backend.mip_upload_level[2] == 2u &&
           backend.mip_upload_width[2] == 1u &&
           backend.mip_upload_height[2] == 1u &&
           backend.mip_upload_size[2] == sizeof(expected_packed_mip_level2));
    assert(memcmp(backend.mip_upload[2], &expected_packed_mip_level2,
                  sizeof(expected_packed_mip_level2)) == 0);

    ringl_tex_image_2d_from_bytes(RINGL_TEXTURE_2D, 1, RINGL_RGB, 2, 2, 0,
                                  RINGL_RGB, RINGL_UNSIGNED_SHORT_5_6_5,
                                  manual_packed_mip_level1,
                                  sizeof(manual_packed_mip_level1));
    assert(ringl_get_error() == RINGL_NO_ERROR);
    backend.mip_image_creates = 0u;
    backend.mip_uploads = 0u;
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) == 0);
    assert(backend.mip_image_creates == 1u && backend.mip_uploads == 3u);
    assert(memcmp(backend.mip_upload[1], manual_packed_mip_level1,
                  sizeof(manual_packed_mip_level1)) == 0);

    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_RGBA, 4, 4, 0,
                       RINGL_RGBA, RINGL_UNSIGNED_SHORT_4_4_4_4,
                       rgba4_mip_pixels);
    ringl_generate_mipmap(RINGL_TEXTURE_2D);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    backend.mip_image_creates = 0u;
    backend.mip_uploads = 0u;
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) == 0);
    assert(backend.last_format == RINGL_RIN_GPU_FORMAT_RGBA4_UNORM &&
           backend.mip_image_creates == 1u && backend.mip_uploads == 3u);
    assert(memcmp(backend.mip_upload[1], expected_rgba4_mip_level1,
                  sizeof(expected_rgba4_mip_level1)) == 0);
    assert(memcmp(backend.mip_upload[2], &expected_rgba4_mip_level2,
                  sizeof(expected_rgba4_mip_level2)) == 0);
    ringl_tex_image_2d_from_bytes(RINGL_TEXTURE_2D, 1, RINGL_RGBA, 2, 2, 0,
                                  RINGL_RGBA, RINGL_UNSIGNED_SHORT_4_4_4_4,
                                  expected_rgba4_mip_level1,
                                  sizeof(expected_rgba4_mip_level1));
    assert(ringl_get_error() == RINGL_NO_ERROR);

    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_RGBA, 4, 4, 0,
                       RINGL_RGBA, RINGL_UNSIGNED_SHORT_5_5_5_1,
                       rgb5_a1_mip_pixels);
    ringl_generate_mipmap(RINGL_TEXTURE_2D);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    backend.mip_image_creates = 0u;
    backend.mip_uploads = 0u;
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) == 0);
    assert(backend.last_format == RINGL_RIN_GPU_FORMAT_RGB5_A1_UNORM &&
           backend.mip_image_creates == 1u && backend.mip_uploads == 3u);
    assert(memcmp(backend.mip_upload[1], expected_rgb5_a1_mip_level1,
                  sizeof(expected_rgb5_a1_mip_level1)) == 0);
    assert(memcmp(backend.mip_upload[2], &expected_rgb5_a1_mip_level2,
                  sizeof(expected_rgb5_a1_mip_level2)) == 0);
    ringl_tex_image_2d_from_bytes(RINGL_TEXTURE_2D, 1, RINGL_RGBA, 2, 2, 0,
                                  RINGL_RGBA, RINGL_UNSIGNED_SHORT_5_5_5_1,
                                  expected_rgb5_a1_mip_level1,
                                  sizeof(expected_rgb5_a1_mip_level1));
    assert(ringl_get_error() == RINGL_NO_ERROR);

    /* A base sub-image mutation drops generated levels only. Explicit level
     * one data remains valid and is uploaded with the updated base level. */
    ringl_bind_texture(RINGL_TEXTURE_2D, mip_texture);
    ringl_tex_sub_image_2d(RINGL_TEXTURE_2D, 0, 0, 0, 1, 1,
                           RINGL_RGBA, RINGL_UNSIGNED_BYTE, patch);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    backend.mip_image_creates = 0u;
    backend.mip_uploads = 0u;
    assert(ringl_texture_realize_unit(context, 1u, &image, &sampler) == 0);
    assert(backend.mip_image_creates == 1u && backend.mip_uploads == 2u);
    assert(memcmp(backend.mip_upload[1], patch, sizeof(patch)) == 0);

    ringl_context_destroy(context);

    /* A V1-prefix backend cannot accidentally accept generated storage that
     * it has no way to allocate or upload. */
    {
        RinGLRinGpuOpsV1 v1_ops = ops;
        RinGLRinGpuBindingV1 v1_binding = binding;
        RinGLContextDescV1 v1_desc = desc;
        RinGLContext* v1_context = NULL;
        uint32_t v1_texture;

        v1_ops.struct_size = offsetof(RinGLRinGpuOpsV1,
                                      create_image_2d_mip_v2);
        v1_binding.ops = &v1_ops;
        v1_desc.ringpu = &v1_binding;
        assert(ringl_context_create(&v1_desc, &v1_context) == 0);
        assert(ringl_make_current(v1_context) == 0);
        ringl_gen_textures(1, &v1_texture);
        ringl_bind_texture(RINGL_TEXTURE_2D, v1_texture);
        ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_RGBA, 2, 2, 0,
                           RINGL_RGBA, RINGL_UNSIGNED_BYTE, pixels);
        ringl_generate_mipmap(RINGL_TEXTURE_2D);
        assert(ringl_get_error() == RINGL_INVALID_OPERATION);
        ringl_context_destroy(v1_context);
    }
    return 0;
}
