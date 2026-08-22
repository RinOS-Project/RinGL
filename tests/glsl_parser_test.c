/* SPDX-License-Identifier: MIT */
#include <ringl/ringl.h>

#include <assert.h>
#include <string.h>

static RinGLContext* make_context(void)
{
    RinGLContext* context = NULL;
    RinGLContextDescV1 desc = {
        .struct_size = sizeof(desc),
        .api_version = RINGL_API_VERSION,
    };
    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_make_current(context) == 0);
    return context;
}

int main(void)
{
    RinGLContext* context = make_context();
    uint32_t vertex = ringl_create_shader(RINGL_VERTEX_SHADER);
    uint32_t fragment = ringl_create_shader(RINGL_FRAGMENT_SHADER);
    char log[192];

    ringl_shader_source(vertex,
        "attribute float position;\n"
        "void main() { float x = position * 2.0; gl_Position = x; }\n", -1);
    ringl_compile_shader(vertex);
    assert(ringl_get_shader_compile_status(vertex) == RINGL_TRUE);
    assert(ringl_get_shader_info_log(vertex, log, sizeof(log)) == 0u);

    ringl_shader_source(fragment,
        "uniform sampler2D colorTexture;\n"
        "void main() { float c = 1.0; gl_FragColor = c; }\n", -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_TRUE);

    ringl_shader_source(fragment,
        "uniform sampler2D colorTexture;\n"
        "void main() { gl_FragColor = texture2D(colorTexture, vec2(0.25, 0.75)); }\n",
        -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_TRUE);
    assert(ringl_get_shader_info_log(fragment, log, sizeof(log)) == 0u);

    ringl_shader_source(fragment,
        "uniform sampler2D colorTexture;\n"
        "void main() { gl_FragColor = texture2D(missing, vec2(0.0, 1.0)); }\n",
        -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_FALSE);
    assert(ringl_get_shader_info_log(fragment, log, sizeof(log)) > 0u);
    assert(strstr(log, "sampler2D") != NULL);

    ringl_shader_source(fragment,
        "uniform sampler2D colorTexture;\n"
        "void main() { gl_FragColor = texture2D(colorTexture, 0.5); }\n", -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_FALSE);
    assert(ringl_get_shader_info_log(fragment, log, sizeof(log)) > 0u);
    assert(strstr(log, "vec2") != NULL);

    ringl_shader_source(vertex,
        "uniform sampler2D colorTexture;\n"
        "void main() { gl_Position = texture2D(colorTexture, vec2(0.0, 0.0)); }\n",
        -1);
    ringl_compile_shader(vertex);
    assert(ringl_get_shader_compile_status(vertex) == RINGL_FALSE);
    assert(ringl_get_shader_info_log(vertex, log, sizeof(log)) > 0u);
    assert(strstr(log, "fragment") != NULL);

    ringl_shader_source(fragment,
        "uniform float invalid; void main() { gl_FragColor = 1.0; }", -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_FALSE);
    assert(ringl_get_shader_info_log(fragment, log, sizeof(log)) > 0u);
    assert(strstr(log, "sampler2D") != NULL);

    ringl_shader_source(fragment,
        "attribute float invalid; void main() { gl_FragColor = invalid; }", -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_FALSE);
    assert(ringl_get_shader_info_log(fragment, log, sizeof(log)) > 0u);
    assert(strstr(log, "attribute") != NULL);

    ringl_shader_source(vertex,
        "void main() { gl_Position = missing + 1.0; }", -1);
    ringl_compile_shader(vertex);
    assert(ringl_get_shader_compile_status(vertex) == RINGL_FALSE);
    assert(ringl_get_shader_info_log(vertex, log, sizeof(log)) > 0u);
    assert(strstr(log, "undeclared") != NULL);

    ringl_shader_source(vertex,
        "void main() { float x = 1.0; } void main() { float y = 2.0; }", -1);
    ringl_compile_shader(vertex);
    assert(ringl_get_shader_compile_status(vertex) == RINGL_FALSE);
    assert(ringl_get_shader_info_log(vertex, log, sizeof(log)) > 0u);
    assert(strstr(log, "duplicate main") != NULL);

    ringl_context_destroy(context);
    return 0;
}
