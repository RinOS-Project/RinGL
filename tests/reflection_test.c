/* SPDX-License-Identifier: MIT */
#include <assert.h>

#include <ringl/reflection.h>
#include <ringl/ringl.h>

int main(void)
{
    RinGLContextDescV1 desc = {0};
    RinGLContext* context = NULL;
    RinGLProgramReflectionV1 reflection = {0};
    uint32_t vertex;
    uint32_t fragment;
    uint32_t program;

    desc.struct_size = sizeof(desc);
    desc.api_version = RINGL_API_VERSION;
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

    reflection.struct_size = sizeof(reflection);
    reflection.api_version = RINGL_API_VERSION;
    assert(ringl_get_program_reflection(program, &reflection) == 0);
    assert(reflection.vertex_input_count == 1u);
    assert(reflection.vertex_output_count == 1u);
    assert(reflection.fragment_input_count == 0u);
    assert(reflection.fragment_output_count == 1u);
    assert(reflection.active_uniform_count == 0u);
    assert(reflection.vertex_shader_module == 0u);
    assert(reflection.fragment_shader_module == 0u);

    ringl_context_destroy(context);
    return 0;
}
