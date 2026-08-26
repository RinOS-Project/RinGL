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

    ringl_shader_source(vertex,
        "attribute vec2 position; attribute vec4 color; varying vec4 vertexColor;\n"
        "void main() { gl_Position = vec4(position, 0.0, 1.0); vertexColor = color; }\n",
        -1);
    ringl_compile_shader(vertex);
    assert(ringl_get_shader_compile_status(vertex) == RINGL_TRUE);
    ringl_shader_source(fragment,
        "varying vec4 vertexColor;\n"
        "void main() { gl_FragColor = vertexColor.stpq.bgra; }\n", -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_TRUE);

    ringl_shader_source(fragment,
        "uniform sampler2D colorTexture;\n"
        "void main() { gl_FragColor = texture2D(colorTexture, vec2(0.25)); }\n",
        -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_TRUE);

    ringl_shader_source(vertex,
        "attribute vec2 position; attribute vec2 colorRG; attribute vec2 colorBA; "
        "varying vec2 vertexRG; varying vec2 vertexBA;\n"
        "void main() { gl_Position = vec4(position, 0.0, 1.0); "
        "vertexRG = colorRG; vertexBA = colorBA; }\n", -1);
    ringl_compile_shader(vertex);
    assert(ringl_get_shader_compile_status(vertex) == RINGL_TRUE);
    ringl_shader_source(fragment,
        "varying vec2 vertexRG; varying vec2 vertexBA;\n"
        "void main() { gl_FragColor = vec4(vertexRG, vertexBA); }\n", -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_TRUE);

    ringl_shader_source(vertex,
        "attribute vec2 position; attribute vec3 color; varying vec3 vertexColor;\n"
        "void main() { gl_Position = vec4(position, 0.0, 1.0); vertexColor = color; }\n",
        -1);
    ringl_compile_shader(vertex);
    assert(ringl_get_shader_compile_status(vertex) == RINGL_TRUE);
    ringl_shader_source(fragment,
        "varying vec3 vertexColor;\n"
        "void main() { gl_FragColor = vec4(vertexColor.bgr, 1.0); }\n", -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_TRUE);

    ringl_shader_source(vertex,
        "attribute vec2 position; attribute vec2 texCoord; varying vec2 uv;\n"
        "void main() { gl_Position = vec4(position, 0.0, 1.0); uv = texCoord; }\n",
        -1);
    ringl_compile_shader(vertex);
    assert(ringl_get_shader_compile_status(vertex) == RINGL_TRUE);
    assert(ringl_get_shader_info_log(vertex, log, sizeof(log)) == 0u);

    ringl_shader_source(fragment,
        "uniform sampler2D colorTexture; varying vec2 uv;\n"
        "void main() { gl_FragColor = texture2D(colorTexture, uv); }\n", -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_TRUE);
    assert(ringl_get_shader_info_log(fragment, log, sizeof(log)) == 0u);

    /* A texture coordinate remains vec2 after a same-width read swizzle.
     * Chained selectors exercise the exact parser contract used by the RSH1
     * coordinate-register permutation. */
    ringl_shader_source(fragment,
        "uniform sampler2D colorTexture; varying vec2 uv;\n"
        "void main() { gl_FragColor = texture2D(colorTexture, uv.yx.st); }\n",
        -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_TRUE);
    assert(ringl_get_shader_info_log(fragment, log, sizeof(log)) == 0u);

    ringl_shader_source(fragment,
        "uniform sampler2D colorTexture; varying vec2 uv;\n"
        "void main() { gl_FragColor = texture2D(colorTexture, uv.x); }\n",
        -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_FALSE);
    assert(ringl_get_shader_info_log(fragment, log, sizeof(log)) > 0u);
    assert(strstr(log, "vec2") != NULL);

    ringl_shader_source(fragment,
        "varying vec2 uv; void main() { uv = vec2(0.0, 1.0); gl_FragColor = 1.0; }",
        -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_FALSE);
    assert(ringl_get_shader_info_log(fragment, log, sizeof(log)) > 0u);
    assert(strstr(log, "read-only") != NULL);

    /* A scalar float varying consumes one real native interpolant slot. It is
     * not coerced into a vec2 profile or rejected before linker validation. */
    ringl_shader_source(fragment,
        "varying float intensity; void main() { gl_FragColor = intensity; }",
        -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_TRUE);
    assert(ringl_get_shader_info_log(fragment, log, sizeof(log)) == 0u);

    ringl_shader_source(fragment,
        "varying int invalid; void main() { gl_FragColor = 1.0; }", -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_FALSE);
    assert(ringl_get_shader_info_log(fragment, log, sizeof(log)) > 0u);
    assert(strstr(log, "varying float") != NULL);

    ringl_shader_source(fragment,
        "#extension GL_OES_standard_derivatives : enable\n"
        "varying vec2 uv;\n"
        "void main() { gl_FragColor = vec4(dFdx(uv), fwidth(uv.x), 1.0); }\n",
        -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_FALSE);
    assert(ringl_get_shader_info_log(fragment, log, sizeof(log)) > 0u);
    assert(strstr(log, "not enabled") != NULL);
    assert(ringl_enable_webgl_standard_derivatives() == 0);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_TRUE);
    assert(ringl_get_shader_info_log(fragment, log, sizeof(log)) == 0u);

    ringl_shader_source(fragment,
        "varying vec2 uv;\n"
        "void main() { gl_FragColor = vec4(dFdy(uv), 0.0, 1.0); }\n", -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_FALSE);
    assert(ringl_get_shader_info_log(fragment, log, sizeof(log)) > 0u);
    assert(strstr(log, "require GL_OES_standard_derivatives") != NULL);

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
        "uniform bool enabled; void main() { if (enabled) { "
        "gl_FragColor = vec4(1.0); } else { gl_FragColor = vec4(0.0); } }", -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_TRUE);
    assert(ringl_get_shader_info_log(fragment, log, sizeof(log)) == 0u);

    /* Boolean vectors share the typed uniform path with scalar bool and can
     * use the GLSL Boolean vector builtins. Arrays remain outside the bounded
     * profile and must fail during source validation. */
    ringl_shader_source(fragment,
        "uniform bvec2 enabled; void main() { if ((all(equal(enabled, bvec2(true, false))) "
        "&& !any(notEqual(enabled, bvec2(true, false)))) ^^ false || false) { "
        "gl_FragColor = vec4(1.0); } else { gl_FragColor = vec4(0.0); } }", -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_TRUE);
    assert(ringl_get_shader_info_log(fragment, log, sizeof(log)) == 0u);

    ringl_shader_source(fragment,
        "uniform bool enabled; void main() { if (enabled & true) { "
        "gl_FragColor = vec4(1.0); } else { gl_FragColor = vec4(0.0); } }", -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_FALSE);
    assert(ringl_get_shader_info_log(fragment, log, sizeof(log)) > 0u);

    ringl_shader_source(fragment,
        "uniform bvec2 invalid[2]; void main() { gl_FragColor = vec4(1.0); }", -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_FALSE);
    assert(ringl_get_shader_info_log(fragment, log, sizeof(log)) > 0u);

    ringl_shader_source(fragment,
        "uniform int scalar; uniform ivec2 pair; uniform ivec3 triple; "
        "uniform ivec4 quad; void main() { "
        "int total = scalar + pair.x + triple.y + quad.w; "
        "gl_FragColor = vec4(float(total), float(pair.y), "
        "float(triple.z), float(quad.x)); }", -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_TRUE);
    assert(ringl_get_shader_info_log(fragment, log, sizeof(log)) == 0u);

    /* Full read swizzles are ordinary source-language vector values. The
     * executable lowerer checks source width; this parser coverage protects
     * the preceding compile admission from rejecting legal selector families
     * before they reach it. */
    ringl_shader_source(fragment,
        "void main() { vec4 color = vec4(0.125, 0.25, 0.75, 1.0); "
        "gl_FragColor = color.stpq.bgra + 0.0; }", -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_TRUE);
    assert(ringl_get_shader_info_log(fragment, log, sizeof(log)) == 0u);

    ringl_shader_source(fragment,
        "void main() { vec4 color = vec4(0.0); gl_FragColor = color.rgxy; }",
        -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_FALSE);
    assert(ringl_get_shader_info_log(fragment, log, sizeof(log)) > 0u);
    assert(strstr(log, "mixed") != NULL);

    ringl_shader_source(fragment,
        "void main() { vec2 uv = vec2(0.0); gl_FragColor = vec4(uv.z); }",
        -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_FALSE);
    assert(ringl_get_shader_info_log(fragment, log, sizeof(log)) > 0u);
    assert(strstr(log, "outside") != NULL);

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
