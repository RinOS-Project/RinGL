/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>

#include <ringl/ringl.h>

typedef struct FakeBackend {
    uint64_t next_handle;
    uint32_t shader_creates;
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
    (void)rsh1;
    assert(size_bytes > 0u);
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
     * after an update. A rejected replacement must preserve the former
     * matrix state rather than publishing a partly updated executable. */
    matrix_vertex = ringl_create_shader(RINGL_VERTEX_SHADER);
    matrix_fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);
    matrix_program = ringl_create_program();
    assert(matrix_vertex != 0u && matrix_fragment != 0u && matrix_program != 0u);
    ringl_shader_source(matrix_vertex,
        "attribute vec4 position; uniform mat4 transform; "
        "void main() { gl_Position = transform * position; }", -1);
    ringl_shader_source(matrix_fragment,
        "void main() { gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0); }", -1);
    ringl_compile_shader(matrix_vertex);
    ringl_compile_shader(matrix_fragment);
    assert(ringl_get_shader_compile_status(matrix_vertex) == RINGL_TRUE);
    assert(ringl_get_shader_compile_status(matrix_fragment) == RINGL_TRUE);
    ringl_attach_shader(matrix_program, matrix_vertex);
    ringl_attach_shader(matrix_program, matrix_fragment);
    ringl_link_program(matrix_program);
    assert(ringl_get_program_link_status(matrix_program) == RINGL_TRUE);
    assert(backend.shader_creates == 5u);
    {
        float identity[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        };
        float values[16];
        int32_t location = ringl_get_uniform_location(matrix_program, "transform");

        assert(location == 0);
        ringl_use_program(matrix_program);
        ringl_uniform_matrix4fv(location, 0u, identity);
        assert(ringl_get_error() == RINGL_NO_ERROR);
        assert(backend.shader_creates == 7u);
        backend.reject_create = 1u;
        identity[0] = 2.0f;
        ringl_uniform_matrix4fv(location, 0u, identity);
        assert(ringl_get_error() == RINGL_INVALID_OPERATION);
        assert(backend.shader_creates == 8u);
        assert(ringl_get_uniform_matrix4f(matrix_program, location, values) == 0);
        assert(values[0] == 1.0f);
        backend.reject_create = 0u;
    }
    ringl_delete_program(matrix_program);

    ringl_shader_source(fragment,
        "void main() { gl_FragColor = 0.5; }", -1);
    ringl_compile_shader(fragment);
    backend.reject_create = 1u;
    ringl_link_program(program);
    assert(ringl_get_program_link_status(program) == RINGL_FALSE);
    assert(backend.shader_creates == 9u);

    ringl_context_destroy(context);
    return 0;
}
