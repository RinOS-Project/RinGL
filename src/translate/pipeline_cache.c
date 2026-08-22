/* SPDX-License-Identifier: MIT */
#include "pipeline_cache.h"

#include <stdlib.h>
#include <string.h>

typedef struct RinGLPipelineCacheEntry {
    RinGLPipelineKey key;
    uint64_t hash;
    uint64_t pipeline;
    uint32_t valid;
    uint32_t reserved0;
} RinGLPipelineCacheEntry;

typedef struct RinGLPipelineCache {
    RinGLPipelineCacheEntry entries[RINGL_PIPELINE_CACHE_CAPACITY];
    uint32_t next_evict;
    uint32_t count;
} RinGLPipelineCache;

static RinGLProgramObject* current_program(RinGLContext* context)
{
    uint32_t index;

    if (context == NULL || context->current_program == 0u)
        return NULL;
    if (ringl_object_lookup(context, context->current_program,
                            RINGL_OBJECT_PROGRAM) == NULL)
        return NULL;
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

static RinGLPipelineCache* cache_for(RinGLContext* context, int create)
{
    RinGLPipelineCache* cache;

    if (context == NULL)
        return NULL;
    cache = (RinGLPipelineCache*)context->pipeline_cache;
    if (cache != NULL || !create)
        return cache;
    cache = (RinGLPipelineCache*)calloc(1, sizeof(*cache));
    if (cache == NULL)
        return NULL;
    context->pipeline_cache = cache;
    return cache;
}

static uint32_t native_blend_factor(uint32_t factor)
{
    switch (factor) {
    case RINGL_ZERO: return RINGL_RIN_GPU_BLEND_ZERO;
    case RINGL_ONE: return RINGL_RIN_GPU_BLEND_ONE;
    case RINGL_SRC_ALPHA: return RINGL_RIN_GPU_BLEND_SRC_ALPHA;
    case RINGL_ONE_MINUS_SRC_ALPHA:
        return RINGL_RIN_GPU_BLEND_ONE_MINUS_SRC_ALPHA;
    case RINGL_DST_ALPHA: return RINGL_RIN_GPU_BLEND_DST_ALPHA;
    case RINGL_ONE_MINUS_DST_ALPHA:
        return RINGL_RIN_GPU_BLEND_ONE_MINUS_DST_ALPHA;
    default: return 0u;
    }
}

static uint32_t native_blend_op(uint32_t operation)
{
    switch (operation) {
    case RINGL_FUNC_ADD: return RINGL_RIN_GPU_BLEND_ADD;
    case RINGL_FUNC_SUBTRACT: return RINGL_RIN_GPU_BLEND_SUBTRACT;
    case RINGL_FUNC_REVERSE_SUBTRACT:
        return RINGL_RIN_GPU_BLEND_REVERSE_SUBTRACT;
    case RINGL_MIN: return RINGL_RIN_GPU_BLEND_MINIMUM;
    case RINGL_MAX: return RINGL_RIN_GPU_BLEND_MAXIMUM;
    default: return 0u;
    }
}

static uint32_t native_cull_mode(const RinGLPipelineKey* key)
{
    if (key->cull_mode == 0u)
        return RINGL_RIN_GPU_CULL_NONE;
    if (key->cull_mode == RINGL_FRONT)
        return RINGL_RIN_GPU_CULL_FRONT;
    if (key->cull_mode == RINGL_BACK)
        return RINGL_RIN_GPU_CULL_BACK;
    return 0u;
}

static uint32_t native_front_face(uint32_t front_face)
{
    if (front_face == RINGL_CCW)
        return RINGL_RIN_GPU_FRONT_FACE_CCW;
    if (front_face == RINGL_CW)
        return RINGL_RIN_GPU_FRONT_FACE_CW;
    return 0u;
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

    if (context == NULL || key == NULL || color_format == 0u ||
        context->depth_test_enabled)
        return -1;
    program = current_program(context);
    if (program == NULL || !program->link_status)
        return -1;
    vertex = shader_object(context, program->vertex_shader);
    fragment = shader_object(context, program->fragment_shader);
    if (vertex == NULL || fragment == NULL ||
        vertex->ringpu_module == 0u || fragment->ringpu_module == 0u)
        return -1;
    if (ringl_resolve_vertex_layout(context, &layout) != 0)
        return -1;

    memset(&result, 0, sizeof(result));
    result.vertex_shader_module = vertex->ringpu_module;
    result.fragment_shader_module = fragment->ringpu_module;
    result.color_format = color_format;
    result.primitive_topology = RINGL_NATIVE_PRIMITIVE_TRIANGLE_LIST;
    result.vertex_stride = layout.stride;
    result.attribute_count = layout.attribute_count;
    result.blend_enabled = context->blend_enabled;
    result.blend_source_rgb = context->blend_source_rgb;
    result.blend_destination_rgb = context->blend_destination_rgb;
    result.blend_equation_rgb = context->blend_equation_rgb;
    result.blend_source_alpha = context->blend_source_alpha;
    result.blend_destination_alpha = context->blend_destination_alpha;
    result.blend_equation_alpha = context->blend_equation_alpha;
    result.color_write_mask = context->color_write_mask;
    result.cull_mode = context->cull_face_enabled ? context->cull_face_mode : 0u;
    result.front_face = context->front_face;
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

static int create_pipeline(RinGLContext* context,
                           const RinGLPipelineKey* key,
                           uint64_t* pipeline_out)
{
    RinGLRinGpuVertexAttributeV1 attributes[RINGL_MAX_VERTEX_ATTRIBS];
    RinGLProgramObject* program;
    uint32_t index;

    if (context == NULL || key == NULL || pipeline_out == NULL ||
        key->attribute_count > RINGL_MAX_VERTEX_ATTRIBS)
        return -1;
    program = current_program(context);
    if (program == NULL || !program->link_status)
        return -1;

    memset(attributes, 0, sizeof(attributes));
    for (index = 0u; index < key->attribute_count; ++index) {
        attributes[index].location = key->attributes[index].location;
        attributes[index].format = key->attributes[index].format;
        attributes[index].offset = key->attributes[index].offset;
    }

    if (context->ringpu_ops.create_graphics_pipeline_native != NULL) {
        RinGLRinGpuGraphicsPipelineNativeV1 desc;
        RinGLRinGpuVaryingV1 varyings[RINGL_MAX_VARYINGS * 2u];
        uint32_t varying_count = 0u;
        uint32_t cull = native_cull_mode(key);
        uint32_t front = native_front_face(key->front_face);

        if (cull == 0u || front == 0u || key->color_write_mask == 0u)
            return -1;
        memset(&desc, 0, sizeof(desc));
        memset(varyings, 0, sizeof(varyings));
        desc.vertex_shader = key->vertex_shader_module;
        desc.fragment_shader = key->fragment_shader_module;
        desc.color_format = key->color_format;
        desc.primitive_topology = key->primitive_topology;
        desc.vertex_stride = key->vertex_stride;
        desc.position_output_location = 0u;
        desc.color_write_mask = key->color_write_mask;
        desc.cull_mode = cull;
        desc.front_face = front;
        if (key->blend_enabled) {
            desc.blend_enabled = 1u;
            desc.source_color_factor = native_blend_factor(key->blend_source_rgb);
            desc.destination_color_factor =
                native_blend_factor(key->blend_destination_rgb);
            desc.color_operation = native_blend_op(key->blend_equation_rgb);
            desc.source_alpha_factor =
                native_blend_factor(key->blend_source_alpha);
            desc.destination_alpha_factor =
                native_blend_factor(key->blend_destination_alpha);
            desc.alpha_operation = native_blend_op(key->blend_equation_alpha);
            if (desc.source_color_factor == 0u ||
                desc.destination_color_factor == 0u ||
                desc.color_operation == 0u ||
                desc.source_alpha_factor == 0u ||
                desc.destination_alpha_factor == 0u ||
                desc.alpha_operation == 0u)
                return -1;
        }
        for (index = 0u; index < program->varying_count; ++index) {
            uint32_t component;
            const RinGLProgramVarying* varying = &program->varyings[index];
            if (varying->width != 2u || varying_count + varying->width >
                RINGL_MAX_VARYINGS * 2u)
                return -1;
            for (component = 0u; component < varying->width; ++component) {
                RinGLRinGpuVaryingV1* native = &varyings[varying_count++];
                native->vertex_output_location =
                    varying->vertex_output_location + component;
                native->fragment_input_location =
                    varying->fragment_input_location + component;
                native->type = 1u; /* RIN_GPU_VARYING_FLOAT32 */
                native->interpolation = 1u; /* perspective */
            }
        }
        return ringl_backend_create_graphics_pipeline_native(
            context, &desc, attributes, key->attribute_count,
            varying_count == 0u ? NULL : varyings, varying_count,
            pipeline_out);
    }

    if (program->varying_count != 0u || key->blend_enabled ||
        key->cull_mode != 0u || key->front_face != RINGL_CCW ||
        key->color_write_mask != RINGL_RIN_GPU_COLOR_WRITE_ALL)
        return -1;
    {
        RinGLRinGpuGraphicsPipelineV1 desc;
        memset(&desc, 0, sizeof(desc));
        desc.vertex_shader = key->vertex_shader_module;
        desc.fragment_shader = key->fragment_shader_module;
        desc.color_format = key->color_format;
        desc.primitive_topology = key->primitive_topology;
        desc.vertex_stride = key->vertex_stride;
        desc.attribute_count = key->attribute_count;
        return ringl_backend_create_graphics_pipeline(
            context, &desc, attributes, key->attribute_count, pipeline_out);
    }
}

int ringl_pipeline_cache_get_or_create(RinGLContext* context,
                                       const RinGLPipelineKey* key,
                                       uint64_t* pipeline_out)
{
    RinGLPipelineCache* cache;
    uint64_t hash;
    uint64_t pipeline = 0u;
    uint32_t index;
    uint32_t target;

    if (context == NULL || key == NULL || pipeline_out == NULL)
        return -1;
    *pipeline_out = 0u;
    hash = ringl_pipeline_key_hash(key);
    if (hash == 0u)
        return -1;

    cache = cache_for(context, 1);
    if (cache == NULL)
        return -1;

    for (index = 0u; index < RINGL_PIPELINE_CACHE_CAPACITY; ++index) {
        RinGLPipelineCacheEntry* entry = &cache->entries[index];
        if (entry->valid && entry->hash == hash &&
            ringl_pipeline_key_equal(&entry->key, key)) {
            *pipeline_out = entry->pipeline;
            return 0;
        }
    }

    if (create_pipeline(context, key, &pipeline) != 0 || pipeline == 0u)
        return -1;

    if (cache->count < RINGL_PIPELINE_CACHE_CAPACITY) {
        target = cache->count++;
    } else {
        target = cache->next_evict;
        cache->next_evict = (cache->next_evict + 1u) %
                            RINGL_PIPELINE_CACHE_CAPACITY;
        if (cache->entries[target].valid &&
            cache->entries[target].pipeline != 0u)
            ringl_backend_destroy_object(context, cache->entries[target].pipeline);
    }

    cache->entries[target].key = *key;
    cache->entries[target].hash = hash;
    cache->entries[target].pipeline = pipeline;
    cache->entries[target].valid = 1u;
    *pipeline_out = pipeline;
    return 0;
}

int ringl_get_or_create_graphics_pipeline(RinGLContext* context,
                                          uint32_t color_format,
                                          uint64_t* pipeline_out)
{
    RinGLPipelineKey key;

    if (ringl_build_pipeline_key(context, color_format, &key) != 0)
        return -1;
    return ringl_pipeline_cache_get_or_create(context, &key, pipeline_out);
}

void ringl_pipeline_cache_destroy(RinGLContext* context)
{
    RinGLPipelineCache* cache;
    uint32_t index;

    if (context == NULL)
        return;
    cache = cache_for(context, 0);
    if (cache == NULL)
        return;
    for (index = 0u; index < RINGL_PIPELINE_CACHE_CAPACITY; ++index) {
        if (cache->entries[index].valid && cache->entries[index].pipeline != 0u)
            ringl_backend_destroy_object(context, cache->entries[index].pipeline);
    }
    free(cache);
    context->pipeline_cache = NULL;
}

void ringl_invalidate_graphics_artifacts(RinGLContext* context)
{
    if (context == NULL)
        return;
    if (context->graphics_command_list != 0u) {
        ringl_backend_destroy_object(context, context->graphics_command_list);
        context->graphics_command_list = 0u;
    }
    if (context->graphics_bind_group != 0u) {
        ringl_backend_destroy_object(context, context->graphics_bind_group);
        context->graphics_bind_group = 0u;
    }
    ringl_pipeline_cache_destroy(context);
    ringl_context_mark_dirty(context, RINGL_DIRTY_PIPELINE | RINGL_DIRTY_BINDINGS);
}
