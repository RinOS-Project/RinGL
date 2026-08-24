/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <ringl/ringl.h>
#include <ringl/ringl_sync.h>

#include "../src/ringl_internal.h"

enum FakeBackendTraceEvent {
    FAKE_TRACE_CREATE_COMMAND_LIST = 1,
    FAKE_TRACE_RESET_COMMAND_LIST,
    FAKE_TRACE_TRANSITION_IMAGE,
    FAKE_TRACE_BEGIN_RENDER_PASS,
    FAKE_TRACE_END_RENDER_PASS,
    FAKE_TRACE_CLOSE_COMMAND_LIST,
    FAKE_TRACE_QUEUE_SUBMIT,
    FAKE_TRACE_CREATE_FENCE,
    FAKE_TRACE_SUBMIT_FENCED,
    FAKE_TRACE_WAIT_FENCE,
    FAKE_TRACE_READBACK,
};

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
    uint32_t trace_enabled;
    uint32_t trace_event_count;
    uint32_t trace_events[64];
} FakeBackend;

static void fake_trace_event(FakeBackend* backend, uint32_t event)
{
    if (backend->trace_enabled == 0u)
        return;
    assert(backend->trace_event_count <
           sizeof(backend->trace_events) / sizeof(backend->trace_events[0]));
    backend->trace_events[backend->trace_event_count++] = event;
}

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
    fake_trace_event(backend, FAKE_TRACE_CREATE_COMMAND_LIST);
    *command_list_out = ++backend->next_handle;
    return 0;
}

static int fake_reset_command_list(void* session, uint64_t command_list)
{
    FakeBackend* backend = session;
    assert(command_list != 0u);
    fake_trace_event(backend, FAKE_TRACE_RESET_COMMAND_LIST);
    return 0;
}

static int fake_transition_image(void* session, uint64_t command_list,
                                 uint64_t image, uint32_t old_state,
                                 uint32_t new_state)
{
    FakeBackend* backend = session;
    assert(command_list != 0u);
    if (image == 700u) {
        assert((old_state == RINGL_RIN_GPU_IMAGE_PRESENT &&
                (new_state == RINGL_RIN_GPU_IMAGE_COPY_SOURCE ||
                 new_state == RINGL_RIN_GPU_IMAGE_COLOR_TARGET)) ||
               (old_state == RINGL_RIN_GPU_IMAGE_COLOR_TARGET &&
                new_state == RINGL_RIN_GPU_IMAGE_COPY_SOURCE) ||
               (old_state == RINGL_RIN_GPU_IMAGE_COPY_SOURCE &&
                new_state == RINGL_RIN_GPU_IMAGE_COLOR_TARGET));
    } else if (image == 702u) {
        assert((old_state == RINGL_RIN_GPU_IMAGE_UNDEFINED &&
                new_state == RINGL_RIN_GPU_IMAGE_COLOR_TARGET) ||
               (old_state == RINGL_RIN_GPU_IMAGE_COLOR_TARGET &&
                new_state == RINGL_RIN_GPU_IMAGE_COPY_SOURCE));
    } else {
        assert(image == 701u);
        assert((old_state == RINGL_RIN_GPU_IMAGE_UNDEFINED &&
                new_state == RINGL_RIN_GPU_IMAGE_COLOR_TARGET) ||
               (old_state == RINGL_RIN_GPU_IMAGE_COLOR_TARGET &&
                new_state == RINGL_RIN_GPU_IMAGE_COPY_SOURCE));
    }
    backend->transitions++;
    fake_trace_event(backend, FAKE_TRACE_TRANSITION_IMAGE);
    return 0;
}

static int fake_create_image_2d(void* session,
                                const RinGLRinGpuImage2DV1* desc,
                                uint64_t* image_out)
{
    (void)session;
    assert(desc != NULL && image_out != NULL);
    assert(desc->width == 8u && desc->height == 8u);
    assert(desc->usage == (RINGL_RIN_GPU_IMAGE_USAGE_COLOR_TARGET |
                           RINGL_RIN_GPU_IMAGE_USAGE_COPY_SOURCE));
    if (desc->format == RINGL_RIN_GPU_FORMAT_RGBA8_UNORM)
        *image_out = 701u;
    else if (desc->format == RINGL_RIN_GPU_FORMAT_RGBA32_FLOAT)
        *image_out = 703u;
    else {
        assert(desc->format == RINGL_RIN_GPU_FORMAT_RGBA4_UNORM);
        *image_out = 702u;
    }
    return 0;
}

static int fake_begin_render_pass(void* session, uint64_t command_list,
                                  const RinGLRinGpuRenderPassV1* pass)
{
    FakeBackend* backend = session;

    assert(command_list != 0u && pass != NULL);
    assert(pass->color_target == 700u || pass->color_target == 701u ||
           pass->color_target == 702u);
    assert(pass->load_op == RINGL_RIN_GPU_RENDER_CLEAR);
    assert(pass->store_op == RINGL_RIN_GPU_RENDER_STORE);
    backend->render_passes++;
    fake_trace_event(backend, FAKE_TRACE_BEGIN_RENDER_PASS);
    return 0;
}

static int fake_end_render_pass(void* session, uint64_t command_list)
{
    FakeBackend* backend = session;
    fake_trace_event(backend, FAKE_TRACE_END_RENDER_PASS);
    return command_list != 0u ? 0 : -1;
}

static int fake_queue_submit(void* session, uint64_t queue,
                             uint64_t command_list)
{
    FakeBackend* backend = session;

    assert(queue == 900u && command_list != 0u);
    backend->queue_submits++;
    fake_trace_event(backend, FAKE_TRACE_QUEUE_SUBMIT);
    return 0;
}

static int fake_close(void* session, uint64_t command_list)
{
    FakeBackend* backend = session;
    assert(command_list != 0u);
    fake_trace_event(backend, FAKE_TRACE_CLOSE_COMMAND_LIST);
    return 0;
}

static int fake_create_fence(void* session, uint64_t initial_value,
                             uint64_t* fence_out)
{
    FakeBackend* backend = session;
    assert(initial_value == 0u);
    backend->fence = ++backend->next_handle;
    fake_trace_event(backend, FAKE_TRACE_CREATE_FENCE);
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
    fake_trace_event(backend, FAKE_TRACE_SUBMIT_FENCED);
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
    fake_trace_event(backend, FAKE_TRACE_WAIT_FENCE);
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
    const uint16_t expected_rgba4[2] = { UINT16_C(0xf00f), UINT16_C(0x0f08) };
    assert((image == 700u || image == 701u || image == 702u) && readback != NULL &&
           destination != NULL);
    assert(readback->y == 2u);
    if (readback->x == 0u) {
        assert(image == 700u);
        assert(readback->width == 1u && readback->height == 1u);
        assert(readback->destination_row_pitch_bytes == 4u);
        assert(destination_size == 4u);
    } else if (readback->width == 1u && readback->height == 2u) {
        assert(readback->x == 1u);
        assert(image == 700u);
        assert(readback->destination_row_pitch_bytes == 4u);
        assert(destination_size == sizeof(expected_bgra));
    } else if (image == 702u) {
        assert(readback->x == 1u);
        assert(readback->width == 2u && readback->height == 1u);
        assert(readback->destination_row_pitch_bytes == sizeof(expected_rgba4));
        assert(destination_size == sizeof(expected_rgba4));
    } else {
        assert(readback->x == 1u);
        assert(readback->width == 2u && readback->height == 1u);
        assert(readback->destination_row_pitch_bytes == 8u);
        assert(destination_size == sizeof(expected_bgra));
    }
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
    if (image == 702u)
        memcpy(destination, expected_rgba4, sizeof(expected_rgba4));
    else if (readback->x == 0u)
        memcpy(destination, expected_bgra, 4u);
    else
        memcpy(destination, image == 700u ? expected_bgra : expected_rgba,
               sizeof(expected_bgra));
    backend->readbacks++;
    fake_trace_event(backend, FAKE_TRACE_READBACK);
    return 0;
}

static void copy_to_packed_mip_texture(RinGLContext* context,
                                       uint32_t internal_format,
                                       uint32_t format, uint32_t type,
                                       uint16_t first_expected,
                                       uint16_t second_expected)
{
    uint32_t texture = 0u;
    uint16_t base_pixels[8] = {0u};
    uint16_t level_pixels[2] = {0u};
    RinGLTextureObject* object;
    RinGLTextureMipStorage* level;
    uint16_t copied[2] = {0u};

    ringl_gen_textures(1, &texture);
    ringl_bind_texture(RINGL_TEXTURE_2D, texture);
    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, internal_format, 4, 2, 0,
                       format, type, base_pixels);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_tex_image_2d(RINGL_TEXTURE_2D, 1, internal_format, 2, 1, 0,
                       format, type, level_pixels);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_copy_tex_sub_image_2d(RINGL_TEXTURE_2D, 1, 0, 0, 1, 2, 2, 1);
    assert(ringl_get_error() == RINGL_NO_ERROR);

    object = &context->textures[ringl_object_slot_index(texture)];
    level = &object->mip_storage[0];
    assert(level->defined == RINGL_TRUE && level->generated == RINGL_FALSE);
    assert(level->width == 2u && level->height == 1u);
    memcpy(copied, level->shadow_bytes, sizeof(copied));
    assert(copied[0] == first_expected && copied[1] == second_expected);
}

static void copy_to_native_texture(RinGLContext* context,
                                   uint32_t internal_format,
                                   const void* expected,
                                   uint64_t expected_size)
{
    uint32_t texture = 0u;
    RinGLTextureObject* object;

    ringl_gen_textures(1, &texture);
    ringl_bind_texture(RINGL_TEXTURE_2D, texture);
    ringl_copy_tex_image_2d(RINGL_TEXTURE_2D, 0, internal_format, 1, 2,
                            2, 1, 0);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    object = &context->textures[ringl_object_slot_index(texture)];
    assert(object->defined == RINGL_TRUE &&
           object->format == internal_format &&
           object->shadow_size == expected_size &&
           memcmp(object->shadow_bytes, expected, (size_t)expected_size) == 0);
}

static void copy_to_canonical_color_texture(RinGLContext* context,
                                            uint32_t internal_format,
                                            const uint8_t expected_pixels[8])
{
    uint32_t texture = 0u;
    RinGLTextureObject* object;

    ringl_gen_textures(1, &texture);
    ringl_bind_texture(RINGL_TEXTURE_2D, texture);
    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, internal_format, 2, 1, 0,
                       internal_format, RINGL_UNSIGNED_BYTE, NULL);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_copy_tex_sub_image_2d(RINGL_TEXTURE_2D, 0, 0, 0, 1, 2, 2, 1);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    object = &context->textures[ringl_object_slot_index(texture)];
    assert(object->format == internal_format &&
           memcmp(object->shadow_bytes, expected_pixels, 8u) == 0);
}

static void copy_to_packed_mip_definition(RinGLContext* context,
                                          uint32_t base_internal_format,
                                          uint32_t format, uint32_t type,
                                          uint32_t copy_internal_format,
                                          uint16_t first_expected,
                                          uint16_t second_expected)
{
    uint32_t texture = 0u;
    uint16_t base_pixels[8] = {0u};
    RinGLTextureObject* object;
    RinGLTextureMipStorage* level;
    uint16_t copied[2] = {0u};

    ringl_gen_textures(1, &texture);
    ringl_bind_texture(RINGL_TEXTURE_2D, texture);
    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, base_internal_format, 4, 2, 0,
                       format, type, base_pixels);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_copy_tex_image_2d(RINGL_TEXTURE_2D, 1, copy_internal_format, 1, 2,
                            2, 1, 0);
    assert(ringl_get_error() == RINGL_NO_ERROR);

    object = &context->textures[ringl_object_slot_index(texture)];
    level = &object->mip_storage[0];
    assert(level->defined == RINGL_TRUE && level->generated == RINGL_FALSE &&
           level->width == 2u && level->height == 1u);
    memcpy(copied, level->shadow_bytes, sizeof(copied));
    assert(copied[0] == first_expected && copied[1] == second_expected);
}

static void verify_full_submission_ordering(void)
{
    static const uint32_t expected_trace[] = {
        FAKE_TRACE_CREATE_COMMAND_LIST,
        FAKE_TRACE_TRANSITION_IMAGE,
        FAKE_TRACE_BEGIN_RENDER_PASS,
        FAKE_TRACE_END_RENDER_PASS,
        FAKE_TRACE_CLOSE_COMMAND_LIST,
        FAKE_TRACE_QUEUE_SUBMIT,
        FAKE_TRACE_RESET_COMMAND_LIST,
        FAKE_TRACE_TRANSITION_IMAGE,
        FAKE_TRACE_CREATE_FENCE,
        FAKE_TRACE_CLOSE_COMMAND_LIST,
        FAKE_TRACE_SUBMIT_FENCED,
        FAKE_TRACE_WAIT_FENCE,
        FAKE_TRACE_READBACK,
        FAKE_TRACE_RESET_COMMAND_LIST,
        FAKE_TRACE_TRANSITION_IMAGE,
        FAKE_TRACE_BEGIN_RENDER_PASS,
        FAKE_TRACE_END_RENDER_PASS,
        FAKE_TRACE_CLOSE_COMMAND_LIST,
        FAKE_TRACE_QUEUE_SUBMIT,
        FAKE_TRACE_RESET_COMMAND_LIST,
        FAKE_TRACE_CLOSE_COMMAND_LIST,
        FAKE_TRACE_SUBMIT_FENCED,
        FAKE_TRACE_WAIT_FENCE,
        FAKE_TRACE_RESET_COMMAND_LIST,
        FAKE_TRACE_TRANSITION_IMAGE,
        FAKE_TRACE_CLOSE_COMMAND_LIST,
        FAKE_TRACE_SUBMIT_FENCED,
        FAKE_TRACE_WAIT_FENCE,
        FAKE_TRACE_READBACK,
    };
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
    uint8_t pixels[8] = {0u};
    uint32_t event_index;
    uint32_t trace_count_before_flush;

    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_context_set_sync_ops(context, &sync_ops) == 0);
    assert(ringl_make_current(context) == 0);
    assert(ringl_set_default_framebuffer(&framebuffer) == 0);
    backend.trace_enabled = 1u;

    ringl_clear(RINGL_COLOR_BUFFER_BIT);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_gen_textures(1, &texture);
    ringl_bind_texture(RINGL_TEXTURE_2D, texture);
    ringl_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_RGBA, 2, 1, 0,
                       RINGL_RGBA, RINGL_UNSIGNED_BYTE, NULL);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_copy_tex_sub_image_2d(RINGL_TEXTURE_2D, 0, 0, 0, 1, 2, 2, 1);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_clear(RINGL_COLOR_BUFFER_BIT);
    assert(ringl_get_error() == RINGL_NO_ERROR);

    trace_count_before_flush = backend.trace_event_count;
    ringl_flush();
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(backend.trace_event_count == trace_count_before_flush);
    ringl_finish();
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_read_pixels_to_bytes(1, 2, 2, 1, RINGL_RGBA,
                               RINGL_UNSIGNED_BYTE, pixels, sizeof(pixels));
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(pixels[0] == 10u && pixels[1] == 20u && pixels[2] == 30u &&
           pixels[3] == 255u && pixels[4] == 40u && pixels[5] == 50u &&
           pixels[6] == 60u && pixels[7] == 128u);
    assert(backend.trace_event_count ==
           sizeof(expected_trace) / sizeof(expected_trace[0]));
    for (event_index = 0u; event_index < backend.trace_event_count;
         ++event_index) {
        assert(backend.trace_events[event_index] == expected_trace[event_index]);
    }

    ringl_context_destroy(context);
}

int main(void)
{
    verify_full_submission_ordering();

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
    uint32_t packed_renderbuffer = 0u;
    uint32_t packed_framebuffer = 0u;
    uint32_t float_renderbuffer = 0u;
    uint32_t float_framebuffer = 0u;
    uint32_t depth_renderbuffer = 0u;
    uint32_t texture_before_loss;
    uint32_t submits_before_loss;
    uint32_t buffer = 0u;
    uint32_t index;
    uint32_t pack_readbacks_before;
    uint32_t implementation_read_format = 0u;
    uint32_t implementation_read_type = 0u;
    const uint8_t buffer_data[4] = {1u, 2u, 3u, 4u};
    FakeBackend command_loss_backend = {0};
    uint8_t pixels[8] = {0};
    uint8_t fbo_pixels[8] = {0};
    uint8_t packed_pixels[12] = {0};
    uint8_t clipped_pixels[8] = {0};
    uint8_t invalid_float_pixels[16] = {0};
    const uint8_t expected_rgba[8] = {
        10u, 20u, 30u, 255u,
        40u, 50u, 60u, 128u,
    };
    const uint8_t expected_packed_rgba[8] = {
        255u, 0u, 0u, 255u,
        0u, 255u, 0u, 136u,
    };

    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_context_set_sync_ops(context, &sync_ops) == 0);
    assert(ringl_make_current(context) == 0);
    assert(ringl_set_default_framebuffer(&framebuffer) == 0);
    assert(ringl_get_implementation_color_read_format_type(
               &implementation_read_format, &implementation_read_type) == 0);
    assert(implementation_read_format == RINGL_RGBA);
    assert(implementation_read_type == RINGL_UNSIGNED_BYTE);

    ringl_flush();
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_finish();
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(backend.submits == 1u && backend.waits == 1u);

    memset(pixels, 0xa5, sizeof(pixels));
    ringl_read_pixels_to_bytes(1, 2, 2, 1, RINGL_RGBA,
                               RINGL_UNSIGNED_BYTE, pixels,
                               sizeof(pixels) - 1u);
    assert(ringl_get_error() == RINGL_INVALID_OPERATION);
    assert(backend.transitions == 0u && backend.submits == 1u &&
           backend.waits == 1u && backend.readbacks == 0u);
    for (index = 0u; index < sizeof(pixels); ++index)
        assert(pixels[index] == 0xa5u);

    ringl_read_pixels_to_bytes(1, 2, 2, 1, RINGL_RGBA,
                               RINGL_UNSIGNED_BYTE, pixels, sizeof(pixels));
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(backend.transitions == 1u);
    assert(backend.submits == 2u && backend.waits == 2u);
    assert(backend.readbacks == 1u);
    assert(memcmp(pixels, expected_rgba, sizeof(pixels)) == 0);

    memset(invalid_float_pixels, 0xa5, sizeof(invalid_float_pixels));
    ringl_read_pixels_to_bytes(1, 2, 1, 1, RINGL_RGBA, RINGL_FLOAT,
                               invalid_float_pixels,
                               sizeof(invalid_float_pixels));
    assert(ringl_get_error() == RINGL_INVALID_OPERATION);
    assert(backend.readbacks == 1u);
    for (index = 0u; index < sizeof(invalid_float_pixels); ++index)
        assert(invalid_float_pixels[index] == 0xa5u);

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

    ringl_gen_renderbuffers(1, &packed_renderbuffer);
    ringl_bind_renderbuffer(RINGL_RENDERBUFFER, packed_renderbuffer);
    ringl_renderbuffer_storage(RINGL_RENDERBUFFER, RINGL_RGBA4, 8, 8);
    ringl_gen_framebuffers(1, &packed_framebuffer);
    ringl_bind_framebuffer(RINGL_FRAMEBUFFER, packed_framebuffer);
    ringl_framebuffer_renderbuffer(RINGL_FRAMEBUFFER, RINGL_COLOR_ATTACHMENT0,
                                   RINGL_RENDERBUFFER, packed_renderbuffer);
    assert(ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) ==
           RINGL_FRAMEBUFFER_COMPLETE);
    ringl_clear(RINGL_COLOR_BUFFER_BIT);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_copy_tex_image_2d(RINGL_TEXTURE_2D, 0, RINGL_RGBA, 1, 2, 2, 1, 0);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(backend.transitions == 5u);
    assert(backend.submits == 8u && backend.waits == 8u);
    assert(backend.readbacks == 6u);
    assert(memcmp(context->textures[ringl_object_slot_index(copied_texture)].shadow_bytes,
                  expected_packed_rgba, sizeof(expected_packed_rgba)) == 0);

    {
        const uint8_t expected_rgb[8] = {
            255u, 0u, 0u, 255u,
            0u, 255u, 0u, 255u,
        };
        const uint16_t expected_rgb565[2] = {
            UINT16_C(0xf800), UINT16_C(0x07e0),
        };
        const uint16_t expected_rgba4[2] = {
            UINT16_C(0xf00f), UINT16_C(0x0f08),
        };
        const uint16_t expected_rgb5_a1[2] = {
            UINT16_C(0xf801), UINT16_C(0x07c1),
        };

        /* copyTexImage2D defines native storage only after the RGBA snapshot
         * succeeds. RGB canonicalizes alpha to one; packed formats quantize
         * the same snapshot directly rather than round-tripping through RGBA. */
        copy_to_native_texture(context, RINGL_RGB, expected_rgb,
                               sizeof(expected_rgb));
        copy_to_native_texture(context, RINGL_RGB565, expected_rgb565,
                               sizeof(expected_rgb565));
        copy_to_native_texture(context, RINGL_RGBA4, expected_rgba4,
                               sizeof(expected_rgba4));
        copy_to_native_texture(context, RINGL_RGB5_A1, expected_rgb5_a1,
                               sizeof(expected_rgb5_a1));
    }
    assert(backend.transitions == 5u);
    assert(backend.submits == 12u && backend.waits == 12u);
    assert(backend.readbacks == 10u);

    /* The canonical color formats have different component rules despite
     * sharing the same four-byte RinGPU storage. copyTexSubImage2D must apply
     * them after the source RGBA snapshot, not forward its raw channels. */
    {
        const uint8_t expected_rgb[8] = {
            255u, 0u, 0u, 255u,
            0u, 255u, 0u, 255u,
        };
        const uint8_t expected_alpha[8] = {
            0u, 0u, 0u, 255u,
            0u, 0u, 0u, 136u,
        };
        const uint8_t expected_luminance[8] = {
            255u, 255u, 255u, 255u,
            0u, 0u, 0u, 255u,
        };
        const uint8_t expected_luminance_alpha[8] = {
            255u, 255u, 255u, 255u,
            0u, 0u, 0u, 136u,
        };

        copy_to_canonical_color_texture(context, RINGL_RGB, expected_rgb);
        copy_to_canonical_color_texture(context, RINGL_ALPHA, expected_alpha);
        copy_to_canonical_color_texture(context, RINGL_LUMINANCE,
                                        expected_luminance);
        copy_to_canonical_color_texture(context, RINGL_LUMINANCE_ALPHA,
                                        expected_luminance_alpha);
    }
    assert(backend.transitions == 5u);
    assert(backend.submits == 16u && backend.waits == 16u);
    assert(backend.readbacks == 14u);

    /* A nonzero copy definition creates an explicit same-format mip only
     * after its fenced snapshot succeeds. It must not fall back to base-level
     * storage or normalize the native packed bytes. */
    copy_to_packed_mip_definition(context, RINGL_RGB, RINGL_RGB,
                                  RINGL_UNSIGNED_SHORT_5_6_5,
                                  RINGL_RGB565,
                                  UINT16_C(0xf800), UINT16_C(0x07e0));
    copy_to_packed_mip_definition(context, RINGL_RGBA, RINGL_RGBA,
                                  RINGL_UNSIGNED_SHORT_4_4_4_4,
                                  RINGL_RGBA4,
                                  UINT16_C(0xf00f), UINT16_C(0x0f08));
    copy_to_packed_mip_definition(context, RINGL_RGBA, RINGL_RGBA,
                                  RINGL_UNSIGNED_SHORT_5_5_5_1,
                                  RINGL_RGB5_A1,
                                  UINT16_C(0xf801), UINT16_C(0x07c1));
    assert(backend.transitions == 5u);
    assert(backend.submits == 19u && backend.waits == 19u);
    assert(backend.readbacks == 17u);

    /* The current source FBO is native RGBA4. Each destination is an explicit
     * level-one packed image, so this also exercises canonical readback before
     * direct native-precision conversion. */
    copy_to_packed_mip_texture(context, RINGL_RGB, RINGL_RGB,
                               RINGL_UNSIGNED_SHORT_5_6_5, UINT16_C(0xf800),
                               UINT16_C(0x07e0));
    copy_to_packed_mip_texture(context, RINGL_RGBA, RINGL_RGBA,
                               RINGL_UNSIGNED_SHORT_4_4_4_4, UINT16_C(0xf00f),
                               UINT16_C(0x0f08));
    copy_to_packed_mip_texture(context, RINGL_RGBA, RINGL_RGBA,
                               RINGL_UNSIGNED_SHORT_5_5_5_1, UINT16_C(0xf801),
                               UINT16_C(0x07c1));
    assert(backend.transitions == 5u);
    assert(backend.submits == 22u && backend.waits == 22u);
    assert(backend.readbacks == 20u);

    ringl_gen_renderbuffers(1, &depth_renderbuffer);
    ringl_bind_renderbuffer(RINGL_RENDERBUFFER, depth_renderbuffer);
    ringl_renderbuffer_storage(RINGL_RENDERBUFFER, RINGL_DEPTH_COMPONENT32F,
                               4, 4);
    ringl_framebuffer_renderbuffer(RINGL_FRAMEBUFFER, RINGL_DEPTH_ATTACHMENT,
                                   RINGL_RENDERBUFFER, depth_renderbuffer);
    assert(ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) ==
           RINGL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT);
    ringl_copy_tex_sub_image_2d(RINGL_TEXTURE_2D, 0, 0, 0, 1, 2, 2, 1);
    assert(ringl_get_error() == RINGL_INVALID_FRAMEBUFFER_OPERATION);
    assert(backend.transitions == 5u && backend.readbacks == 20u);
    assert(memcmp(context->textures[ringl_object_slot_index(copied_texture)].shadow_bytes,
                  expected_packed_rgba, sizeof(expected_packed_rgba)) == 0);

    ringl_gen_renderbuffers(1, &float_renderbuffer);
    ringl_bind_renderbuffer(RINGL_RENDERBUFFER, float_renderbuffer);
    ringl_renderbuffer_storage(RINGL_RENDERBUFFER, RINGL_RGBA32F, 8, 8);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_gen_framebuffers(1, &float_framebuffer);
    ringl_bind_framebuffer(RINGL_FRAMEBUFFER, float_framebuffer);
    ringl_framebuffer_renderbuffer(RINGL_FRAMEBUFFER, RINGL_COLOR_ATTACHMENT0,
                                   RINGL_RENDERBUFFER, float_renderbuffer);
    assert(ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) ==
           RINGL_FRAMEBUFFER_COMPLETE);
    assert(ringl_get_implementation_color_read_format_type(
               &implementation_read_format, &implementation_read_type) == 0);
    assert(implementation_read_format == RINGL_RGBA);
    assert(implementation_read_type == RINGL_FLOAT);

    ringl_bind_framebuffer(RINGL_FRAMEBUFFER, 0u);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    pack_readbacks_before = backend.readbacks;
    memset(clipped_pixels, 0xa5, sizeof(clipped_pixels));
    ringl_read_pixels_to_bytes(-1, 2, 2, 1, RINGL_RGBA,
                               RINGL_UNSIGNED_BYTE, clipped_pixels,
                               sizeof(clipped_pixels));
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(backend.readbacks == pack_readbacks_before + 1u);
    for (index = 0u; index < 4u; ++index)
        assert(clipped_pixels[index] == 0xa5u);
    assert(memcmp(clipped_pixels + 4u, expected_rgba, 4u) == 0);

    memset(clipped_pixels, 0xa5, sizeof(clipped_pixels));
    ringl_read_pixels_to_bytes(-9, 2, 1, 1, RINGL_RGBA,
                               RINGL_UNSIGNED_BYTE, clipped_pixels,
                               4u);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(backend.readbacks == pack_readbacks_before + 1u);
    for (index = 0u; index < sizeof(clipped_pixels); ++index)
        assert(clipped_pixels[index] == 0xa5u);

    ringl_pixel_storei(RINGL_PACK_ALIGNMENT, 8);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    pack_readbacks_before = backend.readbacks;
    memset(packed_pixels, 0xa5, sizeof(packed_pixels));
    ringl_read_pixels_to_bytes(1, 2, 1, 2, RINGL_RGBA,
                               RINGL_UNSIGNED_BYTE, packed_pixels,
                               sizeof(packed_pixels) - 1u);
    assert(ringl_get_error() == RINGL_INVALID_OPERATION);
    assert(backend.readbacks == pack_readbacks_before);
    for (index = 0u; index < sizeof(packed_pixels); ++index)
        assert(packed_pixels[index] == 0xa5u);

    ringl_read_pixels_to_bytes(1, 2, 1, 2, RINGL_RGBA,
                               RINGL_UNSIGNED_BYTE, packed_pixels,
                               sizeof(packed_pixels));
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(backend.readbacks == pack_readbacks_before + 1u);
    assert(memcmp(packed_pixels, expected_rgba, 4u) == 0);
    for (index = 4u; index < 8u; ++index)
        assert(packed_pixels[index] == 0xa5u);
    assert(memcmp(packed_pixels + 8u, expected_rgba + 4u, 4u) == 0);

    memset(packed_pixels, 0xa5, sizeof(packed_pixels));
    backend.fail_next_readback = 1u;
    ringl_read_pixels_to_bytes(1, 2, 1, 2, RINGL_RGBA,
                               RINGL_UNSIGNED_BYTE, packed_pixels,
                               sizeof(packed_pixels));
    assert(ringl_get_error() == RINGL_INVALID_OPERATION);
    assert(backend.readbacks == pack_readbacks_before + 1u);
    assert(backend.failed_readbacks == 2u);
    for (index = 0u; index < sizeof(packed_pixels); ++index)
        assert(packed_pixels[index] == 0xa5u);
    ringl_pixel_storei(RINGL_PACK_ALIGNMENT, 4);
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
    assert(backend.destroys == 5u); /* three color images + command list + fence */

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
