/* SPDX-License-Identifier: MIT */
#ifndef RINGL_PIPELINE_CACHE_H
#define RINGL_PIPELINE_CACHE_H

#include <stdint.h>

#include "../ringl_internal.h"

#define RINGL_NATIVE_PRIMITIVE_TRIANGLE_LIST 1u

typedef struct RinGLPipelineKey {
    uint64_t vertex_shader_module;
    uint64_t fragment_shader_module;
    uint32_t color_format;
    uint32_t primitive_topology;
    uint32_t vertex_stride;
    uint32_t attribute_count;
    RinGLResolvedVertexAttribute attributes[RINGL_MAX_VERTEX_ATTRIBS];
} RinGLPipelineKey;

int ringl_build_pipeline_key(RinGLContext* context,
                             uint32_t color_format,
                             RinGLPipelineKey* key);
uint64_t ringl_pipeline_key_hash(const RinGLPipelineKey* key);
int ringl_pipeline_key_equal(const RinGLPipelineKey* left,
                             const RinGLPipelineKey* right);

#endif /* RINGL_PIPELINE_CACHE_H */
