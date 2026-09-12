/* SPDX-License-Identifier: MIT */
#include <ringl/ringl.h>

#include "shader/glsl_lower.h"
#include "shader/rsh1_abi.h"

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

    /* GLSL block comments are lexical whitespace for both the generic
     * lowerer and the compact texture profile.  Keep a malformed comment
     * failure-atomic: it must not publish a stale executable from the
     * previous source. */
    ringl_shader_source(fragment,
        "/* header */ uniform sampler2D colorTexture; /* body */\n"
        "void main() { gl_FragColor = /* coordinate */\n"
        "texture2D(colorTexture, vec2(0.25, 0.75)); } /* tail */\n", -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_TRUE);
    assert(ringl_get_shader_info_log(fragment, log, sizeof(log)) == 0u);

    ringl_shader_source(fragment,
        "uniform sampler2D colorTexture; void main() { /* unterminated",
        -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_FALSE);
    assert(ringl_get_shader_info_log(fragment, log, sizeof(log)) > 0u);

    /* EXT_shader_texture_lod is an empty WebGL object, but its GLSL builtin
     * remains unavailable until the current RinGL context has been enabled.
     * The emitted RSH1 op carries a live Float32 LOD register and real
     * resource pair instead of treating explicit mip selection as a host
     * shortcut. */
    {
        static const char lod_source[] =
            "#extension GL_EXT_shader_texture_lod : enable\n"
            "uniform sampler2D colorTexture; uniform float lod; varying vec2 uv;\n"
            "void main() { gl_FragColor = "
            "texture2DLodEXT(colorTexture, uv, lod); }\n";
        static const char projected_lod_source[] =
            "#extension GL_EXT_shader_texture_lod : require\n"
            "uniform sampler2D colorTexture; void main() { gl_FragColor = "
            "texture2DProjLodEXT(colorTexture, vec3(0.25, 0.75, 0.5), "
            "1.0); }\n";
        static const char gradient_source[] =
            "#extension GL_EXT_shader_texture_lod : enable\n"
            "uniform sampler2D colorTexture; void main() { gl_FragColor = "
            "texture2DGradEXT(colorTexture, vec2(0.5), vec2(0.5, 0.0), "
            "vec2(0.0, 0.5)); }\n";
        static const char projected_gradient_source[] =
            "#extension GL_EXT_shader_texture_lod : enable\n"
            "uniform sampler2D colorTexture; void main() { gl_FragColor = "
            "texture2DProjGradEXT(colorTexture, vec3(0.25, 0.75, 0.5), "
            "vec2(0.5, 0.0), vec2(0.0, 0.5)); }\n";
        RinGLGlslUniformValue lod_uniform = {
            .name = "lod", .type = RINGL_FLOAT, .values = { 1.0f },
        };
        RinGLGlslLowerResult lowered;
        RinGLRsh1HeaderV1 header;
        const RinGLRsh1InstructionV1* instructions;
        uint32_t lod_samples = 0u;
        uint32_t projected_divisions = 0u;
        int saw_lod_register = 0;

        ringl_shader_source(fragment, lod_source, -1);
        ringl_compile_shader(fragment);
        assert(ringl_get_shader_compile_status(fragment) == RINGL_FALSE);
        assert(ringl_get_shader_info_log(fragment, log, sizeof(log)) > 0u);
        assert(strstr(log, "GL_EXT_shader_texture_lod") != NULL);
        assert(ringl_enable_webgl_shader_texture_lod() == 0);
        ringl_compile_shader(fragment);
        assert(ringl_get_shader_compile_status(fragment) == RINGL_TRUE);
        assert(ringl_glsl_lower_rsh1_with_uniforms(
                   RINGL_FRAGMENT_SHADER, lod_source,
                   sizeof(lod_source) - 1u, &lod_uniform, 1u,
                   &lowered) == 0);
        assert(lowered.ok != 0u && lowered.sampler_binding_count == 1u);
        memcpy(&header, lowered.bytes, sizeof(header));
        instructions = (const RinGLRsh1InstructionV1*)(
            lowered.bytes + header.header_size);
        for (uint32_t index = 0u; index < header.instruction_count; ++index) {
            if (instructions[index].opcode !=
                RINGL_RSH1_OP_SAMPLE_IMAGE_2D_LOD_F32) {
                continue;
            }
            assert(instructions[index].resource ==
                   RINGL_RSH1_SAMPLE_2D_LOD_PACK_BINDINGS(0u, 1u));
            assert(instructions[index].immediate < header.register_count);
            for (uint32_t prior = 0u; prior < index; ++prior) {
                if (instructions[prior].destination ==
                    instructions[index].immediate) {
                    assert(instructions[prior].opcode ==
                           RINGL_RSH1_OP_CONST_F32);
                    assert(instructions[prior].immediate ==
                           UINT32_C(0x3f800000));
                    saw_lod_register = 1;
                }
            }
            ++lod_samples;
        }
        assert(lod_samples == 4u && saw_lod_register != 0);
        ringl_shader_source(fragment, projected_lod_source, -1);
        ringl_compile_shader(fragment);
        assert(ringl_get_shader_compile_status(fragment) == RINGL_TRUE);
        assert(ringl_glsl_lower_rsh1(RINGL_FRAGMENT_SHADER,
                                     projected_lod_source,
                                     sizeof(projected_lod_source) - 1u,
                                     &lowered) == 0);
        assert(lowered.ok != 0u && lowered.sampler_binding_count == 1u);
        memcpy(&header, lowered.bytes, sizeof(header));
        instructions = (const RinGLRsh1InstructionV1*)(
            lowered.bytes + header.header_size);
        lod_samples = 0u;
        for (uint32_t index = 0u; index < header.instruction_count; ++index) {
            if (instructions[index].opcode == RINGL_RSH1_OP_DIV_F32)
                ++projected_divisions;
            if (instructions[index].opcode ==
                RINGL_RSH1_OP_SAMPLE_IMAGE_2D_LOD_F32) {
                assert(instructions[index].resource ==
                       RINGL_RSH1_SAMPLE_2D_LOD_PACK_BINDINGS(0u, 1u));
                ++lod_samples;
            }
        }
        assert(lod_samples == 4u && projected_divisions == 2u);
        ringl_shader_source(fragment, gradient_source, -1);
        ringl_compile_shader(fragment);
        assert(ringl_get_shader_compile_status(fragment) == RINGL_TRUE);
        assert(ringl_glsl_lower_rsh1(RINGL_FRAGMENT_SHADER, gradient_source,
                                     sizeof(gradient_source) - 1u,
                                     &lowered) == 0);
        assert(lowered.ok != 0u && lowered.sampler_binding_count == 1u);
        memcpy(&header, lowered.bytes, sizeof(header));
        instructions = (const RinGLRsh1InstructionV1*)(
            lowered.bytes + header.header_size);
        lod_samples = 0u;
        for (uint32_t index = 0u; index < header.instruction_count; ++index) {
            if (instructions[index].opcode ==
                RINGL_RSH1_OP_SAMPLE_IMAGE_2D_GRAD_F32) {
                assert(instructions[index].resource ==
                       RINGL_RSH1_SAMPLE_2D_GRAD_PACK_BINDINGS(0u, 1u));
                assert(instructions[index].immediate + 3u <
                       header.register_count);
                ++lod_samples;
            }
        }
        assert(lod_samples == 4u);
        assert(ringl_glsl_lower_rsh1(
                   RINGL_FRAGMENT_SHADER, projected_gradient_source,
                   sizeof(projected_gradient_source) - 1u, &lowered) == 0);
        assert(lowered.ok != 0u && lowered.sampler_binding_count == 1u);
    }

    /* The ordinary GLSL ES texture2D overload keeps implicit derivatives and
     * carries its third Float argument as a live RSH1 bias register. */
    {
        static const char bias_source[] =
            "uniform sampler2D colorTexture; uniform float bias; varying vec2 uv;\n"
            "void main() { gl_FragColor = texture2D(colorTexture, uv, bias); }\n";
        static const char invalid_bias_source[] =
            "uniform sampler2D colorTexture; void main() { gl_FragColor = "
            "texture2D(colorTexture, vec2(0.5), vec2(1.0)); }";
        static const char projected_bias_source[] =
            "uniform sampler2D colorTexture; uniform float q; uniform float bias; "
            "void main() { gl_FragColor = texture2DProj(colorTexture, "
            "vec3(0.25, 0.75, q), bias); }";
        RinGLGlslUniformValue bias_uniform = {
            .name = "bias", .type = RINGL_FLOAT, .values = { 1.0f },
        };
        RinGLGlslLowerResult lowered;
        RinGLRsh1HeaderV1 header;
        const RinGLRsh1InstructionV1* instructions;
        uint32_t bias_samples = 0u;
        int saw_bias_register = 0;

        ringl_shader_source(fragment, bias_source, -1);
        ringl_compile_shader(fragment);
        assert(ringl_get_shader_compile_status(fragment) == RINGL_TRUE);
        assert(ringl_glsl_lower_rsh1_with_uniforms(
                   RINGL_FRAGMENT_SHADER, bias_source,
                   sizeof(bias_source) - 1u, &bias_uniform, 1u,
                   &lowered) == 0);
        assert(lowered.ok != 0u && lowered.sampler_binding_count == 1u);
        memcpy(&header, lowered.bytes, sizeof(header));
        instructions = (const RinGLRsh1InstructionV1*)(
            lowered.bytes + header.header_size);
        for (uint32_t index = 0u; index < header.instruction_count; ++index) {
            if (instructions[index].opcode !=
                RINGL_RSH1_OP_SAMPLE_IMAGE_2D_BIAS_F32) {
                continue;
            }
            assert(instructions[index].resource ==
                   RINGL_RSH1_SAMPLE_2D_BIAS_PACK_BINDINGS(0u, 1u));
            assert(instructions[index].immediate < header.register_count);
            for (uint32_t prior = 0u; prior < index; ++prior) {
                if (instructions[prior].destination ==
                    instructions[index].immediate) {
                    assert(instructions[prior].opcode ==
                           RINGL_RSH1_OP_CONST_F32);
                    assert(instructions[prior].immediate ==
                           UINT32_C(0x3f800000));
                    saw_bias_register = 1;
                }
            }
            ++bias_samples;
        }
        assert(bias_samples == 4u && saw_bias_register != 0);
        assert(ringl_glsl_lower_rsh1(RINGL_FRAGMENT_SHADER,
                                     projected_bias_source,
                                     sizeof(projected_bias_source) - 1u,
                                     &lowered) == 0);
        assert(lowered.ok != 0u && lowered.sampler_binding_count == 1u);
        assert(ringl_glsl_lower_rsh1(RINGL_FRAGMENT_SHADER,
                                     invalid_bias_source,
                                     sizeof(invalid_bias_source) - 1u,
                                     &lowered) != 0);
        assert(strstr(lowered.diagnostic, "bias must") != NULL);
    }

    /* GLSL ES 1 projective sampling divides xy by the final homogeneous
     * coordinate in RSH1 before the normal real image/sampler lookup. The
     * divisor remains a live uniform register, so this is not a parser-time
     * coordinate shortcut. */
    {
        static const char projected_source[] =
            "uniform sampler2D colorTexture; uniform float q; varying vec2 uv;\n"
            "void main() { gl_FragColor = "
            "texture2DProj(colorTexture, vec3(uv, q)); }\n";
        static const char zero_projected_source[] =
            "uniform sampler2D colorTexture; void main() { gl_FragColor = "
            "texture2DProj(colorTexture, vec3(0.25, 0.75, 0.0)); }";
        static const char vec4_projected_source[] =
            "uniform sampler2D colorTexture; void main() { gl_FragColor = "
            "texture2DProj(colorTexture, vec4(0.25, 0.75, 9.0, 2.0)); }";
        static const char invalid_projected_source[] =
            "uniform sampler2D colorTexture; void main() { gl_FragColor = "
            "texture2DProj(colorTexture, vec2(0.25, 0.75)); }";
        RinGLGlslUniformValue q_uniform = {
            .name = "q", .type = RINGL_FLOAT, .values = { 0.5f },
        };
        RinGLGlslLowerResult lowered;
        RinGLRsh1HeaderV1 header;
        const RinGLRsh1InstructionV1* instructions;
        uint32_t divisions = 0u;
        uint32_t samples = 0u;
        int saw_q_register = 0;

        ringl_shader_source(fragment, projected_source, -1);
        ringl_compile_shader(fragment);
        assert(ringl_get_shader_compile_status(fragment) == RINGL_TRUE);
        assert(ringl_glsl_lower_rsh1_with_uniforms(
                   RINGL_FRAGMENT_SHADER, projected_source,
                   sizeof(projected_source) - 1u, &q_uniform, 1u,
                   &lowered) == 0);
        assert(lowered.ok != 0u && lowered.sampler_binding_count == 1u);
        memcpy(&header, lowered.bytes, sizeof(header));
        instructions = (const RinGLRsh1InstructionV1*)(
            lowered.bytes + header.header_size);
        for (uint32_t index = 0u; index < header.instruction_count; ++index) {
            if (instructions[index].opcode == RINGL_RSH1_OP_DIV_F32) {
                assert(instructions[index].source1 < header.register_count);
                ++divisions;
            } else if (instructions[index].opcode ==
                       RINGL_RSH1_OP_SAMPLE_IMAGE_2D_F32) {
                assert(instructions[index].resource == 0u);
                assert(instructions[index].immediate == 1u);
                ++samples;
            }
            if (instructions[index].opcode == RINGL_RSH1_OP_CONST_F32 &&
                instructions[index].immediate == UINT32_C(0x3f000000)) {
                saw_q_register = 1;
            }
        }
        assert(divisions == 2u && samples == 4u && saw_q_register != 0);
        assert(ringl_glsl_lower_rsh1(RINGL_FRAGMENT_SHADER,
                                     vec4_projected_source,
                                     sizeof(vec4_projected_source) - 1u,
                                     &lowered) == 0);
        assert(lowered.ok != 0u && lowered.sampler_binding_count == 1u);

        ringl_shader_source(fragment, zero_projected_source, -1);
        ringl_compile_shader(fragment);
        assert(ringl_get_shader_compile_status(fragment) == RINGL_TRUE);
        assert(ringl_glsl_lower_rsh1(RINGL_FRAGMENT_SHADER,
                                     zero_projected_source,
                                     sizeof(zero_projected_source) - 1u,
                                     &lowered) != 0);
        assert(strstr(lowered.diagnostic, "zero literal") != NULL);
        ringl_shader_source(fragment, invalid_projected_source, -1);
        ringl_compile_shader(fragment);
        assert(ringl_get_shader_compile_status(fragment) == RINGL_TRUE);
        assert(ringl_glsl_lower_rsh1(RINGL_FRAGMENT_SHADER,
                                     invalid_projected_source,
                                     sizeof(invalid_projected_source) - 1u,
                                     &lowered) != 0);
        assert(strstr(lowered.diagnostic, "vec3 or vec4") != NULL);
    }

    /* Numeric arrays stay in the generic RSH1 path. Alongside decimal
     * literals, a const int initialized with an integer literal selects the
     * program-owned element constants at compile time. */
    {
        static const char float_array_source[] =
            "uniform float gains[2]; void main() { "
            "gl_FragColor = vec4(gains[1], gains[0], 0.0, 1.0); }";
        static const char const_index_float_array_source[] =
            "const int selected = 1; uniform float gains[2]; void main() { "
            "gl_FragColor = vec4(gains[selected], gains[0], 0.0, 1.0); }";
        static const char local_const_index_float_array_source[] =
            "uniform float gains[2]; void main() { const int selected = 1; "
            "gl_FragColor = vec4(gains[selected], gains[0], 0.0, 1.0); }";
        static const char dynamic_float_array_source[] =
            "uniform float gains[2]; uniform int index; void main() { "
            "gl_FragColor = vec4(gains[index]); }";
        static const char negative_const_index_source[] =
            "const int selected = -1; uniform float gains[2]; void main() { "
            "gl_FragColor = vec4(gains[selected]); }";
        static const char mutable_const_index_source[] =
            "const int selected = 1; uniform float gains[2]; void main() { "
            "selected = 0; gl_FragColor = vec4(gains[selected]); }";
        RinGLGlslUniformValue uniforms[2] = {
            { .name = "gains[0]", .type = RINGL_FLOAT, .values = { 0.25f } },
            { .name = "gains[1]", .type = RINGL_FLOAT, .values = { 0.75f } },
        };
        RinGLGlslLowerResult lowered;

        assert(ringl_glsl_lower_rsh1_with_uniforms(
                   RINGL_FRAGMENT_SHADER, float_array_source,
                   sizeof(float_array_source) - 1u, uniforms, 2u,
                   &lowered) == 0);
        assert(lowered.ok != 0u);
        assert(ringl_glsl_lower_rsh1_with_uniforms(
                   RINGL_FRAGMENT_SHADER, const_index_float_array_source,
                   sizeof(const_index_float_array_source) - 1u, uniforms, 2u,
                   &lowered) == 0);
        assert(lowered.ok != 0u);
        assert(ringl_glsl_lower_rsh1_with_uniforms(
                   RINGL_FRAGMENT_SHADER, local_const_index_float_array_source,
                   sizeof(local_const_index_float_array_source) - 1u, uniforms, 2u,
                   &lowered) == 0);
        assert(lowered.ok != 0u);
        assert(ringl_glsl_lower_rsh1(
                   RINGL_FRAGMENT_SHADER, dynamic_float_array_source,
                   sizeof(dynamic_float_array_source) - 1u,
                   &lowered) != 0);
        assert(strstr(lowered.diagnostic, "array") != NULL);
        assert(ringl_glsl_lower_rsh1(
                   RINGL_FRAGMENT_SHADER, negative_const_index_source,
                   sizeof(negative_const_index_source) - 1u,
                   &lowered) != 0);
        assert(strstr(lowered.diagnostic, "constant") != NULL);
        ringl_shader_source(fragment, mutable_const_index_source, -1);
        ringl_compile_shader(fragment);
        assert(ringl_get_shader_compile_status(fragment) == RINGL_FALSE);
        assert(ringl_get_shader_info_log(fragment, log, sizeof(log)) > 0u);
        assert(strstr(log, "read-only") != NULL);
    }

    /* Generic texture lowering owns the sampler reflection and emits actual
     * RinGPU resource-pair samples. The first declaration is deliberately
     * inactive: the resource binding must point at the second shader sampler
     * rather than assuming declaration zero or a host-side texture result. */
    {
        static const char generic_texture_source[] =
            "uniform sampler2D unused; uniform sampler2D image; varying vec2 uv; "
            "void main() { vec2 coordinates = uv * 0.5 + vec2(0.25, 0.25); "
            "gl_FragColor = texture2D(image, coordinates); }";
        static const char invalid_generic_texture_source[] =
            "uniform sampler2D image; void main() { "
            "gl_FragColor = texture2D(missing, vec2(0.0)); }";
        static const char const_index_sampler_source[] =
            "uniform sampler2D palette[2]; const int selected = 1; "
            "void main() { gl_FragColor = texture2D(palette[selected], "
            "vec2(0.25, 0.75)); }";
        static const char builtin_coordinate_source[] =
            "uniform sampler2D image; uniform vec2 offset; "
            "void main() { gl_FragColor = texture2D(image, "
            "normalize(vec2(0.25, 0.75) + offset)); }";
        RinGLGlslLowerResult lowered;
        RinGLRsh1HeaderV1 header;
        const RinGLRsh1InstructionV1* instructions;
        uint32_t sample_count = 0u;

        assert(ringl_glsl_lower_rsh1(
                   RINGL_FRAGMENT_SHADER, generic_texture_source,
                   sizeof(generic_texture_source) - 1u, &lowered) == 0);
        assert(lowered.ok != 0u);
        assert(lowered.sampler_binding_count == 1u);
        assert(lowered.sampler_binding_indices[0] == 1u);
        memcpy(&header, lowered.bytes, sizeof(header));
        assert(header.resource_count == 2u);
        instructions = (const RinGLRsh1InstructionV1*)(
            lowered.bytes + sizeof(header));
        for (uint32_t index = 0u; index < header.instruction_count; ++index) {
            if (instructions[index].opcode != RINGL_RSH1_OP_SAMPLE_IMAGE_2D_F32)
                continue;
            assert(instructions[index].flags == sample_count);
            assert(instructions[index].resource == 0u);
            assert(instructions[index].immediate == 1u);
            assert(instructions[index].source0 != RINGL_RSH1_UNUSED);
            assert(instructions[index].source1 != RINGL_RSH1_UNUSED);
            ++sample_count;
        }
        assert(sample_count == 4u);
        assert(ringl_glsl_lower_rsh1(
                   RINGL_FRAGMENT_SHADER, invalid_generic_texture_source,
                   sizeof(invalid_generic_texture_source) - 1u, &lowered) != 0);
        assert(strstr(lowered.diagnostic, "sampler2D") != NULL);
        assert(ringl_glsl_lower_rsh1(
                   RINGL_FRAGMENT_SHADER, const_index_sampler_source,
                   sizeof(const_index_sampler_source) - 1u, &lowered) == 0);
        assert(lowered.ok != 0u);
        assert(lowered.sampler_binding_count == 1u);
        assert(lowered.sampler_binding_indices[0] == 1u);
        assert(ringl_glsl_lower_rsh1(
                   RINGL_FRAGMENT_SHADER, builtin_coordinate_source,
                   sizeof(builtin_coordinate_source) - 1u, &lowered) == 0);
        assert(lowered.ok != 0u);
        assert(lowered.sampler_binding_count == 1u);
    }

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
     * use the GLSL Boolean vector builtins. Their bounded arrays retain a
     * source aggregate while publishing contiguous element reflection. */
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
    assert(ringl_get_shader_compile_status(fragment) == RINGL_TRUE);
    assert(ringl_get_shader_info_log(fragment, log, sizeof(log)) == 0u);

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

    /* Fixed shader outputs use the same writable-selector rules as locals,
     * but lower each chosen component to its real RSH1 output slot. Both stage
     * outputs must be completed before linking/rasterization can observe one. */
    ringl_shader_source(vertex,
        "attribute vec2 position; void main() { gl_Position.xy = position.yx; "
        "gl_Position.zw = vec2(0.0, 1.0); }", -1);
    ringl_compile_shader(vertex);
    assert(ringl_get_shader_compile_status(vertex) == RINGL_TRUE);
    assert(ringl_get_shader_info_log(vertex, log, sizeof(log)) == 0u);

    ringl_shader_source(fragment,
        "void main() { vec3 color = vec3(0.125, 0.5, 0.75); "
        "gl_FragColor.bgr = color; gl_FragColor.a = 1.0; }", -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_TRUE);
    assert(ringl_get_shader_info_log(fragment, log, sizeof(log)) == 0u);

    ringl_shader_source(fragment,
        "void main() { gl_FragColor.rgb = vec3(1.0); }", -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_TRUE);

    ringl_shader_source(fragment,
        "void main() { gl_FragColor.rr = vec2(1.0); "
        "gl_FragColor.gba = vec3(1.0); }", -1);
    ringl_compile_shader(fragment);
    assert(ringl_get_shader_compile_status(fragment) == RINGL_FALSE);
    assert(ringl_get_shader_info_log(fragment, log, sizeof(log)) > 0u);
    assert(strstr(log, "invalid writable") != NULL);

    /* GL_EXT_draw_buffers selectors address the selected attachment's real
     * scalar RSH1 slots. Missing attachment components are handled only by the
     * explicit MRT zero-store pass; repeated lvalues remain a source error. */
    {
        static const char fragment_data_selector_source[] =
            "#extension GL_EXT_draw_buffers : require\n"
            "void main() { gl_FragData[2].bgr = vec3(1.0, 0.0, 0.0); "
            "gl_FragData[2].a = 1.0; }";
        static const char duplicate_fragment_data_selector_source[] =
            "#extension GL_EXT_draw_buffers : require\n"
            "void main() { gl_FragData[0].rr = vec2(1.0); }";
        RinGLGlslLowerResult lowered;

        assert(ringl_glsl_lower_rsh1(
                   RINGL_FRAGMENT_SHADER, fragment_data_selector_source,
                   sizeof(fragment_data_selector_source) - 1u, &lowered) == 0);
        assert(lowered.ok != 0u);
        assert(lowered.output_count == RINGL_MAX_COLOR_ATTACHMENTS * 4u);
        assert(ringl_glsl_lower_rsh1(
                   RINGL_FRAGMENT_SHADER,
                   duplicate_fragment_data_selector_source,
                   sizeof(duplicate_fragment_data_selector_source) - 1u,
                   &lowered) != 0);
        assert(strstr(lowered.diagnostic, "invalid writable") != NULL);
    }

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
