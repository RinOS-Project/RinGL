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
        "void main() { gl_FragColor = 0.5; }", -1);
    ringl_compile_shader(fragment);
    backend.reject_create = 1u;
    ringl_link_program(program);
    assert(ringl_get_program_link_status(program) == RINGL_FALSE);

    ringl_context_destroy(context);
    return 0;
}
