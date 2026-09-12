/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <math.h>
#include <string.h>
#include <ringl/ringl.h>

typedef struct RinGLArrayTestProgram {
    uint32_t vertex;
    uint32_t fragment;
    uint32_t program;
} RinGLArrayTestProgram;

static RinGLArrayTestProgram ringl_create_array_test_program(
    const char* fragment_source)
{
    RinGLArrayTestProgram result = {
        .vertex = ringl_create_shader(RINGL_VERTEX_SHADER),
        .fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER),
        .program = ringl_create_program(),
    };

    assert(fragment_source != NULL && result.vertex != 0u &&
           result.fragment != 0u && result.program != 0u);
    ringl_shader_source(result.vertex,
                        "void main() { gl_Position = vec4(0.0); }", -1);
    ringl_shader_source(result.fragment, fragment_source, -1);
    ringl_compile_shader(result.vertex);
    ringl_compile_shader(result.fragment);
    assert(ringl_get_shader_compile_status(result.vertex) == RINGL_TRUE);
    assert(ringl_get_shader_compile_status(result.fragment) == RINGL_TRUE);
    ringl_attach_shader(result.program, result.vertex);
    ringl_attach_shader(result.program, result.fragment);
    ringl_link_program(result.program);
    assert(ringl_get_program_link_status(result.program) == RINGL_TRUE);
    return result;
}

static RinGLArrayTestProgram ringl_create_vertex_array_test_program(
    const char* vertex_source)
{
    RinGLArrayTestProgram result = {
        .vertex = ringl_create_shader(RINGL_VERTEX_SHADER),
        .fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER),
        .program = ringl_create_program(),
    };

    assert(vertex_source != NULL && result.vertex != 0u &&
           result.fragment != 0u && result.program != 0u);
    ringl_shader_source(result.vertex, vertex_source, -1);
    ringl_shader_source(result.fragment,
                        "void main() { gl_FragColor = vec4(1.0); }", -1);
    ringl_compile_shader(result.vertex);
    ringl_compile_shader(result.fragment);
    assert(ringl_get_shader_compile_status(result.vertex) == RINGL_TRUE);
    assert(ringl_get_shader_compile_status(result.fragment) == RINGL_TRUE);
    ringl_attach_shader(result.program, result.vertex);
    ringl_attach_shader(result.program, result.fragment);
    ringl_link_program(result.program);
    assert(ringl_get_program_link_status(result.program) == RINGL_TRUE);
    return result;
}

static void ringl_destroy_array_test_program(RinGLArrayTestProgram* program)
{
    assert(program != NULL);
    ringl_delete_program(program->program);
    ringl_delete_shader(program->vertex);
    ringl_delete_shader(program->fragment);
}

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
    uint32_t vec4_vertex;
    uint32_t vec4_fragment;
    uint32_t vec4_program;
    uint32_t vec4_peer_program;
    uint32_t float_vertex;
    uint32_t float_fragment;
    uint32_t float_program;
    uint32_t float_peer_program;
    uint32_t vec2_vertex;
    uint32_t vec2_fragment;
    uint32_t vec2_program;
    uint32_t vec2_peer_program;
    uint32_t vec3_vertex;
    uint32_t vec3_fragment;
    uint32_t vec3_program;
    uint32_t vec3_peer_program;
    uint32_t integer_vertex;
    uint32_t integer_fragment;
    uint32_t integer_program;
    uint32_t integer_peer_program;
    uint32_t bool_vertex;
    uint32_t bool_fragment;
    uint32_t bool_program;
    uint32_t bool_peer_program;
    uint32_t bvec_vertex;
    uint32_t bvec_fragment;
    uint32_t bvec_program;
    uint32_t bvec_peer_program;
    uint32_t mat4_vertex;
    uint32_t mat4_fragment;
    uint32_t mat4_program;
    uint32_t mat4_peer_program;
    uint32_t matrix_vertex;
    uint32_t matrix_fragment;
    uint32_t matrix_program;
    uint32_t attached_shaders[2] = { 0u, 0u };
    uint32_t attached_shader_count = 0u;
    int32_t location;
    int32_t uniform_value = -1;
    int32_t query_value = -1;
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
    assert(ringl_get_attached_shaders(program, NULL, 0u,
                                      &attached_shader_count) == 0);
    assert(attached_shader_count == 0u);
    ringl_attach_shader(program, vertex);
    ringl_attach_shader(program, fragment);
    attached_shaders[0] = 0u;
    attached_shaders[1] = 0u;
    assert(ringl_get_attached_shaders(program, attached_shaders, 2u,
                                      &attached_shader_count) == 0);
    assert(attached_shader_count == 2u && attached_shaders[0] == vertex &&
           attached_shaders[1] == fragment);
    attached_shaders[0] = 0xfeedfaceu;
    attached_shader_count = 0xfeedfaceu;
    assert(ringl_get_attached_shaders(program, attached_shaders, 1u,
                                      &attached_shader_count) == -1);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    assert(attached_shaders[0] == 0xfeedfaceu &&
           attached_shader_count == 0xfeedfaceu);
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
    assert(ringl_get_program_parameteriv_bounded(
               program, RINGL_LINK_STATUS, &query_value, 1u) == 0);
    assert(query_value == (int32_t)RINGL_TRUE);
    assert(ringl_get_program_parameteriv_bounded(
               program, RINGL_ACTIVE_ATTRIBUTES, &query_value, 1u) == 0);
    assert(query_value == 1);
    assert(ringl_get_program_parameteriv_bounded(
               program, RINGL_ACTIVE_UNIFORMS, &query_value, 1u) == 0);
    assert(query_value == 1);
    assert(ringl_get_program_parameteriv_bounded(
               program, RINGL_INFO_LOG_LENGTH, &query_value, 1u) == 0);
    assert(query_value == 1);
    assert(ringl_get_program_parameteriv_bounded(
               program, RINGL_ACTIVE_ATTRIBUTE_MAX_LENGTH, &query_value, 1u) ==
           0);
    assert(query_value == 9);
    assert(ringl_get_program_parameteriv_bounded(
               program, RINGL_ACTIVE_UNIFORM_MAX_LENGTH, &query_value, 1u) == 0);
    assert(query_value == 13);
    ringl_validate_program(program);
    assert(ringl_get_program_info(program, &info) == 0);
    assert(info.validate_status == RINGL_TRUE);
    assert(ringl_get_program_parameteriv_bounded(
               program, RINGL_VALIDATE_STATUS, &query_value, 1u) == 0);
    assert(query_value == (int32_t)RINGL_TRUE);
    query_value = 999;
    assert(ringl_get_program_parameteriv_bounded(
               program, RINGL_LINK_STATUS, &query_value, 0u) == -1);
    assert(query_value == 999);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
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
    {
        const int32_t scalar_sampler_unit = 3;

        ringl_uniform_1iv(location, 1u, &scalar_sampler_unit);
    }
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_get_uniform_1i(program, location, &uniform_value) == 0);
    assert(uniform_value == 3);
    {
        const int32_t invalid_sampler_units[] = {
            -1, (int32_t)RINGL_MAX_TEXTURE_UNITS,
        };

        ringl_uniform_1iv(location, 1u, &invalid_sampler_units[0]);
        assert(ringl_get_error() == RINGL_INVALID_VALUE);
        ringl_uniform_1iv(location, 1u, &invalid_sampler_units[1]);
        assert(ringl_get_error() == RINGL_INVALID_VALUE);
        assert(ringl_get_uniform_1i(program, location, &uniform_value) == 0);
        assert(uniform_value == 3);
    }

    {
        uint32_t array_vertex = ringl_create_shader(RINGL_VERTEX_SHADER);
        uint32_t array_fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);
        uint32_t array_program = ringl_create_program();
        int32_t array_units[2] = { 1, 3 };
        int32_t array_base_location;
        int32_t array_second_location;

        assert(array_vertex != 0u && array_fragment != 0u &&
               array_program != 0u);
        ringl_shader_source(array_vertex,
                            "void main() { gl_Position = vec4(0.0); }", -1);
        ringl_shader_source(array_fragment,
                            "uniform sampler2D palette[2]; "
                            "void main() { gl_FragColor = "
                            "texture2D(palette[1], vec2(0.5, 0.5)); }", -1);
        ringl_compile_shader(array_vertex);
        ringl_compile_shader(array_fragment);
        assert(ringl_get_shader_compile_status(array_vertex) == RINGL_TRUE);
        assert(ringl_get_shader_compile_status(array_fragment) == RINGL_TRUE);
        ringl_attach_shader(array_program, array_vertex);
        ringl_attach_shader(array_program, array_fragment);
        ringl_link_program(array_program);
        assert(ringl_get_program_link_status(array_program) == RINGL_TRUE);
        assert(ringl_get_program_info(array_program, &info) == 0);
        assert(info.active_uniform_count == 1u);
        assert(ringl_get_active_uniform(array_program, 0u, &active_info) == 0);
        assert(active_info.type == RINGL_SAMPLER_2D && active_info.size == 2u &&
               strcmp(active_info.name, "palette[0]") == 0);
        array_base_location = ringl_get_uniform_location(array_program, "palette");
        array_second_location = ringl_get_uniform_location(array_program, "palette[1]");
        assert(array_base_location >= 0 &&
               array_base_location == ringl_get_uniform_location(array_program,
                                                                  "palette[0]") &&
               array_second_location == array_base_location + 1);
        ringl_use_program(array_program);
        ringl_uniform_1iv(array_base_location, 2u, array_units);
        assert(ringl_get_error() == RINGL_NO_ERROR);
        assert(ringl_get_uniform_1i(array_program, array_second_location,
                                    &uniform_value) == 0);
        assert(uniform_value == 3);
        /* Both active samplers have the same sampler2D type, so aliasing one
         * texture unit is valid and must not be mistaken for a type conflict. */
        array_units[0] = 2;
        array_units[1] = 2;
        ringl_uniform_1iv(array_base_location, 2u, array_units);
        assert(ringl_get_error() == RINGL_NO_ERROR);
        assert(ringl_get_uniform_1i(array_program, array_base_location,
                                    &uniform_value) == 0);
        assert(uniform_value == 2);
        assert(ringl_get_uniform_1i(array_program, array_second_location,
                                    &uniform_value) == 0);
        assert(uniform_value == 2);
        ringl_uniform_1iv(array_second_location, 2u, array_units);
        assert(ringl_get_error() == RINGL_INVALID_OPERATION);
        assert(ringl_get_uniform_1i(array_program, array_second_location,
                                    &uniform_value) == 0);
        assert(uniform_value == 2);
        ringl_delete_program(array_program);
        ringl_delete_shader(array_vertex);
        ringl_delete_shader(array_fragment);
    }

    {
        uint32_t dynamic_fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);

        assert(dynamic_fragment != 0u);
        ringl_shader_source(dynamic_fragment,
                            "uniform sampler2D palette[2]; uniform int i; "
                            "void main() { gl_FragColor = "
                            "texture2D(palette[i], vec2(0.5, 0.5)); }", -1);
        ringl_compile_shader(dynamic_fragment);
        assert(ringl_get_shader_compile_status(dynamic_fragment) == RINGL_FALSE);
        ringl_delete_shader(dynamic_fragment);
    }

    {
        uint32_t array_vertex = ringl_create_shader(RINGL_VERTEX_SHADER);
        uint32_t array_fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);
        uint32_t array_program = ringl_create_program();
        float gains[2] = { 0.25f, 0.75f };
        float value = -1.0f;
        int32_t base_location;
        int32_t second_location;

        assert(array_vertex != 0u && array_fragment != 0u &&
               array_program != 0u);
        ringl_shader_source(array_vertex,
                            "void main() { gl_Position = vec4(0.0); }", -1);
        ringl_shader_source(array_fragment,
                            "uniform float gains[2]; void main() { "
                            "gl_FragColor = vec4(gains[1], gains[0], 0.0, 1.0); }",
                            -1);
        ringl_compile_shader(array_vertex);
        ringl_compile_shader(array_fragment);
        assert(ringl_get_shader_compile_status(array_vertex) == RINGL_TRUE);
        assert(ringl_get_shader_compile_status(array_fragment) == RINGL_TRUE);
        ringl_attach_shader(array_program, array_vertex);
        ringl_attach_shader(array_program, array_fragment);
        ringl_link_program(array_program);
        assert(ringl_get_program_link_status(array_program) == RINGL_TRUE);
        assert(ringl_get_program_info(array_program, &info) == 0);
        assert(info.active_uniform_count == 1u);
        assert(ringl_get_active_uniform(array_program, 0u, &active_info) == 0);
        assert(active_info.type == RINGL_FLOAT && active_info.size == 2u &&
               strcmp(active_info.name, "gains[0]") == 0);
        base_location = ringl_get_uniform_location(array_program, "gains");
        second_location = ringl_get_uniform_location(array_program, "gains[1]");
        assert(base_location >= 0 &&
               base_location == ringl_get_uniform_location(array_program,
                                                            "gains[0]") &&
               second_location == base_location + 1);
        ringl_use_program(array_program);
        ringl_uniform_1fv(base_location, 2u, gains);
        assert(ringl_get_error() == RINGL_NO_ERROR);
        assert(ringl_get_uniform_1f(array_program, second_location, &value) == 0);
        assert(value == 0.75f);
        ringl_uniform_1fv(second_location, 2u, gains);
        assert(ringl_get_error() == RINGL_INVALID_OPERATION);
        assert(ringl_get_uniform_1f(array_program, second_location, &value) == 0);
        assert(value == 0.75f);
        gains[0] = NAN;
        ringl_uniform_1fv(base_location, 2u, gains);
        assert(ringl_get_error() == RINGL_INVALID_VALUE);
        assert(ringl_get_uniform_1f(array_program, base_location, &value) == 0);
        assert(value == 0.25f);
        ringl_delete_program(array_program);
        ringl_delete_shader(array_vertex);
        ringl_delete_shader(array_fragment);
    }

    {
        uint32_t mismatch_vertex = ringl_create_shader(RINGL_VERTEX_SHADER);
        uint32_t mismatch_fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);
        uint32_t mismatch_program = ringl_create_program();

        assert(mismatch_vertex != 0u && mismatch_fragment != 0u &&
               mismatch_program != 0u);
        ringl_shader_source(mismatch_vertex,
                            "uniform sampler2D palette[1]; "
                            "void main() { gl_Position = vec4(0.0); }", -1);
        ringl_shader_source(mismatch_fragment,
                            "uniform sampler2D palette[2]; "
                            "void main() { gl_FragColor = "
                            "texture2D(palette[1], vec2(0.5, 0.5)); }", -1);
        ringl_compile_shader(mismatch_vertex);
        ringl_compile_shader(mismatch_fragment);
        assert(ringl_get_shader_compile_status(mismatch_vertex) == RINGL_TRUE);
        assert(ringl_get_shader_compile_status(mismatch_fragment) == RINGL_TRUE);
        ringl_attach_shader(mismatch_program, mismatch_vertex);
        ringl_attach_shader(mismatch_program, mismatch_fragment);
        ringl_link_program(mismatch_program);
        assert(ringl_get_program_link_status(mismatch_program) == RINGL_FALSE);
        assert(ringl_get_program_info_log(mismatch_program, log, sizeof(log)) > 0u);
        assert(strstr(log, "incompatible") != NULL);
        ringl_delete_program(mismatch_program);
        ringl_delete_shader(mismatch_vertex);
        ringl_delete_shader(mismatch_fragment);
    }

    {
        RinGLArrayTestProgram array_program = ringl_create_array_test_program(
            "uniform vec4 colors[2]; void main() { "
            "gl_FragColor = colors[1]; }");
        float colors[8] = { 1.0f, 2.0f, 3.0f, 4.0f,
                            5.0f, 6.0f, 7.0f, 8.0f };
        float readback[4] = { 0.0f };

        assert(ringl_get_program_info(array_program.program, &info) == 0 &&
               info.active_uniform_count == 1u);
        assert(ringl_get_active_uniform(array_program.program, 0u,
                                        &active_info) == 0 &&
               active_info.type == RINGL_FLOAT_VEC4 && active_info.size == 2u &&
               strcmp(active_info.name, "colors[0]") == 0);
        location = ringl_get_uniform_location(array_program.program, "colors");
        assert(location >= 0 && location + 1 ==
               ringl_get_uniform_location(array_program.program, "colors[1]"));
        ringl_use_program(array_program.program);
        ringl_uniform_4fv(location, 2u, colors);
        assert(ringl_get_error() == RINGL_NO_ERROR);
        assert(ringl_get_uniform_4f(array_program.program, location + 1,
                                    readback) == 0 &&
               memcmp(readback, colors + 4, sizeof(readback)) == 0);
        ringl_uniform_4fv(location + 1, 2u, colors);
        assert(ringl_get_error() == RINGL_INVALID_OPERATION);
        assert(ringl_get_uniform_4f(array_program.program, location + 1,
                                    readback) == 0 &&
               memcmp(readback, colors + 4, sizeof(readback)) == 0);
        ringl_destroy_array_test_program(&array_program);
    }

    {
        RinGLArrayTestProgram array_program = ringl_create_array_test_program(
            "uniform vec2 pairs[2]; void main() { "
            "gl_FragColor = vec4(pairs[1], 0.0, 1.0); }");
        const float values[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
        float readback[2] = { 0.0f };

        location = ringl_get_uniform_location(array_program.program, "pairs");
        ringl_use_program(array_program.program);
        ringl_uniform_2fv(location, 2u, values);
        assert(ringl_get_error() == RINGL_NO_ERROR);
        assert(ringl_get_uniform_2f(array_program.program, location + 1,
                                    readback) == 0 &&
               memcmp(readback, values + 2, sizeof(readback)) == 0);
        ringl_destroy_array_test_program(&array_program);
    }

    {
        RinGLArrayTestProgram array_program = ringl_create_array_test_program(
            "uniform vec3 triples[2]; void main() { "
            "gl_FragColor = vec4(triples[1], 1.0); }");
        const float values[6] = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f };
        float readback[3] = { 0.0f };

        location = ringl_get_uniform_location(array_program.program, "triples");
        ringl_use_program(array_program.program);
        ringl_uniform_3fv(location, 2u, values);
        assert(ringl_get_error() == RINGL_NO_ERROR);
        assert(ringl_get_uniform_3f(array_program.program, location + 1,
                                    readback) == 0 &&
               memcmp(readback, values + 3, sizeof(readback)) == 0);
        ringl_destroy_array_test_program(&array_program);
    }

    {
        RinGLArrayTestProgram array_program = ringl_create_array_test_program(
            "uniform int levels[2]; void main() { "
            "gl_FragColor = vec4(float(levels[1])); }");
        const int32_t values[2] = { -3, 7 };
        int32_t readback = 0;

        location = ringl_get_uniform_location(array_program.program, "levels");
        ringl_use_program(array_program.program);
        ringl_uniform_1iv(location, 2u, values);
        assert(ringl_get_error() == RINGL_NO_ERROR);
        assert(ringl_get_uniform_1i(array_program.program, location + 1,
                                    &readback) == 0 && readback == 7);
        ringl_destroy_array_test_program(&array_program);
    }

    {
        RinGLArrayTestProgram array_program = ringl_create_array_test_program(
            "uniform bvec2 flags[2]; void main() { gl_FragColor = vec4(1.0); }");
        const int32_t values[4] = { 0, 2, -3, 0 };
        int32_t readback[2] = { 0, 0 };

        location = ringl_get_uniform_location(array_program.program, "flags");
        ringl_use_program(array_program.program);
        ringl_uniform_2iv(location, 2u, values);
        assert(ringl_get_error() == RINGL_NO_ERROR);
        assert(ringl_get_uniform_2i(array_program.program, location + 1,
                                    readback) == 0 &&
               readback[0] == 1 && readback[1] == 0);
        ringl_destroy_array_test_program(&array_program);
    }

    {
        RinGLArrayTestProgram array_program = ringl_create_array_test_program(
            "uniform ivec3 triples[2]; void main() { "
            "gl_FragColor = vec4(float(triples[1].z)); }");
        const int32_t values[6] = { 1, 2, 3, 4, 5, 6 };
        int32_t readback[3] = { 0, 0, 0 };

        location = ringl_get_uniform_location(array_program.program, "triples");
        ringl_use_program(array_program.program);
        ringl_uniform_3iv(location, 2u, values);
        assert(ringl_get_error() == RINGL_NO_ERROR);
        assert(ringl_get_uniform_3i(array_program.program, location + 1,
                                    readback) == 0 && readback[2] == 6);
        ringl_destroy_array_test_program(&array_program);
    }

    {
        RinGLArrayTestProgram array_program = ringl_create_array_test_program(
            "uniform bvec4 flags[2]; void main() { gl_FragColor = vec4(1.0); }");
        const int32_t values[8] = { 0, 0, 0, 0, 0, 2, -3, 4 };
        int32_t readback[4] = { 0, 0, 0, 0 };

        location = ringl_get_uniform_location(array_program.program, "flags");
        ringl_use_program(array_program.program);
        ringl_uniform_4iv(location, 2u, values);
        assert(ringl_get_error() == RINGL_NO_ERROR);
        assert(ringl_get_uniform_4i(array_program.program, location + 1,
                                    readback) == 0 &&
               readback[1] == 1 && readback[2] == 1 && readback[3] == 1);
        ringl_destroy_array_test_program(&array_program);
    }

    {
        RinGLArrayTestProgram array_program = ringl_create_vertex_array_test_program(
            "attribute vec2 position; uniform mat2 transforms[2]; "
            "void main() { gl_Position = vec4(transforms[1] * position, 0.0, 1.0); }");
        const float values[8] = { 1.0f, 0.0f, 0.0f, 1.0f,
                                  2.0f, 0.0f, 0.0f, 2.0f };
        float readback[4] = { 0.0f };

        location = ringl_get_uniform_location(array_program.program, "transforms");
        ringl_use_program(array_program.program);
        ringl_uniform_matrix2fv_array(location, 2u, RINGL_FALSE, values);
        assert(ringl_get_error() == RINGL_NO_ERROR);
        assert(ringl_get_uniform_matrix2f(array_program.program, location + 1,
                                          readback) == 0 &&
               memcmp(readback, values + 4, sizeof(readback)) == 0);
        ringl_destroy_array_test_program(&array_program);
    }

    {
        RinGLArrayTestProgram array_program = ringl_create_vertex_array_test_program(
            "attribute vec3 position; uniform mat3 transforms[2]; "
            "void main() { gl_Position = vec4(transforms[1] * position, 1.0); }");
        float values[18] = { 0.0f };
        float readback[9] = { 0.0f };

        values[0] = values[4] = values[8] = 1.0f;
        values[9] = values[13] = values[17] = 2.0f;
        location = ringl_get_uniform_location(array_program.program, "transforms");
        ringl_use_program(array_program.program);
        ringl_uniform_matrix3fv_array(location, 2u, RINGL_FALSE, values);
        assert(ringl_get_error() == RINGL_NO_ERROR);
        assert(ringl_get_uniform_matrix3f(array_program.program, location + 1,
                                          readback) == 0 &&
               readback[0] == 2.0f && readback[4] == 2.0f &&
               readback[8] == 2.0f);
        ringl_destroy_array_test_program(&array_program);
    }

    {
        RinGLArrayTestProgram array_program = ringl_create_vertex_array_test_program(
            "attribute vec4 position; uniform mat4 transforms[2]; "
            "void main() { gl_Position = transforms[1] * position; }");
        float values[32] = { 0.0f };
        float readback[16] = { 0.0f };

        values[0] = values[5] = values[10] = values[15] = 1.0f;
        values[16] = values[21] = values[26] = values[31] = 2.0f;
        location = ringl_get_uniform_location(array_program.program, "transforms");
        ringl_use_program(array_program.program);
        ringl_uniform_matrix4fv_array(location, 2u, RINGL_FALSE, values);
        assert(ringl_get_error() == RINGL_NO_ERROR);
        assert(ringl_get_uniform_matrix4f(array_program.program, location + 1,
                                          readback) == 0 &&
               readback[0] == 2.0f && readback[5] == 2.0f &&
               readback[10] == 2.0f && readback[15] == 2.0f);
        ringl_destroy_array_test_program(&array_program);
    }

    /* Fragment matrices use the same program-owned RSH1 constants and
     * column-major multiplication as the vertex profile.  Keep this as a
     * real indexed array expression so the lowerer, linker, reflection, and
     * contiguous setter path must all agree on the fragment-stage location. */
    {
        RinGLArrayTestProgram array_program = ringl_create_array_test_program(
            "uniform mat2 colors[2]; void main() { "
            "vec2 color = colors[1] * vec2(0.25, 0.5); "
            "gl_FragColor = vec4(color, 0.0, 1.0); }");
        const float values[8] = { 1.0f, 0.0f, 0.0f, 1.0f,
                                  0.5f, 0.2f, 0.1f, 0.6f };
        float readback[4] = { 0.0f };

        assert(ringl_get_program_info(array_program.program, &info) == 0 &&
               info.active_uniform_count == 1u);
        assert(ringl_get_active_uniform(array_program.program, 0u,
                                        &active_info) == 0 &&
               active_info.type == RINGL_FLOAT_MAT2 && active_info.size == 2u &&
               strcmp(active_info.name, "colors[0]") == 0);
        location = ringl_get_uniform_location(array_program.program, "colors");
        assert(location == ringl_get_uniform_location(array_program.program,
                                                      "colors[0]"));
        ringl_use_program(array_program.program);
        ringl_uniform_matrix2fv_array(location, 2u, RINGL_FALSE, values);
        assert(ringl_get_error() == RINGL_NO_ERROR);
        assert(ringl_get_uniform_matrix2f(array_program.program, location + 1,
                                          readback) == 0 &&
               memcmp(readback, values + 4, sizeof(readback)) == 0);
        ringl_uniform_matrix2fv_array(location + 1, 2u, RINGL_FALSE, values);
        assert(ringl_get_error() == RINGL_INVALID_OPERATION);
        assert(ringl_get_uniform_matrix2f(array_program.program, location + 1,
                                          readback) == 0 &&
               memcmp(readback, values + 4, sizeof(readback)) == 0);
        ringl_destroy_array_test_program(&array_program);
    }

    ringl_uniform_1i(-1, 7);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_uniform_1i(99, 0);
    assert(ringl_get_error() == RINGL_INVALID_OPERATION);
    uniform_value = -1;
    assert(ringl_get_uniform_1i(program, 99, &uniform_value) == -1);
    assert(ringl_get_error() == RINGL_INVALID_OPERATION);
    assert(uniform_value == -1);

    /* Vec4 uniforms are linked program state, not shader-object state. A
     * second program sharing the same shader pair keeps WebGL's zero default
     * after the first program changes its own RinGPU executable. */
    vec4_vertex = ringl_create_shader(RINGL_VERTEX_SHADER);
    vec4_fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);
    vec4_program = ringl_create_program();
    vec4_peer_program = ringl_create_program();
    assert(vec4_vertex != 0u && vec4_fragment != 0u && vec4_program != 0u &&
           vec4_peer_program != 0u);
    ringl_shader_source(vec4_vertex,
                        "void main() { gl_Position = vec4(-1.0, -1.0, 0.0, 1.0); }",
                        -1);
    ringl_shader_source(vec4_fragment,
                        "precision mediump float; precision highp int; "
                        "uniform vec4 tint; void main() { "
                        "vec4 shaded = -tint * 0.5; "
                        "gl_FragColor = shaded + vec4(1.0, 0.5, 0.25, 0.0); }",
                        -1);
    ringl_compile_shader(vec4_vertex);
    ringl_compile_shader(vec4_fragment);
    assert(ringl_get_shader_compile_status(vec4_vertex) == RINGL_TRUE);
    assert(ringl_get_shader_compile_status(vec4_fragment) == RINGL_TRUE);
    ringl_attach_shader(vec4_program, vec4_vertex);
    ringl_attach_shader(vec4_program, vec4_fragment);
    ringl_attach_shader(vec4_peer_program, vec4_vertex);
    ringl_attach_shader(vec4_peer_program, vec4_fragment);
    ringl_link_program(vec4_program);
    ringl_link_program(vec4_peer_program);
    assert(ringl_get_program_link_status(vec4_program) == RINGL_TRUE);
    assert(ringl_get_program_link_status(vec4_peer_program) == RINGL_TRUE);
    assert(ringl_get_program_info(vec4_program, &info) == 0);
    assert(info.active_uniform_count == 1u);
    location = ringl_get_uniform_location(vec4_program, "tint");
    assert(location == 0);
    assert(ringl_get_active_uniform(vec4_program, 0u, &active_info) == 0);
    assert(active_info.type == RINGL_FLOAT_VEC4 &&
           strcmp(active_info.name, "tint") == 0);
    {
        float values[4] = { -1.0f, -1.0f, -1.0f, -1.0f };

        assert(ringl_get_uniform_4f(vec4_program, location, values) == 0);
        assert(values[0] == 0.0f && values[1] == 0.0f &&
               values[2] == 0.0f && values[3] == 0.0f);
        ringl_use_program(vec4_program);
        ringl_uniform_4f(location, 1.0f, 0.25f, 0.0f, 1.0f);
        assert(ringl_get_error() == RINGL_NO_ERROR);
        assert(ringl_get_uniform_4f(vec4_program, location, values) == 0);
        assert(values[0] == 1.0f && values[1] == 0.25f &&
               values[2] == 0.0f && values[3] == 1.0f);
        ringl_uniform_4f(location, NAN, 0.0f, 0.0f, 1.0f);
        assert(ringl_get_error() == RINGL_INVALID_VALUE);
        assert(ringl_get_uniform_4f(vec4_program, location, values) == 0);
        assert(values[0] == 1.0f && values[1] == 0.25f &&
               values[2] == 0.0f && values[3] == 1.0f);
        ringl_uniform_4f(location, INFINITY, 0.0f, 0.0f, 1.0f);
        assert(ringl_get_error() == RINGL_INVALID_VALUE);
        assert(ringl_get_uniform_4f(vec4_program, location, values) == 0);
        assert(values[0] == 1.0f && values[1] == 0.25f &&
               values[2] == 0.0f && values[3] == 1.0f);
        memset(values, 0xff, sizeof(values));
        assert(ringl_get_uniform_4f(vec4_peer_program, location, values) == 0);
        assert(values[0] == 0.0f && values[1] == 0.0f &&
               values[2] == 0.0f && values[3] == 0.0f);
    }
    ringl_uniform_1i(location, 0);
    assert(ringl_get_error() == RINGL_INVALID_OPERATION);
    ringl_delete_program(vec4_peer_program);
    ringl_delete_program(vec4_program);

    /* A scalar float follows the same program-owned lowering path as vec4:
     * per-program defaults, reflection, readback, and finite atomic update. */
    float_vertex = ringl_create_shader(RINGL_VERTEX_SHADER);
    float_fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);
    float_program = ringl_create_program();
    float_peer_program = ringl_create_program();
    assert(float_vertex != 0u && float_fragment != 0u &&
           float_program != 0u && float_peer_program != 0u);
    ringl_shader_source(float_vertex,
                        "void main() { gl_Position = vec4(-1.0, -1.0, 0.0, 1.0); }",
                        -1);
    ringl_shader_source(float_fragment,
                        "uniform float opacity; "
                        "void main() { gl_FragColor = vec4(opacity, 0.0, 0.0, 1.0); }",
                        -1);
    ringl_compile_shader(float_vertex);
    ringl_compile_shader(float_fragment);
    assert(ringl_get_shader_compile_status(float_vertex) == RINGL_TRUE);
    assert(ringl_get_shader_compile_status(float_fragment) == RINGL_TRUE);
    ringl_attach_shader(float_program, float_vertex);
    ringl_attach_shader(float_program, float_fragment);
    ringl_attach_shader(float_peer_program, float_vertex);
    ringl_attach_shader(float_peer_program, float_fragment);
    ringl_link_program(float_program);
    ringl_link_program(float_peer_program);
    assert(ringl_get_program_link_status(float_program) == RINGL_TRUE);
    assert(ringl_get_program_link_status(float_peer_program) == RINGL_TRUE);
    assert(ringl_get_program_info(float_program, &info) == 0);
    assert(info.active_uniform_count == 1u);
    location = ringl_get_uniform_location(float_program, "opacity");
    assert(location == 0);
    assert(ringl_get_active_uniform(float_program, 0u, &active_info) == 0);
    assert(active_info.type == RINGL_FLOAT &&
           strcmp(active_info.name, "opacity") == 0);
    {
        float value = -1.0f;

        assert(ringl_get_uniform_1f(float_program, location, &value) == 0);
        assert(value == 0.0f);
        ringl_use_program(float_program);
        ringl_uniform_1f(location, 0.25f);
        assert(ringl_get_error() == RINGL_NO_ERROR);
        assert(ringl_get_uniform_1f(float_program, location, &value) == 0);
        assert(value == 0.25f);
        ringl_uniform_1f(location, NAN);
        assert(ringl_get_error() == RINGL_INVALID_VALUE);
        assert(ringl_get_uniform_1f(float_program, location, &value) == 0);
        assert(value == 0.25f);
        value = -1.0f;
        assert(ringl_get_uniform_1f(float_peer_program, location, &value) == 0);
        assert(value == 0.0f);
    }
    ringl_uniform_4f(location, 1.0f, 1.0f, 1.0f, 1.0f);
    assert(ringl_get_error() == RINGL_INVALID_OPERATION);
    ringl_delete_program(float_peer_program);
    ringl_delete_program(float_program);

    /* vec2 and vec3 share the program-owned mutable lowering path. Their
     * locations must remain type-separated so a setter can never index a
     * differently-sized uniform record. */
    vec2_vertex = ringl_create_shader(RINGL_VERTEX_SHADER);
    vec2_fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);
    vec2_program = ringl_create_program();
    vec2_peer_program = ringl_create_program();
    assert(vec2_vertex != 0u && vec2_fragment != 0u && vec2_program != 0u &&
           vec2_peer_program != 0u);
    ringl_shader_source(vec2_vertex,
                        "void main() { gl_Position = vec4(-1.0, -1.0, 0.0, 1.0); }",
                        -1);
    ringl_shader_source(vec2_fragment,
                        "uniform vec2 tint; "
                        "void main() { gl_FragColor = vec4(tint, 0.0, 1.0); }",
                        -1);
    ringl_compile_shader(vec2_vertex);
    ringl_compile_shader(vec2_fragment);
    assert(ringl_get_shader_compile_status(vec2_vertex) == RINGL_TRUE);
    assert(ringl_get_shader_compile_status(vec2_fragment) == RINGL_TRUE);
    ringl_attach_shader(vec2_program, vec2_vertex);
    ringl_attach_shader(vec2_program, vec2_fragment);
    ringl_attach_shader(vec2_peer_program, vec2_vertex);
    ringl_attach_shader(vec2_peer_program, vec2_fragment);
    ringl_link_program(vec2_program);
    ringl_link_program(vec2_peer_program);
    assert(ringl_get_program_link_status(vec2_program) == RINGL_TRUE);
    assert(ringl_get_program_link_status(vec2_peer_program) == RINGL_TRUE);
    assert(ringl_get_program_info(vec2_program, &info) == 0);
    assert(info.active_uniform_count == 1u);
    location = ringl_get_uniform_location(vec2_program, "tint");
    assert(location == 0);
    assert(ringl_get_active_uniform(vec2_program, 0u, &active_info) == 0);
    assert(active_info.type == RINGL_FLOAT_VEC2 &&
           strcmp(active_info.name, "tint") == 0);
    {
        float values[2] = { -1.0f, -1.0f };

        assert(ringl_get_uniform_2f(vec2_program, location, values) == 0);
        assert(values[0] == 0.0f && values[1] == 0.0f);
        ringl_use_program(vec2_program);
        ringl_uniform_2f(location, 0.25f, 0.75f);
        assert(ringl_get_error() == RINGL_NO_ERROR);
        assert(ringl_get_uniform_2f(vec2_program, location, values) == 0);
        assert(values[0] == 0.25f && values[1] == 0.75f);
        ringl_uniform_2f(location, NAN, 0.5f);
        assert(ringl_get_error() == RINGL_INVALID_VALUE);
        assert(ringl_get_uniform_2f(vec2_program, location, values) == 0);
        assert(values[0] == 0.25f && values[1] == 0.75f);
        ringl_uniform_3f(location, 1.0f, 1.0f, 1.0f);
        assert(ringl_get_error() == RINGL_INVALID_OPERATION);
        values[0] = -1.0f;
        values[1] = -1.0f;
        assert(ringl_get_uniform_2f(vec2_peer_program, location, values) == 0);
        assert(values[0] == 0.0f && values[1] == 0.0f);
    }
    ringl_delete_program(vec2_peer_program);
    ringl_delete_program(vec2_program);

    vec3_vertex = ringl_create_shader(RINGL_VERTEX_SHADER);
    vec3_fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);
    vec3_program = ringl_create_program();
    vec3_peer_program = ringl_create_program();
    assert(vec3_vertex != 0u && vec3_fragment != 0u && vec3_program != 0u &&
           vec3_peer_program != 0u);
    ringl_shader_source(vec3_vertex,
                        "void main() { gl_Position = vec4(-1.0, -1.0, 0.0, 1.0); }",
                        -1);
    ringl_shader_source(vec3_fragment,
                        "uniform vec3 tint; "
                        "void main() { gl_FragColor = vec4(tint, 1.0); }",
                        -1);
    ringl_compile_shader(vec3_vertex);
    ringl_compile_shader(vec3_fragment);
    assert(ringl_get_shader_compile_status(vec3_vertex) == RINGL_TRUE);
    assert(ringl_get_shader_compile_status(vec3_fragment) == RINGL_TRUE);
    ringl_attach_shader(vec3_program, vec3_vertex);
    ringl_attach_shader(vec3_program, vec3_fragment);
    ringl_attach_shader(vec3_peer_program, vec3_vertex);
    ringl_attach_shader(vec3_peer_program, vec3_fragment);
    ringl_link_program(vec3_program);
    ringl_link_program(vec3_peer_program);
    assert(ringl_get_program_link_status(vec3_program) == RINGL_TRUE);
    assert(ringl_get_program_link_status(vec3_peer_program) == RINGL_TRUE);
    assert(ringl_get_program_info(vec3_program, &info) == 0);
    assert(info.active_uniform_count == 1u);
    location = ringl_get_uniform_location(vec3_program, "tint");
    assert(location == 0);
    assert(ringl_get_active_uniform(vec3_program, 0u, &active_info) == 0);
    assert(active_info.type == RINGL_FLOAT_VEC3 &&
           strcmp(active_info.name, "tint") == 0);
    {
        float values[3] = { -1.0f, -1.0f, -1.0f };

        assert(ringl_get_uniform_3f(vec3_program, location, values) == 0);
        assert(values[0] == 0.0f && values[1] == 0.0f && values[2] == 0.0f);
        ringl_use_program(vec3_program);
        ringl_uniform_3f(location, 0.1f, 0.2f, 0.3f);
        assert(ringl_get_error() == RINGL_NO_ERROR);
        assert(ringl_get_uniform_3f(vec3_program, location, values) == 0);
        assert(values[0] == 0.1f && values[1] == 0.2f && values[2] == 0.3f);
        ringl_uniform_3f(location, 0.1f, INFINITY, 0.3f);
        assert(ringl_get_error() == RINGL_INVALID_VALUE);
        assert(ringl_get_uniform_3f(vec3_program, location, values) == 0);
        assert(values[0] == 0.1f && values[1] == 0.2f && values[2] == 0.3f);
        ringl_uniform_4f(location, 1.0f, 1.0f, 1.0f, 1.0f);
        assert(ringl_get_error() == RINGL_INVALID_OPERATION);
        values[0] = -1.0f;
        values[1] = -1.0f;
        values[2] = -1.0f;
        assert(ringl_get_uniform_3f(vec3_peer_program, location, values) == 0);
        assert(values[0] == 0.0f && values[1] == 0.0f && values[2] == 0.0f);
    }
    ringl_delete_program(vec3_peer_program);
    ringl_delete_program(vec3_program);

    /* Integer uniform state uses the same program-owned executable path as
     * float uniforms, but remains i32 until an explicit GLSL float() cast.
     * This exercises scalar and all integer vector widths, reflection,
     * location validation, readback, and per-program zero defaults. */
    integer_vertex = ringl_create_shader(RINGL_VERTEX_SHADER);
    integer_fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);
    integer_program = ringl_create_program();
    integer_peer_program = ringl_create_program();
    assert(integer_vertex != 0u && integer_fragment != 0u &&
           integer_program != 0u && integer_peer_program != 0u);
    ringl_shader_source(integer_vertex,
                        "void main() { gl_Position = vec4(-1.0, -1.0, 0.0, 1.0); }",
                        -1);
    ringl_shader_source(integer_fragment,
                        "uniform int scalar; uniform ivec2 pair; "
                        "uniform ivec3 triple; uniform ivec4 quad; "
                        "void main() { int total = scalar + pair.x + triple.y + quad.w; "
                        "gl_FragColor = vec4(float(total), float(pair.y), "
                        "float(triple.z), float(quad.x)); }",
                        -1);
    ringl_compile_shader(integer_vertex);
    ringl_compile_shader(integer_fragment);
    assert(ringl_get_shader_compile_status(integer_vertex) == RINGL_TRUE);
    assert(ringl_get_shader_compile_status(integer_fragment) == RINGL_TRUE);
    ringl_attach_shader(integer_program, integer_vertex);
    ringl_attach_shader(integer_program, integer_fragment);
    ringl_attach_shader(integer_peer_program, integer_vertex);
    ringl_attach_shader(integer_peer_program, integer_fragment);
    ringl_link_program(integer_program);
    ringl_link_program(integer_peer_program);
    assert(ringl_get_program_link_status(integer_program) == RINGL_TRUE);
    assert(ringl_get_program_link_status(integer_peer_program) == RINGL_TRUE);
    assert(ringl_get_program_info(integer_program, &info) == 0);
    assert(info.active_uniform_count == 4u);
    assert(ringl_get_active_uniform(integer_program, 0u, &active_info) == 0);
    assert(active_info.type == RINGL_INT && strcmp(active_info.name, "scalar") == 0);
    assert(ringl_get_active_uniform(integer_program, 1u, &active_info) == 0);
    assert(active_info.type == RINGL_INT_VEC2 && strcmp(active_info.name, "pair") == 0);
    assert(ringl_get_active_uniform(integer_program, 2u, &active_info) == 0);
    assert(active_info.type == RINGL_INT_VEC3 && strcmp(active_info.name, "triple") == 0);
    assert(ringl_get_active_uniform(integer_program, 3u, &active_info) == 0);
    assert(active_info.type == RINGL_INT_VEC4 && strcmp(active_info.name, "quad") == 0);
    {
        int32_t scalar_location = ringl_get_uniform_location(integer_program, "scalar");
        int32_t pair_location = ringl_get_uniform_location(integer_program, "pair");
        int32_t triple_location = ringl_get_uniform_location(integer_program, "triple");
        int32_t quad_location = ringl_get_uniform_location(integer_program, "quad");
        int32_t scalar = -1;
        int32_t pair[2] = { -1, -1 };
        int32_t triple[3] = { -1, -1, -1 };
        int32_t quad[4] = { -1, -1, -1, -1 };

        assert(scalar_location == 0 && pair_location == 1 &&
               triple_location == 2 && quad_location == 3);
        assert(ringl_get_uniform_1i(integer_program, scalar_location, &scalar) == 0);
        assert(ringl_get_uniform_2i(integer_program, pair_location, pair) == 0);
        assert(ringl_get_uniform_3i(integer_program, triple_location, triple) == 0);
        assert(ringl_get_uniform_4i(integer_program, quad_location, quad) == 0);
        assert(scalar == 0 && pair[0] == 0 && pair[1] == 0 &&
               triple[0] == 0 && triple[1] == 0 && triple[2] == 0 &&
               quad[0] == 0 && quad[1] == 0 && quad[2] == 0 && quad[3] == 0);
        ringl_use_program(integer_program);
        ringl_uniform_1i(scalar_location, -3);
        ringl_uniform_2i(pair_location, 4, -5);
        ringl_uniform_3i(triple_location, 6, -7, 8);
        ringl_uniform_4i(quad_location, -9, 10, -11, 12);
        assert(ringl_get_error() == RINGL_NO_ERROR);
        assert(ringl_get_uniform_1i(integer_program, scalar_location, &scalar) == 0);
        assert(ringl_get_uniform_2i(integer_program, pair_location, pair) == 0);
        assert(ringl_get_uniform_3i(integer_program, triple_location, triple) == 0);
        assert(ringl_get_uniform_4i(integer_program, quad_location, quad) == 0);
        assert(scalar == -3 && pair[0] == 4 && pair[1] == -5 &&
               triple[0] == 6 && triple[1] == -7 && triple[2] == 8 &&
               quad[0] == -9 && quad[1] == 10 && quad[2] == -11 && quad[3] == 12);
        ringl_uniform_2i(scalar_location, 1, 2);
        assert(ringl_get_error() == RINGL_INVALID_OPERATION);
        memset(pair, 0xff, sizeof(pair));
        assert(ringl_get_uniform_2i(integer_peer_program, pair_location, pair) == 0);
        assert(pair[0] == 0 && pair[1] == 0);
    }
    ringl_delete_program(integer_peer_program);
    ringl_delete_program(integer_program);

    /* A scalar bool preserves its WebGL-facing type while the RSH1 branch
     * condition remains a normalized i32. The two programs prove that the
     * program-owned executable does not leak a uniform update across links. */
    bool_vertex = ringl_create_shader(RINGL_VERTEX_SHADER);
    bool_fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);
    bool_program = ringl_create_program();
    bool_peer_program = ringl_create_program();
    assert(bool_vertex != 0u && bool_fragment != 0u && bool_program != 0u &&
           bool_peer_program != 0u);
    ringl_shader_source(bool_vertex,
                        "void main() { gl_Position = vec4(-1.0, -1.0, 0.0, 1.0); }",
                        -1);
    ringl_shader_source(bool_fragment,
                        "precision mediump float; precision highp int; "
                        "uniform bool enabled; void main() { if (enabled) { "
                        "gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); } else { "
                        "gl_FragColor = vec4(0.0, 0.0, 1.0, 1.0); } }",
                        -1);
    ringl_compile_shader(bool_vertex);
    ringl_compile_shader(bool_fragment);
    assert(ringl_get_shader_compile_status(bool_vertex) == RINGL_TRUE);
    assert(ringl_get_shader_compile_status(bool_fragment) == RINGL_TRUE);
    ringl_attach_shader(bool_program, bool_vertex);
    ringl_attach_shader(bool_program, bool_fragment);
    ringl_attach_shader(bool_peer_program, bool_vertex);
    ringl_attach_shader(bool_peer_program, bool_fragment);
    ringl_link_program(bool_program);
    ringl_link_program(bool_peer_program);
    assert(ringl_get_program_link_status(bool_program) == RINGL_TRUE);
    assert(ringl_get_program_link_status(bool_peer_program) == RINGL_TRUE);
    assert(ringl_get_program_info(bool_program, &info) == 0);
    assert(info.active_uniform_count == 1u);
    assert(ringl_get_active_uniform(bool_program, 0u, &active_info) == 0);
    assert(active_info.type == RINGL_BOOL && strcmp(active_info.name, "enabled") == 0);
    {
        int32_t bool_location = ringl_get_uniform_location(bool_program, "enabled");
        int32_t enabled = -1;

        assert(bool_location == 0);
        assert(ringl_get_uniform_1i(bool_program, bool_location, &enabled) == 0);
        assert(enabled == 0);
        ringl_use_program(bool_program);
        ringl_uniform_1i(bool_location, -7);
        assert(ringl_get_error() == RINGL_NO_ERROR);
        assert(ringl_get_uniform_1i(bool_program, bool_location, &enabled) == 0);
        assert(enabled == 1);
        ringl_uniform_1i(bool_location, 0);
        assert(ringl_get_error() == RINGL_NO_ERROR);
        assert(ringl_get_uniform_1i(bool_program, bool_location, &enabled) == 0);
        assert(enabled == 0);
        enabled = -1;
        assert(ringl_get_uniform_1i(bool_peer_program, bool_location, &enabled) == 0);
        assert(enabled == 0);
    }
    ringl_delete_program(bool_peer_program);
    ringl_delete_program(bool_program);

    /* bvec2/3/4 keep their WebGL-visible types while uniform*i updates are
     * normalized componentwise before RinGL regenerates the program-owned
     * RSH1 executable. The peer link verifies no state escapes its program. */
    bvec_vertex = ringl_create_shader(RINGL_VERTEX_SHADER);
    bvec_fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);
    bvec_program = ringl_create_program();
    bvec_peer_program = ringl_create_program();
    assert(bvec_vertex != 0u && bvec_fragment != 0u && bvec_program != 0u &&
           bvec_peer_program != 0u);
    ringl_shader_source(bvec_vertex,
                        "void main() { gl_Position = vec4(-1.0, -1.0, 0.0, 1.0); }",
                        -1);
    ringl_shader_source(bvec_fragment,
                        "precision mediump float; precision highp int; "
                        "uniform bvec2 pair; uniform bvec3 triple; uniform bvec4 quad; "
                        "void main() { bvec3 inverse = not(triple); "
                        "bvec4 difference = notEqual(quad, bvec4(false)); "
                        "bool has_true = any(inverse); "
                        "if ((all(equal(pair, bvec2(true, false))) "
                        "&& !any(notEqual(pair, bvec2(true, false)))) ^^ false || false) { "
                        "gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); } else { "
                        "gl_FragColor = vec4(0.0, 0.0, 1.0, 1.0); } }",
                        -1);
    ringl_compile_shader(bvec_vertex);
    ringl_compile_shader(bvec_fragment);
    assert(ringl_get_shader_compile_status(bvec_vertex) == RINGL_TRUE);
    assert(ringl_get_shader_compile_status(bvec_fragment) == RINGL_TRUE);
    ringl_attach_shader(bvec_program, bvec_vertex);
    ringl_attach_shader(bvec_program, bvec_fragment);
    ringl_attach_shader(bvec_peer_program, bvec_vertex);
    ringl_attach_shader(bvec_peer_program, bvec_fragment);
    ringl_link_program(bvec_program);
    ringl_link_program(bvec_peer_program);
    assert(ringl_get_program_link_status(bvec_program) == RINGL_TRUE);
    assert(ringl_get_program_link_status(bvec_peer_program) == RINGL_TRUE);
    assert(ringl_get_program_info(bvec_program, &info) == 0);
    assert(info.active_uniform_count == 3u);
    assert(ringl_get_active_uniform(bvec_program, 0u, &active_info) == 0);
    assert(active_info.type == RINGL_BOOL_VEC2 && strcmp(active_info.name, "pair") == 0);
    assert(ringl_get_active_uniform(bvec_program, 1u, &active_info) == 0);
    assert(active_info.type == RINGL_BOOL_VEC3 && strcmp(active_info.name, "triple") == 0);
    assert(ringl_get_active_uniform(bvec_program, 2u, &active_info) == 0);
    assert(active_info.type == RINGL_BOOL_VEC4 && strcmp(active_info.name, "quad") == 0);
    {
        int32_t pair_location = ringl_get_uniform_location(bvec_program, "pair");
        int32_t triple_location = ringl_get_uniform_location(bvec_program, "triple");
        int32_t quad_location = ringl_get_uniform_location(bvec_program, "quad");
        int32_t pair[2] = { -1, -1 };
        int32_t triple[3] = { -1, -1, -1 };
        int32_t quad[4] = { -1, -1, -1, -1 };

        assert(pair_location == 0 && triple_location == 1 && quad_location == 2);
        ringl_use_program(bvec_program);
        ringl_uniform_2i(pair_location, -7, 0);
        ringl_uniform_3i(triple_location, 0, 5, -1);
        ringl_uniform_4i(quad_location, 0, -3, 2, 0);
        assert(ringl_get_error() == RINGL_NO_ERROR);
        assert(ringl_get_uniform_2i(bvec_program, pair_location, pair) == 0);
        assert(ringl_get_uniform_3i(bvec_program, triple_location, triple) == 0);
        assert(ringl_get_uniform_4i(bvec_program, quad_location, quad) == 0);
        assert(pair[0] == 1 && pair[1] == 0);
        assert(triple[0] == 0 && triple[1] == 1 && triple[2] == 1);
        assert(quad[0] == 0 && quad[1] == 1 && quad[2] == 1 && quad[3] == 0);
        memset(pair, 0xff, sizeof(pair));
        assert(ringl_get_uniform_2i(bvec_peer_program, pair_location, pair) == 0);
        assert(pair[0] == 0 && pair[1] == 0);
    }
    ringl_delete_program(bvec_peer_program);
    ringl_delete_program(bvec_program);

    /* Source validation admits builtin syntax, while linking owns the exact
     * typed lowering contract. A scalar passed to all() must therefore fail
     * closed before an RSH1 module can be published. */
    bvec_program = ringl_create_program();
    assert(bvec_program != 0u);
    ringl_shader_source(bvec_fragment,
                        "uniform bvec2 pair; void main() { if (all(pair.x)) { "
                        "gl_FragColor = vec4(1.0); } else { "
                        "gl_FragColor = vec4(0.0); } }",
                        -1);
    ringl_compile_shader(bvec_fragment);
    assert(ringl_get_shader_compile_status(bvec_fragment) == RINGL_TRUE);
    ringl_attach_shader(bvec_program, bvec_vertex);
    ringl_attach_shader(bvec_program, bvec_fragment);
    ringl_link_program(bvec_program);
    assert(ringl_get_program_link_status(bvec_program) == RINGL_FALSE);
    ringl_delete_program(bvec_program);

    /* A vertex mat4 is lowered to a real column-major RSH1 matrix/vector
     * multiply. It is program-owned just like the scalar/vector profile. */
    mat4_vertex = ringl_create_shader(RINGL_VERTEX_SHADER);
    mat4_fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);
    mat4_program = ringl_create_program();
    mat4_peer_program = ringl_create_program();
    assert(mat4_vertex != 0u && mat4_fragment != 0u && mat4_program != 0u &&
           mat4_peer_program != 0u);
    ringl_shader_source(mat4_vertex,
                        "attribute vec4 position; uniform mat4 transform; "
                        "void main() { gl_Position = transform * position "
                        "+ vec4(0.0, 0.0, 0.0, 0.0); }",
                        -1);
    ringl_shader_source(mat4_fragment,
                        "void main() { gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0); }",
                        -1);
    ringl_compile_shader(mat4_vertex);
    ringl_compile_shader(mat4_fragment);
    assert(ringl_get_shader_compile_status(mat4_vertex) == RINGL_TRUE);
    assert(ringl_get_shader_compile_status(mat4_fragment) == RINGL_TRUE);
    ringl_attach_shader(mat4_program, mat4_vertex);
    ringl_attach_shader(mat4_program, mat4_fragment);
    ringl_attach_shader(mat4_peer_program, mat4_vertex);
    ringl_attach_shader(mat4_peer_program, mat4_fragment);
    ringl_link_program(mat4_program);
    ringl_link_program(mat4_peer_program);
    assert(ringl_get_program_link_status(mat4_program) == RINGL_TRUE);
    assert(ringl_get_program_link_status(mat4_peer_program) == RINGL_TRUE);
    assert(ringl_get_program_info(mat4_program, &info) == 0);
    assert(info.active_uniform_count == 1u);
    location = ringl_get_uniform_location(mat4_program, "transform");
    assert(location == 0);
    assert(ringl_get_active_uniform(mat4_program, 0u, &active_info) == 0);
    assert(active_info.type == RINGL_FLOAT_MAT4 &&
           strcmp(active_info.name, "transform") == 0);
    {
        float values[16];
        float identity[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        };
        uint32_t index;

        memset(values, 0xff, sizeof(values));
        assert(ringl_get_uniform_matrix4f(mat4_program, location, values) == 0);
        for (index = 0u; index < 16u; ++index)
            assert(values[index] == 0.0f);
        ringl_use_program(mat4_program);
        ringl_uniform_matrix4fv(location, 0u, identity);
        assert(ringl_get_error() == RINGL_NO_ERROR);
        assert(ringl_get_uniform_matrix4f(mat4_program, location, values) == 0);
        assert(memcmp(values, identity, sizeof(values)) == 0);
        identity[5] = NAN;
        ringl_uniform_matrix4fv(location, 0u, identity);
        assert(ringl_get_error() == RINGL_INVALID_VALUE);
        assert(ringl_get_uniform_matrix4f(mat4_program, location, values) == 0);
        assert(values[5] == 1.0f);
        identity[5] = 1.0f;
        ringl_uniform_matrix4fv(location, 1u, identity);
        assert(ringl_get_error() == RINGL_INVALID_VALUE);
        assert(ringl_get_uniform_matrix4f(mat4_program, location, values) == 0);
        assert(memcmp(values, identity, sizeof(values)) == 0);
        ringl_uniform_4f(location, 1.0f, 1.0f, 1.0f, 1.0f);
        assert(ringl_get_error() == RINGL_INVALID_OPERATION);
        memset(values, 0xff, sizeof(values));
        assert(ringl_get_uniform_matrix4f(mat4_peer_program, location, values) == 0);
        for (index = 0u; index < 16u; ++index)
            assert(values[index] == 0.0f);
    }
    ringl_delete_program(mat4_peer_program);
    ringl_delete_program(mat4_program);

    /* Mat2/3 use the same program-owned RSH1 uniform path as mat4, but each
     * multiplication has its real GLSL vector width and column-major layout. */
    matrix_vertex = ringl_create_shader(RINGL_VERTEX_SHADER);
    matrix_fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);
    matrix_program = ringl_create_program();
    assert(matrix_vertex != 0u && matrix_fragment != 0u && matrix_program != 0u);
    ringl_shader_source(
        matrix_vertex,
        "attribute vec2 position; uniform mat2 transform2; uniform mat3 transform3; "
        "void main() { vec2 rotated = transform2 * position; "
        "vec3 translated = transform3 * vec3(rotated, 1.0); "
        "gl_Position = vec4(translated, 1.0); }",
        -1);
    ringl_shader_source(
        matrix_fragment,
        "void main() { gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0); }", -1);
    ringl_compile_shader(matrix_vertex);
    ringl_compile_shader(matrix_fragment);
    assert(ringl_get_shader_compile_status(matrix_vertex) == RINGL_TRUE);
    assert(ringl_get_shader_compile_status(matrix_fragment) == RINGL_TRUE);
    ringl_attach_shader(matrix_program, matrix_vertex);
    ringl_attach_shader(matrix_program, matrix_fragment);
    ringl_link_program(matrix_program);
    assert(ringl_get_program_link_status(matrix_program) == RINGL_TRUE);
    assert(ringl_get_program_info(matrix_program, &info) == 0);
    assert(info.active_uniform_count == 2u);
    {
        int32_t matrix2_location =
            ringl_get_uniform_location(matrix_program, "transform2");
        int32_t matrix3_location =
            ringl_get_uniform_location(matrix_program, "transform3");
        float matrix2[4] = { 2.0f, 0.0f, 0.0f, 3.0f };
        float matrix3[9] = {
            1.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f,
            0.5f, -0.25f, 1.0f,
        };
        float read2[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
        float read3[9] = { 0.0f };

        assert(matrix2_location == 0 && matrix3_location == 1);
        assert(ringl_get_active_uniform(matrix_program, 0u, &active_info) == 0);
        assert(active_info.type == RINGL_FLOAT_MAT2 &&
               strcmp(active_info.name, "transform2") == 0);
        assert(ringl_get_active_uniform(matrix_program, 1u, &active_info) == 0);
        assert(active_info.type == RINGL_FLOAT_MAT3 &&
               strcmp(active_info.name, "transform3") == 0);
        assert(ringl_get_uniform_matrix2f(matrix_program, matrix2_location,
                                           read2) == 0);
        assert(ringl_get_uniform_matrix3f(matrix_program, matrix3_location,
                                           read3) == 0);
        assert(read2[0] == 0.0f && read2[3] == 0.0f && read3[8] == 0.0f);
        ringl_use_program(matrix_program);
        ringl_uniform_matrix2fv(matrix2_location, RINGL_FALSE, matrix2);
        ringl_uniform_matrix3fv(matrix3_location, RINGL_FALSE, matrix3);
        assert(ringl_get_error() == RINGL_NO_ERROR);
        assert(ringl_get_uniform_matrix2f(matrix_program, matrix2_location,
                                           read2) == 0);
        assert(ringl_get_uniform_matrix3f(matrix_program, matrix3_location,
                                           read3) == 0);
        assert(memcmp(read2, matrix2, sizeof(read2)) == 0);
        assert(memcmp(read3, matrix3, sizeof(read3)) == 0);
        matrix3[4] = NAN;
        ringl_uniform_matrix3fv(matrix3_location, RINGL_FALSE, matrix3);
        assert(ringl_get_error() == RINGL_INVALID_VALUE);
        assert(ringl_get_uniform_matrix3f(matrix_program, matrix3_location,
                                           read3) == 0);
        assert(read3[4] == 1.0f);
        matrix3[4] = 1.0f;
        ringl_uniform_matrix2fv(matrix3_location, RINGL_FALSE, matrix2);
        assert(ringl_get_error() == RINGL_INVALID_OPERATION);
        ringl_uniform_matrix3fv(matrix3_location, RINGL_TRUE, matrix3);
        assert(ringl_get_error() == RINGL_INVALID_VALUE);
    }
    ringl_delete_program(matrix_program);
    /* A current program is retained after deletion until the binding is
     * explicitly replaced, matching the GLES object lifetime rule. */
    ringl_use_program(0u);

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
    assert(ringl_get_attached_shaders(retained_program, attached_shaders, 2u,
                                      &attached_shader_count) == 0);
    assert(attached_shader_count == 2u &&
           attached_shaders[0] == retained_vertex &&
           attached_shaders[1] == retained_fragment);
    ringl_compile_shader(retained_vertex);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    ringl_link_program(retained_program);
    assert(ringl_get_program_link_status(retained_program) == RINGL_TRUE);
    ringl_attach_shader(retained_program, retained_vertex);
    assert(ringl_get_error() == RINGL_INVALID_VALUE);
    ringl_detach_shader(retained_program, retained_vertex);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    assert(ringl_get_attached_shaders(retained_program, attached_shaders, 2u,
                                      &attached_shader_count) == 0);
    assert(attached_shader_count == 1u &&
           attached_shaders[0] == retained_fragment);
    assert(ringl_get_program_link_status(retained_program) == RINGL_TRUE);
    ringl_use_program(retained_program);
    assert(ringl_get_error() == RINGL_NO_ERROR);
    ringl_detach_shader(retained_program, retained_vertex);
    assert(ringl_get_error() == RINGL_INVALID_OPERATION);

    ringl_delete_shader(retained_fragment);
    {
        int32_t delete_status = -1;

        ringl_use_program(retained_program);
        assert(ringl_get_program_parameteriv_bounded(
                   retained_program, RINGL_DELETE_STATUS, &delete_status,
                   1u) == 0);
        assert(delete_status == (int32_t)RINGL_FALSE);
        ringl_delete_program(retained_program);
        assert(!ringl_is_program(retained_program));
        assert(ringl_get_current_program() == retained_program);
        delete_status = -1;
        assert(ringl_get_program_parameteriv_bounded(
                   retained_program, RINGL_DELETE_STATUS, &delete_status,
                   1u) == 0);
        assert(delete_status == (int32_t)RINGL_TRUE);
        ringl_use_program(0u);
        assert(ringl_get_current_program() == 0u);
        delete_status = 0x5a5a;
        assert(ringl_get_program_parameteriv_bounded(
                   retained_program, RINGL_DELETE_STATUS, &delete_status,
                   1u) == -1);
        assert(delete_status == 0x5a5a);
        assert(ringl_get_error() == RINGL_INVALID_VALUE);
    }

    ringl_context_destroy(context);
    return 0;
}
