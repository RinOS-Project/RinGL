/* SPDX-License-Identifier: MIT */
#include "pipeline_cache.h"

#include <string.h>

static RinGLProgramObject* current_program(RinGLContext* context)
{
    uint32_t index;

    if (context == NULL || context->current_program == 0u)
        return NULL;
    if (ringl_object_lookup(context, context->current_program,
                            RINGL_OBJECT_PROGRAM) == NULL) {
        return NULL;
    }
    index = ringl_object_slot_index(context->current_program);
    if (index >= RINGL_OBJECT_SLOT_COUNT)
        return NULL;
    return &context->programs[index];
}

static RinGLShaderObject* shader_object(RinGLContext* context,
                                        uint32_t shader)
{
    uint32_t index;

    if (ringl_object_lookup(context, shader, RINGL_OBJECT_SHADER) == NULL)
        return NULL;
    index = ringl_object_slot_index(shader);
    if (index >= RINGL_OBJECT_SLOT_COUNT)
        return NULL;
    return &context->shaders[index];
}

int ringl_build_pipeline_key(RinGLContext* context,
                             uint32_t color_format,
                             RinGLPipelineKey* key)
{
    RinGLProgramObject* program;
    RinGLShaderObject* vertex;
    RinGLShaderObject* fragment;
    RinGLResolvedVertexLayout layout;
    RinGLPipelineKey result;
    uint32_t index;

    if (context == NULL || key == NULL || color_format == 0u)
        return -1;
    program = current_program(context);
    if (program == NULL || !program->link_status)
        return -1;
    vertex = shader_object(context, program->vertex_shader);
    fragment = shader_object(context, program->fragment_shader);
    if (vertex == NULL || fragment == NULL ||
        vertex->ringpu_module == 0u || fragment->ringpu_module == 0u) {
        return -1;
    }
    if (ringl_resolve_vertex_layout(context, &layout) != 0)
        return -1;

    memset(&result, 0, sizeof(result));
    result.vertex_shader_module = vertex->ringpu_module;
    result.fragment_shader_module = fragment->ringpu_module;
    result.color_format = color_format;
    result.primitive_topology = RINGL_NATIVE_PRIMITIVE_TRIANGLE_LIST;
    result.vertex_stride = layout.stride;
    result.attribute_count = layout.attribute_count;
    for (index = 0u; index < layout.attribute_count; ++index)
        result.attributes[index] = layout.attributes[index];

    *key = result;
    return 0;
}

uint64_t ringl_pipeline_key_hash(const RinGLPipelineKey* key)
{
    const unsigned char* bytes = (const unsigned char*)key;
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t index;

    if (key == NULL)
        return 0u;
    for (index = 0u; index < sizeof(*key); ++index) {
        hash ^= bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

int ringl_pipeline_key_equal(const RinGLPipelineKey* left,
                             const RinGLPipelineKey* right)
{
    if (left == NULL || right == NULL)
        return 0;
    return memcmp(left, right, sizeof(*left)) == 0;
}
