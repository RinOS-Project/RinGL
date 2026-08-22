/* SPDX-License-Identifier: MIT */
#include <assert.h>
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
    uint8_t last_upload[16];
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
    assert(desc->format == RINGL_RIN_GPU_FORMAT_RGBA8_UNORM);
    assert(desc->reserved0 == 0u);
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
    assert(upload->source_row_pitch_bytes == 8u);
    assert(size_bytes == sizeof(backend->last_upload));
    memcpy(backend->last_upload, data, sizeof(backend->last_upload));
    backend->image_uploads++;
    return 0;
}

static int fake_create_sampler(void* session,
                               const RinGLRinGpuSamplerV1* desc,
                               uint64_t* sampler_out)
{
    FakeBackend* backend = session;
    assert(desc != NULL && sampler_out != NULL);
    assert(desc->min_filter == RINGL_RIN_GPU_SAMPLER_LINEAR);
    assert(desc->mag_filter == RINGL_RIN_GPU_SAMPLER_LINEAR);
    assert(desc->mip_filter == RINGL_RIN_GPU_SAMPLER_NEAREST);
    assert(desc->address_u == RINGL_RIN_GPU_ADDRESS_REPEAT ||
           desc->address_u == RINGL_RIN_GPU_ADDRESS_CLAMP);
    assert(desc->address_v == RINGL_RIN_GPU_ADDRESS_REPEAT);
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
    uint64_t image;
    uint64_t sampler;
    const uint8_t pixels[16] = {
        1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u,
        9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u,
    };
    const uint8_t patch[4] = {21u, 22u, 23u, 24u};

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

    ringl_context_destroy(context);
    return 0;
}