/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <string.h>

#include "translate/pipeline_cache.h"

typedef struct FakeBackend {
    uint64_t next_handle;
    uint32_t pipeline_creates;
    uint32_t destroys;
} FakeBackend;

static int fake_create_buffer(void* session, uint64_t size_bytes,
                              uint64_t* buffer_out)
{
    FakeBackend* backend = (FakeBackend*)session;
    (void)size_bytes;
    *buffer_out = ++backend->next_handle;
    return 0;
}

static int fake_upload_buffer(void* session, uint64_t buffer, uint64_t offset,
                              const void* data, uint64_t size_bytes)
{
    (void)session;
    (void)buffer;
    (void)offset;
    (void)data;
    (void)size_bytes;
    return 0;
}

static int fake_destroy_object(void* session, uint64_t object)
{
    FakeBackend* backend = (FakeBackend*)session;
    assert(object != 0u);
    ++backend->destroys;
    return 0;
}

static int fake_create_graphics_pipeline(
    void* session, const RinGLRinGpuGraphicsPipelineV1* desc,
    const RinGLRinGpuVertexAttributeV1* attributes,
    uint32_t attribute_count, uint64_t* pipeline_out)
{
    FakeBackend* backend = (FakeBackend*)session;
    assert(desc != NULL);
    assert(pipeline_out != NULL);
    assert(desc->vertex_shader != 0u);
    assert(desc->fragment_shader != 0u);
    assert(desc->attribute_count == attribute_count);
    if (attribute_count != 0u)
        assert(attributes != NULL);
    ++backend->pipeline_creates;
    *pipeline_out = ++backend->next_handle;
    return 0;
}

static RinGLPipelineKey make_key(uint32_t color_format, uint32_t offset)
{
    RinGLPipelineKey key;
    memset(&key, 0, sizeof(key));
    key.vertex_shader_module = 11u;
    key.fragment_shader_module = 12u;
    key.color_format = color_format;
    key.primitive_topology = RINGL_NATIVE_PRIMITIVE_TRIANGLE_LIST;
    key.vertex_stride = 8u;
    key.attribute_count = 2u;
    key.attributes[0].location = 0u;
    key.attributes[0].format = RINGL_NATIVE_VERTEX_FLOAT32;
    key.attributes[0].offset = 0u;
    key.attributes[1].location = 1u;
    key.attributes[1].format = RINGL_NATIVE_VERTEX_FLOAT32;
    key.attributes[1].offset = offset;
    return key;
}

int main(void)
{
    FakeBackend backend = {0};
    RinGLRinGpuOpsV1 ops = {
        .struct_size = sizeof(ops),
        .api_version = RINGL_API_VERSION,
        .create_buffer = fake_create_buffer,
        .upload_buffer = fake_upload_buffer,
        .destroy_object = fake_destroy_object,
        .create_graphics_pipeline = fake_create_graphics_pipeline,
    };
    RinGLRinGpuBindingV1 binding = {
        .struct_size = sizeof(binding),
        .api_version = RINGL_API_VERSION,
        .session = &backend,
        .ops = &ops,
    };
    RinGLContextDescV1 desc = {
        .struct_size = sizeof(desc),
        .api_version = RINGL_API_VERSION,
        .ringpu = &binding,
    };
    RinGLContext* context = NULL;
    RinGLPipelineKey a = make_key(2u, 4u);
    RinGLPipelineKey b = a;
    uint64_t first;
    uint64_t second;
    uint64_t hash_a;
    uint32_t index;

    hash_a = ringl_pipeline_key_hash(&a);
    assert(hash_a != 0u);
    assert(ringl_pipeline_key_equal(&a, &b));
    assert(ringl_pipeline_key_hash(&b) == hash_a);
    b.color_format = 3u;
    assert(!ringl_pipeline_key_equal(&a, &b));
    b = a;
    b.attributes[1].offset = 8u;
    assert(!ringl_pipeline_key_equal(&a, &b));
    b = a;
    b.fragment_shader_module++;
    assert(!ringl_pipeline_key_equal(&a, &b));

    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_pipeline_cache_get_or_create(context, &a, &first) == 0);
    assert(first != 0u);
    assert(backend.pipeline_creates == 1u);
    assert(ringl_pipeline_cache_get_or_create(context, &a, &second) == 0);
    assert(second == first);
    assert(backend.pipeline_creates == 1u);

    b = make_key(3u, 4u);
    assert(ringl_pipeline_cache_get_or_create(context, &b, &second) == 0);
    assert(second != first);
    assert(backend.pipeline_creates == 2u);

    for (index = 0u; index < RINGL_PIPELINE_CACHE_CAPACITY; ++index) {
        RinGLPipelineKey key = make_key(100u + index, 4u);
        assert(ringl_pipeline_cache_get_or_create(context, &key, &second) == 0);
    }
    assert(backend.pipeline_creates == 2u + RINGL_PIPELINE_CACHE_CAPACITY);
    assert(backend.destroys >= 2u);

    ringl_context_destroy(context);
    assert(backend.destroys == backend.pipeline_creates);
    return 0;
}
