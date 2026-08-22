/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <ringl/ringl.h>

#define RSH1_MAGIC UINT32_C(0x31485352)

typedef struct __attribute__((packed)) Header {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t total_size;
    uint32_t stage;
    uint32_t flags;
    uint32_t instruction_count;
    uint32_t register_count;
    uint32_t input_count;
    uint32_t output_count;
    uint32_t resource_count;
    uint32_t workgroup_x;
    uint32_t workgroup_y;
    uint32_t workgroup_z;
    uint32_t entry_instruction;
    uint32_t reserved0;
    uint32_t reserved1;
} Header;

typedef struct FakeBackend {
    uint64_t next_handle;
    uint32_t shader_creates;
    uint32_t destroys;
} FakeBackend;

static int fake_create_buffer(void* session, uint64_t size_bytes,
                              uint64_t* buffer_out)
{
    FakeBackend* backend = session;
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
    FakeBackend* backend = session;
    assert(object != 0u);
    backend->destroys++;
    return 0;
}

static int fake_create_shader_module(void* session, const void* rsh1,
                                     uint64_t size_bytes,
                                     uint64_t* shader_module_out)
{
    FakeBackend* backend = session;
    Header header;

    assert(rsh1 != NULL);
    assert(size_bytes >= sizeof(header));
    memcpy(&header, rsh1, sizeof(header));
    if (header.magic != RSH1_MAGIC || header.total_size != size_bytes)
        return -1;
    backend->shader_creates++;
    *shader_module_out = ++backend->next_handle;
    return 0;
}

static Header lower_and_read_header(uint32_t shader, const char* source,
                                    uint8_t* blob, uint32_t capacity)
{
    Header header;
    uint32_t size;

    ringl_shader_source(shader, source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size >= sizeof(Header));
    assert(size <= capacity);
    assert(ringl_copy_shader_rsh1(shader, blob, capacity) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.magic == RSH1_MAGIC);
    assert(header.version == 1u);
    assert(header.total_size == size);
    return header;
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
        .create_shader_module = fake_create_shader_module,
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
    uint32_t vertex;
    uint32_t fragment;
    uint8_t blob[4096];
    Header header;
    uint64_t first_module;
    char log[192];
    const char* scalar_source =
        "attribute float x;\n"
        "void main() {\n"
        "  float y = x * 2.0;\n"
        "  gl_Position = y + 1.0;\n"
        "}\n";
    const char* vector_source =
        "attribute vec2 position;\n"
        "void main() {\n"
        "  gl_Position = vec4(position, 0.0, 1.0);\n"
        "}\n";
    const char* fragment_source =
        "void main() {\n"
        "  gl_FragColor = vec4(1.0, 0.25, 0.0, 1.0);\n"
        "}\n";
    const char* texture_source =
        "uniform sampler2D colorTexture;\n"
        "void main() {\n"
        "  gl_FragColor = texture2D(colorTexture, vec2(0.25, 0.75));\n"
        "}\n";

    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_make_current(context) == 0);

    vertex = ringl_create_shader(RINGL_VERTEX_SHADER);
    fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);
    assert(vertex != 0u && fragment != 0u);

    header = lower_and_read_header(vertex, scalar_source, blob, sizeof(blob));
    assert(header.stage == 1u);
    assert(header.input_count == 1u);
    assert(header.output_count == 1u);
    assert(header.instruction_count >= 5u);
    assert(header.register_count >= 4u);

    assert(ringl_realize_shader_module(vertex) == 0);
    first_module = ringl_get_shader_module(vertex);
    assert(first_module != 0u);
    assert(backend.shader_creates == 1u);

    assert(ringl_realize_shader_module(vertex) == 0);
    assert(ringl_get_shader_module(vertex) != 0u);
    assert(ringl_get_shader_module(vertex) != first_module);
    assert(backend.shader_creates == 2u);
    assert(backend.destroys == 1u);

    header = lower_and_read_header(vertex, vector_source, blob, sizeof(blob));
    assert(header.stage == 1u);
    assert(header.input_count == 2u);
    assert(header.output_count == 4u);
    assert(header.instruction_count >= 7u);
    assert(ringl_get_shader_module(vertex) == 0u);
    assert(backend.destroys == 2u);

    header = lower_and_read_header(fragment, fragment_source, blob, sizeof(blob));
    assert(header.stage == 2u);
    assert(header.input_count == 0u);
    assert(header.output_count == 4u);
    assert(header.instruction_count >= 5u);

    ringl_shader_source(fragment, texture_source, -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(fragment) != 0);
    assert(ringl_get_shader_rsh1_size(fragment) == 0u);
    assert(ringl_get_shader_info_log(fragment, log, sizeof(log)) > 0u);
    assert(strstr(log, "texture2D") != NULL);
    assert(strstr(log, "vec4") != NULL);

    ringl_shader_source(vertex, "void main() { gl_Position = 0.0; }", -1);
    assert(ringl_get_shader_compile_status(vertex) == RINGL_FALSE);
    assert(ringl_get_shader_rsh1_size(vertex) == 0u);
    assert(ringl_get_shader_module(vertex) == 0u);

    ringl_context_destroy(context);
    return 0;
}
