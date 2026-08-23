/* SPDX-License-Identifier: MIT */
#include "../ringl_internal.h"
#include "pipeline_cache.h"

#include <limits.h>
#include <string.h>

static float clamp_color(float value)
{
    if (!(value >= 0.0f))
        return 0.0f;
    if (value > 1.0f)
        return 1.0f;
    return value;
}

int ringl_resolve_color_target(RinGLContext* context,
                               RinGLColorTarget* target)
{
    RinGLFramebufferObject* framebuffer;
    uint32_t index;

    if (context == NULL || target == NULL)
        return -1;
    memset(target, 0, sizeof(*target));
    if (context->framebuffer_binding == 0u) {
        if (!context->has_default_framebuffer)
            return -1;
        target->image = context->default_framebuffer.color_target;
        target->format = context->default_framebuffer.color_format;
        target->width = context->default_framebuffer.width;
        target->height = context->default_framebuffer.height;
        target->mip_level = 0u;
        target->state = &context->default_framebuffer_state;
        return target->image != 0u && target->format != 0u &&
               target->width != 0u && target->height != 0u ? 0 : -1;
    }
    if (ringl_object_lookup(context, context->framebuffer_binding,
                            RINGL_OBJECT_FRAMEBUFFER) == NULL)
        return -1;
    index = ringl_object_slot_index(context->framebuffer_binding);
    if (index >= RINGL_OBJECT_SLOT_COUNT)
        return -1;
    framebuffer = &context->framebuffers[index];
    if (framebuffer->color_attachment_kind ==
        RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D) {
        if (ringl_texture_realize_color_target(
                context, framebuffer->color_attachment_object,
                (uint32_t)framebuffer->color_attachment_level,
                &target->image, &target->state, &target->width,
                &target->height) != 0) {
            return -1;
        }
        target->mip_level = (uint32_t)framebuffer->color_attachment_level;
    } else if (framebuffer->color_attachment_kind ==
               RINGL_FRAMEBUFFER_ATTACHMENT_RENDERBUFFER) {
        if (ringl_renderbuffer_realize_color_target(
                context, framebuffer->color_attachment_object,
                &target->image, &target->state, &target->width,
                &target->height) != 0) {
            return -1;
        }
        target->mip_level = 0u;
        index = ringl_object_slot_index(framebuffer->color_attachment_object);
        if (index >= RINGL_OBJECT_SLOT_COUNT)
            return -1;
        switch (context->renderbuffers[index].internal_format) {
        case RINGL_RGB565:
            target->format = RINGL_RIN_GPU_FORMAT_RGB565_UNORM;
            break;
        case RINGL_RGBA4:
            target->format = RINGL_RIN_GPU_FORMAT_RGBA4_UNORM;
            break;
        case RINGL_RGB5_A1:
            target->format = RINGL_RIN_GPU_FORMAT_RGB5_A1_UNORM;
            break;
        default:
            target->format = RINGL_RIN_GPU_FORMAT_RGBA8_UNORM;
            break;
        }
    } else {
        return -1;
    }
    if (framebuffer->color_attachment_kind ==
        RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D) {
        target->format = RINGL_RIN_GPU_FORMAT_RGBA8_UNORM;
    }
    return target->image != 0u && target->state != NULL &&
           target->width != 0u && target->height != 0u ? 0 : -1;
}

int ringl_resolve_depth_target(RinGLContext* context, RinGLDepthTarget* target)
{
    RinGLFramebufferObject* framebuffer;
    uint32_t index;
    uint32_t width;
    uint32_t height;

    if (context == NULL || target == NULL)
        return -1;
    memset(target, 0, sizeof(*target));
    if (context->framebuffer_binding != 0u) {
        if (ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) !=
            RINGL_FRAMEBUFFER_COMPLETE ||
            ringl_object_lookup(context, context->framebuffer_binding,
                                RINGL_OBJECT_FRAMEBUFFER) == NULL)
            return -1;
        index = ringl_object_slot_index(context->framebuffer_binding);
        if (index >= RINGL_OBJECT_SLOT_COUNT)
            return -1;
        framebuffer = &context->framebuffers[index];
        if (framebuffer->depth_attachment_kind ==
            RINGL_FRAMEBUFFER_ATTACHMENT_NONE)
            return 1;
        if (framebuffer->depth_attachment_kind ==
            RINGL_FRAMEBUFFER_ATTACHMENT_RENDERBUFFER) {
            if (ringl_renderbuffer_realize_depth_target(
                    context, framebuffer->depth_attachment_object,
                    &target->image, &target->state, &width, &height) != 0)
                return -1;
            index = ringl_object_slot_index(
                framebuffer->depth_attachment_object);
            if (index >= RINGL_OBJECT_SLOT_COUNT)
                return -1;
            target->format =
                context->renderbuffers[index].internal_format ==
                        RINGL_STENCIL_INDEX8
                    ? RINGL_RIN_GPU_FORMAT_S8_UINT
                    : context->renderbuffers[index].internal_format ==
                              RINGL_DEPTH24_STENCIL8
                    ? RINGL_RIN_GPU_FORMAT_D32_FLOAT_S8_UINT
                    : RINGL_RIN_GPU_FORMAT_D32_FLOAT;
        } else if (framebuffer->depth_attachment_kind ==
                   RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D) {
            if (ringl_texture_realize_depth_target(
                    context, framebuffer->depth_attachment_object,
                    (uint32_t)framebuffer->depth_attachment_level,
                    &target->image, &target->state, &width, &height) != 0)
                return -1;
            index = ringl_object_slot_index(framebuffer->depth_attachment_object);
            if (index >= RINGL_OBJECT_SLOT_COUNT)
                return -1;
            target->format = context->textures[index].format ==
                    RINGL_DEPTH24_STENCIL8
                ? RINGL_RIN_GPU_FORMAT_D32_FLOAT_S8_UINT
                : RINGL_RIN_GPU_FORMAT_D32_FLOAT;
        } else {
            return -1;
        }
        target->has_depth = framebuffer->depth_attachment_has_depth;
        target->has_stencil = framebuffer->depth_attachment_has_stencil;
        target->mip_level = framebuffer->depth_attachment_kind ==
                RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D
            ? (uint32_t)framebuffer->depth_attachment_level
            : 0u;
        return target->image != 0u && target->state != NULL && width != 0u &&
                       height != 0u
            ? 0
            : -1;
    }
    if (!context->has_default_framebuffer ||
        context->default_framebuffer.depth_target == 0u)
        return 1;
    target->image = context->default_framebuffer.depth_target;
    target->format = context->default_framebuffer.depth_format;
    target->has_depth = RINGL_TRUE;
    target->has_stencil =
        target->format == RINGL_RIN_GPU_FORMAT_D32_FLOAT_S8_UINT;
    target->state = &context->default_depth_framebuffer_state;
    return (target->format == RINGL_RIN_GPU_FORMAT_D32_FLOAT ||
            target->format == RINGL_RIN_GPU_FORMAT_D32_FLOAT_S8_UINT) &&
                   target->state != NULL
        ? 0
        : -1;
}

static int resolve_depth_stencil_attachment(
    RinGLContext* context, uint32_t kind, uint32_t object, int32_t level,
    uint32_t has_depth, uint32_t has_stencil, RinGLDepthTarget* target)
{
    uint32_t index;
    uint32_t format;
    uint32_t width;
    uint32_t height;

    if (context == NULL || target == NULL || object == 0u || level < 0 ||
        (uint32_t)level >= RINGL_MAX_TEXTURE_MIP_LEVELS)
        return -1;
    memset(target, 0, sizeof(*target));
    if (kind == RINGL_FRAMEBUFFER_ATTACHMENT_RENDERBUFFER) {
        if (ringl_renderbuffer_realize_depth_target(context, object,
                                                    &target->image,
                                                    &target->state, &width,
                                                    &height) != 0)
            return -1;
        index = ringl_object_slot_index(object);
        if (index >= RINGL_OBJECT_SLOT_COUNT)
            return -1;
        format = context->renderbuffers[index].internal_format;
    } else if (kind == RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D) {
        if (ringl_texture_realize_depth_target(context, object,
                                               (uint32_t)level, &target->image,
                                               &target->state, &width,
                                               &height) != 0)
            return -1;
        index = ringl_object_slot_index(object);
        if (index >= RINGL_OBJECT_SLOT_COUNT)
            return -1;
        format = context->textures[index].format;
    } else {
        return -1;
    }
    if (format == RINGL_STENCIL_INDEX8)
        target->format = RINGL_RIN_GPU_FORMAT_S8_UINT;
    else if (format == RINGL_DEPTH24_STENCIL8)
        target->format = RINGL_RIN_GPU_FORMAT_D32_FLOAT_S8_UINT;
    else
        target->format = RINGL_RIN_GPU_FORMAT_D32_FLOAT;
    target->has_depth = has_depth;
    target->has_stencil = has_stencil;
    target->mip_level = kind == RINGL_FRAMEBUFFER_ATTACHMENT_TEXTURE_2D
        ? (uint32_t)level : 0u;
    return target->image != 0u && target->state != NULL && width != 0u &&
                   height != 0u
        ? 0
        : -1;
}

int ringl_resolve_depth_stencil_targets(RinGLContext* context,
                                        RinGLDepthStencilTargets* targets)
{
    RinGLFramebufferObject* framebuffer;
    uint32_t index;

    if (context == NULL || targets == NULL)
        return -1;
    memset(targets, 0, sizeof(*targets));
    if (context->framebuffer_binding == 0u) {
        int result = ringl_resolve_depth_target(context, &targets->depth);
        if (result != 0)
            return result;
        if (targets->depth.has_stencil != 0u) {
            targets->stencil = targets->depth;
            targets->combined = RINGL_TRUE;
        }
        return 0;
    }
    if (ringl_check_framebuffer_status(RINGL_FRAMEBUFFER) !=
            RINGL_FRAMEBUFFER_COMPLETE ||
        ringl_object_lookup(context, context->framebuffer_binding,
                            RINGL_OBJECT_FRAMEBUFFER) == NULL)
        return -1;
    index = ringl_object_slot_index(context->framebuffer_binding);
    if (index >= RINGL_OBJECT_SLOT_COUNT)
        return -1;
    framebuffer = &context->framebuffers[index];
    if (framebuffer->depth_attachment_kind !=
        RINGL_FRAMEBUFFER_ATTACHMENT_NONE) {
        if (resolve_depth_stencil_attachment(
                context, framebuffer->depth_attachment_kind,
                framebuffer->depth_attachment_object,
                framebuffer->depth_attachment_level, RINGL_TRUE,
                framebuffer->depth_attachment_has_stencil, &targets->depth) !=
            0)
            return -1;
    }
    if (framebuffer->stencil_attachment_kind !=
        RINGL_FRAMEBUFFER_ATTACHMENT_NONE) {
        if (resolve_depth_stencil_attachment(
                context, framebuffer->stencil_attachment_kind,
                framebuffer->stencil_attachment_object,
                framebuffer->stencil_attachment_level, RINGL_FALSE,
                RINGL_TRUE, &targets->stencil) != 0)
            return -1;
    }
    if (targets->depth.image == 0u && targets->stencil.image == 0u)
        return 1;
    targets->combined = targets->depth.image != 0u &&
        targets->depth.image == targets->stencil.image &&
        targets->depth.state == targets->stencil.state &&
        targets->depth.mip_level == targets->stencil.mip_level &&
        targets->depth.format == RINGL_RIN_GPU_FORMAT_D32_FLOAT_S8_UINT;
    return 0;
}

static int command_ops_ready(const RinGLContext* context)
{
    return context != NULL && context->has_ringpu_ops &&
        context->ringpu.graphics_queue != 0u &&
        (context->ringpu.queue_capabilities & RINGL_RIN_GPU_QUEUE_GRAPHICS) != 0u &&
        context->ringpu_ops.create_command_list != NULL &&
        context->ringpu_ops.reset_command_list != NULL &&
        context->ringpu_ops.transition_image != NULL &&
        context->ringpu_ops.begin_render_pass != NULL &&
        context->ringpu_ops.end_render_pass != NULL &&
        context->ringpu_ops.close_command_list != NULL &&
        context->ringpu_ops.queue_submit != NULL;
}

static int legacy_pipeline_state_supported(const RinGLContext* context,
                                           uint32_t stencil_test_enabled)
{
    return context != NULL && !context->blend_enabled &&
        !context->cull_face_enabled && context->front_face == RINGL_CCW &&
        context->color_write_mask == RINGL_RIN_GPU_COLOR_WRITE_ALL &&
        stencil_test_enabled == 0u;
}

static int draw_state_supported(const RinGLContext* context,
                                const RinGLColorTarget* target,
                                const RinGLDepthStencilTargets* targets,
                                uint32_t depth_test_enabled,
                                uint32_t stencil_test_enabled)
{
    if (context == NULL || target == NULL || target->image == 0u ||
        target->width == 0u || target->height == 0u)
        return 0;
    if ((depth_test_enabled != 0u || stencil_test_enabled != 0u) &&
        (targets == NULL ||
         (depth_test_enabled != 0u &&
          (targets->depth.image == 0u || targets->depth.has_depth == 0u ||
           (targets->depth.format != RINGL_RIN_GPU_FORMAT_D32_FLOAT &&
            targets->depth.format != RINGL_RIN_GPU_FORMAT_D32_FLOAT_S8_UINT) ||
           targets->depth.state == NULL)) ||
         (stencil_test_enabled != 0u &&
          (targets->stencil.image == 0u ||
           targets->stencil.has_stencil == 0u ||
           (targets->stencil.format !=
                RINGL_RIN_GPU_FORMAT_D32_FLOAT_S8_UINT &&
            targets->stencil.format != RINGL_RIN_GPU_FORMAT_S8_UINT) ||
           targets->stencil.state == NULL)) ||
         (depth_test_enabled != 0u && stencil_test_enabled != 0u &&
          targets->combined == 0u &&
          context->ringpu_ops.begin_render_pass_depth_stencil == NULL) ||
         (targets->combined != 0u &&
          context->ringpu_ops.begin_render_pass_depth == NULL) ||
         (targets->combined == 0u &&
          (depth_test_enabled == 0u || stencil_test_enabled == 0u) &&
          context->ringpu_ops.begin_render_pass_depth == NULL) ||
         context->ringpu_ops.create_graphics_pipeline_native == NULL))
        return 0;
    if ((depth_test_enabled != 0u || stencil_test_enabled != 0u) &&
        (target->mip_level != 0u ||
         (targets != NULL &&
          (targets->depth.mip_level != 0u ||
           targets->stencil.mip_level != 0u))) &&
        (context->ringpu_ops.transition_image_2d_mip_v2 == NULL ||
         (targets != NULL && targets->depth.has_depth != 0u &&
          targets->stencil.has_stencil != 0u && targets->combined == 0u
              ? context->ringpu_ops.begin_render_pass_depth_stencil_mip_v2 ==
                    NULL
              : context->ringpu_ops.begin_render_pass_depth_mip_v2 == NULL)))
        return 0;
    if (context->ringpu_ops.create_graphics_pipeline_native == NULL &&
        !legacy_pipeline_state_supported(context, stencil_test_enabled))
        return 0;
    if (context->ringpu_ops.set_raster_state == NULL) {
        if (context->scissor_enabled)
            return 0;
        if (context->viewport_initialized &&
            (context->viewport_x != 0 || context->viewport_y != 0 ||
             context->viewport_width != target->width ||
             context->viewport_height != target->height))
            return 0;
    }
    return 1;
}

static int draw_is_noop(const RinGLContext* context)
{
    if (context == NULL)
        return 1;
    if (context->color_write_mask == 0u && !context->depth_test_enabled &&
        !context->stencil_test_enabled)
        return 1;
    if (context->cull_face_enabled &&
        context->cull_face_mode == RINGL_FRONT_AND_BACK &&
        !context->depth_test_enabled && !context->stencil_test_enabled)
        return 1;
    if (context->viewport_initialized &&
        (context->viewport_width == 0u || context->viewport_height == 0u))
        return 1;
    return 0;
}

static int begin_commands(RinGLContext* context, uint64_t* command_list)
{
    int created = 0;

    if (!command_ops_ready(context) || command_list == NULL)
        return -1;
    if (context->graphics_command_list == 0u) {
        if (ringl_backend_create_command_list(
                context, RINGL_RIN_GPU_QUEUE_GRAPHICS,
                &context->graphics_command_list) != 0 ||
            context->graphics_command_list == 0u) {
            context->graphics_command_list = 0u;
            return -1;
        }
        created = 1;
    }
    if (!created &&
        ringl_backend_reset_command_list(context,
                                         context->graphics_command_list) != 0)
        return -1;

    if (context->graphics_bind_group != 0u) {
        ringl_backend_destroy_object(context, context->graphics_bind_group);
        context->graphics_bind_group = 0u;
    }
    *command_list = context->graphics_command_list;
    return 0;
}

static int submit_commands(RinGLContext* context, uint64_t command_list)
{
    if (ringl_backend_close_command_list(context, command_list) != 0)
        return -1;
    if (ringl_backend_queue_submit(context, context->ringpu.graphics_queue,
                                   command_list) != 0)
        return -1;
    return 0;
}

static int transition_to_color_target(RinGLContext* context,
                                      uint64_t command_list,
                                      RinGLColorTarget* target)
{
    if (context == NULL || target == NULL || target->state == NULL)
        return -1;
    if (*target->state == RINGL_RIN_GPU_IMAGE_COLOR_TARGET)
        return 0;
    if (*target->state == RINGL_RIN_GPU_IMAGE_UNDEFINED &&
        context->framebuffer_binding == 0u)
        return -1;
    if (target->mip_level == 0u) {
        return ringl_backend_transition_image(
            context, command_list, target->image, *target->state,
            RINGL_RIN_GPU_IMAGE_COLOR_TARGET);
    }
    {
        RinGLRinGpuImageTransition2DMipV2 transition;

        memset(&transition, 0, sizeof(transition));
        transition.image = target->image;
        transition.mip_level = target->mip_level;
        transition.old_state = *target->state;
        transition.new_state = RINGL_RIN_GPU_IMAGE_COLOR_TARGET;
        return ringl_backend_transition_image_2d_mip_v2(context, command_list,
                                                         &transition);
    }
}

static int transition_to_depth_target(RinGLContext* context,
                                      uint64_t command_list,
                                      RinGLDepthTarget* target)
{
    if (context == NULL || target == NULL || target->image == 0u ||
        target->state == NULL)
        return -1;
    if (*target->state == RINGL_RIN_GPU_IMAGE_DEPTH_TARGET)
        return 0;
    if (target->mip_level == 0u) {
        return ringl_backend_transition_image(
            context, command_list, target->image, *target->state,
            RINGL_RIN_GPU_IMAGE_DEPTH_TARGET);
    }
    {
        RinGLRinGpuImageTransition2DMipV2 transition;

        memset(&transition, 0, sizeof(transition));
        transition.image = target->image;
        transition.mip_level = target->mip_level;
        transition.old_state = *target->state;
        transition.new_state = RINGL_RIN_GPU_IMAGE_DEPTH_TARGET;
        return ringl_backend_transition_image_2d_mip_v2(context, command_list,
                                                         &transition);
    }
}

static int transition_to_depth_stencil_targets(
    RinGLContext* context, uint64_t command_list,
    RinGLDepthStencilTargets* targets)
{
    if (context == NULL || targets == NULL)
        return -1;
    if (targets->depth.image != 0u && targets->depth.has_depth != 0u &&
        transition_to_depth_target(context, command_list, &targets->depth) != 0)
        return -1;
    if (targets->stencil.image != 0u && targets->stencil.has_stencil != 0u &&
        (targets->combined == 0u ||
         targets->stencil.image != targets->depth.image) &&
        transition_to_depth_target(context, command_list, &targets->stencil) !=
            0)
        return -1;
    return 0;
}

static uint32_t depth_stencil_pipeline_format(
    const RinGLDepthStencilTargets* targets, uint32_t use_depth_target)
{
    if (use_depth_target == 0u || targets == NULL)
        return 0u;
    if (targets->depth.has_depth != 0u &&
        targets->stencil.has_stencil != 0u)
        return RINGL_RIN_GPU_FORMAT_D32_FLOAT_S8_UINT;
    return targets->depth.has_depth != 0u ? targets->depth.format
                                          : targets->stencil.format;
}

static void configure_clear_region(const RinGLContext* context,
                                   const RinGLColorTarget* target,
                                   RinGLRinGpuClearRegionV1* region)
{
    int64_t x0;
    int64_t y0;
    int64_t x1;
    int64_t y1;

    if (context == NULL || target == NULL || region == NULL ||
        !context->scissor_enabled)
        return;
    x0 = context->scissor_x;
    y0 = context->scissor_y;
    x1 = x0 + (int64_t)context->scissor_width;
    y1 = y0 + (int64_t)context->scissor_height;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 < 0)
        x1 = 0;
    if (y1 < 0)
        y1 = 0;
    if (x0 > (int64_t)target->width)
        x0 = target->width;
    if (y0 > (int64_t)target->height)
        y0 = target->height;
    if (x1 > (int64_t)target->width)
        x1 = target->width;
    if (y1 > (int64_t)target->height)
        y1 = target->height;
    if (x1 < x0)
        x1 = x0;
    if (y1 < y0)
        y1 = y0;
    region->x = (int32_t)x0;
    region->y = (int32_t)y0;
    region->width = (uint32_t)(x1 - x0);
    region->height = (uint32_t)(y1 - y0);
    region->enabled = RINGL_TRUE;
}

static int begin_color_pass(RinGLContext* context,
                            uint64_t command_list,
                            const RinGLColorTarget* target,
                            uint32_t load_op)
{
    RinGLRinGpuRenderPassV1 render_pass;

    if (target == NULL || target->image == 0u)
        return -1;
    memset(&render_pass, 0, sizeof(render_pass));
    render_pass.color_target = target->image;
    render_pass.load_op = load_op;
    render_pass.store_op = RINGL_RIN_GPU_RENDER_STORE;
    if (load_op == RINGL_RIN_GPU_RENDER_CLEAR) {
        render_pass.clear_red = clamp_color(context->clear_red);
        render_pass.clear_green = clamp_color(context->clear_green);
        render_pass.clear_blue = clamp_color(context->clear_blue);
        render_pass.clear_alpha = clamp_color(context->clear_alpha);
        render_pass.color_write_mask = context->color_write_mask;
        configure_clear_region(context, target, &render_pass.clear_region);
    }
    if (target->mip_level == 0u)
        return ringl_backend_begin_render_pass(context, command_list, &render_pass);
    {
        RinGLRinGpuRenderPassMipV2 mip_render_pass;

        memset(&mip_render_pass, 0, sizeof(mip_render_pass));
        mip_render_pass.base = render_pass;
        mip_render_pass.color_mip_level = target->mip_level;
        return ringl_backend_begin_render_pass_mip_v2(context, command_list,
                                                       &mip_render_pass);
    }
}

static int begin_depth_pass(RinGLContext* context, uint64_t command_list,
                            const RinGLColorTarget* color_target,
                            const RinGLDepthStencilTargets* targets,
                            uint32_t color_load_op, uint32_t depth_load_op,
                            uint32_t stencil_load_op)
{
    RinGLRinGpuRenderPassDepthV1 render_pass;
    const RinGLDepthTarget* depth_target;

    if (context == NULL || color_target == NULL || targets == NULL ||
        color_target->image == 0u)
        return -1;
    if (targets->depth.has_depth != 0u &&
        targets->stencil.has_stencil != 0u && targets->combined == 0u) {
        RinGLRinGpuRenderPassDepthStencilV1 separate_pass;

        if (context->ringpu_ops.begin_render_pass_depth_stencil == NULL)
            return -1;
        memset(&separate_pass, 0, sizeof(separate_pass));
        separate_pass.color_target = color_target->image;
        separate_pass.depth_target = targets->depth.image;
        separate_pass.stencil_target = targets->stencil.image;
        separate_pass.color_load_op = color_load_op;
        separate_pass.color_store_op = RINGL_RIN_GPU_RENDER_STORE;
        separate_pass.depth_load_op = depth_load_op;
        separate_pass.depth_store_op = RINGL_RIN_GPU_RENDER_STORE;
        separate_pass.stencil_load_op = stencil_load_op;
        separate_pass.stencil_store_op = RINGL_RIN_GPU_RENDER_STORE;
        if (color_load_op == RINGL_RIN_GPU_RENDER_CLEAR) {
            separate_pass.clear_red = clamp_color(context->clear_red);
            separate_pass.clear_green = clamp_color(context->clear_green);
            separate_pass.clear_blue = clamp_color(context->clear_blue);
            separate_pass.clear_alpha = clamp_color(context->clear_alpha);
            separate_pass.color_write_mask = context->color_write_mask;
        }
        if (depth_load_op == RINGL_RIN_GPU_RENDER_CLEAR)
            separate_pass.clear_depth = clamp_color(context->clear_depth);
        if (stencil_load_op == RINGL_RIN_GPU_RENDER_CLEAR) {
            separate_pass.clear_stencil = context->clear_stencil;
            separate_pass.stencil_write_mask = context->stencil_write_mask;
        }
        if (color_load_op == RINGL_RIN_GPU_RENDER_CLEAR ||
            depth_load_op == RINGL_RIN_GPU_RENDER_CLEAR ||
            stencil_load_op == RINGL_RIN_GPU_RENDER_CLEAR) {
            configure_clear_region(context, color_target,
                                   &separate_pass.clear_region);
        }
        if (color_target->mip_level == 0u &&
            targets->depth.mip_level == 0u &&
            targets->stencil.mip_level == 0u) {
            return ringl_backend_begin_render_pass_depth_stencil(
                context, command_list, &separate_pass);
        }
        {
            RinGLRinGpuRenderPassDepthStencilMipV2 mip_pass;

            memset(&mip_pass, 0, sizeof(mip_pass));
            mip_pass.base = separate_pass;
            mip_pass.color_mip_level = color_target->mip_level;
            mip_pass.depth_mip_level = targets->depth.mip_level;
            mip_pass.stencil_mip_level = targets->stencil.mip_level;
            return ringl_backend_begin_render_pass_depth_stencil_mip_v2(
                context, command_list, &mip_pass);
        }
    }
    depth_target = targets->depth.has_depth != 0u ? &targets->depth
                                                   : &targets->stencil;
    if (depth_target->image == 0u)
        return -1;
    memset(&render_pass, 0, sizeof(render_pass));
    render_pass.color_target = color_target->image;
    render_pass.depth_target = depth_target->image;
    render_pass.color_load_op = color_load_op;
    render_pass.color_store_op = RINGL_RIN_GPU_RENDER_STORE;
    render_pass.depth_load_op = depth_load_op;
    render_pass.depth_store_op = RINGL_RIN_GPU_RENDER_STORE;
    if (targets->stencil.has_stencil != 0u) {
        render_pass.stencil_load_op = stencil_load_op;
        render_pass.stencil_store_op = RINGL_RIN_GPU_RENDER_STORE;
        if (stencil_load_op == RINGL_RIN_GPU_RENDER_CLEAR) {
            render_pass.clear_stencil = context->clear_stencil;
            /* ES 2.0 §4.2.2 requires Clear to use the front write mask. */
            render_pass.stencil_write_mask = context->stencil_write_mask;
        }
    } else if (depth_target->format == RINGL_RIN_GPU_FORMAT_D32_FLOAT_S8_UINT) {
        /* Preserve an unbound physical S8 plane while addressing only depth. */
        render_pass.stencil_load_op = RINGL_RIN_GPU_RENDER_LOAD;
        render_pass.stencil_store_op = RINGL_RIN_GPU_RENDER_STORE;
    } else if (stencil_load_op != 0u) {
        return -1;
    }
    if (color_load_op == RINGL_RIN_GPU_RENDER_CLEAR) {
        render_pass.clear_red = clamp_color(context->clear_red);
        render_pass.clear_green = clamp_color(context->clear_green);
        render_pass.clear_blue = clamp_color(context->clear_blue);
        render_pass.clear_alpha = clamp_color(context->clear_alpha);
        render_pass.color_write_mask = context->color_write_mask;
    }
    if (depth_load_op == RINGL_RIN_GPU_RENDER_CLEAR)
        render_pass.clear_depth = clamp_color(context->clear_depth);
    if (color_load_op == RINGL_RIN_GPU_RENDER_CLEAR ||
        depth_load_op == RINGL_RIN_GPU_RENDER_CLEAR ||
        stencil_load_op == RINGL_RIN_GPU_RENDER_CLEAR) {
        configure_clear_region(context, color_target, &render_pass.clear_region);
    }
    if (color_target->mip_level == 0u && depth_target->mip_level == 0u)
        return ringl_backend_begin_render_pass_depth(context, command_list,
                                                      &render_pass);
    {
        RinGLRinGpuRenderPassDepthMipV2 mip_pass;

        memset(&mip_pass, 0, sizeof(mip_pass));
        mip_pass.base = render_pass;
        mip_pass.color_mip_level = color_target->mip_level;
        mip_pass.depth_mip_level = depth_target->mip_level;
        return ringl_backend_begin_render_pass_depth_mip_v2(
            context, command_list, &mip_pass);
    }
}

static int set_raster_state(RinGLContext* context, uint64_t command_list,
                            const RinGLColorTarget* target)
{
    RinGLRinGpuRasterStateV1 state;
    int64_t x0;
    int64_t y0;
    int64_t x1;
    int64_t y1;

    if (context == NULL || target == NULL ||
        context->ringpu_ops.set_raster_state == NULL)
        return 0;

    memset(&state, 0, sizeof(state));
    state.viewport_x = (float)context->viewport_x;
    state.viewport_y = (float)context->viewport_y;
    state.viewport_width = (float)context->viewport_width;
    state.viewport_height = (float)context->viewport_height;
    state.min_depth = context->depth_range_near;
    state.max_depth = context->depth_range_far;
    state.polygon_offset_fill_enabled = context->polygon_offset_fill_enabled;
    state.polygon_offset_factor = context->polygon_offset_factor;
    state.polygon_offset_units = context->polygon_offset_units;
    state.line_width = context->line_width;
    state.sample_coverage_enabled = context->sample_coverage_enabled;
    state.sample_coverage_value = context->sample_coverage_value;
    state.sample_coverage_invert = context->sample_coverage_invert;

    if (context->scissor_enabled) {
        x0 = context->scissor_x;
        y0 = context->scissor_y;
        x1 = x0 + (int64_t)context->scissor_width;
        y1 = y0 + (int64_t)context->scissor_height;
        if (x0 < 0)
            x0 = 0;
        if (y0 < 0)
            y0 = 0;
        if (x1 < 0)
            x1 = 0;
        if (y1 < 0)
            y1 = 0;
        if (x0 > (int64_t)target->width)
            x0 = target->width;
        if (y0 > (int64_t)target->height)
            y0 = target->height;
        if (x1 > (int64_t)target->width)
            x1 = target->width;
        if (y1 > (int64_t)target->height)
            y1 = target->height;
        if (x1 < x0)
            x1 = x0;
        if (y1 < y0)
            y1 = y0;
        state.scissor_x = (int32_t)x0;
        state.scissor_y = (int32_t)y0;
        state.scissor_width = (uint32_t)(x1 - x0);
        state.scissor_height = (uint32_t)(y1 - y0);
        state.scissor_enabled = 1u;
    }
    return ringl_backend_set_raster_state(context, command_list, &state);
}

static RinGLProgramObject* current_program(RinGLContext* context)
{
    uint32_t index;

    if (context == NULL || context->current_program == 0u ||
        ringl_object_lookup(context, context->current_program,
                            RINGL_OBJECT_PROGRAM) == NULL)
        return NULL;
    index = ringl_object_slot_index(context->current_program);
    if (index >= RINGL_OBJECT_SLOT_COUNT)
        return NULL;
    return &context->programs[index];
}

static RinGLShaderObject* linked_fragment_shader(
    RinGLContext* context, const RinGLProgramObject* program)
{
    uint32_t index;

    if (context == NULL || program == NULL ||
        program->linked_fragment_shader == 0u ||
        ringl_object_lookup(context, program->linked_fragment_shader,
                            RINGL_OBJECT_SHADER) == NULL) {
        return NULL;
    }
    index = ringl_object_slot_index(program->linked_fragment_shader);
    if (index >= RINGL_OBJECT_SLOT_COUNT)
        return NULL;
    return &context->shaders[index];
}

static int program_sampler_index_for_name(const RinGLProgramObject* program,
                                          const char* name,
                                          uint32_t* index_out)
{
    uint32_t index;

    if (program == NULL || name == NULL || index_out == NULL)
        return -1;
    for (index = 0u; index < program->sampler_uniform_count; ++index) {
        if (strcmp(program->sampler_uniforms[index].name, name) == 0) {
            *index_out = index;
            return 0;
        }
    }
    return -1;
}

typedef struct RinGLTextureTransitionSet {
    uint32_t texture_indices[RINGL_MAX_SAMPLER_UNIFORMS];
    uint32_t count;
} RinGLTextureTransitionSet;

static int texture_transition_set_contains(
    const RinGLTextureTransitionSet* set, uint32_t texture_index)
{
    uint32_t index;

    if (set == NULL)
        return 0;
    for (index = 0u; index < set->count; ++index) {
        if (set->texture_indices[index] == texture_index)
            return 1;
    }
    return 0;
}

static int texture_transition_set_add(RinGLTextureTransitionSet* set,
                                      uint32_t texture_index)
{
    if (set == NULL || set->count >= RINGL_MAX_SAMPLER_UNIFORMS)
        return -1;
    set->texture_indices[set->count++] = texture_index;
    return 0;
}

static int prepare_graphics_resources(RinGLContext* context,
                                      uint64_t command_list,
                                      uint64_t pipeline,
                                      uint64_t color_target,
                                      uint64_t depth_target,
                                      uint64_t stencil_target,
                                      RinGLTextureTransitionSet*
                                          transitioned_out)
{
    RinGLProgramObject* program;
    RinGLShaderObject* fragment;
    RinGLRinGpuGraphicsBindingV1
        bindings[RINGL_MAX_SAMPLER_UNIFORMS * 2u];
    uint64_t image;
    uint64_t sampler;
    uint32_t texture_name;
    uint32_t texture_index;
    uint32_t sampler_binding_index;
    uint32_t binding_count;
    RinGLTextureTransitionSet transitioned = {0};
    int32_t unit;

    if (transitioned_out != NULL)
        memset(transitioned_out, 0, sizeof(*transitioned_out));

    program = current_program(context);
    if (program == NULL)
        return -1;
    fragment = linked_fragment_shader(context, program);
    if (fragment == NULL)
        return -1;
    if (fragment->rsh1_sampler_binding_count == 0u)
        return 0;
    if (program->sampler_uniform_count > RINGL_MAX_SAMPLER_UNIFORMS ||
        fragment->rsh1_sampler_binding_count > RINGL_MAX_SAMPLER_UNIFORMS ||
        context->ringpu_ops.create_graphics_bind_group == NULL ||
        context->ringpu_ops.bind_graphics_resources == NULL)
        return -1;

    memset(bindings, 0, sizeof(bindings));
    binding_count = fragment->rsh1_sampler_binding_count * 2u;
    for (sampler_binding_index = 0u;
         sampler_binding_index < fragment->rsh1_sampler_binding_count;
         ++sampler_binding_index) {
        RinGLTextureObject* texture;
        uint32_t shader_sampler_index =
            fragment->rsh1_sampler_binding_indices[sampler_binding_index];
        uint32_t program_sampler_index;

        if (shader_sampler_index >= fragment->sampler_uniform_count ||
            program_sampler_index_for_name(
                program, fragment->sampler_uniform_names[shader_sampler_index],
                &program_sampler_index) != 0) {
            return -1;
        }
        unit = program->sampler_uniforms[program_sampler_index].texture_unit;
        if (unit < 0 || (uint32_t)unit >= RINGL_MAX_TEXTURE_UNITS)
            return -1;
        texture_name = context->bound_texture_2d[(uint32_t)unit];
        if (texture_name == 0u ||
            ringl_object_lookup(context, texture_name,
                                RINGL_OBJECT_TEXTURE) == NULL) {
            return -1;
        }
        texture_index = ringl_object_slot_index(texture_name);
        if (texture_index >= RINGL_OBJECT_SLOT_COUNT)
            return -1;
        texture = &context->textures[texture_index];
        if (ringl_texture_realize_unit(context, (uint32_t)unit, &image,
                                       &sampler) != 0 ||
            image == 0u || sampler == 0u || image == color_target ||
            image == depth_target || image == stencil_target) {
            return -1;
        }
        if (texture->ringpu_image_state[0] != RINGL_RIN_GPU_IMAGE_SHADER_READ &&
            !texture_transition_set_contains(&transitioned, texture_index)) {
            if (ringl_backend_transition_image(
                    context, command_list, image,
                    texture->ringpu_image_state[0],
                    RINGL_RIN_GPU_IMAGE_SHADER_READ) != 0) {
                return -1;
            }
            if (texture_transition_set_add(&transitioned, texture_index) != 0)
                return -1;
        }
        bindings[sampler_binding_index * 2u].binding =
            sampler_binding_index * 2u;
        bindings[sampler_binding_index * 2u].kind =
            RINGL_RIN_GPU_RESOURCE_SAMPLED_IMAGE;
        bindings[sampler_binding_index * 2u].access =
            RINGL_RIN_GPU_RESOURCE_READ;
        bindings[sampler_binding_index * 2u].resource = image;
        bindings[sampler_binding_index * 2u + 1u].binding =
            sampler_binding_index * 2u + 1u;
        bindings[sampler_binding_index * 2u + 1u].kind =
            RINGL_RIN_GPU_RESOURCE_SAMPLER;
        bindings[sampler_binding_index * 2u + 1u].resource = sampler;
    }

    if (ringl_backend_create_graphics_bind_group(
            context, pipeline, bindings, binding_count,
            &context->graphics_bind_group) != 0 ||
        context->graphics_bind_group == 0u)
        return -1;

    if (transitioned_out != NULL)
        *transitioned_out = transitioned;
    return 0;
}

static int bind_graphics_resources(RinGLContext* context,
                                   uint64_t command_list)
{
    if (context->graphics_bind_group == 0u)
        return 0;
    return ringl_backend_bind_graphics_resources(
        context, command_list, context->graphics_bind_group);
}

static void publish_texture_transitions(RinGLContext* context,
                                        const RinGLTextureTransitionSet*
                                            transitioned)
{
    uint32_t index;

    if (context == NULL || transitioned == NULL ||
        transitioned->count > RINGL_MAX_SAMPLER_UNIFORMS)
        return;
    for (index = 0u; index < transitioned->count; ++index) {
        if (transitioned->texture_indices[index] >= RINGL_OBJECT_SLOT_COUNT)
            return;
    }
    for (index = 0u; index < transitioned->count; ++index) {
        uint32_t texture_index = transitioned->texture_indices[index];

        context->textures[texture_index].ringpu_image_state[0] =
            RINGL_RIN_GPU_IMAGE_SHADER_READ;
    }
}

int ringl_get_clear_values(RinGLClearValuesV1* values)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLClearValuesV1 snapshot;

    if (context == NULL || values == NULL ||
        values->struct_size < sizeof(*values) ||
        values->api_version != RINGL_API_VERSION)
        return -1;

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.struct_size = sizeof(snapshot);
    snapshot.api_version = RINGL_API_VERSION;
    snapshot.red = context->clear_red;
    snapshot.green = context->clear_green;
    snapshot.blue = context->clear_blue;
    snapshot.alpha = context->clear_alpha;
    snapshot.depth = context->clear_depth;
    snapshot.stencil = (int32_t)context->clear_stencil;
    *values = snapshot;
    return 0;
}

void ringl_clear_color(float red, float green, float blue, float alpha)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL)
        return;
    context->clear_red = red;
    context->clear_green = green;
    context->clear_blue = blue;
    context->clear_alpha = alpha;
}

void ringl_clear_depth(float depth)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL)
        return;
    context->clear_depth = depth;
}

void ringl_clear_stencil(int32_t stencil)
{
    RinGLContext* context = ringl_get_current_context();

    if (context == NULL)
        return;
    context->clear_stencil = (uint32_t)stencil & 0xffu;
}

static uint32_t ringl_submit_clear(RinGLContext* context, uint32_t mask)
{
    RinGLColorTarget target;
    RinGLDepthStencilTargets depth_targets;
    uint64_t command_list;
    int depth_status;
    int use_depth_pass;
    uint32_t color_load_op;
    uint32_t depth_load_op;
    uint32_t stencil_load_op;

    if (context == NULL)
        return RINGL_INVALID_OPERATION;
    if (mask == 0u)
        return RINGL_NO_ERROR;
    if ((mask & ~(RINGL_COLOR_BUFFER_BIT | RINGL_DEPTH_BUFFER_BIT |
                  RINGL_STENCIL_BUFFER_BIT)) != 0u) {
        return RINGL_INVALID_VALUE;
    }
    if (!command_ops_ready(context) ||
        ringl_resolve_color_target(context, &target) != 0) {
        return RINGL_INVALID_OPERATION;
    }
    depth_status = ringl_resolve_depth_stencil_targets(context, &depth_targets);
    if (depth_status < 0) {
        return RINGL_INVALID_OPERATION;
    }
    color_load_op = (mask & RINGL_COLOR_BUFFER_BIT) != 0u
        ? RINGL_RIN_GPU_RENDER_CLEAR
        : RINGL_RIN_GPU_RENDER_LOAD;
    depth_load_op = (depth_status == 0 &&
                     depth_targets.depth.has_depth != 0u &&
                     (mask & RINGL_DEPTH_BUFFER_BIT) != 0u &&
                     context->depth_write_mask != 0u)
        ? RINGL_RIN_GPU_RENDER_CLEAR
        : RINGL_RIN_GPU_RENDER_LOAD;
    stencil_load_op = depth_status == 0 &&
            depth_targets.stencil.has_stencil != 0u &&
            (depth_targets.stencil.format ==
                 RINGL_RIN_GPU_FORMAT_D32_FLOAT_S8_UINT ||
             depth_targets.stencil.format == RINGL_RIN_GPU_FORMAT_S8_UINT)
        ? ((mask & RINGL_STENCIL_BUFFER_BIT) != 0u
               ? RINGL_RIN_GPU_RENDER_CLEAR
               : RINGL_RIN_GPU_RENDER_LOAD)
        : 0u;
    use_depth_pass = depth_load_op == RINGL_RIN_GPU_RENDER_CLEAR ||
                     stencil_load_op == RINGL_RIN_GPU_RENDER_CLEAR;
    if (use_depth_pass &&
        ((depth_targets.depth.has_depth != 0u &&
          depth_targets.stencil.has_stencil != 0u &&
          depth_targets.combined == 0u)
             ? context->ringpu_ops.begin_render_pass_depth_stencil == NULL
             : context->ringpu_ops.begin_render_pass_depth == NULL)) {
        return RINGL_INVALID_OPERATION;
    }
    if (color_load_op != RINGL_RIN_GPU_RENDER_CLEAR && !use_depth_pass)
        return RINGL_NO_ERROR;
    if (begin_commands(context, &command_list) != 0 ||
        transition_to_color_target(context, command_list, &target) != 0 ||
        (use_depth_pass && transition_to_depth_stencil_targets(
                               context, command_list, &depth_targets) != 0) ||
        (use_depth_pass
             ? begin_depth_pass(context, command_list, &target, &depth_targets,
                                color_load_op, depth_load_op,
                                stencil_load_op)
             : begin_color_pass(context, command_list, &target,
                                color_load_op)) != 0 ||
        ringl_backend_end_render_pass(context, command_list) != 0 ||
        submit_commands(context, command_list) != 0) {
        return RINGL_INVALID_OPERATION;
    }
    *target.state = RINGL_RIN_GPU_IMAGE_COLOR_TARGET;
    if (use_depth_pass && depth_targets.depth.state != NULL)
        *depth_targets.depth.state = RINGL_RIN_GPU_IMAGE_DEPTH_TARGET;
    if (use_depth_pass && depth_targets.stencil.state != NULL)
        *depth_targets.stencil.state = RINGL_RIN_GPU_IMAGE_DEPTH_TARGET;
    ringl_context_clear_dirty(context, RINGL_DIRTY_FRAMEBUFFER);
    return RINGL_NO_ERROR;
}

void ringl_clear(uint32_t mask)
{
    RinGLContext* context = ringl_get_current_context();
    uint32_t error;

    if (context == NULL)
        return;
    error = ringl_submit_clear(context, mask);
    if (error != RINGL_NO_ERROR)
        ringl_context_record_error(context, error);
}

int ringl_clear_default_framebuffer_for_embedding(uint32_t* error_out)
{
    RinGLContext* context = ringl_get_current_context();
    uint32_t saved_framebuffer_binding;
    uint32_t saved_dirty_bits;
    uint32_t saved_scissor_enabled;
    uint32_t saved_color_write_mask;
    uint32_t saved_depth_write_mask;
    uint32_t saved_stencil_write_mask;
    float saved_clear_red;
    float saved_clear_green;
    float saved_clear_blue;
    float saved_clear_alpha;
    float saved_clear_depth;
    uint32_t saved_clear_stencil;
    uint32_t error;

    if (error_out == NULL)
        return -1;
    *error_out = RINGL_INVALID_OPERATION;
    if (context == NULL)
        return -1;

    saved_framebuffer_binding = context->framebuffer_binding;
    saved_dirty_bits = context->dirty_bits;
    saved_scissor_enabled = context->scissor_enabled;
    saved_color_write_mask = context->color_write_mask;
    saved_depth_write_mask = context->depth_write_mask;
    saved_stencil_write_mask = context->stencil_write_mask;
    saved_clear_red = context->clear_red;
    saved_clear_green = context->clear_green;
    saved_clear_blue = context->clear_blue;
    saved_clear_alpha = context->clear_alpha;
    saved_clear_depth = context->clear_depth;
    saved_clear_stencil = context->clear_stencil;

    context->framebuffer_binding = 0u;
    context->scissor_enabled = 0u;
    context->color_write_mask = RINGL_RIN_GPU_COLOR_WRITE_ALL;
    context->depth_write_mask = RINGL_TRUE;
    context->stencil_write_mask = 0xffu;
    context->clear_red = 0.0f;
    context->clear_green = 0.0f;
    context->clear_blue = 0.0f;
    context->clear_alpha = 0.0f;
    context->clear_depth = 1.0f;
    context->clear_stencil = 0u;

    error = ringl_submit_clear(
        context, RINGL_COLOR_BUFFER_BIT | RINGL_DEPTH_BUFFER_BIT |
                     RINGL_STENCIL_BUFFER_BIT);

    context->framebuffer_binding = saved_framebuffer_binding;
    context->dirty_bits = saved_dirty_bits;
    context->scissor_enabled = saved_scissor_enabled;
    context->color_write_mask = saved_color_write_mask;
    context->depth_write_mask = saved_depth_write_mask;
    context->stencil_write_mask = saved_stencil_write_mask;
    context->clear_red = saved_clear_red;
    context->clear_green = saved_clear_green;
    context->clear_blue = saved_clear_blue;
    context->clear_alpha = saved_clear_alpha;
    context->clear_depth = saved_clear_depth;
    context->clear_stencil = saved_clear_stencil;

    *error_out = error;
    return error == RINGL_NO_ERROR ? 0 : -1;
}

static int ringl_resolve_vertex_buffer_bindings(
    const RinGLContext* context, const RinGLResolvedVertexLayout* layout,
    RinGLRinGpuVertexBufferBindingV1* bindings)
{
    uint32_t binding;

    if (context == NULL || layout == NULL || bindings == NULL ||
        layout->binding_count == 0u ||
        layout->binding_count > RINGL_MAX_VERTEX_ATTRIBS)
        return -1;
    for (binding = 0u; binding < layout->binding_count; ++binding) {
        uint32_t index = ringl_object_slot_index(layout->bindings[binding].buffer);
        const RinGLBufferObject* buffer;

        if (layout->bindings[binding].buffer == 0u ||
            layout->bindings[binding].stride == 0u ||
            index >= RINGL_OBJECT_SLOT_COUNT)
            return -1;
        buffer = &context->buffers[index];
        if (buffer->ringpu_handle == 0u)
            return -1;
        bindings[binding].binding = binding;
        bindings[binding].buffer = buffer->ringpu_handle;
    }
    return 0;
}

static uint32_t ringl_native_primitive_topology(uint32_t mode)
{
    switch (mode) {
    case RINGL_POINTS:
        return RINGL_NATIVE_PRIMITIVE_POINT_LIST;
    case RINGL_LINES:
        return RINGL_NATIVE_PRIMITIVE_LINE_LIST;
    case RINGL_LINE_LOOP:
        return RINGL_NATIVE_PRIMITIVE_LINE_LOOP;
    case RINGL_LINE_STRIP:
        return RINGL_NATIVE_PRIMITIVE_LINE_STRIP;
    case RINGL_TRIANGLES:
        return RINGL_NATIVE_PRIMITIVE_TRIANGLE_LIST;
    case RINGL_TRIANGLE_STRIP:
        return RINGL_NATIVE_PRIMITIVE_TRIANGLE_STRIP;
    case RINGL_TRIANGLE_FAN:
        return RINGL_NATIVE_PRIMITIVE_TRIANGLE_FAN;
    default:
        return 0u;
    }
}

void ringl_draw_arrays(uint32_t mode, int32_t first, int32_t count)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLResolvedVertexLayout layout;
    RinGLBufferObject* vertex_buffer = NULL;
    RinGLRinGpuDrawVerticesV1 draw;
    RinGLRinGpuDrawVerticesV2 draw_v2;
    RinGLRinGpuDrawVerticesMipV3 draw_mip;
    RinGLRinGpuDrawVerticesBindingsMipV3 draw_bindings_mip;
    RinGLRinGpuVertexBufferBindingV1
        vertex_bindings[RINGL_MAX_VERTEX_ATTRIBS];
    RinGLColorTarget target;
    RinGLDepthStencilTargets depth_targets;
    uint64_t command_list;
    uint64_t pipeline;
    uint32_t buffer_index;
    uint32_t primitive_topology;
    RinGLTextureTransitionSet texture_transitions = {0};
    uint32_t effective_depth_test;
    uint32_t effective_stencil_test;
    uint32_t use_depth_target;
    int depth_status;
    int use_multi_buffer;
    int draw_result;

    if (context == NULL)
        return;
    primitive_topology = ringl_native_primitive_topology(mode);
    if (primitive_topology == 0u) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (first < 0 || count < 0) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (count == 0 || draw_is_noop(context))
        return;
    depth_status = ringl_resolve_depth_stencil_targets(context, &depth_targets);
    effective_depth_test =
        depth_status == 0 && depth_targets.depth.has_depth != 0u
        ? context->depth_test_enabled
        : RINGL_FALSE;
    effective_stencil_test =
        depth_status == 0 && depth_targets.stencil.has_stencil != 0u
        ? context->stencil_test_enabled
        : RINGL_FALSE;
    use_depth_target =
        effective_depth_test != 0u || effective_stencil_test != 0u;
    if (!command_ops_ready(context) ||
        ringl_resolve_color_target(context, &target) != 0 ||
        depth_status < 0 ||
        !draw_state_supported(context, &target,
                              use_depth_target != 0u ? &depth_targets : NULL,
                              effective_depth_test,
                              effective_stencil_test)) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if (ringl_validate_vertex_fetch(context, (uint32_t)first,
                                    (uint32_t)count, &layout) != 0 ||
        (layout.has_constant_attributes != 0u &&
         (context->ringpu.vertex_input_capabilities &
          RINGL_RIN_GPU_VERTEX_INPUT_CONSTANT_FLOAT32) == 0u)) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    use_multi_buffer = layout.binding_count > 1u;
    if ((use_multi_buffer &&
         (((context->ringpu.vertex_input_capabilities &
            RINGL_RIN_GPU_VERTEX_INPUT_MULTI_BUFFER) == 0u) ||
          context->ringpu_ops.draw_vertices_v2 == NULL ||
          (context->ringpu_ops.create_graphics_pipeline_vertex_bindings == NULL &&
           context->ringpu_ops
                   .create_graphics_pipeline_native_vertex_bindings == NULL))) ||
        (!use_multi_buffer &&
         (context->ringpu_ops.draw_vertices == NULL ||
          (context->ringpu_ops.create_graphics_pipeline == NULL &&
           context->ringpu_ops.create_graphics_pipeline_native == NULL)))) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    memset(vertex_bindings, 0, sizeof(vertex_bindings));
    if (use_multi_buffer) {
        if (ringl_resolve_vertex_buffer_bindings(context, &layout,
                                                 vertex_bindings) != 0) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return;
        }
    } else if (layout.buffer != 0u) {
        buffer_index = ringl_object_slot_index(layout.buffer);
        if (buffer_index >= RINGL_OBJECT_SLOT_COUNT) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return;
        }
        vertex_buffer = &context->buffers[buffer_index];
        if (vertex_buffer->ringpu_handle == 0u) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return;
        }
    }

    if (begin_commands(context, &command_list) != 0 ||
        ringl_get_or_create_graphics_pipeline(
            context, target.format,
            depth_stencil_pipeline_format(&depth_targets, use_depth_target),
            primitive_topology,
            effective_depth_test, effective_stencil_test,
            &pipeline) != 0 ||
        pipeline == 0u ||
        prepare_graphics_resources(context, command_list, pipeline, target.image,
                                   use_depth_target != 0u ?
                                       depth_targets.depth.image : 0u,
                                   use_depth_target != 0u ?
                                       depth_targets.stencil.image : 0u,
                                   &texture_transitions) != 0 ||
        transition_to_color_target(context, command_list, &target) != 0 ||
        (use_depth_target != 0u && transition_to_depth_stencil_targets(
                                      context, command_list, &depth_targets) != 0) ||
        (use_depth_target != 0u
             ? begin_depth_pass(context, command_list, &target, &depth_targets,
                                RINGL_RIN_GPU_RENDER_LOAD,
                                RINGL_RIN_GPU_RENDER_LOAD,
                                depth_targets.stencil.has_stencil != 0u
                                    ? RINGL_RIN_GPU_RENDER_LOAD
                                    : 0u)
             : begin_color_pass(context, command_list, &target,
                                RINGL_RIN_GPU_RENDER_LOAD)) != 0 ||
        set_raster_state(context, command_list, &target) != 0 ||
        bind_graphics_resources(context, command_list) != 0) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }

    if (use_multi_buffer) {
        memset(&draw_v2, 0, sizeof(draw_v2));
        draw_v2.pipeline = pipeline;
        draw_v2.color_target = target.image;
        draw_v2.vertex_count = (uint32_t)count;
        draw_v2.first_vertex = (uint32_t)first;
        draw_v2.instance_count = 1u;
        draw_v2.binding_count = layout.binding_count;
        memcpy(draw_v2.vertex_buffers, vertex_bindings,
               sizeof(RinGLRinGpuVertexBufferBindingV1) *
                   layout.binding_count);
        if (target.mip_level == 0u) {
            draw_result = ringl_backend_draw_vertices_v2(context, command_list,
                                                         &draw_v2);
        } else {
            memset(&draw_bindings_mip, 0, sizeof(draw_bindings_mip));
            draw_bindings_mip.base = draw_v2;
            draw_bindings_mip.color_mip_level = target.mip_level;
            draw_result = ringl_backend_draw_vertices_bindings_mip_v3(
                context, command_list, &draw_bindings_mip);
        }
    } else {
        memset(&draw, 0, sizeof(draw));
        draw.pipeline = pipeline;
        draw.color_target = target.image;
        draw.vertex_buffer =
            vertex_buffer != NULL ? vertex_buffer->ringpu_handle : 0u;
        draw.vertex_count = (uint32_t)count;
        draw.first_vertex = (uint32_t)first;
        draw.instance_count = 1u;
        if (target.mip_level == 0u) {
            draw_result = ringl_backend_draw_vertices(context, command_list,
                                                      &draw);
        } else {
            memset(&draw_mip, 0, sizeof(draw_mip));
            draw_mip.base = draw;
            draw_mip.color_mip_level = target.mip_level;
            draw_result = ringl_backend_draw_vertices_mip_v3(context,
                                                              command_list,
                                                              &draw_mip);
        }
    }
    if (draw_result != 0 ||
        ringl_backend_end_render_pass(context, command_list) != 0 ||
        submit_commands(context, command_list) != 0) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    publish_texture_transitions(context, &texture_transitions);
    *target.state = RINGL_RIN_GPU_IMAGE_COLOR_TARGET;
    if (use_depth_target != 0u && depth_targets.depth.state != NULL)
        *depth_targets.depth.state = RINGL_RIN_GPU_IMAGE_DEPTH_TARGET;
    if (use_depth_target != 0u && depth_targets.stencil.state != NULL)
        *depth_targets.stencil.state = RINGL_RIN_GPU_IMAGE_DEPTH_TARGET;
    ringl_context_clear_dirty(context, RINGL_DIRTY_PIPELINE |
                                       RINGL_DIRTY_BINDINGS |
                                       RINGL_DIRTY_FRAMEBUFFER |
                                       RINGL_DIRTY_VIEWPORT);
}

void ringl_draw_elements(uint32_t mode, int32_t count, uint32_t type,
                         uint64_t offset)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLResolvedVertexLayout layout;
    RinGLBufferObject* vertex_buffer = NULL;
    RinGLBufferObject* index_buffer;
    RinGLRinGpuDrawIndexedV1 draw;
    RinGLRinGpuDrawIndexedV2 draw_v2;
    RinGLRinGpuDrawIndexedMipV3 draw_mip;
    RinGLRinGpuDrawIndexedBindingsMipV3 draw_bindings_mip;
    RinGLRinGpuVertexBufferBindingV1
        vertex_bindings[RINGL_MAX_VERTEX_ATTRIBS];
    RinGLColorTarget target;
    RinGLDepthStencilTargets depth_targets;
    uint64_t command_list;
    uint64_t pipeline;
    uint32_t max_index;
    uint32_t vertex_count;
    uint32_t vertex_buffer_index;
    uint32_t index_buffer_index;
    uint32_t primitive_topology;
    RinGLTextureTransitionSet texture_transitions = {0};
    uint32_t effective_depth_test;
    uint32_t effective_stencil_test;
    uint32_t use_depth_target;
    int depth_status;
    int use_multi_buffer;
    int draw_result;

    if (context == NULL)
        return;
    primitive_topology = ringl_native_primitive_topology(mode);
    if (primitive_topology == 0u) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (count < 0) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (type != RINGL_UNSIGNED_BYTE && type != RINGL_UNSIGNED_SHORT &&
        type != RINGL_UNSIGNED_INT) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (count == 0 || draw_is_noop(context))
        return;
    depth_status = ringl_resolve_depth_stencil_targets(context, &depth_targets);
    effective_depth_test =
        depth_status == 0 && depth_targets.depth.has_depth != 0u
        ? context->depth_test_enabled
        : RINGL_FALSE;
    effective_stencil_test =
        depth_status == 0 && depth_targets.stencil.has_stencil != 0u
        ? context->stencil_test_enabled
        : RINGL_FALSE;
    use_depth_target =
        effective_depth_test != 0u || effective_stencil_test != 0u;
    if (!command_ops_ready(context) ||
        ringl_resolve_color_target(context, &target) != 0 ||
        depth_status < 0 ||
        !draw_state_supported(context, &target,
                              use_depth_target != 0u ? &depth_targets : NULL,
                              effective_depth_test,
                              effective_stencil_test)) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if (ringl_validate_index_fetch(context, type, offset, (uint32_t)count,
                                   &max_index) != 0 ||
        max_index == UINT32_MAX) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    vertex_count = max_index + 1u;
    if (ringl_validate_vertex_fetch(context, 0u, vertex_count, &layout) != 0 ||
        (layout.has_constant_attributes != 0u &&
         (context->ringpu.vertex_input_capabilities &
          RINGL_RIN_GPU_VERTEX_INPUT_CONSTANT_FLOAT32) == 0u) ||
        context->element_array_buffer == 0u) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    use_multi_buffer = layout.binding_count > 1u;
    if ((use_multi_buffer &&
         (((context->ringpu.vertex_input_capabilities &
            RINGL_RIN_GPU_VERTEX_INPUT_MULTI_BUFFER) == 0u) ||
          context->ringpu_ops.draw_indexed_v2 == NULL ||
          (context->ringpu_ops.create_graphics_pipeline_vertex_bindings == NULL &&
           context->ringpu_ops
                   .create_graphics_pipeline_native_vertex_bindings == NULL))) ||
        (!use_multi_buffer &&
         (context->ringpu_ops.draw_indexed == NULL ||
          (context->ringpu_ops.create_graphics_pipeline == NULL &&
           context->ringpu_ops.create_graphics_pipeline_native == NULL)))) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }

    index_buffer_index = ringl_object_slot_index(context->element_array_buffer);
    if (index_buffer_index >= RINGL_OBJECT_SLOT_COUNT) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    memset(vertex_bindings, 0, sizeof(vertex_bindings));
    if (use_multi_buffer) {
        if (ringl_resolve_vertex_buffer_bindings(context, &layout,
                                                 vertex_bindings) != 0) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return;
        }
    } else if (layout.buffer != 0u) {
        vertex_buffer_index = ringl_object_slot_index(layout.buffer);
        if (vertex_buffer_index >= RINGL_OBJECT_SLOT_COUNT) {
            ringl_context_record_error(context, RINGL_INVALID_OPERATION);
            return;
        }
        vertex_buffer = &context->buffers[vertex_buffer_index];
    }
    index_buffer = &context->buffers[index_buffer_index];
    if ((!use_multi_buffer && vertex_buffer != NULL &&
         vertex_buffer->ringpu_handle == 0u) ||
        index_buffer->ringpu_handle == 0u) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }

    if (begin_commands(context, &command_list) != 0 ||
        ringl_get_or_create_graphics_pipeline(
            context, target.format,
            depth_stencil_pipeline_format(&depth_targets, use_depth_target),
            primitive_topology,
            effective_depth_test, effective_stencil_test,
            &pipeline) != 0 ||
        pipeline == 0u ||
        prepare_graphics_resources(context, command_list, pipeline, target.image,
                                   use_depth_target != 0u ?
                                       depth_targets.depth.image : 0u,
                                   use_depth_target != 0u ?
                                       depth_targets.stencil.image : 0u,
                                   &texture_transitions) != 0 ||
        transition_to_color_target(context, command_list, &target) != 0 ||
        (use_depth_target != 0u && transition_to_depth_stencil_targets(
                                      context, command_list, &depth_targets) != 0) ||
        (use_depth_target != 0u
             ? begin_depth_pass(context, command_list, &target, &depth_targets,
                                RINGL_RIN_GPU_RENDER_LOAD,
                                RINGL_RIN_GPU_RENDER_LOAD,
                                depth_targets.stencil.has_stencil != 0u
                                    ? RINGL_RIN_GPU_RENDER_LOAD
                                    : 0u)
             : begin_color_pass(context, command_list, &target,
                                RINGL_RIN_GPU_RENDER_LOAD)) != 0 ||
        set_raster_state(context, command_list, &target) != 0 ||
        bind_graphics_resources(context, command_list) != 0) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }

    if (use_multi_buffer) {
        memset(&draw_v2, 0, sizeof(draw_v2));
        draw_v2.pipeline = pipeline;
        draw_v2.color_target = target.image;
        draw_v2.index_buffer = index_buffer->ringpu_handle;
        draw_v2.index_offset = offset;
        if (type == RINGL_UNSIGNED_BYTE)
            draw_v2.index_format = RINGL_RIN_GPU_INDEX_UINT8;
        else if (type == RINGL_UNSIGNED_SHORT)
            draw_v2.index_format = RINGL_RIN_GPU_INDEX_UINT16;
        else
            draw_v2.index_format = RINGL_RIN_GPU_INDEX_UINT32;
        draw_v2.index_count = (uint32_t)count;
        draw_v2.vertex_count = vertex_count;
        draw_v2.instance_count = 1u;
        draw_v2.binding_count = layout.binding_count;
        memcpy(draw_v2.vertex_buffers, vertex_bindings,
               sizeof(RinGLRinGpuVertexBufferBindingV1) *
                   layout.binding_count);
        if (target.mip_level == 0u) {
            draw_result = ringl_backend_draw_indexed_v2(context, command_list,
                                                        &draw_v2);
        } else {
            memset(&draw_bindings_mip, 0, sizeof(draw_bindings_mip));
            draw_bindings_mip.base = draw_v2;
            draw_bindings_mip.color_mip_level = target.mip_level;
            draw_result = ringl_backend_draw_indexed_bindings_mip_v3(
                context, command_list, &draw_bindings_mip);
        }
    } else {
        memset(&draw, 0, sizeof(draw));
        draw.pipeline = pipeline;
        draw.color_target = target.image;
        draw.vertex_buffer =
            vertex_buffer != NULL ? vertex_buffer->ringpu_handle : 0u;
        draw.index_buffer = index_buffer->ringpu_handle;
        draw.index_offset = offset;
        if (type == RINGL_UNSIGNED_BYTE)
            draw.index_format = RINGL_RIN_GPU_INDEX_UINT8;
        else if (type == RINGL_UNSIGNED_SHORT)
            draw.index_format = RINGL_RIN_GPU_INDEX_UINT16;
        else
            draw.index_format = RINGL_RIN_GPU_INDEX_UINT32;
        draw.index_count = (uint32_t)count;
        draw.vertex_count = vertex_count;
        draw.instance_count = 1u;
        if (target.mip_level == 0u) {
            draw_result = ringl_backend_draw_indexed(context, command_list,
                                                     &draw);
        } else {
            memset(&draw_mip, 0, sizeof(draw_mip));
            draw_mip.base = draw;
            draw_mip.color_mip_level = target.mip_level;
            draw_result = ringl_backend_draw_indexed_mip_v3(context,
                                                             command_list,
                                                             &draw_mip);
        }
    }
    if (draw_result != 0 ||
        ringl_backend_end_render_pass(context, command_list) != 0 ||
        submit_commands(context, command_list) != 0) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }

    publish_texture_transitions(context, &texture_transitions);
    *target.state = RINGL_RIN_GPU_IMAGE_COLOR_TARGET;
    if (use_depth_target != 0u && depth_targets.depth.state != NULL)
        *depth_targets.depth.state = RINGL_RIN_GPU_IMAGE_DEPTH_TARGET;
    if (use_depth_target != 0u && depth_targets.stencil.state != NULL)
        *depth_targets.stencil.state = RINGL_RIN_GPU_IMAGE_DEPTH_TARGET;
    ringl_context_clear_dirty(context, RINGL_DIRTY_PIPELINE |
                                       RINGL_DIRTY_BINDINGS |
                                       RINGL_DIRTY_FRAMEBUFFER |
                                       RINGL_DIRTY_VIEWPORT);
}

int ringl_present(void)
{
    RinGLContext* context = ringl_get_current_context();
    uint64_t command_list;

    if (context == NULL)
        return -1;
    if (!context->has_default_framebuffer || !command_ops_ready(context) ||
        context->ringpu_ops.present == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return -1;
    }
    if (begin_commands(context, &command_list) != 0)
        goto fail;
    if (context->default_framebuffer_state != RINGL_RIN_GPU_IMAGE_PRESENT) {
        if (ringl_backend_transition_image(
                context, command_list,
                context->default_framebuffer.color_target,
                context->default_framebuffer_state,
                RINGL_RIN_GPU_IMAGE_PRESENT) != 0)
            goto fail;
    }
    if (ringl_backend_present(context, command_list,
                              context->default_framebuffer.color_target,
                              context->default_framebuffer.display_id) != 0 ||
        submit_commands(context, command_list) != 0)
        goto fail;
    context->default_framebuffer_state = RINGL_RIN_GPU_IMAGE_PRESENT;
    return 0;

fail:
    ringl_context_record_error(context, RINGL_INVALID_OPERATION);
    return -1;
}
