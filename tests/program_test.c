/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <ringl/ringl.h>

int main(void)
{
    RinGLContext* context = NULL;
    RinGLContextDescV1 desc = {
        .struct_size = sizeof(desc),
        .api_version = RINGL_API_VERSION,
    };
    uint32_t vertex;
    uint32_t fragment;
    uint32_t program;
    char log[160];

    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_make_current(context) == 0);

    vertex = ringl_create_shader(RINGL_VERTEX_SHADER);
    fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);
    program = ringl_create_program();
    assert(vertex != 0u && fragment != 0u && program != 0u);

    ringl_shader_source(vertex,
                        "attribute float position; void main() { gl_Position = position; }",
                        -1);
    ringl_shader_source(fragment,
                        "void main() { gl_FragColor = 1.0; }",
                        -1);
    ringl_compile_shader(vertex);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(vertex) == RINGL_TRUE);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_TRUE);

    ringl_attach_shader(program, vertex);
    ringl_attach_shader(program, fragment);
    ringl_link_program(program);
    assert(ringl_get_program_link_status(program) == RINGL_TRUE);
    assert(ringl_get_program_info_log(program, log, sizeof(log)) == 0u);

    ringl_use_program(program);
    assert(ringl_get_current_program() == program);

    ringl_delete_program(program);
    assert(ringl_get_current_program() == 0u);
    assert(!ringl_is_program(program));

    program = ringl_create_program();
    fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);
    ringl_shader_source(fragment, "void main() { gl_FragColor = missing; }", -1);
    ringl_compile_shader(fragment);
    ringl_attach_shader(program, vertex);
    ringl_attach_shader(program, fragment);
    ringl_link_program(program);
    assert(ringl_get_program_link_status(program) == RINGL_FALSE);
    assert(ringl_get_program_info_log(program, log, sizeof(log)) > 0u);
    ringl_use_program(program);
    assert(ringl_get_error() == RINGL_INVALID_OPERATION);

    ringl_context_destroy(context);
    return 0;
}
