/* SPDX-License-Identifier: MIT */
#include "../ringl_internal.h"

#include <float.h>
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

static int unpack_rgba16f_to_rgba(const uint8_t* source, void* destination,
                                  uint64_t pixel_count)
{
    uint64_t pixel_index;

    if (source == NULL || destination == NULL)
        return -1;
    /* Validate the complete native image before publishing Float32 output. */
    for (pixel_index = 0u; pixel_index < pixel_count; ++pixel_index) {
        uint32_t component;

        for (component = 0u; component < 4u; ++component) {
            uint16_t half;

            memcpy(&half, source + pixel_index * 4u * sizeof(half) +
                   component * sizeof(half), sizeof(half));
            if ((half & 0x7c00u) == 0x7c00u)
                return -1;
        }
    }
    for (pixel_index = 0u; pixel_index < pixel_count; ++pixel_index) {
        uint32_t component;

        for (component = 0u; component < 4u; ++component) {
            uint16_t half;
            uint32_t sign;
            uint32_t exponent;
            uint32_t mantissa;
            uint32_t bits;
            float value;

            memcpy(&half, source + pixel_index * 4u * sizeof(half) +
                   component * sizeof(half), sizeof(half));
            sign = ((uint32_t)half & 0x8000u) << 16u;
            exponent = ((uint32_t)half >> 10u) & 0x1fu;
            mantissa = (uint32_t)half & 0x03ffu;
            if (exponent == 0u) {
                if (mantissa == 0u) {
                    bits = sign;
                } else {
                    int32_t unbiased_exponent = -14;

                    while ((mantissa & 0x0400u) == 0u) {
                        mantissa <<= 1u;
                        --unbiased_exponent;
                    }
                    bits = sign |
                        ((uint32_t)(unbiased_exponent + 127) << 23u) |
                        ((mantissa & 0x03ffu) << 13u);
                }
            } else {
                bits = sign | ((exponent + 112u) << 23u) | (mantissa << 13u);
            }
            memcpy(&value, &bits, sizeof(value));
            memcpy((uint8_t*)destination +
                   (pixel_index * 4u + component) * sizeof(value),
                   &value, sizeof(value));
        }
    }
    return 0;
}

static int ringl_prepare_read_color_target(RinGLContext* context,
                                           RinGLColorTarget* target)
{
    if (context == NULL || context->lost || !context->has_sync_ops ||
        (context->framebuffer_binding != 0u &&
         ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) !=
             RINGL_FRAMEBUFFER_COMPLETE) ||
        ringl_resolve_color_target(context, target) != 0 ||
        (target->mip_level == 0u &&
         context->sync_ops.readback_image_2d == NULL) ||
        (target->mip_level != 0u &&
         context->sync_ops.readback_image_2d_mip_v2 == NULL)) {
        return -1;
    }
    return 0;
}

static int ringl_color_target_readback_type_supported(
    const RinGLColorTarget* target, uint32_t type)
{
    uint32_t format;

    if (target == NULL)
        return 0;
    format = target->format;
    if (target->srgb_encoding != 0u)
        return format == RINGL_RIN_GPU_FORMAT_RGBA32_FLOAT &&
               type == RINGL_UNSIGNED_BYTE;
    if (packed_color_format(format))
        return type == RINGL_UNSIGNED_BYTE;
    if (format == RINGL_RIN_GPU_FORMAT_RGBA32_FLOAT ||
        format == RINGL_RIN_GPU_FORMAT_RGBA16_FLOAT) {
        return type == RINGL_FLOAT;
    }
    return type == RINGL_UNSIGNED_BYTE &&
        (format == RINGL_RIN_GPU_FORMAT_RGBA8_UNORM ||
         format == RINGL_RIN_GPU_FORMAT_BGRA8_UNORM);
}

static int ringl_rgba_float_snapshot_is_finite(const void* pixels,
                                               uint64_t pixel_count)
{
    const uint8_t* bytes = pixels;
    uint64_t component;

    if (bytes == NULL || pixel_count > UINT64_MAX / 4u)
        return 0;
    for (component = 0u; component < pixel_count * 4u; ++component) {
        float value;

        memcpy(&value, bytes + component * sizeof(value), sizeof(value));
        if (value != value || value > FLT_MAX || value < -FLT_MAX)
            return 0;
    }
    return 1;
}

int ringl_get_implementation_color_read_format_type(uint32_t* format_out,
                                                     uint32_t* type_out)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLColorTarget target;
    uint32_t type;

    if (context == NULL || context->lost || format_out == NULL || type_out == NULL ||
        (context->framebuffer_binding != 0u &&
         ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) !=
             RINGL_FRAMEBUFFER_COMPLETE) ||
        ringl_resolve_color_target(context, &target) != 0) {
        if (context != NULL)
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    type = (target.format == RINGL_RIN_GPU_FORMAT_RGBA32_FLOAT ||
            target.format == RINGL_RIN_GPU_FORMAT_RGBA16_FLOAT) &&
            target.srgb_encoding == 0u
        ? RINGL_FLOAT
        : RINGL_UNSIGNED_BYTE;
    if (!ringl_color_target_readback_type_supported(&target, type)) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    *format_out = RINGL_RGBA;
    *type_out = type;
    return 0;
}

static uint8_t ringl_linear_alpha_to_unorm8(float value)
{
    if (!(value > 0.0f))
        return 0u;
    if (value >= 1.0f)
        return UINT8_MAX;
    return (uint8_t)(value * 255.0f + 0.5f);
}

static void ringl_encode_linear_rgba_to_srgb(const uint8_t* source,
                                               uint8_t* destination,
                                               uint64_t pixel_count,
                                               uint32_t has_alpha)
{
    uint64_t pixel;

    for (pixel = 0u; pixel < pixel_count; ++pixel) {
        float red;
        float green;
        float blue;
        float alpha;

        memcpy(&red, source + (pixel * 4u + 0u) * sizeof(float), sizeof(red));
        memcpy(&green, source + (pixel * 4u + 1u) * sizeof(float), sizeof(green));
        memcpy(&blue, source + (pixel * 4u + 2u) * sizeof(float), sizeof(blue));
        memcpy(&alpha, source + (pixel * 4u + 3u) * sizeof(float), sizeof(alpha));
        destination[pixel * 4u + 0u] = ringl_srgb_encode_float(red);
        destination[pixel * 4u + 1u] = ringl_srgb_encode_float(green);
        destination[pixel * 4u + 2u] = ringl_srgb_encode_float(blue);
        destination[pixel * 4u + 3u] = has_alpha != RINGL_FALSE
            ? ringl_linear_alpha_to_unorm8(alpha) : UINT8_MAX;
    }
}

static int ringl_read_color_target_to_type(RinGLContext* context, int32_t x,
                                           int32_t y, int32_t width,
                                           int32_t height, uint32_t type,
                                           void* pixels)
{
    RinGLRinGpuImageReadback2DV1 readback;
    RinGLRinGpuImageReadback2DMipV2 mip_readback;
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
    if (pixels == NULL || ringl_prepare_read_color_target(context, &target) != 0 ||
        x < 0 || y < 0 ||
        (uint64_t)(uint32_t)x + (uint64_t)(uint32_t)width >
            target.width ||
        (uint64_t)(uint32_t)y + (uint64_t)(uint32_t)height >
            target.height) {
        return -1;
    }
    if (type != RINGL_UNSIGNED_BYTE && type != RINGL_FLOAT)
        return -1;
    if (!ringl_color_target_readback_type_supported(&target, type))
        return -1;
    row_bytes = (uint64_t)(uint32_t)width *
        (type == RINGL_FLOAT ? 4u * sizeof(float) : 4u);
    if ((uint64_t)(uint32_t)height > UINT64_MAX / row_bytes)
        return -1;
    total_bytes = row_bytes * (uint64_t)(uint32_t)height;
    if (packed_color_format(target.format)) {
        native_row_bytes = (uint64_t)(uint32_t)width * sizeof(uint16_t);
        if ((uint64_t)(uint32_t)height > UINT64_MAX / native_row_bytes)
            return -1;
        native_total_bytes = native_row_bytes * (uint64_t)(uint32_t)height;
        if (native_total_bytes > SIZE_MAX)
            return -1;
        native_pixels = ringl_context_alloc_temporary(context, native_total_bytes);
        if (native_pixels == NULL)
            return -1;
    } else if (target.format == RINGL_RIN_GPU_FORMAT_RGBA32_FLOAT) {
        native_row_bytes = (uint64_t)(uint32_t)width * 4u * sizeof(float);
        if ((uint64_t)(uint32_t)height > UINT64_MAX / native_row_bytes)
            return -1;
        native_total_bytes = native_row_bytes * (uint64_t)(uint32_t)height;
        if (target.srgb_encoding != 0u) {
            if (native_total_bytes > SIZE_MAX)
                return -1;
            native_pixels = ringl_context_alloc_temporary(context, native_total_bytes);
            if (native_pixels == NULL)
                return -1;
        }
    } else if (target.format == RINGL_RIN_GPU_FORMAT_RGBA16_FLOAT) {
        native_row_bytes = (uint64_t)(uint32_t)width * 4u * sizeof(uint16_t);
        if ((uint64_t)(uint32_t)height > UINT64_MAX / native_row_bytes)
            return -1;
        native_total_bytes = native_row_bytes * (uint64_t)(uint32_t)height;
        if (native_total_bytes > SIZE_MAX)
            return -1;
        native_pixels = ringl_context_alloc_temporary(context, native_total_bytes);
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
         (target.mip_level == 0u
              ? ringl_backend_transition_image(
                    context, command_list, target.image, old_state,
                    RINGL_RIN_GPU_IMAGE_COPY_SOURCE)
              : ringl_backend_transition_image_2d_mip_v2(
                    context, command_list,
                    &(RinGLRinGpuImageTransition2DMipV2){
                        .image = target.image,
                        .mip_level = target.mip_level,
                        .old_state = old_state,
                        .new_state = RINGL_RIN_GPU_IMAGE_COPY_SOURCE,
                    })) != 0) ||
        submit_and_wait(context, command_list) != 0) {
        ringl_context_free_temporary(context, native_pixels,
                                      native_total_bytes);
        return -1;
    }
    *target.state = RINGL_RIN_GPU_IMAGE_COPY_SOURCE;

    memset(&readback, 0, sizeof(readback));
    readback.x = (uint32_t)x;
    readback.y = (uint32_t)y;
    readback.width = (uint32_t)width;
    readback.height = (uint32_t)height;
    readback.destination_row_pitch_bytes = native_row_bytes;
    if (target.mip_level == 0u) {
        result = context->sync_ops.readback_image_2d(
            context->ringpu.session, target.image, &readback,
            native_pixels != NULL ? native_pixels : pixels, native_total_bytes);
    } else {
        memset(&mip_readback, 0, sizeof(mip_readback));
        mip_readback.base = readback;
        mip_readback.mip_level = target.mip_level;
        result = context->sync_ops.readback_image_2d_mip_v2(
            context->ringpu.session, target.image, &mip_readback,
            native_pixels != NULL ? native_pixels : pixels, native_total_bytes);
    }
    if (result == RINGL_RIN_GPU_ERROR_DEVICE_LOST) {
        ringl_context_mark_lost(context);
        ringl_context_free_temporary(context, native_pixels,
                                      native_total_bytes);
        return -1;
    }
    if (result != 0) {
        ringl_context_free_temporary(context, native_pixels,
                                      native_total_bytes);
        return -1;
    }

    if (target.srgb_encoding != 0u) {
        ringl_encode_linear_rgba_to_srgb(
            native_pixels != NULL ? native_pixels : (const uint8_t*)pixels,
            (uint8_t*)pixels, (uint64_t)(uint32_t)width * (uint32_t)height,
            target.has_alpha);
    } else if (target.format == RINGL_RIN_GPU_FORMAT_BGRA8_UNORM) {
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
    } else if (target.format == RINGL_RIN_GPU_FORMAT_RGBA16_FLOAT &&
               unpack_rgba16f_to_rgba(native_pixels, pixels,
                                       (uint64_t)(uint32_t)width *
                                           (uint32_t)height) != 0) {
        ringl_context_free_temporary(context, native_pixels,
                                      native_total_bytes);
        return -1;
    }
    if (type == RINGL_FLOAT &&
        !ringl_rgba_float_snapshot_is_finite(
            pixels, (uint64_t)(uint32_t)width * (uint32_t)height)) {
        ringl_context_free_temporary(context, native_pixels,
                                      native_total_bytes);
        return -1;
    }
    ringl_context_free_temporary(context, native_pixels, native_total_bytes);
    return 0;
}

int ringl_read_color_target_rgba(RinGLContext* context, int32_t x, int32_t y,
                                  int32_t width, int32_t height, void* pixels)
{
    return ringl_read_color_target_to_type(context, x, y, width, height,
                                           RINGL_UNSIGNED_BYTE, pixels);
}

int ringl_read_color_target_rgba_float(RinGLContext* context, int32_t x,
                                        int32_t y, int32_t width,
                                        int32_t height, void* pixels)
{
    return ringl_read_color_target_to_type(context, x, y, width, height,
                                           RINGL_FLOAT, pixels);
}

static int readback_layout(int32_t width, int32_t height, uint32_t type,
                           uint32_t alignment, uint64_t* tight_row_bytes_out,
                           uint64_t* packed_row_bytes_out,
                           uint64_t* tight_total_bytes_out,
                           uint64_t* required_bytes_out)
{
    uint64_t tight_row_bytes;
    uint64_t packed_row_bytes;
    uint64_t height_u64;

    if (tight_row_bytes_out == NULL || packed_row_bytes_out == NULL ||
        tight_total_bytes_out == NULL || required_bytes_out == NULL ||
        width < 0 || height < 0 ||
        (type != RINGL_UNSIGNED_BYTE && type != RINGL_FLOAT) ||
        (alignment != 1u && alignment != 2u && alignment != 4u &&
         alignment != 8u)) {
        return -1;
    }

    if (width == 0 || height == 0) {
        *tight_row_bytes_out = 0u;
        *packed_row_bytes_out = 0u;
        *tight_total_bytes_out = 0u;
        *required_bytes_out = 0u;
        return 0;
    }

    if ((uint64_t)(uint32_t)width >
        UINT64_MAX / (type == RINGL_FLOAT ? 4u * sizeof(float) : 4u)) {
        return -1;
    }
    tight_row_bytes = (uint64_t)(uint32_t)width *
        (type == RINGL_FLOAT ? 4u * sizeof(float) : 4u);
    if (tight_row_bytes > UINT64_MAX - (alignment - 1u))
        return -1;
    packed_row_bytes = (tight_row_bytes + alignment - 1u) &
        ~((uint64_t)alignment - 1u);
    height_u64 = (uint64_t)(uint32_t)height;
    if (height_u64 > UINT64_MAX / tight_row_bytes ||
        height_u64 - 1u >
            (UINT64_MAX - tight_row_bytes) / packed_row_bytes) {
        return -1;
    }

    *tight_row_bytes_out = tight_row_bytes;
    *packed_row_bytes_out = packed_row_bytes;
    *tight_total_bytes_out = tight_row_bytes * height_u64;
    /* PACK_ALIGNMENT separates consecutive rows. The final row needs no
     * trailing padding, matching GLES and WebGL readPixels storage. */
    *required_bytes_out = packed_row_bytes * (height_u64 - 1u) +
        tight_row_bytes;
    return 0;
}

/* Returns 1 only when allocation of the private staging buffer fails. A
 * native readback failure is -1 and never exposes its partially written data
 * to the caller. */
static int ringl_read_pixels_packed(RinGLContext* context, int32_t x,
                                    int32_t y, int32_t width, int32_t height,
                                    uint32_t type, void* pixels,
                                    uint64_t tight_row_bytes,
                                    uint64_t packed_row_bytes,
                                    uint64_t tight_total_bytes)
{
    uint8_t* tight_pixels;
    uint8_t* clipped_pixels = NULL;
    RinGLColorTarget target;
    uint64_t component_bytes;
    uint64_t clipped_x0;
    uint64_t clipped_y0;
    uint64_t clipped_x1;
    uint64_t clipped_y1;
    uint64_t clipped_width;
    uint64_t clipped_height;
    uint64_t clipped_row_bytes;
    uint64_t clipped_total_bytes;
    uint64_t destination_x;
    uint64_t destination_y;
    int64_t request_x1;
    int64_t request_y1;
    uint64_t row;
    int result;

    if (tight_total_bytes == 0u)
        return 0;
    if (tight_total_bytes > SIZE_MAX)
        return 1;
    if (pixels == NULL || ringl_prepare_read_color_target(context, &target) != 0 ||
         !ringl_color_target_readback_type_supported(&target, type)) {
        return -1;
    }

    request_x1 = (int64_t)x + (int64_t)width;
    request_y1 = (int64_t)y + (int64_t)height;
    clipped_x0 = x < 0 ? 0u : (uint64_t)(uint32_t)x;
    clipped_y0 = y < 0 ? 0u : (uint64_t)(uint32_t)y;
    clipped_x1 = request_x1 <= 0 ? 0u : (uint64_t)request_x1;
    clipped_y1 = request_y1 <= 0 ? 0u : (uint64_t)request_y1;
    if (clipped_x0 > target.width)
        clipped_x0 = target.width;
    if (clipped_y0 > target.height)
        clipped_y0 = target.height;
    if (clipped_x1 > target.width)
        clipped_x1 = target.width;
    if (clipped_y1 > target.height)
        clipped_y1 = target.height;
    if (clipped_x1 <= clipped_x0 || clipped_y1 <= clipped_y0)
        return 0;

    clipped_width = clipped_x1 - clipped_x0;
    clipped_height = clipped_y1 - clipped_y0;
    component_bytes = tight_row_bytes / (uint64_t)(uint32_t)width;
    if (clipped_width > UINT64_MAX / component_bytes ||
        clipped_height > UINT64_MAX /
            (clipped_width * component_bytes)) {
        return 1;
    }
    clipped_row_bytes = clipped_width * component_bytes;
    clipped_total_bytes = clipped_row_bytes * clipped_height;
    if (clipped_total_bytes > SIZE_MAX)
        return 1;
    destination_x = (uint64_t)((int64_t)clipped_x0 - (int64_t)x);
    destination_y = (uint64_t)((int64_t)clipped_y0 - (int64_t)y);
    tight_pixels = ringl_context_alloc_temporary(context, tight_total_bytes);
    if (tight_pixels == NULL)
        return 1;

    if (clipped_width == (uint64_t)(uint32_t)width &&
        clipped_height == (uint64_t)(uint32_t)height) {
        result = ringl_read_color_target_to_type(context, x, y, width, height,
                                                 type, tight_pixels);
    } else {
        for (row = 0u; row < (uint64_t)(uint32_t)height; ++row) {
            memcpy(tight_pixels + (size_t)(row * tight_row_bytes),
                   (const uint8_t*)pixels +
                       (size_t)(row * packed_row_bytes),
                   (size_t)tight_row_bytes);
        }
        clipped_pixels = ringl_context_alloc_temporary(context, clipped_total_bytes);
        if (clipped_pixels == NULL)
            result = 1;
        else {
            result = ringl_read_color_target_to_type(
                context, (int32_t)clipped_x0, (int32_t)clipped_y0,
                (int32_t)clipped_width, (int32_t)clipped_height, type,
                clipped_pixels);
            if (result == 0) {
                for (row = 0u; row < clipped_height; ++row) {
                    memcpy(tight_pixels +
                               (size_t)((destination_y + row) * tight_row_bytes +
                                        destination_x * component_bytes),
                           clipped_pixels + (size_t)(row * clipped_row_bytes),
                           (size_t)clipped_row_bytes);
                }
            }
        }
    }
    if (result == 0) {
        for (row = 0u; row < (uint64_t)(uint32_t)height; ++row) {
            memcpy((uint8_t*)pixels + (size_t)(row * packed_row_bytes),
                   tight_pixels + (size_t)(row * tight_row_bytes),
                   (size_t)tight_row_bytes);
        }
    }
    ringl_context_free_temporary(context, clipped_pixels, clipped_total_bytes);
    ringl_context_free_temporary(context, tight_pixels, tight_total_bytes);
    return result;
}

void ringl_read_pixels(int32_t x, int32_t y,
                       int32_t width, int32_t height,
                       uint32_t format, uint32_t type,
                       void* pixels)
{
    RinGLContext* context = ringl_get_current_context();
    uint64_t tight_row_bytes;
    uint64_t packed_row_bytes;
    uint64_t tight_total_bytes;
    uint64_t required_bytes;
    int result;

    if (context == NULL)
        return;
    if (format != RINGL_RGBA ||
        (type != RINGL_UNSIGNED_BYTE && type != RINGL_FLOAT)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (width < 0 || height < 0) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (readback_layout(width, height, type, context->pack_alignment,
                        &tight_row_bytes, &packed_row_bytes,
                        &tight_total_bytes, &required_bytes) != 0) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (required_bytes > SIZE_MAX) {
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
        return;
    }
    if (required_bytes != 0u && pixels == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    result = ringl_read_pixels_packed(context, x, y, width, height, type,
                                      pixels, tight_row_bytes,
                                      packed_row_bytes, tight_total_bytes);
    if (result == 1)
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
    else if (result != 0)
        ringl_context_record_error(context,
                                   ringl_framebuffer_operation_error(context));
}

void ringl_read_pixels_to_bytes(int32_t x, int32_t y,
                                int32_t width, int32_t height,
                                uint32_t format, uint32_t type,
                                void* pixels, uint64_t pixels_size)
{
    RinGLContext* context = ringl_get_current_context();
    uint64_t tight_row_bytes;
    uint64_t packed_row_bytes;
    uint64_t tight_total_bytes;
    uint64_t required_bytes;
    int result;

    if (context == NULL)
        return;
    if (format != RINGL_RGBA ||
        (type != RINGL_UNSIGNED_BYTE && type != RINGL_FLOAT)) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (readback_layout(width, height, type, context->pack_alignment,
                        &tight_row_bytes, &packed_row_bytes,
                        &tight_total_bytes, &required_bytes) != 0) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if ((required_bytes != 0u && pixels == NULL) || required_bytes > SIZE_MAX ||
        pixels_size < required_bytes) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    result = ringl_read_pixels_packed(context, x, y, width, height, type,
                                      pixels, tight_row_bytes,
                                      packed_row_bytes, tight_total_bytes);
    if (result == 1)
        ringl_context_record_error(context, RINGL_OUT_OF_MEMORY);
    else if (result != 0)
        ringl_context_record_error(context,
                                   ringl_framebuffer_operation_error(context));
}
