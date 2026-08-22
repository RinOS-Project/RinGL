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

static int legacy_pipeline_state_supported(const RinGLContext* context)
{
    return context != NULL && !context->blend_enabled &&
        !context->cull_face_enabled && context->front_face == RINGL_CCW &&
        context->color_write_mask == RINGL_RIN_GPU_COLOR_WRITE_ALL;
}

static int draw_state_supported(const RinGLContext* context)
{
    if (context == NULL || !context->has_default_framebuffer ||
        context->depth_test_enabled)
        return 0;
    if (context->ringpu_ops.create_graphics_pipeline_native == NULL &&
        !legacy_pipeline_state_supported(context))
        return 0;
    if (context->viewport_initialized &&
        (context->viewport_x < 0 || context->viewport_y < 0))
        return 0;
    if (context->ringpu_ops.set_raster_state == NULL) {
        if (context->scissor_enabled)
            return 0;
        if (context->viewport_initialized &&
            (context->viewport_x != 0 || context->viewport_y != 0 ||
             context->viewport_width != context->default_framebuffer.width ||
             context->viewport_height != context->default_framebuffer.height))
            return 0;
    }
    return 1;
}

static int draw_is_noop(const RinGLContext* context)
{
    if (context == NULL)
        return 1;
    if (context->color_write_mask == 0u && !context->depth_test_enabled)
        return 1;
    if (context->cull_face_enabled &&
        context->cull_face_mode == RINGL_FRONT_AND_BACK &&
        !context->depth_test_enabled)
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
                                      uint64_t command_list)
{
    if (context->default_framebuffer_state == RINGL_RIN_GPU_IMAGE_COLOR_TARGET)
        return 0;
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

static int set_raster_state(RinGLContext* context, uint64_t command_list)
{
    RinGLRinGpuRasterStateV1 state;
    int64_t x0;
    int64_t y0;
    int64_t x1;
    int64_t y1;

    if (context->ringpu_ops.set_raster_state == NULL)
        return 0;

    memset(&state, 0, sizeof(state));
    state.viewport_x = (float)context->viewport_x;
    state.viewport_y = (float)context->viewport_y;
    state.viewport_width = (float)context->viewport_width;
    state.viewport_height = (float)context->viewport_height;
    state.min_depth = 0.0f;
    state.max_depth = 1.0f;

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
        if (x0 > (int64_t)context->default_framebuffer.width)
            x0 = context->default_framebuffer.width;
        if (y0 > (int64_t)context->default_framebuffer.height)
            y0 = context->default_framebuffer.height;
        if (x1 > (int64_t)context->default_framebuffer.width)
            x1 = context->default_framebuffer.width;
        if (y1 > (int64_t)context->default_framebuffer.height)
            y1 = context->default_framebuffer.height;
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

static int prepare_graphics_resources(RinGLContext* context,
                                      uint64_t command_list,
                                      uint64_t pipeline,
                                      uint32_t* texture_index_out,
                                      uint32_t* transitioned_out)
{
    RinGLProgramObject* program;
    RinGLRinGpuGraphicsBindingV1 bindings[2];
    RinGLTextureObject* texture;
    uint64_t image;
    uint64_t sampler;
    uint32_t texture_name;
    uint32_t texture_index;
    int32_t unit;

    if (texture_index_out != NULL)
        *texture_index_out = UINT32_MAX;
    if (transitioned_out != NULL)
        *transitioned_out = 0u;

    program = current_program(context);
    if (program == NULL)
        return -1;
    if (program->sampler_uniform_count == 0u)
        return 0;
    if (program->sampler_uniform_count != 1u ||
        context->ringpu_ops.create_graphics_bind_group == NULL ||
        context->ringpu_ops.bind_graphics_resources == NULL)
        return -1;

    unit = program->sampler_uniforms[0].texture_unit;
    if (unit < 0 || (uint32_t)unit >= RINGL_MAX_TEXTURE_UNITS)
        return -1;
    texture_name = context->bound_texture_2d[(uint32_t)unit];
    if (texture_name == 0u ||
        ringl_object_lookup(context, texture_name, RINGL_OBJECT_TEXTURE) == NULL)
        return -1;
    texture_index = ringl_object_slot_index(texture_name);
    if (texture_index >= RINGL_OBJECT_SLOT_COUNT)
        return -1;
    texture = &context->textures[texture_index];

    if (ringl_texture_realize_unit(context, (uint32_t)unit, &image, &sampler) != 0 ||
        image == 0u || sampler == 0u)
        return -1;
    if (texture->ringpu_image_state != RINGL_RIN_GPU_IMAGE_SHADER_READ) {
        if (ringl_backend_transition_image(context, command_list, image,
                                           texture->ringpu_image_state,
                                           RINGL_RIN_GPU_IMAGE_SHADER_READ) != 0)
            return -1;
        if (transitioned_out != NULL)
            *transitioned_out = 1u;
    }

    memset(bindings, 0, sizeof(bindings));
    bindings[0].binding = 0u;
    bindings[0].kind = RINGL_RIN_GPU_RESOURCE_SAMPLED_IMAGE;
    bindings[0].access = RINGL_RIN_GPU_RESOURCE_READ;
    bindings[0].resource = image;
    bindings[1].binding = 1u;
    bindings[1].kind = RINGL_RIN_GPU_RESOURCE_SAMPLER;
    bindings[1].resource = sampler;

    if (ringl_backend_create_graphics_bind_group(
            context, pipeline, bindings, 2u,
            &context->graphics_bind_group) != 0 ||
        context->graphics_bind_group == 0u)
        return -1;

    if (texture_index_out != NULL)
        *texture_index_out = texture_index;
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

static void publish_texture_transition(RinGLContext* context,
                                       uint32_t texture_index,
                                       uint32_t transitioned)
{
    if (context == NULL || !transitioned ||
        texture_index >= RINGL_OBJECT_SLOT_COUNT)
        return;
    context->textures[texture_index].ringpu_image_state =
        RINGL_RIN_GPU_IMAGE_SHADER_READ;
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
    uint32_t texture_index = UINT32_MAX;
    uint32_t texture_transitioned = 0u;

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
    if (count == 0 || draw_is_noop(context))
        return;
    if (!context->has_default_framebuffer || !command_ops_ready(context) ||
        context->ringpu_ops.draw_vertices == NULL ||
        (context->ringpu_ops.create_graphics_pipeline == NULL &&
         context->ringpu_ops.create_graphics_pipeline_native == NULL) ||
        !draw_state_supported(context)) {
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
        prepare_graphics_resources(context, command_list, pipeline,
                                   &texture_index,
                                   &texture_transitioned) != 0 ||
        transition_to_color_target(context, command_list) != 0 ||
        begin_color_pass(context, command_list, RINGL_RIN_GPU_RENDER_LOAD) != 0 ||
        set_raster_state(context, command_list) != 0 ||
        bind_graphics_resources(context, command_list) != 0) {
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
    publish_texture_transition(context, texture_index, texture_transitioned);
    context->default_framebuffer_state = RINGL_RIN_GPU_IMAGE_COLOR_TARGET;
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
    RinGLBufferObject* vertex_buffer;
    RinGLBufferObject* index_buffer;
    RinGLRinGpuDrawIndexedV1 draw;
    uint64_t command_list;
    uint64_t pipeline;
    uint32_t max_index;
    uint32_t vertex_count;
    uint32_t vertex_buffer_index;
    uint32_t index_buffer_index;
    uint32_t texture_index = UINT32_MAX;
    uint32_t texture_transitioned = 0u;

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
    if (count == 0 || draw_is_noop(context))
        return;
    if (!context->has_default_framebuffer || !command_ops_ready(context) ||
        context->ringpu_ops.draw_indexed == NULL ||
        (context->ringpu_ops.create_graphics_pipeline == NULL &&
         context->ringpu_ops.create_graphics_pipeline_native == NULL) ||
        !draw_state_supported(context)) {
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
        prepare_graphics_resources(context, command_list, pipeline,
                                   &texture_index,
                                   &texture_transitioned) != 0 ||
        transition_to_color_target(context, command_list) != 0 ||
        begin_color_pass(context, command_list, RINGL_RIN_GPU_RENDER_LOAD) != 0 ||
        set_raster_state(context, command_list) != 0 ||
        bind_graphics_resources(context, command_list) != 0) {
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
        draw.index_format = RINGL_RIN_GPU_INDEX_UINT8;
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

    publish_texture_transition(context, texture_index, texture_transitioned);
    context->default_framebuffer_state = RINGL_RIN_GPU_IMAGE_COLOR_TARGET;
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
