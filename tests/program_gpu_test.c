/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <ringl/ringl.h>

typedef struct __attribute__((packed)) Rsh1Header {
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
} Rsh1Header;

typedef struct __attribute__((packed)) Rsh1Instruction {
    uint16_t opcode;
    uint16_t flags;
    uint16_t destination;
    uint16_t source0;
    uint16_t source1;
    uint16_t resource;
    uint32_t immediate;
} Rsh1Instruction;

typedef struct FakeBackend {
    uint64_t next_handle;
    uint32_t shader_creates;
    uint32_t texture_fragment_modules;
    uint32_t tinted_texture_fragment_modules;
    uint32_t transformed_texture_vertex_modules;
    float expected_transform[16];
    float expected_tint[4];
    uint32_t validate_expected_transform;
    uint32_t validate_expected_tint;
    uint32_t reject_create;
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
    (void)session; (void)buffer; (void)offset; (void)data; (void)size_bytes;
    return 0;
}

static int fake_destroy_object(void* session, uint64_t object)
{
    (void)session; (void)object;
    return 0;
}

static int fake_create_shader_module(void* session, const void* rsh1,
                                     uint64_t size_bytes,
                                     uint64_t* shader_module_out)
{
    FakeBackend* backend = session;
    Rsh1Header header;

    assert(rsh1 != NULL && size_bytes >= sizeof(header));
    memcpy(&header, rsh1, sizeof(header));
    assert(header.magic == UINT32_C(0x31485352) && header.total_size == size_bytes);
    if (header.stage == 2u && header.resource_count == 2u)
        backend->texture_fragment_modules++;
    if (header.stage == 2u && header.resource_count == 2u &&
        header.instruction_count == 21u) {
        const Rsh1Instruction* instructions =
            (const Rsh1Instruction*)((const uint8_t*)rsh1 + sizeof(header));

        assert(size_bytes == sizeof(header) +
                                 header.instruction_count * sizeof(*instructions));
        if (backend->validate_expected_tint && !backend->reject_create) {
            uint32_t index;

            for (index = 0u; index < 4u; ++index) {
                uint32_t expected_bits;

                memcpy(&expected_bits, &backend->expected_tint[index],
                       sizeof(expected_bits));
                assert(instructions[8u + index].opcode == 16u &&
                       instructions[8u + index].immediate == expected_bits);
            }
        }
        backend->tinted_texture_fragment_modules++;
    }
    if (header.stage == 1u && header.input_count == 6u &&
        header.output_count == 8u && header.resource_count == 0u &&
        header.instruction_count == 61u) {
        const Rsh1Instruction* instructions =
            (const Rsh1Instruction*)((const uint8_t*)rsh1 + sizeof(header));
        uint32_t row;

        assert(size_bytes == sizeof(header) +
                                 header.instruction_count * sizeof(*instructions));
        /* Every row must still read the original xyzw attribute registers.
         * In particular, a prior row's result cannot become the x input for
         * a later row when the transform is non-diagonal. */
        for (row = 0u; row < 4u; ++row) {
            uint32_t column;

            for (column = 0u; column < 4u; ++column) {
                const Rsh1Instruction* multiply =
                    &instructions[22u + row * 7u + column];

                assert(multiply->opcode == 22u && multiply->source1 == column);
            }
        }
        if (backend->validate_expected_transform && !backend->reject_create) {
            uint32_t index;

            for (index = 0u; index < 16u; ++index) {
                uint32_t expected_bits;

                memcpy(&expected_bits, &backend->expected_transform[index],
                       sizeof(expected_bits));
                assert(instructions[6u + index].opcode == 16u &&
                       instructions[6u + index].immediate == expected_bits);
            }
        }
        backend->transformed_texture_vertex_modules++;
    }
    backend->shader_creates++;
    if (backend->reject_create)
        return -1;
    *shader_module_out = ++backend->next_handle;
    return 0;
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
    uint32_t program;
    uint32_t matrix_vertex;
    uint32_t matrix_fragment;
    uint32_t matrix_program;
    char log[192];

    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_make_current(context) == 0);
    vertex = ringl_create_shader(RINGL_VERTEX_SHADER);
    fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);
    program = ringl_create_program();
    ringl_shader_source(vertex,
        "attribute float p; void main() { gl_Position = p; }", -1);
    ringl_shader_source(fragment,
        "void main() { gl_FragColor = 1.0; }", -1);
    ringl_compile_shader(vertex);
    ringl_compile_shader(fragment);
    ringl_attach_shader(program, vertex);
    ringl_attach_shader(program, fragment);

    ringl_link_program(program);
    assert(ringl_get_program_link_status(program) == RINGL_TRUE);
    assert(backend.shader_creates == 2u);
    assert(ringl_get_shader_module(vertex) != 0u);
    assert(ringl_get_shader_module(fragment) != 0u);

    ringl_shader_source(fragment,
        "uniform sampler2D tex; "
        "void main() { gl_FragColor = texture2D(tex, vec2(0.5, 0.5)); }",
        -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_TRUE);
    ringl_link_program(program);
    assert(ringl_get_program_link_status(program) == RINGL_TRUE);
    assert(ringl_get_program_info_log(program, log, sizeof(log)) == 0u);
    assert(ringl_get_shader_module(fragment) != 0u);
    assert(backend.shader_creates == 3u);

    /* A matrix uniform must create program-owned RinGPU modules at link and
     * after an update. This is the real transformed-texture WebGL route: the
     * vertex module transforms a vec4 position and forwards a UV varying into
     * the independently lowered texture fragment module. A rejected
     * replacement must preserve the former matrix state rather than
     * publishing a partly updated executable. */
    matrix_vertex = ringl_create_shader(RINGL_VERTEX_SHADER);
    matrix_fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);
    matrix_program = ringl_create_program();
    assert(matrix_vertex != 0u && matrix_fragment != 0u && matrix_program != 0u);
    ringl_shader_source(matrix_vertex,
        "attribute vec4 position; attribute vec2 texCoord; "
        "uniform mat4 transform; varying vec2 uv; "
        "void main() { gl_Position = transform * position; uv = texCoord; }",
        -1);
    ringl_shader_source(matrix_fragment,
        "precision mediump float; uniform sampler2D texture; uniform vec4 tint; "
        "varying vec2 uv; void main() { gl_FragColor = texture2D(texture, uv) "
        "* tint; }",
        -1);
    ringl_compile_shader(matrix_vertex);
    ringl_compile_shader(matrix_fragment);
    assert(ringl_get_shader_compile_status(matrix_vertex) == RINGL_TRUE);
    assert(ringl_get_shader_compile_status(matrix_fragment) == RINGL_TRUE);
    ringl_attach_shader(matrix_program, matrix_vertex);
    ringl_attach_shader(matrix_program, matrix_fragment);
    ringl_link_program(matrix_program);
    assert(ringl_get_program_link_status(matrix_program) == RINGL_TRUE);
    assert(backend.shader_creates == 5u);
    assert(backend.texture_fragment_modules == 2u);
    assert(backend.tinted_texture_fragment_modules == 1u);
    assert(backend.transformed_texture_vertex_modules == 1u);
    {
        float transform[16] = {
            2.0f, 3.0f, 0.0f, 0.0f,
            1.0f, 4.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            5.0f, 6.0f, 0.0f, 1.0f,
        };
        float values[16];
        int32_t tint_location = ringl_get_uniform_location(matrix_program, "tint");
        int32_t location = ringl_get_uniform_location(matrix_program, "transform");

        /* `texture` occupies sampler location zero. The vertex mat4 remains
         * independently mutable while the fragment uses its ordinary
         * texture-lowering module. */
        assert(tint_location == 1);
        assert(location == 2);
        ringl_use_program(matrix_program);
        memcpy(backend.expected_transform, transform, sizeof(transform));
        backend.validate_expected_transform = 1u;
        ringl_uniform_matrix4fv(location, 0u, transform);
        assert(ringl_get_error() == RINGL_NO_ERROR);
        assert(backend.shader_creates == 7u);
        assert(backend.texture_fragment_modules == 3u);
        assert(backend.tinted_texture_fragment_modules == 2u);
        assert(backend.transformed_texture_vertex_modules == 2u);
        backend.expected_tint[0] = 0.5f;
        backend.expected_tint[1] = 1.0f;
        backend.expected_tint[2] = 0.25f;
        backend.expected_tint[3] = 1.0f;
        backend.validate_expected_tint = 1u;
        ringl_uniform_4f(tint_location, backend.expected_tint[0],
                          backend.expected_tint[1],
                          backend.expected_tint[2],
                          backend.expected_tint[3]);
        assert(ringl_get_error() == RINGL_NO_ERROR);
        assert(backend.shader_creates == 9u);
        assert(backend.texture_fragment_modules == 4u);
        assert(backend.tinted_texture_fragment_modules == 3u);
        assert(backend.transformed_texture_vertex_modules == 3u);
        backend.reject_create = 1u;
        transform[0] = 7.0f;
        ringl_uniform_matrix4fv(location, 0u, transform);
        assert(ringl_get_error() == RINGL_INVALID_OPERATION);
        assert(backend.shader_creates == 10u);
        assert(ringl_get_uniform_matrix4f(matrix_program, location, values) == 0);
        assert(values[0] == 2.0f);
        backend.reject_create = 0u;
    }
    ringl_delete_program(matrix_program);

    ringl_shader_source(fragment,
        "void main() { gl_FragColor = 0.5; }", -1);
    ringl_compile_shader(fragment);
    backend.reject_create = 1u;
    ringl_link_program(program);
    assert(ringl_get_program_link_status(program) == RINGL_FALSE);
    assert(backend.shader_creates == 11u);

    ringl_context_destroy(context);
    return 0;
}
