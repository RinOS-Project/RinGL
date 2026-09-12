/* SPDX-License-Identifier: MIT */
#include "ringl_internal.h"

static int ringl_backend_result(RinGLContext* context, int result)
{
    /* This is an attempted RinGPU operation, not a success indication. Keep
     * the exact backend result in the trace so device loss and ordinary
     * rejection remain distinguishable to the embedding. */
    ringl_context_trace(context, RINGL_TRACE_CALL_SUMMARY, 0u, 0u, result);
    if (result == RINGL_RIN_GPU_ERROR_DEVICE_LOST)
        ringl_context_mark_lost(context);
    return result;
}

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
    return ringl_backend_result(
        context, context->ringpu_ops.create_buffer(context->ringpu.session,
                                                   size_bytes, buffer_out));
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

    return ringl_backend_result(
        context, context->ringpu_ops.upload_buffer(context->ringpu.session,
                                                   buffer, offset, data,
                                                   size_bytes));
}

int ringl_backend_create_sampled_image_2d(
    RinGLContext* context, const RinGLRinGpuSampledImage2DV1* desc,
    uint64_t* image_out)
{
    if (context == NULL || desc == NULL || image_out == NULL ||
        desc->width == 0u || desc->height == 0u ||
        !context->has_ringpu_ops ||
        context->ringpu_ops.create_sampled_image_2d == NULL) {
        return -1;
    }
    *image_out = 0u;
    return ringl_backend_result(
        context, context->ringpu_ops.create_sampled_image_2d(
                     context->ringpu.session, desc, image_out));
}

int ringl_backend_create_image_2d(
    RinGLContext* context, const RinGLRinGpuImage2DV1* desc,
    uint64_t* image_out)
{
    if (context == NULL || desc == NULL || image_out == NULL ||
        desc->width == 0u || desc->height == 0u || desc->format == 0u ||
        desc->usage == 0u || !context->has_ringpu_ops ||
        context->ringpu_ops.create_image_2d == NULL) {
        return -1;
    }
    *image_out = 0u;
    return ringl_backend_result(
        context, context->ringpu_ops.create_image_2d(context->ringpu.session,
                                                     desc, image_out));
}

int ringl_backend_create_image_2d_mip_v2(
    RinGLContext* context, const RinGLRinGpuImage2DMipV2* desc,
    uint64_t* image_out)
{
    if (context == NULL || desc == NULL || image_out == NULL ||
        desc->width == 0u || desc->height == 0u || desc->format == 0u ||
        desc->usage == 0u || desc->mip_levels < 2u || desc->reserved0 != 0u ||
        !context->has_ringpu_ops ||
        context->ringpu_ops.create_image_2d_mip_v2 == NULL) {
        return -1;
    }
    *image_out = 0u;
    return ringl_backend_result(
        context, context->ringpu_ops.create_image_2d_mip_v2(
                     context->ringpu.session, desc, image_out));
}

int ringl_backend_upload_image_2d(
    RinGLContext* context, uint64_t image,
    const RinGLRinGpuImageUpload2DV1* upload,
    const void* data, uint64_t size_bytes)
{
    if (context == NULL || image == 0u || upload == NULL || data == NULL ||
        upload->width == 0u || upload->height == 0u || size_bytes == 0u ||
        !context->has_ringpu_ops ||
        context->ringpu_ops.upload_image_2d == NULL) {
        return -1;
    }
    return ringl_backend_result(
        context, context->ringpu_ops.upload_image_2d(
                     context->ringpu.session, image, upload, data, size_bytes));
}

int ringl_backend_upload_image_2d_mip_v2(
    RinGLContext* context, uint64_t image,
    const RinGLRinGpuImageUpload2DMipV2* upload,
    const void* data, uint64_t size_bytes)
{
    if (context == NULL || image == 0u || upload == NULL || data == NULL ||
        upload->width == 0u || upload->height == 0u ||
        upload->reserved0 != 0u || size_bytes == 0u ||
        !context->has_ringpu_ops ||
        context->ringpu_ops.upload_image_2d_mip_v2 == NULL) {
        return -1;
    }
    return ringl_backend_result(
        context, context->ringpu_ops.upload_image_2d_mip_v2(
                     context->ringpu.session, image, upload, data, size_bytes));
}

int ringl_backend_create_image_array_v1(
    RinGLContext* context, const RinGLRinGpuImageArrayV1* desc,
    uint64_t* image_out)
{
    if (context == NULL || desc == NULL || image_out == NULL ||
        desc->width == 0u || desc->height == 0u ||
        desc->array_layers == 0u || desc->mip_levels == 0u ||
        desc->format == 0u || desc->usage == 0u || desc->reserved0 != 0u ||
        desc->reserved1 != 0u || !context->has_ringpu_ops ||
        context->ringpu_ops.create_image_array_v1 == NULL) {
        return -1;
    }
    *image_out = 0u;
    return ringl_backend_result(
        context, context->ringpu_ops.create_image_array_v1(
                     context->ringpu.session, desc, image_out));
}

int ringl_backend_upload_image_array_v1(
    RinGLContext* context, uint64_t image,
    const RinGLRinGpuImageUploadArrayV1* upload,
    const void* data, uint64_t size_bytes)
{
    if (context == NULL || image == 0u || upload == NULL || data == NULL ||
        upload->width == 0u || upload->height == 0u ||
        upload->reserved0 != 0u || upload->reserved1 != 0u ||
        size_bytes == 0u || !context->has_ringpu_ops ||
        context->ringpu_ops.upload_image_array_v1 == NULL) {
        return -1;
    }
    return ringl_backend_result(
        context, context->ringpu_ops.upload_image_array_v1(
                     context->ringpu.session, image, upload, data, size_bytes));
}

int ringl_backend_create_sampler(RinGLContext* context,
                                 const RinGLRinGpuSamplerV1* desc,
                                 uint64_t* sampler_out)
{
    if (context == NULL || desc == NULL || sampler_out == NULL ||
        !context->has_ringpu_ops || context->ringpu_ops.create_sampler == NULL) {
        return -1;
    }
    *sampler_out = 0u;
    return ringl_backend_result(
        context, context->ringpu_ops.create_sampler(context->ringpu.session,
                                                    desc, sampler_out));
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
    return ringl_backend_result(
        context, context->ringpu_ops.create_shader_module(context->ringpu.session,
                                                          rsh1, size_bytes,
                                                          shader_module_out));
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
    return ringl_backend_result(
        context, context->ringpu_ops.create_graphics_pipeline(
                     context->ringpu.session, desc, attributes, attribute_count,
                     pipeline_out));
}

int ringl_backend_create_graphics_pipeline_native(
    RinGLContext* context,
    const RinGLRinGpuGraphicsPipelineNativeV1* desc,
    const RinGLRinGpuVertexAttributeV1* attributes,
    uint32_t attribute_count,
    const RinGLRinGpuVaryingV1* varyings,
    uint32_t varying_count,
    uint64_t* pipeline_out)
{
    if (context == NULL || desc == NULL || pipeline_out == NULL ||
        (attribute_count != 0u && attributes == NULL) ||
        (varying_count != 0u && varyings == NULL) ||
        !context->has_ringpu_ops ||
        context->ringpu_ops.create_graphics_pipeline_native == NULL) {
        return -1;
    }
    *pipeline_out = 0u;
    return ringl_backend_result(
        context, context->ringpu_ops.create_graphics_pipeline_native(
                     context->ringpu.session, desc, attributes, attribute_count,
                     varyings, varying_count, pipeline_out));
}

int ringl_backend_create_graphics_pipeline_native_v2(
    RinGLContext* context,
    const RinGLRinGpuGraphicsPipelineNativeV2* desc,
    const RinGLRinGpuVertexAttributeV1* attributes,
    uint32_t attribute_count,
    const RinGLRinGpuVaryingV1* varyings,
    uint32_t varying_count,
    uint64_t* pipeline_out)
{
    if (context == NULL || desc == NULL || pipeline_out == NULL ||
        (attribute_count != 0u && attributes == NULL) ||
        (varying_count != 0u && varyings == NULL) ||
        !context->has_ringpu_ops ||
        context->ringpu_ops.create_graphics_pipeline_native_v2 == NULL) {
        return -1;
    }
    *pipeline_out = 0u;
    return ringl_backend_result(
        context, context->ringpu_ops.create_graphics_pipeline_native_v2(
                     context->ringpu.session, desc, attributes, attribute_count,
                     varyings, varying_count, pipeline_out));
}

int ringl_backend_create_graphics_pipeline_vertex_bindings(
    RinGLContext* context,
    const RinGLRinGpuGraphicsPipelineV1* desc,
    const RinGLRinGpuVertexAttributeV2* attributes,
    uint32_t attribute_count,
    const RinGLRinGpuVertexBufferLayoutV1* vertex_bindings,
    uint32_t vertex_binding_count,
    uint64_t* pipeline_out)
{
    if (context == NULL || desc == NULL || pipeline_out == NULL ||
        attribute_count != desc->attribute_count ||
        (attribute_count != 0u && attributes == NULL) ||
        vertex_binding_count == 0u ||
        vertex_binding_count > RINGL_MAX_VERTEX_ATTRIBS ||
        vertex_bindings == NULL || !context->has_ringpu_ops ||
        context->ringpu_ops.create_graphics_pipeline_vertex_bindings == NULL) {
        return -1;
    }
    *pipeline_out = 0u;
    return ringl_backend_result(
        context, context->ringpu_ops.create_graphics_pipeline_vertex_bindings(
                     context->ringpu.session, desc, attributes, attribute_count,
                     vertex_bindings, vertex_binding_count, pipeline_out));
}

int ringl_backend_create_graphics_pipeline_native_vertex_bindings(
    RinGLContext* context,
    const RinGLRinGpuGraphicsPipelineNativeV1* desc,
    const RinGLRinGpuVertexAttributeV2* attributes,
    uint32_t attribute_count,
    const RinGLRinGpuVertexBufferLayoutV1* vertex_bindings,
    uint32_t vertex_binding_count,
    const RinGLRinGpuVaryingV1* varyings,
    uint32_t varying_count,
    uint64_t* pipeline_out)
{
    if (context == NULL || desc == NULL || pipeline_out == NULL ||
        (attribute_count != 0u && attributes == NULL) ||
        vertex_binding_count == 0u ||
        vertex_binding_count > RINGL_MAX_VERTEX_ATTRIBS ||
        vertex_bindings == NULL ||
        (varying_count != 0u && varyings == NULL) ||
        !context->has_ringpu_ops ||
        context->ringpu_ops.create_graphics_pipeline_native_vertex_bindings ==
            NULL) {
        return -1;
    }
    *pipeline_out = 0u;
    return ringl_backend_result(
        context,
        context->ringpu_ops.create_graphics_pipeline_native_vertex_bindings(
            context->ringpu.session, desc, attributes, attribute_count,
            vertex_bindings, vertex_binding_count, varyings, varying_count,
            pipeline_out));
}

int ringl_backend_create_graphics_pipeline_native_vertex_bindings_v2(
    RinGLContext* context,
    const RinGLRinGpuGraphicsPipelineNativeV2* desc,
    const RinGLRinGpuVertexAttributeV2* attributes,
    uint32_t attribute_count,
    const RinGLRinGpuVertexBufferLayoutV1* vertex_bindings,
    uint32_t vertex_binding_count,
    const RinGLRinGpuVaryingV1* varyings,
    uint32_t varying_count,
    uint64_t* pipeline_out)
{
    if (context == NULL || desc == NULL || pipeline_out == NULL ||
        (attribute_count != 0u && attributes == NULL) ||
        vertex_binding_count == 0u ||
        vertex_binding_count > RINGL_MAX_VERTEX_ATTRIBS ||
        vertex_bindings == NULL ||
        (varying_count != 0u && varyings == NULL) ||
        !context->has_ringpu_ops ||
        context->ringpu_ops
                .create_graphics_pipeline_native_vertex_bindings_v2 == NULL) {
        return -1;
    }
    *pipeline_out = 0u;
    return ringl_backend_result(
        context,
        context->ringpu_ops.create_graphics_pipeline_native_vertex_bindings_v2(
            context->ringpu.session, desc, attributes, attribute_count,
            vertex_bindings, vertex_binding_count, varyings, varying_count,
            pipeline_out));
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
    return ringl_backend_result(
        context, context->ringpu_ops.create_command_list(context->ringpu.session,
                                                         capabilities,
                                                         command_list_out));
}

int ringl_backend_reset_command_list(RinGLContext* context,
                                     uint64_t command_list)
{
    if (context == NULL || command_list == 0u || !context->has_ringpu_ops ||
        context->ringpu_ops.reset_command_list == NULL) {
        return -1;
    }
    return ringl_backend_result(
        context, context->ringpu_ops.reset_command_list(context->ringpu.session,
                                                        command_list));
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
    return ringl_backend_result(
        context, context->ringpu_ops.transition_image(context->ringpu.session,
                                                      command_list, image,
                                                      old_state, new_state));
}

int ringl_backend_transition_image_2d_mip_v2(
    RinGLContext* context, uint64_t command_list,
    const RinGLRinGpuImageTransition2DMipV2* transition)
{
    if (context == NULL || command_list == 0u || transition == NULL ||
        transition->image == 0u || !context->has_ringpu_ops ||
        context->ringpu_ops.transition_image_2d_mip_v2 == NULL) {
        return -1;
    }
    return ringl_backend_result(
        context, context->ringpu_ops.transition_image_2d_mip_v2(
                     context->ringpu.session, command_list, transition));
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
    return ringl_backend_result(
        context, context->ringpu_ops.begin_render_pass(context->ringpu.session,
                                                       command_list, render_pass));
}

int ringl_backend_begin_render_pass_mrt_v1(
    RinGLContext* context, uint64_t command_list,
    const RinGLRinGpuRenderPassMrtV1* render_pass)
{
    if (context == NULL || command_list == 0u || render_pass == NULL ||
        render_pass->active_color_mask == 0u || !context->has_ringpu_ops ||
        context->ringpu_ops.begin_render_pass_mrt_v1 == NULL) {
        return -1;
    }
    return ringl_backend_result(
        context, context->ringpu_ops.begin_render_pass_mrt_v1(
                     context->ringpu.session, command_list, render_pass));
}

int ringl_backend_begin_render_pass_mip_v2(
    RinGLContext* context, uint64_t command_list,
    const RinGLRinGpuRenderPassMipV2* render_pass)
{
    if (context == NULL || command_list == 0u || render_pass == NULL ||
        render_pass->base.color_target == 0u || !context->has_ringpu_ops ||
        context->ringpu_ops.begin_render_pass_mip_v2 == NULL) {
        return -1;
    }
    return ringl_backend_result(
        context, context->ringpu_ops.begin_render_pass_mip_v2(
                     context->ringpu.session, command_list, render_pass));
}

int ringl_backend_begin_render_pass_depth(
    RinGLContext* context, uint64_t command_list,
    const RinGLRinGpuRenderPassDepthV1* render_pass)
{
    if (context == NULL || command_list == 0u || render_pass == NULL ||
        !context->has_ringpu_ops ||
        context->ringpu_ops.begin_render_pass_depth == NULL) {
        return -1;
    }
    return ringl_backend_result(
        context, context->ringpu_ops.begin_render_pass_depth(
                     context->ringpu.session, command_list, render_pass));
}

int ringl_backend_begin_render_pass_depth_stencil(
    RinGLContext* context, uint64_t command_list,
    const RinGLRinGpuRenderPassDepthStencilV1* render_pass)
{
    if (context == NULL || command_list == 0u || render_pass == NULL ||
        !context->has_ringpu_ops ||
        context->ringpu_ops.begin_render_pass_depth_stencil == NULL) {
        return -1;
    }
    return ringl_backend_result(
        context, context->ringpu_ops.begin_render_pass_depth_stencil(
                     context->ringpu.session, command_list, render_pass));
}

int ringl_backend_set_raster_state_v2(
    RinGLContext* context, uint64_t command_list,
    const RinGLRinGpuRasterStateV2* state)
{
    if (context == NULL || command_list == 0u || state == NULL ||
        !context->has_ringpu_ops)
        return -1;
    if (context->ringpu_ops.set_raster_state_v2 != NULL) {
        return ringl_backend_result(
            context, context->ringpu_ops.set_raster_state_v2(
                         context->ringpu.session, command_list, state));
    }
    /* V1 has no DITHER member. Falling back is correct only for its GLES
     * default (enabled); an explicit disable must fail rather than misdraw. */
    if (state->dither_enabled == RINGL_FALSE ||
        context->ringpu_ops.set_raster_state == NULL)
        return -1;
    return ringl_backend_result(
        context, context->ringpu_ops.set_raster_state(context->ringpu.session,
                                                      command_list, &state->base));
}

int ringl_backend_create_graphics_bind_group(
    RinGLContext* context, uint64_t pipeline,
    const RinGLRinGpuGraphicsBindingV1* bindings,
    uint32_t binding_count, uint64_t* bind_group_out)
{
    if (context == NULL || pipeline == 0u || bind_group_out == NULL ||
        (binding_count != 0u && bindings == NULL) ||
        !context->has_ringpu_ops ||
        context->ringpu_ops.create_graphics_bind_group == NULL)
        return -1;
    *bind_group_out = 0u;
    return ringl_backend_result(
        context, context->ringpu_ops.create_graphics_bind_group(
                     context->ringpu.session, pipeline, bindings, binding_count,
                     bind_group_out));
}

int ringl_backend_create_graphics_bind_group_v2(
    RinGLContext* context, uint64_t pipeline,
    const RinGLRinGpuGraphicsBindingV2* bindings,
    uint32_t binding_count, uint64_t* bind_group_out)
{
    if (context == NULL || pipeline == 0u || bind_group_out == NULL ||
        (binding_count != 0u && bindings == NULL) ||
        !context->has_ringpu_ops ||
        context->ringpu_ops.create_graphics_bind_group_v2 == NULL)
        return -1;
    *bind_group_out = 0u;
    return ringl_backend_result(
        context, context->ringpu_ops.create_graphics_bind_group_v2(
                     context->ringpu.session, pipeline, bindings, binding_count,
                     bind_group_out));
}

int ringl_backend_bind_graphics_resources(RinGLContext* context,
                                          uint64_t command_list,
                                          uint64_t bind_group)
{
    if (context == NULL || command_list == 0u || bind_group == 0u ||
        !context->has_ringpu_ops ||
        context->ringpu_ops.bind_graphics_resources == NULL)
        return -1;
    return ringl_backend_result(
        context, context->ringpu_ops.bind_graphics_resources(
                     context->ringpu.session, command_list, bind_group));
}

int ringl_backend_draw_vertices(RinGLContext* context,
                                uint64_t command_list,
                                const RinGLRinGpuDrawVerticesV1* draw)
{
    if (context == NULL || command_list == 0u || draw == NULL ||
        !context->has_ringpu_ops || context->ringpu_ops.draw_vertices == NULL) {
        return -1;
    }
    return ringl_backend_result(
        context, context->ringpu_ops.draw_vertices(context->ringpu.session,
                                                   command_list, draw));
}

int ringl_backend_draw_indexed(RinGLContext* context,
                               uint64_t command_list,
                               const RinGLRinGpuDrawIndexedV1* draw)
{
    if (context == NULL || command_list == 0u || draw == NULL ||
        !context->has_ringpu_ops || context->ringpu_ops.draw_indexed == NULL) {
        return -1;
    }
    return ringl_backend_result(
        context, context->ringpu_ops.draw_indexed(context->ringpu.session,
                                                  command_list, draw));
}

int ringl_backend_draw_vertices_v2(RinGLContext* context,
                                   uint64_t command_list,
                                   const RinGLRinGpuDrawVerticesV2* draw)
{
    if (context == NULL || command_list == 0u || draw == NULL ||
        !context->has_ringpu_ops ||
        context->ringpu_ops.draw_vertices_v2 == NULL) {
        return -1;
    }
    return ringl_backend_result(
        context, context->ringpu_ops.draw_vertices_v2(context->ringpu.session,
                                                      command_list, draw));
}

int ringl_backend_draw_indexed_v2(RinGLContext* context,
                                  uint64_t command_list,
                                  const RinGLRinGpuDrawIndexedV2* draw)
{
    if (context == NULL || command_list == 0u || draw == NULL ||
        !context->has_ringpu_ops ||
        context->ringpu_ops.draw_indexed_v2 == NULL) {
        return -1;
    }
    return ringl_backend_result(
        context, context->ringpu_ops.draw_indexed_v2(context->ringpu.session,
                                                     command_list, draw));
}

int ringl_backend_begin_render_pass_depth_mip_v2(
    RinGLContext* context, uint64_t command_list,
    const RinGLRinGpuRenderPassDepthMipV2* render_pass)
{
    if (context == NULL || command_list == 0u || render_pass == NULL ||
        render_pass->base.color_target == 0u ||
        render_pass->base.depth_target == 0u || !context->has_ringpu_ops ||
        context->ringpu_ops.begin_render_pass_depth_mip_v2 == NULL) {
        return -1;
    }
    return ringl_backend_result(
        context, context->ringpu_ops.begin_render_pass_depth_mip_v2(
                     context->ringpu.session, command_list, render_pass));
}

int ringl_backend_begin_render_pass_depth_stencil_mip_v2(
    RinGLContext* context, uint64_t command_list,
    const RinGLRinGpuRenderPassDepthStencilMipV2* render_pass)
{
    if (context == NULL || command_list == 0u || render_pass == NULL ||
        render_pass->base.color_target == 0u ||
        render_pass->base.depth_target == 0u ||
        render_pass->base.stencil_target == 0u || !context->has_ringpu_ops ||
        context->ringpu_ops.begin_render_pass_depth_stencil_mip_v2 == NULL) {
        return -1;
    }
    return ringl_backend_result(
        context, context->ringpu_ops.begin_render_pass_depth_stencil_mip_v2(
                     context->ringpu.session, command_list, render_pass));
}

int ringl_backend_draw_vertices_mip_v3(
    RinGLContext* context, uint64_t command_list,
    const RinGLRinGpuDrawVerticesMipV3* draw)
{
    if (context == NULL || command_list == 0u || draw == NULL ||
        draw->base.pipeline == 0u || draw->base.color_target == 0u ||
        !context->has_ringpu_ops ||
        context->ringpu_ops.draw_vertices_mip_v3 == NULL) {
        return -1;
    }
    return ringl_backend_result(
        context, context->ringpu_ops.draw_vertices_mip_v3(
                     context->ringpu.session, command_list, draw));
}

int ringl_backend_draw_vertices_bindings_mip_v3(
    RinGLContext* context, uint64_t command_list,
    const RinGLRinGpuDrawVerticesBindingsMipV3* draw)
{
    if (context == NULL || command_list == 0u || draw == NULL ||
        draw->base.pipeline == 0u || draw->base.color_target == 0u ||
        !context->has_ringpu_ops ||
        context->ringpu_ops.draw_vertices_bindings_mip_v3 == NULL) {
        return -1;
    }
    return ringl_backend_result(
        context, context->ringpu_ops.draw_vertices_bindings_mip_v3(
                     context->ringpu.session, command_list, draw));
}

int ringl_backend_draw_indexed_mip_v3(
    RinGLContext* context, uint64_t command_list,
    const RinGLRinGpuDrawIndexedMipV3* draw)
{
    if (context == NULL || command_list == 0u || draw == NULL ||
        draw->base.pipeline == 0u || draw->base.color_target == 0u ||
        !context->has_ringpu_ops ||
        context->ringpu_ops.draw_indexed_mip_v3 == NULL) {
        return -1;
    }
    return ringl_backend_result(
        context, context->ringpu_ops.draw_indexed_mip_v3(
                     context->ringpu.session, command_list, draw));
}

int ringl_backend_draw_indexed_bindings_mip_v3(
    RinGLContext* context, uint64_t command_list,
    const RinGLRinGpuDrawIndexedBindingsMipV3* draw)
{
    if (context == NULL || command_list == 0u || draw == NULL ||
        draw->base.pipeline == 0u || draw->base.color_target == 0u ||
        !context->has_ringpu_ops ||
        context->ringpu_ops.draw_indexed_bindings_mip_v3 == NULL) {
        return -1;
    }
    return ringl_backend_result(
        context, context->ringpu_ops.draw_indexed_bindings_mip_v3(
                     context->ringpu.session, command_list, draw));
}

int ringl_backend_end_render_pass(RinGLContext* context,
                                  uint64_t command_list)
{
    if (context == NULL || command_list == 0u || !context->has_ringpu_ops ||
        context->ringpu_ops.end_render_pass == NULL) {
        return -1;
    }
    return ringl_backend_result(
        context, context->ringpu_ops.end_render_pass(context->ringpu.session,
                                                     command_list));
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
    return ringl_backend_result(
        context, context->ringpu_ops.present(context->ringpu.session,
                                             command_list, image, display_id));
}

int ringl_backend_close_command_list(RinGLContext* context,
                                     uint64_t command_list)
{
    if (context == NULL || command_list == 0u || !context->has_ringpu_ops ||
        context->ringpu_ops.close_command_list == NULL) {
        return -1;
    }
    return ringl_backend_result(
        context, context->ringpu_ops.close_command_list(context->ringpu.session,
                                                        command_list));
}

int ringl_backend_queue_submit(RinGLContext* context,
                               uint64_t queue,
                               uint64_t command_list)
{
    if (context == NULL || queue == 0u || command_list == 0u ||
        !context->has_ringpu_ops || context->ringpu_ops.queue_submit == NULL) {
        return -1;
    }
    return ringl_backend_result(
        context, context->ringpu_ops.queue_submit(context->ringpu.session, queue,
                                                  command_list));
}

void ringl_backend_destroy_object(RinGLContext* context, uint64_t object)
{
    if (context == NULL || object == 0u || !context->has_ringpu_ops ||
        context->ringpu_ops.destroy_object == NULL) {
        return;
    }

    (void)ringl_backend_result(
        context, context->ringpu_ops.destroy_object(context->ringpu.session, object));
}
