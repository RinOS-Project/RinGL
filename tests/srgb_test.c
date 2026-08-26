/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <ringl/ringl.h>
#include <ringl/ringl_sync.h>

#include "../src/ringl_internal.h"

typedef struct FakeBackend {
    uint64_t next_handle;
    uint64_t image;
    uint64_t fence;
    uint32_t image_creates;
    uint32_t image_format;
    uint64_t image_usage;
    uint8_t uploaded[16];
    uint64_t uploaded_size;
    uint32_t transitions;
    uint32_t readbacks;
} FakeBackend;

static int fake_create_buffer(void* session, uint64_t size_bytes,
                              uint64_t* buffer_out)
{
    FakeBackend* backend = session;

    assert(size_bytes != 0u && buffer_out != NULL);
    *buffer_out = ++backend->next_handle;
    return 0;
}

static int fake_upload_buffer(void* session, uint64_t buffer, uint64_t offset,
                              const void* data, uint64_t size_bytes)
{
    (void)session;
    return buffer != 0u && offset == 0u && data != NULL && size_bytes != 0u
        ? 0 : -1;
}

static int fake_destroy(void* session, uint64_t object)
{
    (void)session;
    return object != 0u ? 0 : -1;
}

static int fake_create_image_2d(void* session,
                                const RinGLRinGpuImage2DV1* desc,
                                uint64_t* image_out)
{
    FakeBackend* backend = session;

    assert(desc != NULL && image_out != NULL);
    assert(desc->width == 1u && desc->height == 1u);
    assert(desc->format == RINGL_RIN_GPU_FORMAT_RGBA32_FLOAT);
    assert((desc->usage & RINGL_RIN_GPU_IMAGE_USAGE_COLOR_TARGET) != 0u);
    assert((desc->usage & RINGL_RIN_GPU_IMAGE_USAGE_COPY_SOURCE) != 0u);
    backend->image = ++backend->next_handle;
    backend->image_creates++;
    backend->image_format = desc->format;
    backend->image_usage = desc->usage;
    *image_out = backend->image;
    return 0;
}

static int fake_upload_image_2d(void* session, uint64_t image,
                                const RinGLRinGpuImageUpload2DV1* upload,
                                const void* data, uint64_t size_bytes)
{
    FakeBackend* backend = session;

    assert(image == backend->image && upload != NULL && data != NULL);
    assert(upload->width == 1u && upload->height == 1u);
    assert(upload->source_row_pitch_bytes == 4u * sizeof(float));
    assert(size_bytes == sizeof(backend->uploaded));
    memcpy(backend->uploaded, data, sizeof(backend->uploaded));
    backend->uploaded_size = size_bytes;
    return 0;
}

static int fake_create_command_list(void* session, uint32_t capabilities,
                                    uint64_t* command_list_out)
{
    FakeBackend* backend = session;

    assert(capabilities == RINGL_RIN_GPU_QUEUE_GRAPHICS);
    *command_list_out = ++backend->next_handle;
    return 0;
}

static int fake_reset_command_list(void* session, uint64_t command_list)
{
    (void)session;
    return command_list != 0u ? 0 : -1;
}

static int fake_transition_image(void* session, uint64_t command_list,
                                 uint64_t image, uint32_t old_state,
                                 uint32_t new_state)
{
    FakeBackend* backend = session;

    assert(command_list != 0u && image == backend->image);
    assert(old_state == RINGL_RIN_GPU_IMAGE_COPY_DESTINATION);
    assert(new_state == RINGL_RIN_GPU_IMAGE_COPY_SOURCE);
    backend->transitions++;
    return 0;
}

static int fake_begin_render_pass_mrt(void* session, uint64_t command_list,
                                      const RinGLRinGpuRenderPassMrtV1* pass)
{
    (void)session;
    return command_list != 0u && pass != NULL ? 0 : -1;
}

static int fake_close_command_list(void* session, uint64_t command_list)
{
    (void)session;
    return command_list != 0u ? 0 : -1;
}

static int fake_create_fence(void* session, uint64_t initial_value,
                             uint64_t* fence_out)
{
    FakeBackend* backend = session;

    assert(initial_value == 0u && fence_out != NULL);
    backend->fence = ++backend->next_handle;
    *fence_out = backend->fence;
    return 0;
}

static int fake_submit_fenced(void* session, uint64_t queue,
                              uint64_t command_list, uint64_t fence,
                              uint64_t value)
{
    FakeBackend* backend = session;

    assert(queue == 1u && command_list != 0u && fence == backend->fence);
    assert(value != 0u);
    return 0;
}

static int fake_wait_fence(void* session, uint64_t fence, uint64_t value,
                           uint64_t timeout_ns)
{
    FakeBackend* backend = session;

    assert(fence == backend->fence && value != 0u);
    assert(timeout_ns == RINGL_TIMEOUT_INFINITE);
    return 0;
}

static int fake_readback(void* session, uint64_t image,
                         const RinGLRinGpuImageReadback2DV1* readback,
                         void* destination, uint64_t destination_size)
{
    FakeBackend* backend = session;

    assert(image == backend->image && readback != NULL && destination != NULL);
    assert(readback->width == 1u && readback->height == 1u);
    assert(readback->destination_row_pitch_bytes == 4u * sizeof(float));
    assert(destination_size == sizeof(backend->uploaded));
    memcpy(destination, backend->uploaded, sizeof(backend->uploaded));
    backend->readbacks++;
    return 0;
}

static float uploaded_component(const FakeBackend* backend, uint32_t index)
{
    float value;

    assert(backend != NULL && index < 4u);
    memcpy(&value, backend->uploaded + index * sizeof(value), sizeof(value));
    return value;
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
        .create_image_2d = fake_create_image_2d,
        .upload_image_2d = fake_upload_image_2d,
        .create_command_list = fake_create_command_list,
        .reset_command_list = fake_reset_command_list,
        .transition_image = fake_transition_image,
        .begin_render_pass_mrt_v1 = fake_begin_render_pass_mrt,
        .close_command_list = fake_close_command_list,
    };
    RinGLRinGpuBindingV1 binding = {
        .struct_size = sizeof(binding),
        .api_version = RINGL_API_VERSION,
        .session = &backend,
        .ops = &ops,
        .graphics_queue = 1u,
        .queue_capabilities = RINGL_RIN_GPU_QUEUE_GRAPHICS,
    };
    RinGLContextDescV1 desc = {
        .struct_size = sizeof(desc),
        .api_version = RINGL_API_VERSION,
        .ringpu = &binding,
    };
    RinGLRinGpuSyncOpsV1 sync_ops = {
        .struct_size = sizeof(sync_ops),
        .api_version = RINGL_SYNC_API_VERSION,
        .create_fence = fake_create_fence,
        .queue_submit_fenced = fake_submit_fenced,
        .wait_fence = fake_wait_fence,
        .readback_image_2d = fake_readback,
    };
    RinGLContext* context = NULL;
    uint32_t texture = 0u;
    uint32_t rgb_texture = 0u;
    uint32_t framebuffer = 0u;
    uint32_t rgb_framebuffer = 0u;
    uint32_t renderbuffer = 0u;
    uint32_t float_renderbuffer = 0u;
    uint32_t renderbuffer_framebuffer = 0u;
    uint32_t is_srgb = RINGL_FALSE;
    uint32_t is_float = RINGL_FALSE;
    uint32_t component_type = 0u;
    uint8_t pixels[4] = { 128u, 64u, 32u, 77u };
    uint8_t patched_pixels[4] = { 255u, 0u, 0u, 128u };
    uint8_t rgb_pixels[3] = { 128u, 64u, 32u };
    uint8_t readback[4] = { 0u };

    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_make_current(context) == 0);

    ringl_gen_textures(1, &texture);
    ringl_bind_texture(RINGL_TEXTURE_2D, texture);
    ringl_tex_parameteri(RINGL_TEXTURE_2D, RINGL_TEXTURE_MIN_FILTER,
                         RINGL_NEAREST);
    ringl_tex_parameteri(RINGL_TEXTURE_2D, RINGL_TEXTURE_MAG_FILTER,
                         RINGL_NEAREST);
    ringl_tex_image_2d_from_bytes(
        RINGL_TEXTURE_2D, 0, RINGL_SRGB_ALPHA_EXT, 1, 1, 0,
        RINGL_SRGB_ALPHA_EXT, RINGL_UNSIGNED_BYTE, pixels, sizeof(pixels));
    assert(ringl_get_error() == RINGL_NO_ERROR);

    ringl_gen_framebuffers(1, &framebuffer);
    ringl_bind_framebuffer(RINGL_FRAMEBUFFER, framebuffer);
    ringl_framebuffer_texture_2d(RINGL_FRAMEBUFFER, RINGL_COLOR_ATTACHMENT0,
                                 RINGL_TEXTURE_2D, texture, 0);
    assert(ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) ==
           RINGL_FRAMEBUFFER_COMPLETE);
    assert(ringl_framebuffer_color_attachment_is_srgb(&is_srgb) == 0);
    assert(is_srgb == RINGL_TRUE);
    assert(ringl_framebuffer_color_attachment_component_type(&component_type) == 0);
    assert(component_type == RINGL_UNSIGNED_BYTE);
    component_type = UINT32_C(0xdeadbeef);
    assert(ringl_framebuffer_color_attachment_component_type_at(
               RINGL_COLOR_ATTACHMENT1, &component_type) == -1);
    assert(ringl_get_error() == RINGL_INVALID_ENUM);
    assert(component_type == UINT32_C(0xdeadbeef));
    assert(ringl_enable_webgl_draw_buffers() == 0);
    ringl_framebuffer_texture_2d(RINGL_FRAMEBUFFER, RINGL_COLOR_ATTACHMENT1,
                                 RINGL_TEXTURE_2D, texture, 0);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_framebuffer_color_attachment_is_srgb_at(
               RINGL_COLOR_ATTACHMENT1, &is_srgb) == 0);
    assert(is_srgb == RINGL_TRUE);
    assert(ringl_framebuffer_color_attachment_component_type_at(
               RINGL_COLOR_ATTACHMENT1, &component_type) == 0);
    assert(component_type == RINGL_UNSIGNED_BYTE);
    ringl_gen_renderbuffers(1, &float_renderbuffer);
    ringl_bind_renderbuffer(RINGL_RENDERBUFFER, float_renderbuffer);
    ringl_renderbuffer_storage(RINGL_RENDERBUFFER, RINGL_RGBA32F, 1, 1);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_framebuffer_renderbuffer(RINGL_FRAMEBUFFER, RINGL_COLOR_ATTACHMENT2,
                                   RINGL_RENDERBUFFER, float_renderbuffer);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_framebuffer_color_attachment_component_type_at(
               RINGL_COLOR_ATTACHMENT2, &component_type) == 0);
    assert(component_type == RINGL_FLOAT);
    assert(ringl_framebuffer_color_attachment_is_float_at(
               RINGL_COLOR_ATTACHMENT2, &is_float) == 0);
    assert(is_float == RINGL_TRUE);
    ringl_framebuffer_renderbuffer(RINGL_FRAMEBUFFER, RINGL_COLOR_ATTACHMENT2,
                                   RINGL_RENDERBUFFER, 0u);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_delete_renderbuffers(1, &float_renderbuffer);
    ringl_framebuffer_texture_2d(RINGL_FRAMEBUFFER, RINGL_COLOR_ATTACHMENT1,
                                 RINGL_TEXTURE_2D, 0u, 0);
    assert(ringl_get_error() == RINGL_NO_ERROR);

    {
        RinGLColorTarget target;

        assert(ringl_resolve_color_target(context, &target) == 0);
        assert(target.format == RINGL_RIN_GPU_FORMAT_RGBA32_FLOAT);
        assert(target.srgb_encoding == RINGL_TRUE);
    }
    assert(backend.image_creates == 1u);
    assert(backend.image_format == RINGL_RIN_GPU_FORMAT_RGBA32_FLOAT);
    assert(backend.uploaded_size == sizeof(backend.uploaded));
    assert(uploaded_component(&backend, 0u) == ringl_srgb_decode_u8(128u));
    assert(uploaded_component(&backend, 1u) == ringl_srgb_decode_u8(64u));
    assert(uploaded_component(&backend, 2u) == ringl_srgb_decode_u8(32u));
    assert(uploaded_component(&backend, 3u) == (float)77u / 255.0f);

    assert(ringl_context_set_sync_ops(context, &sync_ops) == 0);
    assert(ringl_read_color_target_rgba(context, 0, 0, 1, 1, readback) == 0);
    assert(memcmp(readback, pixels, sizeof(pixels)) == 0);
    assert(backend.transitions == 1u && backend.readbacks == 1u);

    ringl_tex_sub_image_2d_from_bytes(
        RINGL_TEXTURE_2D, 0, 0, 0, 1, 1, RINGL_SRGB_ALPHA_EXT,
        RINGL_UNSIGNED_BYTE, patched_pixels, sizeof(patched_pixels));
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_generate_mipmap(RINGL_TEXTURE_2D);
    assert(ringl_get_error() == RINGL_INVALID_OPERATION);

    ringl_gen_textures(1, &rgb_texture);
    ringl_bind_texture(RINGL_TEXTURE_2D, rgb_texture);
    ringl_tex_image_2d_from_bytes(
        RINGL_TEXTURE_2D, 0, RINGL_SRGB_EXT, 1, 1, 0, RINGL_SRGB_EXT,
        RINGL_UNSIGNED_BYTE, rgb_pixels, sizeof(rgb_pixels));
    assert(ringl_get_error() == RINGL_NO_ERROR);
    {
        RinGLTextureObject* object =
            &context->textures[ringl_object_slot_index(rgb_texture)];
        float red;
        float green;
        float blue;
        float alpha;

        assert(object->format == RINGL_RGB);
        assert(object->color_component_type == RINGL_FLOAT);
        assert(object->srgb_encoding == RINGL_TRUE);
        assert(object->shadow_size == 4u * sizeof(float));
        memcpy(&red, object->shadow_bytes + 0u * sizeof(float), sizeof(red));
        memcpy(&green, object->shadow_bytes + 1u * sizeof(float), sizeof(green));
        memcpy(&blue, object->shadow_bytes + 2u * sizeof(float), sizeof(blue));
        memcpy(&alpha, object->shadow_bytes + 3u * sizeof(float), sizeof(alpha));
        assert(red == ringl_srgb_decode_u8(128u));
        assert(green == ringl_srgb_decode_u8(64u));
        assert(blue == ringl_srgb_decode_u8(32u));
        assert(alpha == 1.0f);
    }

    ringl_gen_framebuffers(1, &rgb_framebuffer);
    ringl_bind_framebuffer(RINGL_FRAMEBUFFER, rgb_framebuffer);
    ringl_framebuffer_texture_2d(RINGL_FRAMEBUFFER, RINGL_COLOR_ATTACHMENT0,
                                 RINGL_TEXTURE_2D, rgb_texture, 0);
    assert(ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) ==
           RINGL_FRAMEBUFFER_COMPLETE);
    assert(ringl_framebuffer_color_attachment_is_srgb(&is_srgb) == 0);
    assert(is_srgb == RINGL_TRUE);
    assert(ringl_effective_color_write_mask(context) ==
           (RINGL_RIN_GPU_COLOR_WRITE_ALL &
            ~RINGL_RIN_GPU_COLOR_WRITE_ALPHA));
    {
        RinGLColorTarget target;
        float physical_alpha = 0.25f;

        assert(ringl_resolve_color_target(context, &target) == 0);
        assert(target.srgb_encoding == RINGL_TRUE);
        assert(target.has_alpha == RINGL_FALSE);
        memcpy(backend.uploaded + 3u * sizeof(physical_alpha),
               &physical_alpha, sizeof(physical_alpha));
        assert(ringl_read_color_target_rgba(context, 0, 0, 1, 1,
                                             readback) == 0);
        assert(readback[0] == rgb_pixels[0]);
        assert(readback[1] == rgb_pixels[1]);
        assert(readback[2] == rgb_pixels[2]);
        assert(readback[3] == UINT8_MAX);
    }

    ringl_tex_image_2d_from_bytes(
        RINGL_TEXTURE_2D, 0, RINGL_SRGB8_ALPHA8_EXT, 1, 1, 0,
        RINGL_SRGB8_ALPHA8_EXT, RINGL_UNSIGNED_BYTE, pixels, sizeof(pixels));
    assert(ringl_get_error() == RINGL_INVALID_ENUM);

    ringl_gen_renderbuffers(1, &renderbuffer);
    ringl_bind_renderbuffer(RINGL_RENDERBUFFER, renderbuffer);
    ringl_renderbuffer_storage(RINGL_RENDERBUFFER, RINGL_SRGB8_ALPHA8_EXT,
                               1, 1);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_gen_framebuffers(1, &renderbuffer_framebuffer);
    ringl_bind_framebuffer(RINGL_FRAMEBUFFER, renderbuffer_framebuffer);
    ringl_framebuffer_renderbuffer(RINGL_FRAMEBUFFER, RINGL_COLOR_ATTACHMENT0,
                                   RINGL_RENDERBUFFER, renderbuffer);
    assert(ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) ==
           RINGL_FRAMEBUFFER_COMPLETE);
    assert(ringl_framebuffer_color_attachment_is_srgb(&is_srgb) == 0);
    assert(is_srgb == RINGL_TRUE);
    assert(ringl_framebuffer_color_attachment_component_type(&component_type) == 0);
    assert(component_type == RINGL_UNSIGNED_BYTE);
    {
        RinGLColorTarget target;

        assert(ringl_resolve_color_target(context, &target) == 0);
        assert(target.format == RINGL_RIN_GPU_FORMAT_RGBA32_FLOAT);
        assert(target.srgb_encoding == RINGL_TRUE);
    }
    assert(backend.image_creates == 3u);

    ringl_context_destroy(context);
    return 0;
}
