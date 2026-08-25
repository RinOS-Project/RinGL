/* SPDX-License-Identifier: MIT */
#include "pipeline_cache.h"
#include "../shader/rsh1_abi.h"

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

static int fixed_raster_interface(const uint8_t* vertex_rsh1,
                                  uint32_t vertex_rsh1_size,
                                  const uint8_t* fragment_rsh1,
                                  uint32_t fragment_rsh1_size,
                                  uint32_t* scalar_varying_count,
                                  uint32_t* point_size_output_enabled)
{
    RinGLRsh1HeaderV1 vertex_header;
    RinGLRsh1HeaderV1 fragment_header;
    uint32_t color_output_count;

    if (scalar_varying_count == NULL || point_size_output_enabled == NULL ||
        vertex_rsh1 == NULL ||
        fragment_rsh1 == NULL || vertex_rsh1_size < sizeof(vertex_header) ||
        fragment_rsh1_size < sizeof(fragment_header)) {
        return 0;
    }
    memcpy(&vertex_header, vertex_rsh1, sizeof(vertex_header));
    memcpy(&fragment_header, fragment_rsh1, sizeof(fragment_header));
    if (vertex_header.stage != RINGL_RSH1_STAGE_VERTEX ||
        fragment_header.stage != RINGL_RSH1_STAGE_FRAGMENT ||
        fragment_header.output_count < 4u ||
        fragment_header.output_count > RINGL_MAX_COLOR_ATTACHMENTS * 4u + 1u)
        return 0;
    color_output_count = fragment_header.output_count;
    if ((color_output_count & 3u) == 1u)
        color_output_count--;
    if ((color_output_count & 3u) != 0u)
        return 0;
    if ((vertex_header.output_count == 8u && fragment_header.input_count == 4u) ||
        (vertex_header.output_count == 10u && fragment_header.input_count == 6u) ||
        (vertex_header.output_count == 12u && fragment_header.input_count == 8u)) {
        *scalar_varying_count = fragment_header.input_count;
        *point_size_output_enabled = 0u;
        return 1;
    }
    if (vertex_header.output_count == 9u && fragment_header.input_count == 4u) {
        *scalar_varying_count = fragment_header.input_count;
        *point_size_output_enabled = 1u;
        return 1;
    }
    return 0;
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
    case RINGL_SRC_COLOR: return RINGL_RIN_GPU_BLEND_SRC_COLOR;
    case RINGL_ONE_MINUS_SRC_COLOR:
        return RINGL_RIN_GPU_BLEND_ONE_MINUS_SRC_COLOR;
    case RINGL_SRC_ALPHA: return RINGL_RIN_GPU_BLEND_SRC_ALPHA;
    case RINGL_ONE_MINUS_SRC_ALPHA:
        return RINGL_RIN_GPU_BLEND_ONE_MINUS_SRC_ALPHA;
    case RINGL_DST_ALPHA: return RINGL_RIN_GPU_BLEND_DST_ALPHA;
    case RINGL_ONE_MINUS_DST_ALPHA:
        return RINGL_RIN_GPU_BLEND_ONE_MINUS_DST_ALPHA;
    case RINGL_DST_COLOR: return RINGL_RIN_GPU_BLEND_DST_COLOR;
    case RINGL_ONE_MINUS_DST_COLOR:
        return RINGL_RIN_GPU_BLEND_ONE_MINUS_DST_COLOR;
    case RINGL_SRC_ALPHA_SATURATE:
        return RINGL_RIN_GPU_BLEND_SRC_ALPHA_SATURATE;
    case RINGL_CONSTANT_COLOR:
        return RINGL_RIN_GPU_BLEND_CONSTANT_COLOR;
    case RINGL_ONE_MINUS_CONSTANT_COLOR:
        return RINGL_RIN_GPU_BLEND_ONE_MINUS_CONSTANT_COLOR;
    case RINGL_CONSTANT_ALPHA:
        return RINGL_RIN_GPU_BLEND_CONSTANT_ALPHA;
    case RINGL_ONE_MINUS_CONSTANT_ALPHA:
        return RINGL_RIN_GPU_BLEND_ONE_MINUS_CONSTANT_ALPHA;
    default: return 0u;
    }
}

static int blend_factor_uses_constant(uint32_t factor)
{
    return factor == RINGL_CONSTANT_COLOR ||
           factor == RINGL_ONE_MINUS_CONSTANT_COLOR ||
           factor == RINGL_CONSTANT_ALPHA ||
           factor == RINGL_ONE_MINUS_CONSTANT_ALPHA;
}

static int key_uses_constant_blend(const RinGLPipelineKey* key)
{
    return key->blend_enabled != 0u &&
           (blend_factor_uses_constant(key->blend_source_rgb) ||
            blend_factor_uses_constant(key->blend_destination_rgb) ||
            blend_factor_uses_constant(key->blend_source_alpha) ||
            blend_factor_uses_constant(key->blend_destination_alpha));
}

static float blend_constant_for_color_target(uint32_t color_format, float value)
{
    if (color_format == RINGL_RIN_GPU_FORMAT_RGBA16_FLOAT ||
        color_format == RINGL_RIN_GPU_FORMAT_RGBA32_FLOAT)
        return value;
    if (value <= 0.0f)
        return 0.0f;
    if (value >= 1.0f)
        return 1.0f;
    return value;
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

static uint32_t native_depth_compare(uint32_t compare)
{
    switch (compare) {
    case RINGL_NEVER: return RINGL_RIN_GPU_COMPARE_NEVER;
    case RINGL_LESS: return RINGL_RIN_GPU_COMPARE_LESS;
    case RINGL_EQUAL: return RINGL_RIN_GPU_COMPARE_EQUAL;
    case RINGL_LEQUAL: return RINGL_RIN_GPU_COMPARE_LEQUAL;
    case RINGL_GREATER: return RINGL_RIN_GPU_COMPARE_GREATER;
    case RINGL_NOTEQUAL: return RINGL_RIN_GPU_COMPARE_NOTEQUAL;
    case RINGL_GEQUAL: return RINGL_RIN_GPU_COMPARE_GEQUAL;
    case RINGL_ALWAYS: return RINGL_RIN_GPU_COMPARE_ALWAYS;
    default: return 0u;
    }
}

static uint32_t native_stencil_operation(uint32_t operation)
{
    switch (operation) {
    case RINGL_KEEP: return RINGL_RIN_GPU_STENCIL_KEEP;
    case RINGL_ZERO: return RINGL_RIN_GPU_STENCIL_ZERO;
    case RINGL_REPLACE: return RINGL_RIN_GPU_STENCIL_REPLACE;
    case RINGL_INCR: return RINGL_RIN_GPU_STENCIL_INCREMENT_CLAMP;
    case RINGL_DECR: return RINGL_RIN_GPU_STENCIL_DECREMENT_CLAMP;
    case RINGL_INVERT: return RINGL_RIN_GPU_STENCIL_INVERT;
    case RINGL_INCR_WRAP: return RINGL_RIN_GPU_STENCIL_INCREMENT_WRAP;
    case RINGL_DECR_WRAP: return RINGL_RIN_GPU_STENCIL_DECREMENT_WRAP;
    default: return 0u;
    }
}

static int native_primitive_topology_valid(uint32_t primitive_topology)
{
    return primitive_topology == RINGL_NATIVE_PRIMITIVE_TRIANGLE_LIST ||
           primitive_topology == RINGL_NATIVE_PRIMITIVE_POINT_LIST ||
           primitive_topology == RINGL_NATIVE_PRIMITIVE_LINE_LIST ||
           primitive_topology == RINGL_NATIVE_PRIMITIVE_LINE_STRIP ||
           primitive_topology == RINGL_NATIVE_PRIMITIVE_LINE_LOOP ||
           primitive_topology == RINGL_NATIVE_PRIMITIVE_TRIANGLE_STRIP ||
           primitive_topology == RINGL_NATIVE_PRIMITIVE_TRIANGLE_FAN;
}

int ringl_build_pipeline_key(RinGLContext* context,
                             uint32_t color_format, uint32_t depth_format,
                             uint32_t primitive_topology,
                             uint32_t depth_test_enabled,
                             uint32_t stencil_test_enabled,
                             RinGLPipelineKey* key)
{
    RinGLProgramObject* program;
    RinGLShaderObject* vertex;
    RinGLShaderObject* fragment;
    RinGLResolvedVertexLayout layout;
    RinGLPipelineKey result;
    const uint8_t* vertex_rsh1;
    const uint8_t* fragment_rsh1;
    uint32_t vertex_rsh1_size;
    uint32_t fragment_rsh1_size;
    uint64_t vertex_module;
    uint64_t fragment_module;
    uint32_t index;
    uint32_t fixed_scalar_varying_count;
    uint32_t fixed_point_size_output;

    if (context == NULL || key == NULL || color_format == 0u ||
        !native_primitive_topology_valid(primitive_topology) ||
        depth_test_enabled > RINGL_TRUE || stencil_test_enabled > RINGL_TRUE ||
        (depth_format != 0u &&
         depth_format != RINGL_RIN_GPU_FORMAT_D32_FLOAT &&
         depth_format != RINGL_RIN_GPU_FORMAT_D32_FLOAT_S8_UINT &&
         depth_format != RINGL_RIN_GPU_FORMAT_S8_UINT) ||
        (depth_test_enabled != 0u &&
         depth_format != RINGL_RIN_GPU_FORMAT_D32_FLOAT &&
         depth_format != RINGL_RIN_GPU_FORMAT_D32_FLOAT_S8_UINT) ||
        (stencil_test_enabled != 0u &&
         depth_format != RINGL_RIN_GPU_FORMAT_D32_FLOAT_S8_UINT &&
         depth_format != RINGL_RIN_GPU_FORMAT_S8_UINT) ||
        (depth_test_enabled == 0u && stencil_test_enabled == 0u &&
         depth_format != 0u))
        return -1;
    program = current_program(context);
    if (program == NULL || !program->link_status)
        return -1;
    vertex = shader_object(context, program->linked_vertex_shader);
    fragment = shader_object(context, program->linked_fragment_shader);
    if (vertex == NULL || fragment == NULL)
        return -1;
    vertex_rsh1 = program->vertex_uniform_rsh1 != NULL
        ? program->vertex_uniform_rsh1 : vertex->rsh1;
    fragment_rsh1 = program->fragment_uniform_rsh1 != NULL
        ? program->fragment_uniform_rsh1 : fragment->rsh1;
    vertex_rsh1_size = program->vertex_uniform_rsh1 != NULL
        ? program->vertex_uniform_rsh1_size : vertex->rsh1_size;
    fragment_rsh1_size = program->fragment_uniform_rsh1 != NULL
        ? program->fragment_uniform_rsh1_size : fragment->rsh1_size;
    vertex_module = program->vertex_uniform_rsh1 != NULL
        ? program->vertex_uniform_module : vertex->ringpu_module;
    fragment_module = program->fragment_uniform_rsh1 != NULL
        ? program->fragment_uniform_module : fragment->ringpu_module;
    if (vertex_module == 0u || fragment_module == 0u)
        return -1;
    if (ringl_resolve_vertex_layout(context, &layout) != 0)
        return -1;

    memset(&result, 0, sizeof(result));
    result.vertex_shader_module = vertex_module;
    result.fragment_shader_module = fragment_module;
    result.color_format = color_format;
    result.depth_format = depth_format;
    if (depth_test_enabled != 0u) {
        result.depth_compare = native_depth_compare(context->depth_func);
        if (result.depth_compare == 0u)
            return -1;
        result.depth_write_enabled = context->depth_write_mask;
    } else if (stencil_test_enabled != 0u &&
               depth_format != RINGL_RIN_GPU_FORMAT_S8_UINT) {
        /* D32S8 stencil-only draws keep the physical depth stage
         * observationally inert. Native S8 has no depth stage at all. */
        result.depth_compare = RINGL_RIN_GPU_COMPARE_ALWAYS;
        result.depth_write_enabled = RINGL_FALSE;
    }
    if (stencil_test_enabled != 0u) {
        result.stencil_test_enabled = RINGL_TRUE;
        result.stencil_compare = native_depth_compare(context->stencil_func);
        result.stencil_reference = context->stencil_reference;
        result.stencil_read_mask = context->stencil_value_mask;
        result.stencil_write_mask = context->stencil_write_mask;
        result.stencil_fail_operation =
            native_stencil_operation(context->stencil_fail_operation);
        result.stencil_depth_fail_operation =
            native_stencil_operation(context->stencil_depth_fail_operation);
        result.stencil_pass_operation =
            native_stencil_operation(context->stencil_pass_operation);
        if (result.stencil_compare == 0u ||
            result.stencil_fail_operation == 0u ||
            result.stencil_depth_fail_operation == 0u ||
            result.stencil_pass_operation == 0u) {
            return -1;
        }
        result.back_stencil_compare =
            native_depth_compare(context->back_stencil_func);
        result.back_stencil_reference = context->back_stencil_reference;
        result.back_stencil_read_mask = context->back_stencil_value_mask;
        result.back_stencil_write_mask = context->back_stencil_write_mask;
        result.back_stencil_fail_operation =
            native_stencil_operation(context->back_stencil_fail_operation);
        result.back_stencil_depth_fail_operation = native_stencil_operation(
            context->back_stencil_depth_fail_operation);
        result.back_stencil_pass_operation =
            native_stencil_operation(context->back_stencil_pass_operation);
        if (result.back_stencil_compare == 0u ||
            result.back_stencil_fail_operation == 0u ||
            result.back_stencil_depth_fail_operation == 0u ||
            result.back_stencil_pass_operation == 0u) {
            return -1;
        }
        if (result.back_stencil_compare == result.stencil_compare &&
            result.back_stencil_reference == result.stencil_reference &&
            result.back_stencil_read_mask == result.stencil_read_mask &&
            result.back_stencil_write_mask == result.stencil_write_mask &&
            result.back_stencil_fail_operation == result.stencil_fail_operation &&
            result.back_stencil_depth_fail_operation ==
                result.stencil_depth_fail_operation &&
            result.back_stencil_pass_operation ==
                result.stencil_pass_operation) {
            result.back_stencil_compare = 0u;
            result.back_stencil_reference = 0u;
            result.back_stencil_read_mask = 0u;
            result.back_stencil_write_mask = 0u;
            result.back_stencil_fail_operation = 0u;
            result.back_stencil_depth_fail_operation = 0u;
            result.back_stencil_pass_operation = 0u;
        } else {
            result.separate_stencil_enabled = RINGL_TRUE;
        }
    }
    result.primitive_topology = primitive_topology;
    result.vertex_stride = layout.stride;
    result.vertex_binding_count = layout.binding_count;
    result.attribute_count = layout.attribute_count;
    result.blend_enabled = context->blend_enabled;
    result.blend_source_rgb = context->blend_source_rgb;
    result.blend_destination_rgb = context->blend_destination_rgb;
    result.blend_equation_rgb = context->blend_equation_rgb;
    result.blend_source_alpha = context->blend_source_alpha;
    result.blend_destination_alpha = context->blend_destination_alpha;
    result.blend_equation_alpha = context->blend_equation_alpha;
    result.blend_constant_red = blend_constant_for_color_target(
        color_format, context->blend_constant_red);
    result.blend_constant_green = blend_constant_for_color_target(
        color_format, context->blend_constant_green);
    result.blend_constant_blue = blend_constant_for_color_target(
        color_format, context->blend_constant_blue);
    result.blend_constant_alpha = blend_constant_for_color_target(
        color_format, context->blend_constant_alpha);
    result.color_write_mask = ringl_effective_color_write_mask(context);
    result.dither_enabled = context->dither_enabled;
    result.cull_mode = context->cull_face_enabled ? context->cull_face_mode : 0u;
    result.front_face = context->front_face;
    for (index = 0u; index < layout.attribute_count; ++index)
        result.attributes[index] = layout.attributes[index];
    for (index = 0u; index < layout.binding_count; ++index)
        result.bindings[index] = layout.bindings[index];
    for (index = 0u; index < program->varying_count; ++index) {
        uint32_t component;
        const RinGLProgramVarying* varying = &program->varyings[index];
        if ((varying->width != 2u && varying->width != 3u &&
             varying->width != 4u) ||
            result.varying_count + varying->width >
                RINGL_PIPELINE_MAX_SCALAR_VARYINGS)
            return -1;
        for (component = 0u; component < varying->width; ++component) {
            RinGLRinGpuVaryingV1* native =
                &result.varyings[result.varying_count++];
            native->vertex_output_location =
                varying->vertex_output_location + component;
            native->fragment_input_location =
                varying->fragment_input_location + component;
            native->type = 1u;
            native->interpolation = 1u;
        }
    }
    if ((context->ringpu_ops.create_graphics_pipeline_native != NULL ||
         ((layout.binding_count > 1u ||
           (layout.binding_count == 1u && layout.stride == 0u)) &&
          context->ringpu_ops
                  .create_graphics_pipeline_native_vertex_bindings != NULL)) &&
        (layout.binding_count == 0u ||
         (layout.binding_count == 1u && layout.stride != 0u) ||
         context->ringpu_ops.create_graphics_pipeline_native_vertex_bindings !=
             NULL) &&
        fixed_raster_interface(vertex_rsh1, vertex_rsh1_size,
                               fragment_rsh1, fragment_rsh1_size,
                               &fixed_scalar_varying_count,
                               &fixed_point_size_output)) {
        result.point_size_output_enabled = fixed_point_size_output;
        if (result.varying_count > fixed_scalar_varying_count)
            return -1;
        while (result.varying_count < fixed_scalar_varying_count) {
            RinGLRinGpuVaryingV1* native =
                &result.varyings[result.varying_count];
            native->vertex_output_location =
                (result.point_size_output_enabled != 0u ? 5u : 4u) +
                result.varying_count;
            native->fragment_input_location = result.varying_count;
            native->type = 1u;
            native->interpolation = 1u;
            result.varying_count++;
        }
    }

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
    RinGLRinGpuVertexAttributeV1
        attributes[RINGL_MAX_VERTEX_INPUT_COMPONENTS];
    RinGLRinGpuVertexAttributeV2
        binding_attributes[RINGL_MAX_VERTEX_INPUT_COMPONENTS];
    RinGLRinGpuVertexBufferLayoutV1
        vertex_bindings[RINGL_MAX_VERTEX_ATTRIBS];
    uint32_t index;
    int requires_v2_blend;
    int uses_vertex_bindings;

    if (context == NULL || key == NULL || pipeline_out == NULL ||
        key->attribute_count > RINGL_MAX_VERTEX_INPUT_COMPONENTS ||
        key->vertex_binding_count > RINGL_MAX_VERTEX_ATTRIBS ||
        key->varying_count > RINGL_PIPELINE_MAX_SCALAR_VARYINGS)
        return -1;

    memset(attributes, 0, sizeof(attributes));
    memset(binding_attributes, 0, sizeof(binding_attributes));
    memset(vertex_bindings, 0, sizeof(vertex_bindings));
    for (index = 0u; index < key->attribute_count; ++index) {
        attributes[index].location = key->attributes[index].location;
        attributes[index].format = key->attributes[index].format;
        attributes[index].offset = key->attributes[index].offset;
        attributes[index].flags = key->attributes[index].flags;
        binding_attributes[index].location = key->attributes[index].location;
        binding_attributes[index].format = key->attributes[index].format;
        binding_attributes[index].offset = key->attributes[index].offset;
        binding_attributes[index].flags = key->attributes[index].flags;
        binding_attributes[index].binding = key->attributes[index].binding;
    }
    for (index = 0u; index < key->vertex_binding_count; ++index) {
        if (key->bindings[index].buffer == 0u ||
            key->bindings[index].stride == 0u)
            return -1;
        vertex_bindings[index].binding = index;
        vertex_bindings[index].stride = key->bindings[index].stride;
        vertex_bindings[index].flags = key->bindings[index].divisor;
    }
    uses_vertex_bindings = key->vertex_binding_count != 0u &&
        key->vertex_stride == 0u;
    if (uses_vertex_bindings &&
        (context->ringpu.vertex_input_capabilities &
         RINGL_RIN_GPU_VERTEX_INPUT_MULTI_BUFFER) == 0u)
        return -1;

    requires_v2_blend = key_uses_constant_blend(key);

    if ((requires_v2_blend != 0 &&
         ((!uses_vertex_bindings &&
           context->ringpu_ops.create_graphics_pipeline_native_v2 != NULL) ||
          (uses_vertex_bindings &&
           context->ringpu_ops
                   .create_graphics_pipeline_native_vertex_bindings_v2 !=
               NULL))) ||
        (requires_v2_blend == 0 &&
         (context->ringpu_ops.create_graphics_pipeline_native != NULL ||
         (uses_vertex_bindings &&
          context->ringpu_ops
                  .create_graphics_pipeline_native_vertex_bindings != NULL)) &&
        (!uses_vertex_bindings ||
         context->ringpu_ops.create_graphics_pipeline_native_vertex_bindings !=
             NULL))) {
        RinGLRinGpuGraphicsPipelineNativeV1 desc;
        uint32_t cull = native_cull_mode(key);
        uint32_t front = native_front_face(key->front_face);

        if (cull == 0u || front == 0u ||
            (key->color_write_mask == 0u && key->depth_format == 0u))
            return -1;
        memset(&desc, 0, sizeof(desc));
        desc.vertex_shader = key->vertex_shader_module;
        desc.fragment_shader = key->fragment_shader_module;
        desc.color_format = key->color_format;
        desc.primitive_topology = key->primitive_topology;
        desc.vertex_stride = key->vertex_stride;
        desc.position_output_location = 0u;
        desc.flags = key->point_size_output_enabled != 0u
            ? RINGL_RIN_GPU_GRAPHICS_PIPELINE_NATIVE_POINT_SIZE_OUTPUT : 0u;
        desc.depth_format = key->depth_format;
        desc.depth_compare = key->depth_compare;
        desc.depth_write_enabled = key->depth_write_enabled;
        desc.stencil_test_enabled = key->stencil_test_enabled;
        desc.stencil_compare = key->stencil_compare;
        desc.stencil_reference = key->stencil_reference;
        desc.stencil_read_mask = key->stencil_read_mask;
        desc.stencil_write_mask = key->stencil_write_mask;
        desc.stencil_fail_operation = key->stencil_fail_operation;
        desc.stencil_depth_fail_operation =
            key->stencil_depth_fail_operation;
        desc.stencil_pass_operation = key->stencil_pass_operation;
        desc.separate_stencil_enabled = key->separate_stencil_enabled;
        desc.back_stencil_compare = key->back_stencil_compare;
        desc.back_stencil_reference = key->back_stencil_reference;
        desc.back_stencil_read_mask = key->back_stencil_read_mask;
        desc.back_stencil_write_mask = key->back_stencil_write_mask;
        desc.back_stencil_fail_operation = key->back_stencil_fail_operation;
        desc.back_stencil_depth_fail_operation =
            key->back_stencil_depth_fail_operation;
        desc.back_stencil_pass_operation = key->back_stencil_pass_operation;
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
        if (uses_vertex_bindings) {
            if (requires_v2_blend != 0) {
                RinGLRinGpuGraphicsPipelineNativeV2 desc_v2;
                memset(&desc_v2, 0, sizeof(desc_v2));
                desc_v2.base = desc;
                desc_v2.blend_constant_red = key->blend_constant_red;
                desc_v2.blend_constant_green = key->blend_constant_green;
                desc_v2.blend_constant_blue = key->blend_constant_blue;
                desc_v2.blend_constant_alpha = key->blend_constant_alpha;
                return ringl_backend_create_graphics_pipeline_native_vertex_bindings_v2(
                    context, &desc_v2, binding_attributes,
                    key->attribute_count, vertex_bindings,
                    key->vertex_binding_count,
                    key->varying_count == 0u ? NULL : key->varyings,
                    key->varying_count, pipeline_out);
            }
            return ringl_backend_create_graphics_pipeline_native_vertex_bindings(
                context, &desc, binding_attributes, key->attribute_count,
                vertex_bindings, key->vertex_binding_count,
                key->varying_count == 0u ? NULL : key->varyings,
                key->varying_count, pipeline_out);
        }
        if (requires_v2_blend != 0) {
            RinGLRinGpuGraphicsPipelineNativeV2 desc_v2;
            memset(&desc_v2, 0, sizeof(desc_v2));
            desc_v2.base = desc;
            desc_v2.blend_constant_red = key->blend_constant_red;
            desc_v2.blend_constant_green = key->blend_constant_green;
            desc_v2.blend_constant_blue = key->blend_constant_blue;
            desc_v2.blend_constant_alpha = key->blend_constant_alpha;
            return ringl_backend_create_graphics_pipeline_native_v2(
                context, &desc_v2, attributes, key->attribute_count,
                key->varying_count == 0u ? NULL : key->varyings,
                key->varying_count, pipeline_out);
        }
        return ringl_backend_create_graphics_pipeline_native(
            context, &desc, attributes, key->attribute_count,
            key->varying_count == 0u ? NULL : key->varyings,
            key->varying_count, pipeline_out);
    }

    if (key->depth_format != 0u || key->stencil_test_enabled ||
        key->varying_count != 0u || key->blend_enabled ||
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
        if (uses_vertex_bindings) {
            return ringl_backend_create_graphics_pipeline_vertex_bindings(
                context, &desc, binding_attributes, key->attribute_count,
                vertex_bindings, key->vertex_binding_count, pipeline_out);
        }
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
                                          uint32_t depth_format,
                                          uint32_t primitive_topology,
                                          uint32_t depth_test_enabled,
                                          uint32_t stencil_test_enabled,
                                          uint64_t* pipeline_out)
{
    RinGLPipelineKey key;

    if (ringl_build_pipeline_key(context, color_format, depth_format,
                                 primitive_topology, depth_test_enabled,
                                 stencil_test_enabled, &key) != 0)
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
