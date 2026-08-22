/* SPDX-License-Identifier: MIT */
#include "../ringl_internal.h"

#include <stddef.h>
#include <stdint.h>
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

    if (context->finish_fence != 0u)
        return 0;
    if (!context->has_sync_ops || context->sync_ops.create_fence == NULL)
        return -1;
    if (context->sync_ops.create_fence(context->ringpu.session, 0u, &fence) != 0 ||
        fence == 0u)
        return -1;
    context->finish_fence = fence;
    return 0;
}

static int prepare_empty_command_list(RinGLContext* context,
                                      uint64_t* command_list_out)
{
    if (context == NULL || command_list_out == NULL ||
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

    if (context == NULL || command_list == 0u || !context->has_sync_ops ||
        context->sync_ops.queue_submit_fenced == NULL ||
        context->sync_ops.wait_fence == NULL || ensure_finish_fence(context) != 0)
        return -1;
    if (context->finish_value == UINT64_MAX)
        return -1;
    value = ++context->finish_value;
    if (ringl_backend_close_command_list(context, command_list) != 0 ||
        context->sync_ops.queue_submit_fenced(
            context->ringpu.session, context->ringpu.graphics_queue,
            command_list, context->finish_fence, value) != 0 ||
        context->sync_ops.wait_fence(
            context->ringpu.session, context->finish_fence, value,
            RINGL_TIMEOUT_INFINITE) != 0)
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

void ringl_read_pixels(int32_t x, int32_t y,
                       int32_t width, int32_t height,
                       uint32_t format, uint32_t type,
                       void* pixels)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLRinGpuImageReadback2DV1 readback;
    uint64_t command_list;
    uint64_t row_bytes;
    uint64_t total_bytes;
    RinGLColorTarget target;
    uint32_t old_state;

    if (context == NULL)
        return;
    if (width < 0 || height < 0) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (format != RINGL_RGBA || type != RINGL_UNSIGNED_BYTE) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (width == 0 || height == 0)
        return;
    if (!context->has_sync_ops || context->sync_ops.readback_image_2d == NULL ||
        pixels == NULL || ringl_resolve_color_target(context, &target) != 0 ||
        x < 0 || y < 0 ||
        (uint64_t)(uint32_t)x + (uint64_t)(uint32_t)width >
            target.width ||
        (uint64_t)(uint32_t)y + (uint64_t)(uint32_t)height >
            target.height) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    row_bytes = (uint64_t)(uint32_t)width * 4u;
    total_bytes = row_bytes * (uint64_t)(uint32_t)height;

    old_state = *target.state;
    if (old_state == 0u ||
        prepare_empty_command_list(context, &command_list) != 0 ||
        (old_state != RINGL_RIN_GPU_IMAGE_COPY_SOURCE &&
         ringl_backend_transition_image(context, command_list, target.image,
                                        old_state,
                                        RINGL_RIN_GPU_IMAGE_COPY_SOURCE) != 0) ||
        submit_and_wait(context, command_list) != 0) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    *target.state = RINGL_RIN_GPU_IMAGE_COPY_SOURCE;

    memset(&readback, 0, sizeof(readback));
    readback.x = (uint32_t)x;
    readback.y = (uint32_t)y;
    readback.width = (uint32_t)width;
    readback.height = (uint32_t)height;
    readback.destination_row_pitch_bytes = row_bytes;
    if (context->sync_ops.readback_image_2d(
            context->ringpu.session, target.image,
            &readback, pixels, total_bytes) != 0) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }

    if (target.format == RINGL_RIN_GPU_FORMAT_BGRA8_UNORM) {
        swizzle_bgra_to_rgba((uint8_t*)pixels,
                             (uint64_t)(uint32_t)width * (uint32_t)height);
    } else if (target.format != RINGL_RIN_GPU_FORMAT_RGBA8_UNORM) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
    }
}
