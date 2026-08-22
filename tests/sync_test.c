/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <ringl/ringl.h>
#include <ringl/ringl_sync.h>

#include "../src/ringl_internal.h"

typedef struct FakeBackend {
    uint64_t next_handle;
    uint64_t fence;
    uint64_t last_signal_value;
    uint32_t submits;
    uint32_t waits;
    uint32_t transitions;
    uint32_t readbacks;
    uint32_t failed_readbacks;
    uint32_t fail_next_readback;
    uint32_t lose_next_readback;
    uint32_t lose_next_create_buffer;
    uint32_t render_passes;
    uint32_t queue_submits;
    uint32_t destroys;
} FakeBackend;

static int fake_create_buffer(void* session, uint64_t size_bytes,
                              uint64_t* buffer_out)
{
    FakeBackend* backend = session;
    (void)size_bytes;
    if (backend->lose_next_create_buffer != 0u) {
        backend->lose_next_create_buffer = 0u;
        return RINGL_RIN_GPU_ERROR_DEVICE_LOST;
    }
    *buffer_out = ++backend->next_handle;
    return 0;
}

static int fake_upload_buffer(void* session, uint64_t buffer, uint64_t offset,
                              const void* data, uint64_t size_bytes)
{
    (void)session; (void)buffer; (void)offset; (void)data; (void)size_bytes;
    return 0;
}

static int fake_destroy(void* session, uint64_t object)
{
    FakeBackend* backend = session;
    assert(object != 0u);
    backend->destroys++;
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
    assert(command_list != 0u);
    return 0;
}

static int fake_transition_image(void* session, uint64_t command_list,
                                 uint64_t image, uint32_t old_state,
                                 uint32_t new_state)
{
    FakeBackend* backend = session;
    assert(command_list != 0u);
    if (image == 700u) {
        assert(old_state == RINGL_RIN_GPU_IMAGE_PRESENT);
        assert(new_state == RINGL_RIN_GPU_IMAGE_COPY_SOURCE);
    } else {
        assert(image == 701u);
        assert((old_state == RINGL_RIN_GPU_IMAGE_UNDEFINED &&
                new_state == RINGL_RIN_GPU_IMAGE_COLOR_TARGET) ||
               (old_state == RINGL_RIN_GPU_IMAGE_COLOR_TARGET &&
                new_state == RINGL_RIN_GPU_IMAGE_COPY_SOURCE));
    }
    backend->transitions++;
    return 0;
}

static int fake_create_image_2d(void* session,
                                const RinGLRinGpuImage2DV1* desc,
                                uint64_t* image_out)
{
    (void)session;
    assert(desc != NULL && image_out != NULL);
    assert(desc->width == 8u && desc->height == 8u);
    assert(desc->format == RINGL_RIN_GPU_FORMAT_RGBA8_UNORM);
    assert(desc->usage == (RINGL_RIN_GPU_IMAGE_USAGE_COLOR_TARGET |
                           RINGL_RIN_GPU_IMAGE_USAGE_COPY_SOURCE));
    *image_out = 701u;
    return 0;
}

static int fake_begin_render_pass(void* session, uint64_t command_list,
                                  const RinGLRinGpuRenderPassV1* pass)
{
    FakeBackend* backend = session;

    assert(command_list != 0u && pass != NULL);
    assert(pass->color_target == 701u);
    assert(pass->load_op == RINGL_RIN_GPU_RENDER_CLEAR);
    assert(pass->store_op == RINGL_RIN_GPU_RENDER_STORE);
    backend->render_passes++;
    return 0;
}

static int fake_end_render_pass(void* session, uint64_t command_list)
{
    (void)session;
    return command_list != 0u ? 0 : -1;
}

static int fake_queue_submit(void* session, uint64_t queue,
                             uint64_t command_list)
{
    FakeBackend* backend = session;

    assert(queue == 900u && command_list != 0u);
    backend->queue_submits++;
    return 0;
}

static int fake_close(void* session, uint64_t command_list)
{
    (void)session;
    assert(command_list != 0u);
    return 0;
}

static int fake_create_fence(void* session, uint64_t initial_value,
                             uint64_t* fence_out)
{
    FakeBackend* backend = session;
    assert(initial_value == 0u);
    backend->fence = ++backend->next_handle;
    *fence_out = backend->fence;
    return 0;
}

static int fake_submit_fenced(void* session, uint64_t queue,
                              uint64_t command_list, uint64_t signal_fence,
                              uint64_t signal_value)
{
    FakeBackend* backend = session;
    assert(queue == 900u && command_list != 0u);
    assert(signal_fence == backend->fence);
    assert(signal_value > backend->last_signal_value);
    backend->last_signal_value = signal_value;
    backend->submits++;
    return 0;
}

static int fake_wait_fence(void* session, uint64_t fence, uint64_t value,
                           uint64_t timeout_ns)
{
    FakeBackend* backend = session;
    assert(fence == backend->fence);
    assert(value == backend->last_signal_value);
    assert(timeout_ns == RINGL_TIMEOUT_INFINITE);
    backend->waits++;
    return 0;
}

static int fake_readback(void* session, uint64_t image,
                         const RinGLRinGpuImageReadback2DV1* readback,
                         void* destination, uint64_t destination_size)
{
    FakeBackend* backend = session;
    const uint8_t expected_bgra[8] = {
        30u, 20u, 10u, 255u,
        60u, 50u, 40u, 128u,
    };
    const uint8_t expected_rgba[8] = {
        10u, 20u, 30u, 255u,
        40u, 50u, 60u, 128u,
    };
    assert((image == 700u || image == 701u) && readback != NULL &&
           destination != NULL);
    assert(readback->x == 1u && readback->y == 2u);
    assert(readback->width == 2u && readback->height == 1u);
    assert(readback->destination_row_pitch_bytes == 8u);
    assert(destination_size == sizeof(expected_bgra));
    if (backend->fail_next_readback != 0u) {
        backend->fail_next_readback = 0u;
        backend->failed_readbacks++;
        return -1;
    }
    if (backend->lose_next_readback != 0u) {
        backend->lose_next_readback = 0u;
        backend->failed_readbacks++;
        return RINGL_RIN_GPU_ERROR_DEVICE_LOST;
    }
    memcpy(destination, image == 700u ? expected_bgra : expected_rgba,
           sizeof(expected_bgra));
    backend->readbacks++;
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
        .create_command_list = fake_create_command_list,
        .reset_command_list = fake_reset_command_list,
        .transition_image = fake_transition_image,
        .begin_render_pass = fake_begin_render_pass,
        .close_command_list = fake_close,
        .end_render_pass = fake_end_render_pass,
        .queue_submit = fake_queue_submit,
        .create_image_2d = fake_create_image_2d,
    };
    RinGLRinGpuBindingV1 binding = {
        .struct_size = sizeof(binding),
        .api_version = RINGL_API_VERSION,
        .session = &backend,
        .ops = &ops,
        .graphics_queue = 900u,
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
    RinGLDefaultFramebufferV1 framebuffer = {
        .struct_size = sizeof(framebuffer),
        .api_version = RINGL_API_VERSION,
        .color_target = 700u,
        .color_format = 3u, /* BGRA8_UNORM */
        .width = 8u,
        .height = 8u,
    };
    RinGLContext* context = NULL;
    uint32_t texture = 0u;
    uint32_t copied_texture = 0u;
    uint32_t source_framebuffer = 0u;
    uint32_t renderbuffer = 0u;
    uint32_t depth_renderbuffer = 0u;
    uint32_t texture_before_loss;
    uint32_t submits_before_loss;
    uint32_t buffer = 0u;
    const uint8_t buffer_data[4] = {1u, 2u, 3u, 4u};
    FakeBackend command_loss_backend = {0};
    uint8_t pixels[8] = {0};
    uint8_t fbo_pixels[8] = {0};
    const uint8_t expected_rgba[8] = {
        10u, 20u, 30u, 255u,
        40u, 50u, 60u, 128u,
    };

    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_context_set_sync_ops(context, &sync_ops) == 0);
    assert(ringl_make_current(context) == 0);
    assert(ringl_set_default_framebuffer(&framebuffer) == 0);

    ringl_flush();
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_finish();
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(backend.submits == 1u && backend.waits == 1u);

    ringl_read_pixels(1, 2, 2, 1, RINGL_RGBA, RINGL_UNSIGNED_BYTE, pixels);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(backend.transitions == 1u);
    assert(backend.submits == 2u && backend.waits == 2u);
    assert(backend.readbacks == 1u);
    assert(memcmp(pixels, expected_rgba, sizeof(pixels)) == 0);

    ringl_gen_textures(1, &texture);
    ringl_bind_texture(RINGL_TEXTURE_2D, texture);
    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_RGBA, 2, 1, 0,
                       RINGL_RGBA, RINGL_UNSIGNED_BYTE, NULL);
    ringl_copy_tex_sub_image_2d(RINGL_TEXTURE_2D, 0, 0, 0, 1, 2, 2, 1);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(backend.transitions == 1u);
    assert(backend.submits == 3u && backend.waits == 3u);
    assert(backend.readbacks == 2u);
    assert(memcmp(context->textures[ringl_object_slot_index(texture)].shadow_bytes,
                  expected_rgba, sizeof(expected_rgba)) == 0);
    ringl_copy_tex_sub_image_2d(RINGL_TEXTURE_2D, 0, 0, 0, 7, 2, 2, 1);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    assert(backend.readbacks == 2u);

    ringl_gen_renderbuffers(1, &renderbuffer);
    ringl_bind_renderbuffer(RINGL_RENDERBUFFER, renderbuffer);
    ringl_renderbuffer_storage(RINGL_RENDERBUFFER, RINGL_RGBA8, 8, 8);
    ringl_gen_framebuffers(1, &source_framebuffer);
    ringl_bind_framebuffer(RINGL_FRAMEBUFFER, source_framebuffer);
    ringl_framebuffer_renderbuffer(RINGL_FRAMEBUFFER, RINGL_COLOR_ATTACHMENT0,
                                   RINGL_RENDERBUFFER, renderbuffer);
    assert(ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) ==
           RINGL_FRAMEBUFFER_COMPLETE);
    ringl_clear(RINGL_COLOR_BUFFER_BIT);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(backend.transitions == 2u && backend.render_passes == 1u &&
           backend.queue_submits == 1u);
    ringl_copy_tex_sub_image_2d(RINGL_TEXTURE_2D, 0, 0, 0, 1, 2, 2, 1);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(backend.transitions == 3u);
    assert(backend.submits == 4u && backend.waits == 4u);
    assert(backend.readbacks == 3u);
    assert(memcmp(context->textures[ringl_object_slot_index(texture)].shadow_bytes,
                  expected_rgba, sizeof(expected_rgba)) == 0);
    ringl_read_pixels(1, 2, 2, 1, RINGL_RGBA, RINGL_UNSIGNED_BYTE, fbo_pixels);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(backend.transitions == 3u);
    assert(backend.submits == 5u && backend.waits == 5u);
    assert(backend.readbacks == 4u);
    assert(memcmp(fbo_pixels, expected_rgba, sizeof(expected_rgba)) == 0);
    ringl_gen_textures(1, &copied_texture);
    ringl_bind_texture(RINGL_TEXTURE_2D, copied_texture);
    ringl_copy_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_RGBA, 7, 2, 2, 1, 0);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    assert(context->textures[ringl_object_slot_index(copied_texture)].defined ==
           RINGL_FALSE);
    ringl_copy_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_RGBA, 1, 2, 2, 1, 0);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(backend.transitions == 3u);
    assert(backend.submits == 6u && backend.waits == 6u);
    assert(backend.readbacks == 5u);
    assert(memcmp(context->textures[ringl_object_slot_index(copied_texture)].shadow_bytes,
                  expected_rgba, sizeof(expected_rgba)) == 0);
    backend.fail_next_readback = 1u;
    ringl_copy_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_RGBA, 1, 2, 2, 1, 0);
    assert(ringl_get_error() == RINGL_INVALID_OPERATION);
    assert(backend.failed_readbacks == 1u && backend.readbacks == 5u);
    assert(memcmp(context->textures[ringl_object_slot_index(copied_texture)].shadow_bytes,
                  expected_rgba, sizeof(expected_rgba)) == 0);
    ringl_gen_renderbuffers(1, &depth_renderbuffer);
    ringl_bind_renderbuffer(RINGL_RENDERBUFFER, depth_renderbuffer);
    ringl_renderbuffer_storage(RINGL_RENDERBUFFER, RINGL_DEPTH_COMPONENT32F,
                               4, 4);
    ringl_framebuffer_renderbuffer(RINGL_FRAMEBUFFER, RINGL_DEPTH_ATTACHMENT,
                                   RINGL_RENDERBUFFER, depth_renderbuffer);
    assert(ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) ==
           RINGL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT);
    ringl_copy_tex_sub_image_2d(RINGL_TEXTURE_2D, 0, 0, 0, 1, 2, 2, 1);
    assert(ringl_get_error() == RINGL_INVALID_OPERATION);
    assert(backend.transitions == 3u && backend.readbacks == 5u);
    assert(memcmp(context->textures[ringl_object_slot_index(copied_texture)].shadow_bytes,
                  expected_rgba, sizeof(expected_rgba)) == 0);

    ringl_bind_framebuffer(RINGL_FRAMEBUFFER, 0u);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    texture_before_loss =
        context->bound_texture_2d[context->active_texture_unit];
    submits_before_loss = backend.submits;
    backend.lose_next_readback = 1u;
    ringl_read_pixels(1, 2, 2, 1, RINGL_RGBA, RINGL_UNSIGNED_BYTE, pixels);
    assert(ringl_context_is_lost(context) == RINGL_TRUE);
    assert(ringl_get_current_context() == NULL);
    assert(ringl_get_error() == RINGL_CONTEXT_LOST_WEBGL);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_bind_texture(RINGL_TEXTURE_2D, 0u);
    assert(context->bound_texture_2d[context->active_texture_unit] ==
           texture_before_loss);
    ringl_finish();
    assert(backend.submits == submits_before_loss + 1u);
    assert(ringl_context_set_sync_ops(context, NULL) == -1);
    assert(ringl_make_current(context) == -1);
    assert(ringl_get_error() == RINGL_NO_ERROR);

    ringl_context_destroy(context);
    assert(backend.destroys == 3u); /* renderbuffer image + command list + fence */

    binding.session = &command_loss_backend;
    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_make_current(context) == 0);
    ringl_gen_buffers(1, &buffer);
    ringl_bind_buffer(RINGL_ARRAY_BUFFER, buffer);
    command_loss_backend.lose_next_create_buffer = 1u;
    ringl_buffer_data(RINGL_ARRAY_BUFFER, sizeof(buffer_data), buffer_data,
                      RINGL_STATIC_DRAW);
    assert(ringl_context_is_lost(context) == RINGL_TRUE);
    assert(ringl_get_error() == RINGL_CONTEXT_LOST_WEBGL);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_context_destroy(context);
    return 0;
}
