/* SPDX-License-Identifier: MIT */
/*
 * RinGL's browser embedding must stay above RinGPU.  Aquamarine supplies
 * caller-owned drawing-buffer memory, while the generic RinGPU software
 * backend executes the command stream.  This file deliberately contains no
 * rasterizer or direct Aquamarine drawing path.
 */
#include <ringl/ringl_aquamarine_surface.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <ringpu/core.h>
#include <ringpu/software_backend.h>

#define RINGL_AQUAMARINE_SURFACE_MAX_RESOURCE_BYTES \
    ((uint64_t)RINGL_AQUAMARINE_SURFACE_MAX_DIMENSION * \
     RINGL_AQUAMARINE_SURFACE_MAX_DIMENSION * sizeof(uint64_t))
#define RINGL_AQUAMARINE_SURFACE_MAX_TOTAL_ALLOCATION_BYTES \
    (RINGL_AQUAMARINE_SURFACE_MAX_RESOURCE_BYTES * UINT64_C(4))

struct RinGLAquamarineSurfaceContext {
    RinGpuCore core;
    RinGpuSoftwareBackend* software_backend;
    RinGpuDisplayInfoV1 display;
    RinGLAquamarineSurfaceTargetV1 target;
    RinGpuHandle queue;
    RinGpuHandle command_list;
    RinGpuHandle color_image;
    RinGpuHandle depth_image;
    uint32_t color_state;
    uint32_t depth_state;
    uint32_t external_binding_role;
    uint32_t initialized;
};

static int multiply_u64(uint64_t left, uint64_t right, uint64_t* result)
{
    if (!result || (left != 0u && right > UINT64_MAX / left))
        return 0;
    *result = left * right;
    return 1;
}

static int target_valid(const RinGLAquamarineSurfaceTargetV1* target)
{
    uint64_t row_bytes;
    uint64_t color_size;
    uint64_t depth_size;

    if (!target ||
        (target->struct_size !=
             offsetof(RinGLAquamarineSurfaceTargetV1, stencil) &&
         target->struct_size != sizeof(*target)) ||
        target->version != RINGL_AQUAMARINE_SURFACE_VERSION ||
        !target->pixels || target->width == 0u || target->height == 0u ||
        target->width > RINGL_AQUAMARINE_SURFACE_MAX_DIMENSION ||
        target->height > RINGL_AQUAMARINE_SURFACE_MAX_DIMENSION ||
        target->pitch_bytes > RINGL_AQUAMARINE_SURFACE_MAX_PITCH_BYTES ||
        target->reserved0 != 0u ||
        !multiply_u64(target->width, 4u, &row_bytes) ||
        target->pitch_bytes < row_bytes ||
        !multiply_u64(target->pitch_bytes, target->height, &color_size) ||
        color_size == 0u ||
        color_size > RINGL_AQUAMARINE_SURFACE_MAX_RESOURCE_BYTES ||
        (target->depth == NULL) != (target->depth_pitch_floats == 0u)) {
        return 0;
    }
    if (target->depth != NULL &&
        (target->depth_pitch_floats < target->width ||
         target->depth_pitch_floats > RINGL_AQUAMARINE_SURFACE_MAX_DIMENSION ||
         !multiply_u64(target->depth_pitch_floats, target->height,
                       &depth_size) ||
         !multiply_u64(depth_size, sizeof(float), &depth_size) ||
         depth_size == 0u ||
         depth_size > RINGL_AQUAMARINE_SURFACE_MAX_RESOURCE_BYTES)) {
        return 0;
    }
    if (target->struct_size == sizeof(*target) &&
        (target->reserved1 != 0u ||
         (target->stencil == NULL) != (target->stencil_pitch_bytes == 0u) ||
         (target->stencil != NULL &&
          (target->depth == NULL ||
           target->stencil_pitch_bytes < target->width ||
           target->stencil_pitch_bytes >
               RINGL_AQUAMARINE_SURFACE_MAX_PITCH_BYTES)))) {
        return 0;
    }
    return 1;
}

static int surface_external_image(
    void* opaque, const RinGpuImageDescV1* descriptor,
    uint64_t allocation_bytes, RinGpuSoftwareExternalImageV1* storage)
{
    RinGLAquamarineSurfaceContext* context = opaque;
    uint64_t target_size;
    uint64_t depth_size;
    uint64_t tight_color_size;
    uint64_t tight_depth_stencil_size;

    if (!context || !descriptor || !storage ||
        storage->struct_size != sizeof(*storage) ||
        storage->version != RIN_GPU_SOFTWARE_EXTERNAL_IMAGE_VERSION ||
        descriptor->dimension != RIN_GPU_IMAGE_DIMENSION_2D ||
        descriptor->width == 0u || descriptor->height == 0u ||
        descriptor->depth != 1u || descriptor->array_layers != 1u ||
        descriptor->mip_levels == 0u || descriptor->sample_count != 1u ||
        !multiply_u64(context->target.pitch_bytes, context->target.height,
                      &target_size)) {
        return RIN_GPU_ERROR_INVALID_ARGUMENT;
    }

    if (descriptor->format == RIN_GPU_FORMAT_BGRA8_UNORM &&
        (descriptor->usage & RIN_GPU_IMAGE_PRESENT) != 0u &&
        context->external_binding_role == 1u &&
        descriptor->mip_levels == 1u &&
        descriptor->width == context->target.width &&
        descriptor->height == context->target.height) {
        if (!multiply_u64(descriptor->width, descriptor->height,
                          &tight_color_size) ||
            !multiply_u64(tight_color_size, 4u, &tight_color_size) ||
            allocation_bytes != tight_color_size) {
            return RIN_GPU_ERROR_INVALID_ARGUMENT;
        }
        storage->pixels = context->target.pixels;
        storage->size_bytes = target_size;
        storage->row_pitch_bytes = context->target.pitch_bytes;
        return RIN_GPU_OK;
    }

    if (descriptor->format != RIN_GPU_FORMAT_D32_FLOAT ||
        (descriptor->usage & RIN_GPU_IMAGE_DEPTH_STENCIL) == 0u ||
        context->external_binding_role != 2u ||
        context->target.depth == NULL || context->target.stencil != NULL ||
        descriptor->width != context->target.width ||
        descriptor->height != context->target.height) {
        if (descriptor->format != RIN_GPU_FORMAT_D32_FLOAT_S8_UINT ||
            (descriptor->usage & RIN_GPU_IMAGE_DEPTH_STENCIL) == 0u ||
            context->external_binding_role != 2u ||
            descriptor->mip_levels != 1u ||
            context->target.depth == NULL || context->target.stencil == NULL ||
            descriptor->width != context->target.width ||
            descriptor->height != context->target.height) {
            /* A zeroed record requests generic backend-owned storage. */
            return RIN_GPU_OK;
        }
        if (!multiply_u64(descriptor->width, descriptor->height,
                          &tight_depth_stencil_size) ||
            !multiply_u64(tight_depth_stencil_size, 8u,
                          &tight_depth_stencil_size) ||
            allocation_bytes != tight_depth_stencil_size ||
            !multiply_u64(context->target.depth_pitch_floats,
                          context->target.height, &depth_size) ||
            !multiply_u64(depth_size, sizeof(float), &depth_size)) {
            return RIN_GPU_ERROR_INVALID_ARGUMENT;
        }
        storage->depth_pixels = context->target.depth;
        storage->depth_size_bytes = depth_size;
        storage->depth_row_pitch_bytes =
            (uint64_t)context->target.depth_pitch_floats * sizeof(float);
        storage->stencil_pixels = context->target.stencil;
        storage->stencil_size_bytes =
            (uint64_t)context->target.stencil_pitch_bytes * context->target.height;
        storage->stencil_row_pitch_bytes = context->target.stencil_pitch_bytes;
        return RIN_GPU_OK;
    }

    if (!multiply_u64(descriptor->width, descriptor->height, &depth_size) ||
        descriptor->mip_levels != 1u ||
        !multiply_u64(depth_size, sizeof(float), &depth_size) ||
        allocation_bytes != depth_size ||
        !multiply_u64(context->target.depth_pitch_floats,
                      context->target.height, &depth_size) ||
        !multiply_u64(depth_size, sizeof(float), &depth_size)) {
        return RIN_GPU_ERROR_INVALID_ARGUMENT;
    }
    storage->pixels = (uint8_t*)context->target.depth;
    storage->size_bytes = depth_size;
    storage->row_pitch_bytes =
        (uint64_t)context->target.depth_pitch_floats * sizeof(float);
    return RIN_GPU_OK;
}

static int surface_present(void* opaque,
                           const RinGpuSoftwarePresentedImageV1* image)
{
    RinGLAquamarineSurfaceContext* context = opaque;
    uint64_t target_size;

    if (!context || !image || image->struct_size != sizeof(*image) ||
        image->version != RIN_GPU_SOFTWARE_BACKEND_VERSION ||
        !multiply_u64(context->target.pitch_bytes, context->target.height,
                      &target_size) ||
        image->pixels != context->target.pixels || image->size_bytes != target_size ||
        image->row_pitch_bytes != context->target.pitch_bytes ||
        image->format != RIN_GPU_FORMAT_BGRA8_UNORM ||
        image->width != context->target.width ||
        image->height != context->target.height ||
        image->display_id != context->display.display_id) {
        return RIN_GPU_ERROR_INVALID_ARGUMENT;
    }
    return RIN_GPU_OK;
}

static int context_ready(const RinGLAquamarineSurfaceContext* context)
{
    return context != NULL &&
           context->initialized == RINGL_AQUAMARINE_SURFACE_VERSION;
}

static int submit(RinGLAquamarineSurfaceContext* context)
{
    RinGpuSubmitInfoV1 submit_info;
    int result;

    if (!context_ready(context))
        return RINGL_AQUAMARINE_SURFACE_STATE;
    result = ringpu_command_list_close(&context->core, context->command_list);
    if (result != RIN_GPU_OK)
        return RINGL_AQUAMARINE_SURFACE_BACKEND;
    memset(&submit_info, 0, sizeof(submit_info));
    submit_info.abi_version = RIN_GPU_ABI_VERSION;
    submit_info.struct_size = sizeof(submit_info);
    submit_info.command_list = context->command_list;
    result = ringpu_queue_submit(&context->core, context->queue, &submit_info);
    return result == RIN_GPU_OK ? RINGL_AQUAMARINE_SURFACE_OK
                                : RINGL_AQUAMARINE_SURFACE_BACKEND;
}

static void transition_for(RinGpuImageTransitionV1* transition,
                           uint32_t before, uint32_t after)
{
    memset(transition, 0, sizeof(*transition));
    transition->abi_version = RIN_GPU_ABI_VERSION;
    transition->struct_size = sizeof(*transition);
    transition->mip_level_count = 1u;
    transition->array_layer_count = 1u;
    transition->before_state = before;
    transition->after_state = after;
}

static int initialize_context(RinGLAquamarineSurfaceContext* context,
                              const RinGLAquamarineSurfaceTargetV1* target)
{
    RinGpuSoftwareBackendDescV3 software_desc;
    RinGpuCoreConfigV1 config;
    RinGpuImageDescV1 image;
    RinGpuQueueDescV1 queue;
    RinGpuCommandListDescV1 command_list;
    const RinGpuBackendOpsV1* backend_ops;
    int result;

    if (!context || !target_valid(target))
        return RINGL_AQUAMARINE_SURFACE_INVALID_ARGUMENT;

    memset(context, 0, sizeof(*context));
    memcpy(&context->target, target, target->struct_size);
    context->display.abi_version = RIN_GPU_ABI_VERSION;
    context->display.struct_size = sizeof(context->display);
    context->display.display_id = RIN_GPU_PRIMARY_DISPLAY;
    context->display.flags = RIN_GPU_DISPLAY_CONNECTED | RIN_GPU_DISPLAY_PRIMARY;
    context->display.width = target->width;
    context->display.height = target->height;
    context->display.refresh_millihertz = 60000u;
    context->display.format = RIN_GPU_FORMAT_BGRA8_UNORM;
    context->display.physical_width_mm = 1u;
    context->display.physical_height_mm = 1u;
    context->display.scale_milli = 1000u;
    memcpy(context->display.name, "RinGL RinGPU surface", 21u);

    memset(&software_desc, 0, sizeof(software_desc));
    software_desc.base.base.struct_size = sizeof(software_desc);
    software_desc.base.base.version = RIN_GPU_SOFTWARE_BACKEND_VERSION_3;
    software_desc.base.base.max_total_bytes =
        RINGL_AQUAMARINE_SURFACE_MAX_TOTAL_ALLOCATION_BYTES;
    software_desc.base.present_callback = surface_present;
    software_desc.base.present_context = context;
    software_desc.acquire_image = surface_external_image;
    software_desc.image_context = context;
    result = ringpu_software_backend_create(&software_desc.base.base,
                                            &context->software_backend);
    if (result != RIN_GPU_OK)
        return result == RIN_GPU_ERROR_NO_MEMORY
            ? RINGL_AQUAMARINE_SURFACE_NO_MEMORY
            : RINGL_AQUAMARINE_SURFACE_BACKEND;
    backend_ops = ringpu_software_backend_ops();
    if (backend_ops == NULL) {
        ringpu_software_backend_destroy(context->software_backend);
        context->software_backend = NULL;
        return RINGL_AQUAMARINE_SURFACE_BACKEND;
    }

    memset(&config, 0, sizeof(config));
    config.abi_version = RIN_GPU_ABI_VERSION;
    config.struct_size = sizeof(config);
    config.handle_secret = UINT64_C(0x52494e474c535746);
    config.max_buffer_size = RINGL_AQUAMARINE_SURFACE_MAX_RESOURCE_BYTES;
    config.max_image_size =
        RINGL_AQUAMARINE_SURFACE_MAX_RESOURCE_BYTES * UINT64_C(2);
    config.max_total_allocation_size =
        RINGL_AQUAMARINE_SURFACE_MAX_TOTAL_ALLOCATION_BYTES;
    config.max_image_dimension = RINGL_AQUAMARINE_SURFACE_MAX_DIMENSION;
    config.max_image_layers = 1u;
    config.max_image_mip_levels = 13u;
    config.max_image_sample_count = 1u;
    config.adapter.abi_version = RIN_GPU_ABI_VERSION;
    config.adapter.struct_size = sizeof(config.adapter);
    config.adapter.queue_capabilities = RIN_GPU_QUEUE_GRAPHICS;
    memcpy(config.adapter.name, "RinGL RinGPU adapter", 21u);
    config.backend = *backend_ops;
    config.backend_context = context->software_backend;
    config.displays = &context->display;
    config.display_count = 1u;
    result = ringpu_core_init(&context->core, &config);
    if (result != RIN_GPU_OK)
        goto fail;

    memset(&queue, 0, sizeof(queue));
    queue.abi_version = RIN_GPU_ABI_VERSION;
    queue.struct_size = sizeof(queue);
    queue.capabilities = RIN_GPU_QUEUE_GRAPHICS;
    result = ringpu_create_queue(&context->core, &queue, &context->queue);
    if (result != RIN_GPU_OK)
        goto fail;
    memset(&command_list, 0, sizeof(command_list));
    command_list.abi_version = RIN_GPU_ABI_VERSION;
    command_list.struct_size = sizeof(command_list);
    command_list.capabilities = RIN_GPU_QUEUE_GRAPHICS;
    result = ringpu_create_command_list(&context->core, &command_list,
                                        &context->command_list);
    if (result != RIN_GPU_OK)
        goto fail;

    memset(&image, 0, sizeof(image));
    image.abi_version = RIN_GPU_ABI_VERSION;
    image.struct_size = sizeof(image);
    image.dimension = RIN_GPU_IMAGE_DIMENSION_2D;
    image.format = RIN_GPU_FORMAT_BGRA8_UNORM;
    image.width = target->width;
    image.height = target->height;
    image.depth = 1u;
    image.array_layers = 1u;
    image.mip_levels = 1u;
    image.sample_count = 1u;
    image.usage = RIN_GPU_IMAGE_COPY_DESTINATION | RIN_GPU_IMAGE_COPY_SOURCE |
                  RIN_GPU_IMAGE_COLOR_TARGET | RIN_GPU_IMAGE_PRESENT;
    image.flags = RIN_GPU_IMAGE_CPU_READABLE;
    context->external_binding_role = 1u;
    result = ringpu_create_image(&context->core, &image, &context->color_image);
    context->external_binding_role = 0u;
    if (result != RIN_GPU_OK)
        goto fail;
    if (context->target.depth != NULL) {
        image.format = context->target.stencil != NULL
            ? RIN_GPU_FORMAT_D32_FLOAT_S8_UINT : RIN_GPU_FORMAT_D32_FLOAT;
        image.usage = RIN_GPU_IMAGE_COPY_DESTINATION |
                      RIN_GPU_IMAGE_DEPTH_STENCIL;
        image.flags = 0u;
        context->external_binding_role = 2u;
        result = ringpu_create_image(&context->core, &image,
                                     &context->depth_image);
        context->external_binding_role = 0u;
        if (result != RIN_GPU_OK)
            goto fail;
    }
    context->initialized = RINGL_AQUAMARINE_SURFACE_VERSION;
    return RINGL_AQUAMARINE_SURFACE_OK;

fail:
    ringpu_core_shutdown(&context->core);
    ringpu_software_backend_destroy(context->software_backend);
    memset(context, 0, sizeof(*context));
    return result == RIN_GPU_ERROR_NO_MEMORY
        ? RINGL_AQUAMARINE_SURFACE_NO_MEMORY
        : RINGL_AQUAMARINE_SURFACE_BACKEND;
}

int ringl_aquamarine_surface_create(
    const RinGLAquamarineSurfaceTargetV1* target,
    RinGLAquamarineSurfaceContext** context_out)
{
    RinGLAquamarineSurfaceContext* context;
    int result;

    if (!context_out)
        return RINGL_AQUAMARINE_SURFACE_INVALID_ARGUMENT;
    *context_out = NULL;
    context = calloc(1u, sizeof(*context));
    if (!context)
        return RINGL_AQUAMARINE_SURFACE_NO_MEMORY;
    result = initialize_context(context, target);
    if (result != RINGL_AQUAMARINE_SURFACE_OK) {
        free(context);
        return result;
    }
    *context_out = context;
    return RINGL_AQUAMARINE_SURFACE_OK;
}

void ringl_aquamarine_surface_destroy(RinGLAquamarineSurfaceContext* context)
{
    if (!context)
        return;
    ringpu_core_shutdown(&context->core);
    ringpu_software_backend_destroy(context->software_backend);
    memset(context, 0, sizeof(*context));
    free(context);
}

int ringl_aquamarine_surface_begin_content_update(
    RinGLAquamarineSurfaceContext* context)
{
    RinGpuImageTransitionV1 transition;
    int result;

    if (!context_ready(context))
        return RINGL_AQUAMARINE_SURFACE_STATE;
    if (context->color_state == RIN_GPU_IMAGE_STATE_COLOR_TARGET)
        return RINGL_AQUAMARINE_SURFACE_OK;
    result = ringpu_command_list_reset(&context->core, context->command_list);
    if (result != RIN_GPU_OK)
        return RINGL_AQUAMARINE_SURFACE_BACKEND;
    transition_for(&transition, context->color_state,
                   RIN_GPU_IMAGE_STATE_COLOR_TARGET);
    result = ringpu_command_transition_image(&context->core,
                                             context->command_list,
                                             context->color_image,
                                             &transition);
    if (result != RIN_GPU_OK)
        return RINGL_AQUAMARINE_SURFACE_BACKEND;
    result = submit(context);
    if (result != RINGL_AQUAMARINE_SURFACE_OK)
        return result;
    context->color_state = RIN_GPU_IMAGE_STATE_COLOR_TARGET;
    return RINGL_AQUAMARINE_SURFACE_OK;
}

int ringl_aquamarine_surface_sync_external_color_state(
    RinGLAquamarineSurfaceContext* context, uint32_t state)
{
    if (!context_ready(context))
        return RINGL_AQUAMARINE_SURFACE_STATE;
    if (state != RIN_GPU_IMAGE_STATE_COLOR_TARGET &&
        state != RIN_GPU_IMAGE_STATE_PRESENT) {
        return RINGL_AQUAMARINE_SURFACE_INVALID_ARGUMENT;
    }
    context->color_state = state;
    return RINGL_AQUAMARINE_SURFACE_OK;
}

int ringl_aquamarine_surface_sync_external_framebuffer_states(
    RinGLAquamarineSurfaceContext* context, uint32_t color_state,
    uint32_t depth_state)
{
    if (!context_ready(context))
        return RINGL_AQUAMARINE_SURFACE_STATE;
    if (color_state != RIN_GPU_IMAGE_STATE_COLOR_TARGET &&
        color_state != RIN_GPU_IMAGE_STATE_PRESENT) {
        return RINGL_AQUAMARINE_SURFACE_INVALID_ARGUMENT;
    }
    if ((context->depth_image == 0u &&
         depth_state != RIN_GPU_IMAGE_STATE_UNDEFINED) ||
        (context->depth_image != 0u &&
         depth_state != RIN_GPU_IMAGE_STATE_UNDEFINED &&
         depth_state != RIN_GPU_IMAGE_STATE_DEPTH_TARGET)) {
        return RINGL_AQUAMARINE_SURFACE_INVALID_ARGUMENT;
    }
    context->color_state = color_state;
    if (context->depth_image != 0u)
        context->depth_state = depth_state;
    return RINGL_AQUAMARINE_SURFACE_OK;
}

int ringl_aquamarine_surface_get_native(
    RinGLAquamarineSurfaceContext* context,
    RinGLAquamarineSurfaceNativeV1* native_out)
{
    if (!context_ready(context) || !native_out ||
        native_out->struct_size < sizeof(*native_out) ||
        native_out->version != RINGL_AQUAMARINE_SURFACE_NATIVE_VERSION ||
        native_out->reserved0 != 0u) {
        return RINGL_AQUAMARINE_SURFACE_INVALID_ARGUMENT;
    }
    native_out->ringpu_core = &context->core;
    native_out->graphics_queue = context->queue;
    native_out->color_image = context->color_image;
    native_out->queue_capabilities = RIN_GPU_QUEUE_GRAPHICS;
    native_out->color_format = context->display.format;
    native_out->width = context->target.width;
    native_out->height = context->target.height;
    native_out->display_id = context->display.display_id;
    native_out->color_state = context->color_state;
    native_out->depth_image = context->depth_image;
    native_out->depth_format = context->depth_image != 0u
        ? (context->target.stencil != NULL
               ? RIN_GPU_FORMAT_D32_FLOAT_S8_UINT
               : RIN_GPU_FORMAT_D32_FLOAT)
        : 0u;
    native_out->depth_state = context->depth_state;
    return RINGL_AQUAMARINE_SURFACE_OK;
}
