/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <string.h>

#include "translate/pipeline_cache.h"

int main(void)
{
    RinGLPipelineKey a;
    RinGLPipelineKey b;
    uint64_t hash_a;

    memset(&a, 0, sizeof(a));
    a.vertex_shader_module = 11u;
    a.fragment_shader_module = 12u;
    a.color_format = 2u;
    a.primitive_topology = RINGL_NATIVE_PRIMITIVE_TRIANGLE_LIST;
    a.vertex_stride = 8u;
    a.attribute_count = 2u;
    a.attributes[0].location = 0u;
    a.attributes[0].format = RINGL_NATIVE_VERTEX_FLOAT32;
    a.attributes[0].offset = 0u;
    a.attributes[1].location = 1u;
    a.attributes[1].format = RINGL_NATIVE_VERTEX_FLOAT32;
    a.attributes[1].offset = 4u;

    b = a;
    hash_a = ringl_pipeline_key_hash(&a);
    assert(hash_a != 0u);
    assert(ringl_pipeline_key_equal(&a, &b));
    assert(ringl_pipeline_key_hash(&b) == hash_a);

    b.color_format = 3u;
    assert(!ringl_pipeline_key_equal(&a, &b));
    assert(ringl_pipeline_key_hash(&b) != hash_a);

    b = a;
    b.attributes[1].offset = 8u;
    assert(!ringl_pipeline_key_equal(&a, &b));

    b = a;
    b.fragment_shader_module++;
    assert(!ringl_pipeline_key_equal(&a, &b));

    return 0;
}
