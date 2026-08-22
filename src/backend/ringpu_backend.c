/* SPDX-License-Identifier: MIT */
#include "ringl_internal.h"

int ringl_backend_create_buffer(RinGLContext* context,
                                uint64_t size_bytes,
                                uint64_t* buffer_out)
{
    if (context == NULL || buffer_out == NULL ||
        !context->has_ringpu_ops ||
        context->ringpu_ops.create_buffer == NULL) {
        return -1;
    }

    *buffer_out = 0u;
    return context->ringpu_ops.create_buffer(context->ringpu.session,
                                             size_bytes,
                                             buffer_out);
}

int ringl_backend_upload_buffer(RinGLContext* context,
                                uint64_t buffer,
                                uint64_t offset,
                                const void* data,
                                uint64_t size_bytes)
{
    if (context == NULL || !context->has_ringpu_ops ||
        context->ringpu_ops.upload_buffer == NULL) {
        return -1;
    }

    return context->ringpu_ops.upload_buffer(context->ringpu.session,
                                             buffer,
                                             offset,
                                             data,
                                             size_bytes);
}

int ringl_backend_create_shader_module(RinGLContext* context,
                                       const void* rsh1,
                                       uint64_t size_bytes,
                                       uint64_t* shader_module_out)
{
    if (context == NULL || rsh1 == NULL || size_bytes == 0u ||
        shader_module_out == NULL || !context->has_ringpu_ops ||
        context->ringpu_ops.create_shader_module == NULL) {
        return -1;
    }

    *shader_module_out = 0u;
    return context->ringpu_ops.create_shader_module(context->ringpu.session,
                                                    rsh1,
                                                    size_bytes,
                                                    shader_module_out);
}

int ringl_backend_create_graphics_pipeline(
    RinGLContext* context,
    const RinGLRinGpuGraphicsPipelineV1* desc,
    const RinGLRinGpuVertexAttributeV1* attributes,
    uint32_t attribute_count,
    uint64_t* pipeline_out)
{
    if (context == NULL || desc == NULL || pipeline_out == NULL ||
        attribute_count != desc->attribute_count ||
        (attribute_count != 0u && attributes == NULL) ||
        !context->has_ringpu_ops ||
        context->ringpu_ops.create_graphics_pipeline == NULL) {
        return -1;
    }

    *pipeline_out = 0u;
    return context->ringpu_ops.create_graphics_pipeline(
        context->ringpu.session, desc, attributes, attribute_count,
        pipeline_out);
}

int ringl_backend_create_command_list(RinGLContext* context,
                                      uint32_t capabilities,
                                      uint64_t* command_list_out)
{
    if (context == NULL || command_list_out == NULL ||
        !context->has_ringpu_ops ||
        context->ringpu_ops.create_command_list == NULL) {
        return -1;
    }
    *command_list_out = 0u;
    return context->ringpu_ops.create_command_list(context->ringpu.session,
                                                   capabilities,
                                                   command_list_out);
}

int ringl_backend_reset_command_list(RinGLContext* context,
                                     uint64_t command_list)
{
    if (context == NULL || command_list == 0u || !context->has_ringpu_ops ||
        context->ringpu_ops.reset_command_list == NULL) {
        return -1;
    }
    return context->ringpu_ops.reset_command_list(context->ringpu.session,
                                                  command_list);
}

int ringl_backend_transition_image(RinGLContext* context,
                                   uint64_t command_list,
                                   uint64_t image,
                                   uint32_t old_state,
                                   uint32_t new_state)
{
    if (context == NULL || command_list == 0u || image == 0u ||
        !context->has_ringpu_ops ||
        context->ringpu_ops.transition_image == NULL) {
        return -1;
    }
    return context->ringpu_ops.transition_image(context->ringpu.session,
                                                command_list, image,
                                                old_state, new_state);
}

int ringl_backend_begin_render_pass(RinGLContext* context,
                                    uint64_t command_list,
                                    const RinGLRinGpuRenderPassV1* render_pass)
{
    if (context == NULL || command_list == 0u || render_pass == NULL ||
        !context->has_ringpu_ops ||
        context->ringpu_ops.begin_render_pass == NULL) {
        return -1;
    }
    return context->ringpu_ops.begin_render_pass(context->ringpu.session,
                                                 command_list, render_pass);
}

int ringl_backend_draw_vertices(RinGLContext* context,
                                uint64_t command_list,
                                const RinGLRinGpuDrawVerticesV1* draw)
{
    if (context == NULL || command_list == 0u || draw == NULL ||
        !context->has_ringpu_ops || context->ringpu_ops.draw_vertices == NULL) {
        return -1;
    }
    return context->ringpu_ops.draw_vertices(context->ringpu.session,
                                             command_list, draw);
}

int ringl_backend_end_render_pass(RinGLContext* context,
                                  uint64_t command_list)
{
    if (context == NULL || command_list == 0u || !context->has_ringpu_ops ||
        context->ringpu_ops.end_render_pass == NULL) {
        return -1;
    }
    return context->ringpu_ops.end_render_pass(context->ringpu.session,
                                               command_list);
}

int ringl_backend_present(RinGLContext* context,
                          uint64_t command_list,
                          uint64_t image,
                          uint32_t display_id)
{
    if (context == NULL || command_list == 0u || image == 0u ||
        !context->has_ringpu_ops || context->ringpu_ops.present == NULL) {
        return -1;
    }
    return context->ringpu_ops.present(context->ringpu.session, command_list,
                                       image, display_id);
}

int ringl_backend_close_command_list(RinGLContext* context,
                                     uint64_t command_list)
{
    if (context == NULL || command_list == 0u || !context->has_ringpu_ops ||
        context->ringpu_ops.close_command_list == NULL) {
        return -1;
    }
    return context->ringpu_ops.close_command_list(context->ringpu.session,
                                                  command_list);
}

int ringl_backend_queue_submit(RinGLContext* context,
                               uint64_t queue,
                               uint64_t command_list)
{
    if (context == NULL || queue == 0u || command_list == 0u ||
        !context->has_ringpu_ops || context->ringpu_ops.queue_submit == NULL) {
        return -1;
    }
    return context->ringpu_ops.queue_submit(context->ringpu.session, queue,
                                            command_list);
}

void ringl_backend_destroy_object(RinGLContext* context, uint64_t object)
{
    if (context == NULL || object == 0u || !context->has_ringpu_ops ||
        context->ringpu_ops.destroy_object == NULL) {
        return;
    }

    (void)context->ringpu_ops.destroy_object(context->ringpu.session, object);
}
