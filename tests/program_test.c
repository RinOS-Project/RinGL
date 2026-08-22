/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <string.h>
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
    uint32_t multi_vertex;
    uint32_t multi_fragment;
    uint32_t multi_program;
    uint32_t retained_vertex;
    uint32_t retained_fragment;
    uint32_t retained_program;
    int32_t location;
    int32_t uniform_value = -1;
    char log[160];
    RinGLActiveInfoV1 active_info = {
        .struct_size = sizeof(active_info),
        .api_version = RINGL_API_VERSION,
    };
    RinGLProgramInfoV1 info = {
        .struct_size = sizeof(info),
        .api_version = RINGL_API_VERSION,
    };

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
                        "uniform sampler2D colorTexture; "
                        "void main() { gl_FragColor = texture2D(colorTexture, vec2(0.5, 0.5)); }",
                        -1);
    ringl_compile_shader(vertex);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(vertex) == RINGL_TRUE);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_TRUE);

    ringl_bind_attrib_location(program, 3u, "position");
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_bind_attrib_location(program, RINGL_MAX_VERTEX_ATTRIBS, "position");
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    ringl_bind_attrib_location(program, 0u, "gl_reserved");
    assert(ringl_get_error() == RINGL_INVALID_OPERATION);
    ringl_bind_attrib_location(program, 0u, NULL);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);

    /* A validation-only context may link the GL program and expose uniforms
     * even though the current shared RSH1 ABI cannot lower texture2D yet. */
    ringl_attach_shader(program, vertex);
    ringl_attach_shader(program, fragment);
    assert(ringl_get_program_info(program, &info) == 0);
    assert(info.link_status == RINGL_FALSE && info.validate_status == RINGL_FALSE &&
           info.attached_shader_count == 2u && info.active_attribute_count == 0u &&
           info.active_uniform_count == 0u);
    ringl_link_program(program);
    assert(ringl_get_program_link_status(program) == RINGL_TRUE);
    assert(ringl_get_program_info(program, &info) == 0);
    assert(info.link_status == RINGL_TRUE && info.validate_status == RINGL_FALSE &&
           info.attached_shader_count == 2u && info.active_attribute_count == 1u &&
           info.active_uniform_count == 1u);
    ringl_validate_program(program);
    assert(ringl_get_program_info(program, &info) == 0);
    assert(info.validate_status == RINGL_TRUE);
    assert(ringl_get_program_info_log(program, log, sizeof(log)) == 0u);
    assert(ringl_get_attrib_location(program, "position") == 3);
    assert(ringl_get_attrib_location(program, "missing") == -1);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_get_active_attrib(program, 0u, &active_info) == 0);
    assert(active_info.type == RINGL_FLOAT && active_info.size == 1u &&
           active_info.name_length == 8u &&
           strcmp(active_info.name, "position") == 0);
    assert(ringl_get_active_uniform(program, 0u, &active_info) == 0);
    assert(active_info.type == RINGL_SAMPLER_2D && active_info.size == 1u &&
           active_info.name_length == 12u &&
           strcmp(active_info.name, "colorTexture") == 0);
    active_info.type = 0xffffffffu;
    assert(ringl_get_active_attrib(program, 1u, &active_info) == -1);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    assert(active_info.type == 0xffffffffu);
    active_info.struct_size = 0u;
    assert(ringl_get_active_uniform(program, 0u, &active_info) == -1);
    assert(active_info.type == 0xffffffffu);
    active_info.struct_size = sizeof(active_info);

    /* A request after link does not mutate the currently linked executable. */
    ringl_bind_attrib_location(program, 1u, "position");
    assert(ringl_get_attrib_location(program, "position") == 3);
    ringl_link_program(program);
    assert(ringl_get_program_link_status(program) == RINGL_TRUE);
    assert(ringl_get_program_info(program, &info) == 0);
    assert(info.validate_status == RINGL_FALSE);
    assert(ringl_get_attrib_location(program, "position") == 1);

    location = ringl_get_uniform_location(program, "colorTexture");
    assert(location == 0);
    assert(ringl_get_uniform_location(program, "missing") == -1);
    assert(ringl_get_error() == RINGL_NO_ERROR);

    ringl_use_program(program);
    assert(ringl_get_current_program() == program);
    ringl_uniform_1i(location, 3);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_get_uniform_1i(program, location, &uniform_value) == 0);
    assert(uniform_value == 3);
    ringl_uniform_1i(-1, 7);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_uniform_1i(99, 0);
    assert(ringl_get_error() == RINGL_INVALID_OPERATION);
    uniform_value = -1;
    assert(ringl_get_uniform_1i(program, 99, &uniform_value) == -1);
    assert(ringl_get_error() == RINGL_INVALID_OPERATION);
    assert(uniform_value == -1);

    multi_vertex = ringl_create_shader(RINGL_VERTEX_SHADER);
    multi_fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);
    multi_program = ringl_create_program();
    assert(multi_vertex != 0u && multi_fragment != 0u && multi_program != 0u);
    ringl_shader_source(
        multi_vertex,
        "attribute vec2 position; attribute vec2 colorRG; attribute vec2 colorBA; "
        "varying vec2 vertexRG; varying vec2 vertexBA; "
        "void main() { gl_Position = vec4(position, 0.0, 1.0); "
        "vertexRG = colorRG; vertexBA = colorBA; }", -1);
    ringl_shader_source(
        multi_fragment,
        "varying vec2 vertexRG; varying vec2 vertexBA; "
        "void main() { gl_FragColor = vec4(vertexRG, vertexBA); }", -1);
    ringl_compile_shader(multi_vertex);
    ringl_compile_shader(multi_fragment);
    assert(ringl_get_shader_compile_status(multi_vertex) == RINGL_TRUE);
    assert(ringl_get_shader_compile_status(multi_fragment) == RINGL_TRUE);
    ringl_attach_shader(multi_program, multi_vertex);
    ringl_attach_shader(multi_program, multi_fragment);
    ringl_link_program(multi_program);
    assert(ringl_get_program_link_status(multi_program) == RINGL_TRUE);
    assert(ringl_get_attrib_location(multi_program, "position") == 0);
    assert(ringl_get_attrib_location(multi_program, "colorRG") == 1);
    assert(ringl_get_attrib_location(multi_program, "colorBA") == 2);

    /* Two active names cannot occupy one generic array index. The failed link
     * must be observable, and a repaired binding must produce a fresh,
     * deterministic executable. */
    ringl_bind_attrib_location(multi_program, 4u, "position");
    ringl_bind_attrib_location(multi_program, 4u, "colorBA");
    ringl_link_program(multi_program);
    assert(ringl_get_program_link_status(multi_program) == RINGL_FALSE);
    assert(ringl_get_program_info(multi_program, &info) == 0);
    assert(info.link_status == RINGL_FALSE && info.validate_status == RINGL_FALSE &&
           info.attached_shader_count == 2u && info.active_attribute_count == 0u &&
           info.active_uniform_count == 0u);
    assert(ringl_get_program_info_log(multi_program, log, sizeof(log)) > 0u);
    ringl_bind_attrib_location(multi_program, 5u, "colorBA");
    ringl_link_program(multi_program);
    assert(ringl_get_program_link_status(multi_program) == RINGL_TRUE);
    assert(ringl_get_attrib_location(multi_program, "position") == 4);
    assert(ringl_get_attrib_location(multi_program, "colorRG") == 0);
    assert(ringl_get_attrib_location(multi_program, "colorBA") == 5);
    ringl_delete_program(multi_program);

    ringl_delete_program(program);
    assert(ringl_get_current_program() == 0u);
    assert(!ringl_is_program(program));
    info.link_status = 0xffffffffu;
    assert(ringl_get_program_info(program, &info) == -1);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    assert(info.link_status == 0xffffffffu);

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

    retained_vertex = ringl_create_shader(RINGL_VERTEX_SHADER);
    retained_fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);
    retained_program = ringl_create_program();
    assert(retained_vertex != 0u && retained_fragment != 0u &&
           retained_program != 0u);
    ringl_shader_source(retained_vertex,
                        "attribute float position; void main() { gl_Position = position; }",
                        -1);
    ringl_shader_source(retained_fragment,
                        "void main() { gl_FragColor = 1.0; }", -1);
    ringl_compile_shader(retained_vertex);
    ringl_compile_shader(retained_fragment);
    ringl_attach_shader(retained_program, retained_vertex);
    ringl_attach_shader(retained_program, retained_fragment);
    ringl_link_program(retained_program);
    assert(ringl_get_program_link_status(retained_program) == RINGL_TRUE);

    /* WebGL commonly deletes a shader immediately after attaching it. The
     * program must retain it through a later link, then release it at detach. */
    ringl_delete_shader(retained_vertex);
    assert(!ringl_is_shader(retained_vertex));
    ringl_compile_shader(retained_vertex);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    ringl_link_program(retained_program);
    assert(ringl_get_program_link_status(retained_program) == RINGL_TRUE);
    ringl_attach_shader(retained_program, retained_vertex);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    ringl_detach_shader(retained_program, retained_vertex);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_get_program_link_status(retained_program) == RINGL_TRUE);
    ringl_use_program(retained_program);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_detach_shader(retained_program, retained_vertex);
    assert(ringl_get_error() == RINGL_INVALID_OPERATION);

    ringl_delete_shader(retained_fragment);
    ringl_delete_program(retained_program);

    ringl_context_destroy(context);
    return 0;
}
