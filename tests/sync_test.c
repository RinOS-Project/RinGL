/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <ringl/ringl.h>
#include <ringl/ringl_sync.h>

typedef struct FakeBackend {
    uint64_t next_handle;
    uint64_t fence;
    uint64_t last_signal_value;
    uint32_t submits;
    uint32_t waits;
    uint32_t transitions;
    uint32_t readbacks;
    uint32_t destroys;
} FakeBackend;

static int fake_create_buffer(void* session, uint64_t size_bytes,
                              uint64_t* buffer_out)
{
    FakeBackend* backend = session;
    (void)size_bytes;
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
    assert(command_list != 0u && image == 700u);
    assert(old_state == RINGL_RIN_GPU_IMAGE_PRESENT);
    assert(new_state == RINGL_RIN_GPU_IMAGE_COPY_SOURCE);
    backend->transitions++;
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
    uint8_t expected_bgra[8] = {
        30u, 20u, 10u, 255u,
        60u, 50u, 40u, 128u,
    };
    assert(image == 700u && readback != NULL && destination != NULL);
    assert(readback->x == 1u && readback->y == 2u);
    assert(readback->width == 2u && readback->height == 1u);
    assert(readback->destination_row_pitch_bytes == 8u);
    assert(destination_size == sizeof(expected_bgra));
    memcpy(destination, expected_bgra, sizeof(expected_bgra));
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
        .close_command_list = fake_close,
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
    uint8_t pixels[8] = {0};
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

    ringl_context_destroy(context);
    assert(backend.destroys == 2u); /* command list + finish fence */
    return 0;
}
