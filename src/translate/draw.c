/* SPDX-License-Identifier: MIT */
#include "../ringl_internal.h"
#include "pipeline_cache.h"

#include <string.h>

#define RINGL_NATIVE_INDEX_UINT8 3u

static float clamp_color(float value)
{
    if (!(value >= 0.0f))
        return 0.0f;
    if (value > 1.0f)
        return 1.0f;
    return value;
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
                                         context->graphics_command_list) != 0) {
        return -1;
    }
    *command_list = context->graphics_command_list;
    return 0;
}

static int submit_commands(RinGLContext* context, uint64_t command_list)
{
    if (ringl_backend_close_command_list(context, command_list) != 0)
        return -1;
    if (ringl_backend_queue_submit(context, context->ringpu.graphics_queue,
                                   command_list) != 0) {
        return -1;
    }
    return 0;
}

static int transition_to_color_target(RinGLContext* context,
                                      uint64_t command_list)
{
    if (context->default_framebuffer_state ==
        RINGL_RIN_GPU_IMAGE_COLOR_TARGET) {
        return 0;
    }
    if (context->default_framebuffer_state == 0u)
        return -1;
    return ringl_backend_transition_image(
        context, command_list, context->default_framebuffer.color_target,
        context->default_framebuffer_state,
        RINGL_RIN_GPU_IMAGE_COLOR_TARGET);
}

static int begin_color_pass(RinGLContext* context,
                            uint64_t command_list,
                            uint32_t load_op)
{
    RinGLRinGpuRenderPassV1 render_pass;

    memset(&render_pass, 0, sizeof(render_pass));
    render_pass.color_target = context->default_framebuffer.color_target;
    render_pass.load_op = load_op;
    render_pass.store_op = RINGL_RIN_GPU_RENDER_STORE;
    render_pass.clear_red = clamp_color(context->clear_red);
    render_pass.clear_green = clamp_color(context->clear_green);
    render_pass.clear_blue = clamp_color(context->clear_blue);
    render_pass.clear_alpha = clamp_color(context->clear_alpha);
    return ringl_backend_begin_render_pass(context, command_list, &render_pass);
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

void ringl_clear(uint32_t mask)
{
    RinGLContext* context = ringl_get_current_context();
    uint64_t command_list;

    if (context == NULL || mask == 0u)
        return;
    if ((mask & ~RINGL_COLOR_BUFFER_BIT) != 0u) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (!context->has_default_framebuffer || !command_ops_ready(context)) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if (begin_commands(context, &command_list) != 0 ||
        transition_to_color_target(context, command_list) != 0 ||
        begin_color_pass(context, command_list, RINGL_RIN_GPU_RENDER_CLEAR) != 0 ||
        ringl_backend_end_render_pass(context, command_list) != 0 ||
        submit_commands(context, command_list) != 0) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    context->default_framebuffer_state = RINGL_RIN_GPU_IMAGE_COLOR_TARGET;
    ringl_context_clear_dirty(context, RINGL_DIRTY_FRAMEBUFFER);
}

void ringl_draw_arrays(uint32_t mode, int32_t first, int32_t count)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLResolvedVertexLayout layout;
    RinGLBufferObject* vertex_buffer;
    RinGLRinGpuDrawVerticesV1 draw;
    uint64_t command_list;
    uint64_t pipeline;
    uint32_t buffer_index;

    if (context == NULL)
        return;
    if (mode != RINGL_TRIANGLES) {
        ringl_context_record_error(context, RINGL_INVALID_ENUM);
        return;
    }
    if (first < 0 || count < 0) {
        ringl_context_record_error(context, RINGL_INVALID_VALUE);
        return;
    }
    if (count == 0)
        return;
    if (!context->has_default_framebuffer || !command_ops_ready(context) ||
        context->ringpu_ops.draw_vertices == NULL ||
        context->ringpu_ops.create_graphics_pipeline == NULL) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    if (ringl_validate_vertex_fetch(context, (uint32_t)first,
                                    (uint32_t)count, &layout) != 0 ||
        layout.buffer == 0u) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
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

    if (begin_commands(context, &command_list) != 0 ||
        ringl_get_or_create_graphics_pipeline(
            context, context->default_framebuffer.color_format, &pipeline) != 0 ||
        pipeline == 0u ||
        transition_to_color_target(context, command_list) != 0 ||
        begin_color_pass(context, command_list, RINGL_RIN_GPU_RENDER_LOAD) != 0) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }

    memset(&draw, 0, sizeof(draw));
    draw.pipeline = pipeline;
    draw.color_target = context->default_framebuffer.color_target;
    draw.vertex_buffer = vertex_buffer->ringpu_handle;
    draw.vertex_count = (uint32_t)count;
    draw.first_vertex = (uint32_t)first;
    draw.instance_count = 1u;
    if (ringl_backend_draw_vertices(context, command_list, &draw) != 0 ||
        ringl_backend_end_render_pass(context, command_list) != 0 ||
        submit_commands(context, command_list) != 0) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    context->default_framebuffer_state = RINGL_RIN_GPU_IMAGE_COLOR_TARGET;
    ringl_context_clear_dirty(context, RINGL_DIRTY_PIPELINE |
                                       RINGL_DIRTY_BINDINGS |
                                       RINGL_DIRTY_FRAMEBUFFER);
}

void ringl_draw_elements(uint32_t mode, int32_t count, uint32_t type,
                         uint64_t offset)
{
    RinGLContext* context = ringl_get_current_context();
    RinGLResolvedVertexLayout layout;
    RinGLBufferObject* vertex_buffer;
    RinGLBufferObject* index_buffer;
    RinGLRinGpuDrawIndexedV1 draw;
    uint64_t command_list;
    uint64_t pipeline;
    uint32_t max_index;
    uint32_t vertex_count;
    uint32_t vertex_buffer_index;
    uint32_t index_buffer_index;

    if (context == NULL)
        return;
    if (mode != RINGL_TRIANGLES) {
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
    if (count == 0)
        return;
    if (!context->has_default_framebuffer || !command_ops_ready(context) ||
        context->ringpu_ops.draw_indexed == NULL ||
        context->ringpu_ops.create_graphics_pipeline == NULL) {
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
        layout.buffer == 0u || context->element_array_buffer == 0u) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }

    vertex_buffer_index = ringl_object_slot_index(layout.buffer);
    index_buffer_index = ringl_object_slot_index(context->element_array_buffer);
    if (vertex_buffer_index >= RINGL_OBJECT_SLOT_COUNT ||
        index_buffer_index >= RINGL_OBJECT_SLOT_COUNT) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }
    vertex_buffer = &context->buffers[vertex_buffer_index];
    index_buffer = &context->buffers[index_buffer_index];
    if (vertex_buffer->ringpu_handle == 0u || index_buffer->ringpu_handle == 0u) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }

    if (begin_commands(context, &command_list) != 0 ||
        ringl_get_or_create_graphics_pipeline(
            context, context->default_framebuffer.color_format, &pipeline) != 0 ||
        pipeline == 0u ||
        transition_to_color_target(context, command_list) != 0 ||
        begin_color_pass(context, command_list, RINGL_RIN_GPU_RENDER_LOAD) != 0) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }

    memset(&draw, 0, sizeof(draw));
    draw.pipeline = pipeline;
    draw.color_target = context->default_framebuffer.color_target;
    draw.vertex_buffer = vertex_buffer->ringpu_handle;
    draw.index_buffer = index_buffer->ringpu_handle;
    draw.index_offset = offset;
    if (type == RINGL_UNSIGNED_BYTE)
        draw.index_format = RINGL_NATIVE_INDEX_UINT8;
    else if (type == RINGL_UNSIGNED_SHORT)
        draw.index_format = RINGL_RIN_GPU_INDEX_UINT16;
    else
        draw.index_format = RINGL_RIN_GPU_INDEX_UINT32;
    draw.index_count = (uint32_t)count;
    draw.vertex_count = vertex_count;
    draw.instance_count = 1u;
    if (ringl_backend_draw_indexed(context, command_list, &draw) != 0 ||
        ringl_backend_end_render_pass(context, command_list) != 0 ||
        submit_commands(context, command_list) != 0) {
        ringl_context_record_error(context, RINGL_INVALID_OPERATION);
        return;
    }

    context->default_framebuffer_state = RINGL_RIN_GPU_IMAGE_COLOR_TARGET;
    ringl_context_clear_dirty(context, RINGL_DIRTY_PIPELINE |
                                       RINGL_DIRTY_BINDINGS |
                                       RINGL_DIRTY_FRAMEBUFFER);
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
                RINGL_RIN_GPU_IMAGE_PRESENT) != 0) {
            goto fail;
        }
    }
    if (ringl_backend_present(context, command_list,
                              context->default_framebuffer.color_target,
                              context->default_framebuffer.display_id) != 0 ||
        submit_commands(context, command_list) != 0) {
        goto fail;
    }
    context->default_framebuffer_state = RINGL_RIN_GPU_IMAGE_PRESENT;
    return 0;

fail:
    ringl_context_record_error(context, RINGL_INVALID_OPERATION);
    return -1;
}
