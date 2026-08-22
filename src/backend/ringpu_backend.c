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

void ringl_backend_destroy_object(RinGLContext* context, uint64_t object)
{
    if (context == NULL || object == 0u || !context->has_ringpu_ops ||
        context->ringpu_ops.destroy_object == NULL) {
        return;
    }

    (void)context->ringpu_ops.destroy_object(context->ringpu.session, object);
}
