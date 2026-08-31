/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <string.h>

#include "translate/pipeline_cache.h"
#include "shader/rsh1_abi.h"

typedef struct FakeBackend {
    uint64_t next_handle;
    uint32_t pipeline_creates;
    uint32_t shader_module_creates;
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

static int fake_create_shader_module(void* session, const void* rsh1,
                                     uint64_t size_bytes,
                                     uint64_t* shader_module_out)
{
    FakeBackend* backend = (FakeBackend*)session;
    assert(rsh1 != NULL);
    assert(size_bytes != 0u);
    assert(shader_module_out != NULL);
    ++backend->shader_module_creates;
    *shader_module_out = ++backend->next_handle;
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
    key.color_write_mask = RINGL_RIN_GPU_COLOR_WRITE_ALL;
    key.front_face = RINGL_CCW;
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
    b = a;
    b.front_face = RINGL_CW;
    assert(!ringl_pipeline_key_equal(&a, &b));
    b = a;
    b.blend_enabled = 1u;
    assert(!ringl_pipeline_key_equal(&a, &b));
    b = a;
    b.dither_enabled = 1u;
    assert(!ringl_pipeline_key_equal(&a, &b));
    b = a;
    b.varying_count = 1u;
    b.varyings[0].vertex_output_location = 4u;
    b.varyings[0].fragment_input_location = 0u;
    b.varyings[0].type = 1u;
    b.varyings[0].interpolation = 1u;
    assert(!ringl_pipeline_key_equal(&a, &b));

    assert(ringl_context_create(&desc, &context) == 0);
    /* Cache metadata is part of the same per-context budget. A full
     * reservation must reject cache creation before the backend sees a
     * pipeline request, then become reusable after release. */
    {
        uint64_t available = RINGL_MAX_CPU_SHADOW_BYTES -
            context->cpu_shadow_bytes;

        assert(ringl_context_reserve_shadow_bytes(context, available) != 0);
        first = 0u;
        assert(ringl_pipeline_cache_get_or_create(context, &a, &first) == -1);
        assert(first == 0u && backend.pipeline_creates == 0u);
        ringl_context_release_shadow_bytes(context, available);
    }
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

    /* LUMINANCE pipeline realization rewrites a fragment RSH1 module into a
     * short-lived CPU copy. Keep a minimal linked program here so the cache
     * miss exercises that exact staging path rather than only the allocator
     * helper. */
    {
        RinGLContext* luminance_context = NULL;
        RinGLPipelineKey luminance_key = make_key(2u, 4u);
        RinGLShaderObject* fragment;
        RinGLProgramObject* program_object;
        RinGLRsh1HeaderV1 header;
        RinGLRsh1InstructionV1* instructions;
        uint8_t* rsh1;
        uint32_t fragment_shader;
        uint32_t program;
        uint64_t pipeline;
        uint64_t held_bytes;
        uint32_t index;

        ops.create_shader_module = fake_create_shader_module;
        assert(ringl_context_create(&desc, &luminance_context) == 0);
        fragment_shader = ringl_object_allocate(luminance_context,
                                                 RINGL_OBJECT_SHADER);
        program = ringl_object_allocate(luminance_context,
                                         RINGL_OBJECT_PROGRAM);
        assert(fragment_shader != 0u && program != 0u);
        ringl_object_promote(ringl_object_lookup(luminance_context,
                                                  fragment_shader,
                                                  RINGL_OBJECT_SHADER));
        ringl_object_promote(ringl_object_lookup(luminance_context, program,
                                                  RINGL_OBJECT_PROGRAM));
        fragment = &luminance_context->shaders[
            ringl_object_slot_index(fragment_shader)];
        program_object = &luminance_context->programs[
            ringl_object_slot_index(program)];
        memset(&header, 0, sizeof(header));
        header.magic = RINGL_RSH1_MAGIC;
        header.version = RINGL_RSH1_VERSION;
        header.header_size = sizeof(header);
        header.instruction_count = 3u;
        header.register_count = 1u;
        header.output_count = 4u;
        header.stage = RINGL_RSH1_STAGE_FRAGMENT;
        header.total_size = sizeof(header) +
            header.instruction_count * sizeof(RinGLRsh1InstructionV1);
        rsh1 = (uint8_t*)ringl_context_alloc_temporary(
            luminance_context, header.total_size);
        assert(rsh1 != NULL);
        memcpy(rsh1, &header, sizeof(header));
        instructions = (RinGLRsh1InstructionV1*)(rsh1 + sizeof(header));
        for (index = 0u; index < header.instruction_count; ++index) {
            memset(&instructions[index], 0, sizeof(instructions[index]));
            instructions[index].opcode = RINGL_RSH1_OP_STORE_OUTPUT_F32;
            instructions[index].source0 = 0u;
            instructions[index].immediate = index;
        }
        fragment->rsh1 = rsh1;
        fragment->rsh1_size = header.total_size;
        fragment->ringpu_module = 12u;
        fragment->shader_type = RINGL_FRAGMENT_SHADER;
        program_object->linked_fragment_shader = fragment_shader;
        program_object->fragment_shader = fragment_shader;
        program_object->link_status = RINGL_TRUE;
        luminance_context->current_program = program;
        luminance_key.logical_color_format = RINGL_LUMINANCE;
        luminance_key.fragment_shader_module = fragment->ringpu_module;

        held_bytes = RINGL_MAX_CPU_SHADOW_BYTES -
            luminance_context->cpu_shadow_bytes;
        assert(ringl_context_reserve_shadow_bytes(luminance_context,
                                                  held_bytes));
        assert(ringl_pipeline_cache_get_or_create(luminance_context,
                                                  &luminance_key,
                                                  &pipeline) != 0);
        assert(pipeline == 0u);
        assert(backend.shader_module_creates == 0u);
        assert(backend.pipeline_creates == 2u + RINGL_PIPELINE_CACHE_CAPACITY);
        ringl_context_release_shadow_bytes(luminance_context, held_bytes);
        assert(ringl_pipeline_cache_get_or_create(luminance_context,
                                                  &luminance_key,
                                                  &pipeline) == 0);
        assert(pipeline != 0u);
        assert(backend.shader_module_creates == 1u);
        ringl_context_destroy(luminance_context);
    }
    return 0;
}
