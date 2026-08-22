/* SPDX-License-Identifier: MIT */
#ifndef RINGL_PIPELINE_CACHE_H
#define RINGL_PIPELINE_CACHE_H

#include <stdint.h>

#include "../ringl_internal.h"

#define RINGL_NATIVE_PRIMITIVE_TRIANGLE_LIST 1u
#define RINGL_PIPELINE_CACHE_CAPACITY 32u
#define RINGL_PIPELINE_MAX_SCALAR_VARYINGS (RINGL_MAX_VARYINGS * 2u)

typedef struct RinGLPipelineKey {
    uint64_t vertex_shader_module;
    uint64_t fragment_shader_module;
    uint32_t color_format;
    uint32_t depth_format;
    uint32_t depth_compare;
    uint32_t depth_write_enabled;
    uint32_t stencil_test_enabled;
    uint32_t stencil_compare;
    uint32_t stencil_reference;
    uint32_t stencil_read_mask;
    uint32_t stencil_write_mask;
    uint32_t stencil_fail_operation;
    uint32_t stencil_depth_fail_operation;
    uint32_t stencil_pass_operation;
    uint32_t separate_stencil_enabled;
    uint32_t back_stencil_compare;
    uint32_t back_stencil_reference;
    uint32_t back_stencil_read_mask;
    uint32_t back_stencil_write_mask;
    uint32_t back_stencil_fail_operation;
    uint32_t back_stencil_depth_fail_operation;
    uint32_t back_stencil_pass_operation;
    uint32_t primitive_topology;
    uint32_t vertex_stride;
    uint32_t vertex_binding_count;
    uint32_t attribute_count;
    uint32_t varying_count;
    uint32_t blend_enabled;
    uint32_t blend_source_rgb;
    uint32_t blend_destination_rgb;
    uint32_t blend_equation_rgb;
    uint32_t blend_source_alpha;
    uint32_t blend_destination_alpha;
    uint32_t blend_equation_alpha;
    float blend_constant_red;
    float blend_constant_green;
    float blend_constant_blue;
    float blend_constant_alpha;
    uint32_t color_write_mask;
    uint32_t cull_mode;
    uint32_t front_face;
    RinGLResolvedVertexAttribute
        attributes[RINGL_MAX_VERTEX_INPUT_COMPONENTS];
    RinGLResolvedVertexBinding bindings[RINGL_MAX_VERTEX_ATTRIBS];
    RinGLRinGpuVaryingV1 varyings[RINGL_PIPELINE_MAX_SCALAR_VARYINGS];
} RinGLPipelineKey;

int ringl_build_pipeline_key(RinGLContext* context,
                             uint32_t color_format, uint32_t depth_format,
                             RinGLPipelineKey* key);
uint64_t ringl_pipeline_key_hash(const RinGLPipelineKey* key);
int ringl_pipeline_key_equal(const RinGLPipelineKey* left,
                             const RinGLPipelineKey* right);
int ringl_pipeline_cache_get_or_create(RinGLContext* context,
                                       const RinGLPipelineKey* key,
                                       uint64_t* pipeline_out);
int ringl_get_or_create_graphics_pipeline(RinGLContext* context,
                                          uint32_t color_format,
                                          uint32_t depth_format,
                                          uint64_t* pipeline_out);

#endif /* RINGL_PIPELINE_CACHE_H */
