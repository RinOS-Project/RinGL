/* SPDX-License-Identifier: MIT */
#include "../ringl_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RINGL_RIN_GPU_FORMAT_BGRA8_UNORM 3u

static int sync_ops_valid(const RinGLRinGpuSyncOpsV1* ops)
{
    const size_t minimum_size = offsetof(RinGLRinGpuSyncOpsV1,
                                         readback_image_2d);

    if (ops == NULL)
        return 1;
    if (ops->struct_size < minimum_size ||
        ops->api_version != RINGL_SYNC_API_VERSION ||
        ops->create_fence == NULL || ops->queue_submit_fenced == NULL ||
        ops->wait_fence == NULL)
        return 0;
    return 1;
}

int ringl_context_set_sync_ops(RinGLContext* context,
                               const RinGLRinGpuSyncOpsV1* ops)
{
    size_t copy_size;

    if (context == NULL || context->magic != RINGL_CONTEXT_MAGIC ||
        context->lost ||
        !sync_ops_valid(ops))
        return -1;

    if (context->finish_fence != 0u) {
        ringl_backend_destroy_object(context, context->finish_fence);
        context->finish_fence = 0u;
        context->finish_value = 0u;
    }
    memset(&context->sync_ops, 0, sizeof(context->sync_ops));
    context->has_sync_ops = 0;
    if (ops == NULL)
        return 0;

    copy_size = ops->struct_size;
    if (copy_size > sizeof(context->sync_ops))
        copy_size = sizeof(context->sync_ops);
    memcpy(&context->sync_ops, ops, copy_size);
    context->has_sync_ops = 1;
    return 0;
}

static int ensure_finish_fence(RinGLContext* context)
{
    uint64_t fence = 0u;
    int result;

    if (context->finish_fence != 0u)
        return 0;
    if (!context->has_sync_ops || context->sync_ops.create_fence == NULL)
        return -1;
    result = context->sync_ops.create_fence(context->ringpu.session, 0u, &fence);
    if (result == RINGL_RIN_GPU_ERROR_DEVICE_LOST)
        ringl_context_mark_lost(context);
    if (result != 0 || fence == 0u)
        return -1;
    context->finish_fence = fence;
    return 0;
}

static int prepare_empty_command_list(RinGLContext* context,
                                      uint64_t* command_list_out)
{
    if (context == NULL || context->lost || command_list_out == NULL ||
        !context->has_ringpu_ops || context->ringpu.graphics_queue == 0u)
        return -1;

    if (context->graphics_command_list == 0u) {
        if (ringl_backend_create_command_list(
                context, RINGL_RIN_GPU_QUEUE_GRAPHICS,
                &context->graphics_command_list) != 0 ||
            context->graphics_command_list == 0u)
            return -1;
    } else if (ringl_backend_reset_command_list(
                   context, context->graphics_command_list) != 0) {
        return -1;
    }
    *command_list_out = context->graphics_command_list;
    return 0;
}

static int submit_and_wait(RinGLContext* context, uint64_t command_list)
{
    uint64_t value;
    int result;

    if (context == NULL || context->lost || command_list == 0u ||
        !context->has_sync_ops ||
        context->sync_ops.queue_submit_fenced == NULL ||
        context->sync_ops.wait_fence == NULL || ensure_finish_fence(context) != 0)
        return -1;
    if (context->finish_value == UINT64_MAX)
        return -1;
    value = ++context->finish_value;
    if (ringl_backend_close_command_list(context, command_list) != 0)
        return -1;
    result = context->sync_ops.queue_submit_fenced(
        context->ringpu.session, context->ringpu.graphics_queue,
        command_list, context->finish_fence, value);
    if (result == RINGL_RIN_GPU_ERROR_DEVICE_LOST)
        ringl_context_mark_lost(context);
    if (result != 0)
        return -1;
    result = context->sync_ops.wait_fence(context->ringpu.session,
                                          context->finish_fence, value,
                                          RINGL_TIMEOUT_INFINITE);
    if (result == RINGL_RIN_GPU_ERROR_DEVICE_LOST)
        ringl_context_mark_lost(context);
    if (result != 0)
        return -1;
    return 0;
}

void ringl_flush(void)
{
    /* RinGL currently submits clear/draw/present operations immediately.
     * There is therefore no deferred command buffer to flush here. */
}

void ringl_finish(void)
{
    RinGLContext* context = ringl_get_current_context();
    uint64_t command_list;

    if (context == NULL)
        return;
    if (!context->has_ringpu || !context->has_sync_ops ||
        prepare_empty_command_list(context, &command_list) != 0 ||
        submit_and_wait(context, command_list) != 0) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
    }
}

static void swizzle_bgra_to_rgba(uint8_t* bytes, uint64_t pixel_count)
{
    uint64_t index;

    for (index = 0u; index < pixel_count; ++index) {
        uint8_t* pixel = bytes + index * 4u;
        uint8_t red = pixel[2];
        pixel[2] = pixel[0];
        pixel[0] = red;
    }
}

static void unpack_rgb565_to_rgba(const uint8_t* source, uint8_t* destination,
                                  uint64_t pixel_count)
{
    uint64_t index;

    for (index = 0u; index < pixel_count; ++index) {
        uint16_t packed;
        uint8_t* pixel = destination + index * 4u;

        memcpy(&packed, source + index * sizeof(packed), sizeof(packed));
        pixel[0] = (uint8_t)((((packed >> 11u) & 0x1fu) * 255u + 15u) / 31u);
        pixel[1] = (uint8_t)((((packed >> 5u) & 0x3fu) * 255u + 31u) / 63u);
        pixel[2] = (uint8_t)(((packed & 0x1fu) * 255u + 15u) / 31u);
        pixel[3] = UINT8_MAX;
    }
}

static void unpack_rgba4_to_rgba(const uint8_t* source, uint8_t* destination,
                                 uint64_t pixel_count)
{
    uint64_t index;

    for (index = 0u; index < pixel_count; ++index) {
        uint16_t packed;
        uint8_t* pixel = destination + index * 4u;

        memcpy(&packed, source + index * sizeof(packed), sizeof(packed));
        pixel[0] = (uint8_t)((((packed >> 12u) & 0xfu) * 255u + 7u) / 15u);
        pixel[1] = (uint8_t)((((packed >> 8u) & 0xfu) * 255u + 7u) / 15u);
        pixel[2] = (uint8_t)((((packed >> 4u) & 0xfu) * 255u + 7u) / 15u);
        pixel[3] = (uint8_t)(((packed & 0xfu) * 255u + 7u) / 15u);
    }
}

static void unpack_rgb5_a1_to_rgba(const uint8_t* source, uint8_t* destination,
                                   uint64_t pixel_count)
{
    uint64_t index;

    for (index = 0u; index < pixel_count; ++index) {
        uint16_t packed;
        uint8_t* pixel = destination + index * 4u;

        memcpy(&packed, source + index * sizeof(packed), sizeof(packed));
        pixel[0] = (uint8_t)((((packed >> 11u) & 0x1fu) * 255u + 15u) / 31u);
        pixel[1] = (uint8_t)((((packed >> 6u) & 0x1fu) * 255u + 15u) / 31u);
        pixel[2] = (uint8_t)((((packed >> 1u) & 0x1fu) * 255u + 15u) / 31u);
        pixel[3] = (packed & 1u) != 0u ? UINT8_MAX : 0u;
    }
}

static int packed_color_format(uint32_t format)
{
    return format == RINGL_RIN_GPU_FORMAT_RGB565_UNORM ||
           format == RINGL_RIN_GPU_FORMAT_RGBA4_UNORM ||
           format == RINGL_RIN_GPU_FORMAT_RGB5_A1_UNORM;
}

int ringl_read_color_target_rgba(RinGLContext* context, int32_t x, int32_t y,
                                 int32_t width, int32_t height, void* pixels)
{
    RinGLRinGpuImageReadback2DV1 readback;
    uint64_t command_list;
    uint64_t row_bytes;
    uint64_t total_bytes;
    uint64_t native_row_bytes;
    uint64_t native_total_bytes;
    uint8_t* native_pixels = NULL;
    RinGLColorTarget target;
    uint32_t old_state;
    int result;

    if (context == NULL || context->lost)
        return -1;
    if (width < 0 || height < 0) {
        return -1;
    }
    if (width == 0 || height == 0)
        return 0;
    if (!context->has_sync_ops || context->sync_ops.readback_image_2d == NULL ||
        pixels == NULL ||
        (context->framebuffer_binding != 0u &&
         ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) !=
             RINGL_FRAMEBUFFER_COMPLETE) ||
        ringl_resolve_color_target(context, &target) != 0 ||
        x < 0 || y < 0 ||
        (uint64_t)(uint32_t)x + (uint64_t)(uint32_t)width >
            target.width ||
        (uint64_t)(uint32_t)y + (uint64_t)(uint32_t)height >
            target.height) {
        return -1;
    }
    row_bytes = (uint64_t)(uint32_t)width * 4u;
    total_bytes = row_bytes * (uint64_t)(uint32_t)height;
    if (packed_color_format(target.format)) {
        native_row_bytes = (uint64_t)(uint32_t)width * sizeof(uint16_t);
        if ((uint64_t)(uint32_t)height > UINT64_MAX / native_row_bytes)
            return -1;
        native_total_bytes = native_row_bytes * (uint64_t)(uint32_t)height;
        if (native_total_bytes > SIZE_MAX)
            return -1;
        native_pixels = malloc((size_t)native_total_bytes);
        if (native_pixels == NULL)
            return -1;
    } else if (target.format == RINGL_RIN_GPU_FORMAT_RGBA8_UNORM ||
               target.format == RINGL_RIN_GPU_FORMAT_BGRA8_UNORM) {
        native_row_bytes = row_bytes;
        native_total_bytes = total_bytes;
    } else {
        return -1;
    }

    old_state = *target.state;
    if (old_state == 0u ||
        prepare_empty_command_list(context, &command_list) != 0 ||
        (old_state != RINGL_RIN_GPU_IMAGE_COPY_SOURCE &&
         ringl_backend_transition_image(context, command_list, target.image,
                                        old_state,
                                        RINGL_RIN_GPU_IMAGE_COPY_SOURCE) != 0) ||
        submit_and_wait(context, command_list) != 0) {
        free(native_pixels);
        return -1;
    }
    *target.state = RINGL_RIN_GPU_IMAGE_COPY_SOURCE;

    memset(&readback, 0, sizeof(readback));
    readback.x = (uint32_t)x;
    readback.y = (uint32_t)y;
    readback.width = (uint32_t)width;
    readback.height = (uint32_t)height;
    readback.destination_row_pitch_bytes = native_row_bytes;
    result = context->sync_ops.readback_image_2d(
        context->ringpu.session, target.image, &readback,
        native_pixels != NULL ? native_pixels : pixels, native_total_bytes);
    if (result == RINGL_RIN_GPU_ERROR_DEVICE_LOST) {
        ringl_context_mark_lost(context);
        free(native_pixels);
        return -1;
    }
    if (result != 0) {
        free(native_pixels);
        return -1;
    }

    if (target.format == RINGL_RIN_GPU_FORMAT_BGRA8_UNORM) {
        swizzle_bgra_to_rgba((uint8_t*)pixels,
                             (uint64_t)(uint32_t)width * (uint32_t)height);
    } else if (target.format == RINGL_RIN_GPU_FORMAT_RGB565_UNORM) {
        unpack_rgb565_to_rgba(native_pixels, pixels,
                              (uint64_t)(uint32_t)width * (uint32_t)height);
    } else if (target.format == RINGL_RIN_GPU_FORMAT_RGBA4_UNORM) {
        unpack_rgba4_to_rgba(native_pixels, pixels,
                             (uint64_t)(uint32_t)width * (uint32_t)height);
    } else if (target.format == RINGL_RIN_GPU_FORMAT_RGB5_A1_UNORM) {
        unpack_rgb5_a1_to_rgba(native_pixels, pixels,
                               (uint64_t)(uint32_t)width * (uint32_t)height);
    }
    free(native_pixels);
    return 0;
}

static int readback_required_bytes(int32_t width, int32_t height,
                                   uint64_t* bytes_out)
{
    uint64_t row_bytes;

    if (bytes_out == NULL || width < 0 || height < 0)
        return -1;
    if (width == 0 || height == 0) {
        *bytes_out = 0u;
        return 0;
    }

    row_bytes = (uint64_t)(uint32_t)width * 4u;
    if ((uint64_t)(uint32_t)height > UINT64_MAX / row_bytes)
        return -1;
    *bytes_out = row_bytes * (uint64_t)(uint32_t)height;
    return 0;
}

void ringl_read_pixels(int32_t x, int32_t y,
                       int32_t width, int32_t height,
                       uint32_t format, uint32_t type,
                       void* pixels)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL)
        return;
    if (format != RINGL_RGBA || type != RINGL_UNSIGNED_BYTE) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (width < 0 || height < 0) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (ringl_read_color_target_rgba(context, x, y, width, height, pixels) != 0)
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
}

void ringl_read_pixels_to_bytes(int32_t x, int32_t y,
                                int32_t width, int32_t height,
                                uint32_t format, uint32_t type,
                                void* pixels, uint64_t pixels_size)
{
    RinGLContext* context = ringl_get_current_context();
    uint64_t required_bytes;

    if (context == NULL)
        return;
    if (format != RINGL_RGBA || type != RINGL_UNSIGNED_BYTE) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (readback_required_bytes(width, height, &required_bytes) != 0) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if ((required_bytes != 0u && pixels == NULL) ||
        pixels_size < required_bytes) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if (ringl_read_color_target_rgba(context, x, y, width, height, pixels) != 0)
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
}
