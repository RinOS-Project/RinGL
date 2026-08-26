/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <ringl/ringl.h>

#define RSH1_MAGIC UINT32_C(0x31485352)
#define RSH1_LOAD_INPUT_F32 45u
#define RSH1_SAMPLE_IMAGE_2D_F32 55u
#define RSH1_STORE_OUTPUT_F32 46u
#define RSH1_CONST_F32 16u
#define RSH1_ADD_F32 20u
#define RSH1_SUB_F32 21u
#define RSH1_MUL_F32 22u
#define RSH1_DIV_F32 23u

typedef struct __attribute__((packed)) Header {
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
} Header;

typedef struct __attribute__((packed)) Instruction {
    uint16_t opcode;
    uint16_t flags;
    uint16_t destination;
    uint16_t source0;
    uint16_t source1;
    uint16_t resource;
    uint32_t immediate;
} Instruction;

int main(void)
{
    RinGLContext* context = NULL;
    RinGLContextDescV1 desc = {
        .struct_size = sizeof(desc),
        .api_version = RINGL_API_VERSION,
    };
    uint32_t shader;
    uint8_t blob[2048];
    uint32_t size;
    uint32_t expected_bits;
    Header header;
    Instruction instructions[15];
    Instruction two_sampler_instructions[25];
    Instruction three_sampler_instructions[35];
    uint32_t component;
    const char* source =
        "uniform sampler2D colorTexture;\n"
        "void main() {\n"
        "  gl_FragColor = texture2D(colorTexture, vec2(0.25, 0.75));\n"
        "}\n";
    const char* scalar_splat_source =
        "uniform sampler2D colorTexture;\n"
        "void main() {\n"
        "  gl_FragColor = texture2D(colorTexture, vec2(0.75));\n"
        "}\n";
    const char* two_sampler_source =
        "uniform sampler2D firstTexture;\n"
        "uniform sampler2D secondTexture;\n"
        "void main() {\n"
        "  gl_FragColor = texture2D(secondTexture, vec2(0.25, 0.75)) + "
        "texture2D(firstTexture, vec2(0.5));\n"
        "}\n";
    const char* three_sampler_source =
        "uniform sampler2D firstTexture;\n"
        "uniform sampler2D secondTexture;\n"
        "uniform sampler2D thirdTexture;\n"
        "void main() {\n"
        "  gl_FragColor = texture2D(thirdTexture, vec2(0.25, 0.75)) + "
        "texture2D(firstTexture, vec2(0.5)) + "
        "texture2D(secondTexture, vec2(0.125, 0.875));\n"
        "}\n";
    const char* repeated_single_sampler_source =
        "uniform sampler2D colorTexture;\n"
        "void main() {\n"
        "  gl_FragColor = texture2D(colorTexture, vec2(0.25)) + "
        "texture2D(colorTexture, vec2(0.75));\n"
        "}\n";
    const char* partial_sampler_source =
        "uniform sampler2D firstTexture;\n"
        "uniform sampler2D secondTexture;\n"
        "void main() {\n"
        "  gl_FragColor = texture2D(secondTexture, vec2(0.25)) + "
        "texture2D(secondTexture, vec2(0.75));\n"
        "}\n";
    const char* eight_sampler_source =
        "uniform sampler2D s0; uniform sampler2D s1; "
        "uniform sampler2D s2; uniform sampler2D s3; "
        "uniform sampler2D s4; uniform sampler2D s5; "
        "uniform sampler2D s6; uniform sampler2D s7; "
        "void main() { gl_FragColor = texture2D(s7, vec2(0.7)) + "
        "texture2D(s6, vec2(0.6)) + texture2D(s5, vec2(0.5)) + "
        "texture2D(s4, vec2(0.4)) + texture2D(s3, vec2(0.3)) + "
        "texture2D(s2, vec2(0.2)) + texture2D(s1, vec2(0.1)) + "
        "texture2D(s0, vec2(0.0)); }";
    const char* varying_two_sampler_source =
        "uniform sampler2D firstTexture; uniform sampler2D secondTexture; "
        "varying vec2 uv; void main() { gl_FragColor = "
        "texture2D(firstTexture, uv) + texture2D(secondTexture, uv); }";
    const char* varying_three_coordinate_source =
        "uniform sampler2D firstTexture; uniform sampler2D secondTexture; "
        "uniform sampler2D thirdTexture; varying vec2 firstUv; "
        "varying vec2 secondUv; varying vec2 thirdUv; void main() { "
        "gl_FragColor = texture2D(firstTexture, firstUv) + "
        "texture2D(secondTexture, secondUv) + "
        "texture2D(thirdTexture, thirdUv); }";
    const char* varying_four_coordinate_source =
        "uniform sampler2D firstTexture; uniform sampler2D secondTexture; "
        "uniform sampler2D thirdTexture; uniform sampler2D fourthTexture; "
        "varying vec2 firstUv; varying vec2 secondUv; varying vec2 thirdUv; "
        "varying vec2 fourthUv; void main() { gl_FragColor = "
        "texture2D(firstTexture, firstUv) + "
        "texture2D(secondTexture, secondUv) + "
        "texture2D(thirdTexture, thirdUv) + "
        "texture2D(fourthTexture, fourthUv); }";
    const char* varying_eight_coordinate_source =
        "uniform sampler2D firstTexture; uniform sampler2D secondTexture; "
        "uniform sampler2D thirdTexture; uniform sampler2D fourthTexture; "
        "uniform sampler2D fifthTexture; uniform sampler2D sixthTexture; "
        "uniform sampler2D seventhTexture; uniform sampler2D eighthTexture; "
        "varying vec2 firstUv; varying vec2 secondUv; varying vec2 thirdUv; "
        "varying vec2 fourthUv; varying vec2 fifthUv; varying vec2 sixthUv; "
        "varying vec2 seventhUv; varying vec2 eighthUv; void main() { "
        "gl_FragColor = texture2D(firstTexture, firstUv) + "
        "texture2D(secondTexture, secondUv) + "
        "texture2D(thirdTexture, thirdUv) + "
        "texture2D(fourthTexture, fourthUv) + "
        "texture2D(fifthTexture, fifthUv) + "
        "texture2D(sixthTexture, sixthUv) + "
        "texture2D(seventhTexture, seventhUv) + "
        "texture2D(eighthTexture, eighthUv); }";
    const char* varying_four_coordinate_local_source =
        "uniform sampler2D firstTexture; uniform sampler2D secondTexture; "
        "uniform sampler2D thirdTexture; uniform sampler2D fourthTexture; "
        "varying vec2 firstUv; varying vec2 secondUv; varying vec2 thirdUv; "
        "varying vec2 fourthUv; void main() { vec2 mixedUv = secondUv + "
        "fourthUv; gl_FragColor = texture2D(firstTexture, firstUv) + "
        "texture2D(secondTexture, mixedUv) + texture2D(thirdTexture, thirdUv) + "
        "texture2D(fourthTexture, mixedUv); }";
    const char* varying_three_coordinate_local_source =
        "uniform sampler2D firstTexture; uniform sampler2D secondTexture; "
        "uniform sampler2D thirdTexture; varying vec2 firstUv; "
        "varying vec2 secondUv; varying vec2 thirdUv; void main() { "
        "vec2 mixedUv = firstUv + secondUv; gl_FragColor = "
        "texture2D(firstTexture, mixedUv) + "
        "texture2D(secondTexture, mixedUv) + "
        "texture2D(thirdTexture, thirdUv); }";
    const char* varying_three_coordinate_third_pair_local_source =
        "uniform sampler2D firstTexture; uniform sampler2D secondTexture; "
        "uniform sampler2D thirdTexture; varying vec2 firstUv; "
        "varying vec2 secondUv; varying vec2 thirdUv; void main() { "
        "vec2 mixedUv = secondUv - thirdUv; gl_FragColor = "
        "texture2D(firstTexture, firstUv) + "
        "texture2D(secondTexture, mixedUv) + "
        "texture2D(thirdTexture, mixedUv); }";
    const char* varying_three_coordinate_third_pair_local_chain_source =
        "uniform sampler2D firstTexture; uniform sampler2D secondTexture; "
        "uniform sampler2D thirdTexture; varying vec2 firstUv; "
        "varying vec2 secondUv; varying vec2 thirdUv; void main() { "
        "vec2 sampleUv = secondUv - thirdUv + firstUv; gl_FragColor = "
        "texture2D(firstTexture, firstUv) + "
        "texture2D(secondTexture, sampleUv) + "
        "texture2D(thirdTexture, sampleUv); }";
    const char* varying_three_coordinate_reused_pair_source =
        "uniform sampler2D firstTexture; uniform sampler2D secondTexture; "
        "uniform sampler2D thirdTexture; varying vec2 firstUv; "
        "varying vec2 secondUv; varying vec2 thirdUv; void main() { "
        "vec2 mixedUv = firstUv + secondUv; "
        "vec2 invalidUv = mixedUv + secondUv; gl_FragColor = "
        "texture2D(firstTexture, invalidUv); }";
    const char* varying_tinted_texture_source =
        "uniform sampler2D colorTexture; varying vec2 uv; "
        "void main() { gl_FragColor = texture2D(colorTexture, uv) * "
        "vec4(0.5, 1.0, 0.25, 1.0); }";
    const char* varying_two_sampler_tinted_texture_source =
        "uniform sampler2D firstTexture; uniform sampler2D secondTexture; "
        "varying vec2 uv; void main() { gl_FragColor = ("
        "texture2D(firstTexture, uv) + texture2D(secondTexture, uv)) * "
        "vec4(0.5, 1.0, 0.25, 1.0); }";
    const char* varying_biased_texture_source =
        "uniform sampler2D colorTexture; varying vec2 uv; "
        "void main() { gl_FragColor = texture2D(colorTexture, uv) + "
        "vec4(0.1, 0.2, 0.1, 0.0); }";
    const char* varying_subtracted_texture_source =
        "uniform sampler2D colorTexture; varying vec2 uv; "
        "void main() { gl_FragColor = texture2D(colorTexture, uv) - "
        "vec4(0.1, 0.2, 0.1, 0.0); }";
    const char* varying_left_subtracted_texture_source =
        "uniform sampler2D colorTexture; varying vec2 uv; "
        "void main() { gl_FragColor = vec4(0.5, 0.75, 1.0, 1.0) - "
        "texture2D(colorTexture, uv); }";
    const char* varying_left_divided_texture_source =
        "uniform sampler2D colorTexture; varying vec2 uv; "
        "void main() { gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0) / "
        "texture2D(colorTexture, uv); }";
    const char* varying_divided_texture_source =
        "uniform sampler2D colorTexture; varying vec2 uv; "
        "void main() { gl_FragColor = texture2D(colorTexture, uv) / "
        "vec4(2.0, 2.0, 2.0, 1.0); }";
    const char* varying_zero_divisor_texture_source =
        "uniform sampler2D colorTexture; varying vec2 uv; "
        "void main() { gl_FragColor = texture2D(colorTexture, uv) / "
        "vec4(1.0, 1.0, 0.0, 1.0); }";
    const char* varying_two_coordinate_source =
        "uniform sampler2D firstTexture; uniform sampler2D secondTexture; "
        "varying vec2 firstUv; varying vec2 secondUv; "
        "void main() { gl_FragColor = texture2D(firstTexture, firstUv) + "
        "texture2D(secondTexture, secondUv); }";
    const char* varying_coordinate_swizzle_source =
        "uniform sampler2D firstTexture; uniform sampler2D secondTexture; "
        "varying vec2 firstUv; varying vec2 secondUv; "
        "void main() { gl_FragColor = texture2D(firstTexture, firstUv.yx) + "
        "texture2D(secondTexture, secondUv.st); }";
    const char* varying_coordinate_add_source =
        "uniform sampler2D colorTexture; varying vec2 firstUv; "
        "varying vec2 secondUv; void main() { gl_FragColor = "
        "texture2D(colorTexture, firstUv + secondUv); }";
    const char* varying_local_coordinate_add_source =
        "uniform sampler2D colorTexture; varying vec2 firstUv; "
        "varying vec2 secondUv; void main() { vec2 mixedUv = "
        "firstUv + secondUv; gl_FragColor = texture2D(colorTexture, mixedUv); }";
    const char* varying_local_coordinate_sub_source =
        "uniform sampler2D colorTexture; varying vec2 firstUv; "
        "varying vec2 secondUv; void main() { vec2 mixedUv = "
        "secondUv - firstUv; gl_FragColor = texture2D(colorTexture, mixedUv); }";
    const char* varying_local_coordinate_affine_chain_source =
        "uniform sampler2D colorTexture; varying vec2 firstUv; "
        "varying vec2 secondUv; void main() { vec2 mixedUv = "
        "firstUv + secondUv; vec2 sampleUv = mixedUv - vec2(0.25, 0.5); "
        "gl_FragColor = texture2D(colorTexture, sampleUv); }";
    const char* varying_local_alias_source =
        "uniform sampler2D firstTexture; uniform sampler2D secondTexture; "
        "varying vec2 uv; void main() { vec2 sampleUv = uv; gl_FragColor = "
        "texture2D(firstTexture, sampleUv) + "
        "texture2D(secondTexture, sampleUv); }";
    const char* varying_local_affine_source =
        "uniform sampler2D firstTexture; uniform sampler2D secondTexture; "
        "varying vec2 uv; void main() { vec2 sampleUv = uv + vec2(0.5, 0.0); "
        "gl_FragColor = texture2D(firstTexture, sampleUv) + "
        "texture2D(secondTexture, sampleUv); }";
    const char* varying_local_affine_call_offset_source =
        "uniform sampler2D colorTexture; varying vec2 uv; "
        "void main() { vec2 sampleUv = uv + vec2(0.5, -0.5); "
        "gl_FragColor = texture2D(colorTexture, sampleUv - vec2(0.25, 0.125)); }";
    const char* varying_local_affine_chain_source =
        "uniform sampler2D colorTexture; varying vec2 uv; "
        "void main() { vec2 baseUv = uv + vec2(0.5, 0.0); "
        "vec2 sampleUv = baseUv - vec2(0.0, 0.5); "
        "gl_FragColor = texture2D(colorTexture, sampleUv); }";
    const char* varying_six_local_affine_chain_source =
        "uniform sampler2D colorTexture; varying vec2 uv; "
        "void main() { vec2 firstUv = uv * vec2(1.0, 1.0); "
        "vec2 secondUv = firstUv / vec2(1.0, 1.0); "
        "vec2 thirdUv = secondUv + vec2(0.25, 0.0); "
        "vec2 fourthUv = thirdUv + vec2(0.25, 0.0); "
        "vec2 fifthUv = fourthUv - vec2(0.0, 0.25); "
        "vec2 sixthUv = fifthUv - vec2(0.0, 0.25); "
        "gl_FragColor = texture2D(colorTexture, sixthUv); }";
    const char* varying_eight_local_affine_chain_source =
        "uniform sampler2D colorTexture; varying vec2 uv; "
        "void main() { vec2 firstUv = uv * vec2(1.0, 1.0); "
        "vec2 secondUv = firstUv / vec2(1.0, 1.0); "
        "vec2 thirdUv = secondUv + vec2(0.25, 0.0); "
        "vec2 fourthUv = thirdUv + vec2(0.25, 0.0); "
        "vec2 fifthUv = fourthUv - vec2(0.0, 0.25); "
        "vec2 sixthUv = fifthUv - vec2(0.0, 0.25); "
        "vec2 seventhUv = sixthUv * vec2(1.0, 1.0); "
        "vec2 eighthUv = seventhUv / vec2(1.0, 1.0); "
        "gl_FragColor = texture2D(colorTexture, eighthUv); }";
    const char* varying_capacity_rejected_source =
        "uniform sampler2D colorTexture; varying vec2 uv; "
        "void main() { vec2 firstUv = uv + vec2(0.0, 0.0); "
        "vec2 secondUv = firstUv + vec2(0.0, 0.0); "
        "vec2 thirdUv = secondUv + vec2(0.0, 0.0); "
        "vec2 fourthUv = thirdUv + vec2(0.0, 0.0); "
        "vec2 fifthUv = fourthUv + vec2(0.0, 0.0); "
        "vec2 sixthUv = fifthUv + vec2(0.0, 0.0); "
        "vec2 seventhUv = sixthUv + vec2(0.0, 0.0); "
        "vec2 eighthUv = seventhUv + vec2(0.0, 0.0); "
        "gl_FragColor = texture2D(colorTexture, eighthUv + vec2(0.0, 0.0)) + "
        "texture2D(colorTexture, eighthUv + vec2(0.0, 0.0)) + "
        "texture2D(colorTexture, eighthUv + vec2(0.0, 0.0)) + "
        "texture2D(colorTexture, eighthUv + vec2(0.0, 0.0)) + "
        "texture2D(colorTexture, eighthUv + vec2(0.0, 0.0)) + "
        "texture2D(colorTexture, eighthUv + vec2(0.0, 0.0)) + "
        "texture2D(colorTexture, eighthUv + vec2(0.0, 0.0)) + "
        "texture2D(colorTexture, eighthUv + vec2(0.0, 0.0)); }";
    const char* varying_affine_offsets_source =
        "uniform sampler2D firstTexture; uniform sampler2D secondTexture; "
        "varying vec2 uv; void main() { gl_FragColor = "
        "texture2D(firstTexture, uv + vec2(5e-1, -5e-1)) + "
        "texture2D(secondTexture, uv - vec2(2.5e-1, 7.5e-1)); }";
    const char* varying_uniform_offset_source =
        "uniform sampler2D colorTexture; uniform vec2 offset; varying vec2 uv; "
        "void main() { gl_FragColor = texture2D(colorTexture, uv + offset.yx); }";
    const char* varying_uniform_left_subtract_source =
        "uniform sampler2D colorTexture; uniform vec2 offset; varying vec2 uv; "
        "void main() { gl_FragColor = texture2D(colorTexture, offset.yx - uv); }";
    const char* varying_uniform_left_add_source =
        "uniform sampler2D colorTexture; uniform vec2 offset; varying vec2 uv; "
        "void main() { gl_FragColor = texture2D(colorTexture, offset.yx + uv); }";
    const char* varying_uniform_left_multiply_source =
        "uniform sampler2D colorTexture; uniform vec2 offset; varying vec2 uv; "
        "void main() { gl_FragColor = texture2D(colorTexture, offset.yx * uv); }";
    const char* varying_uniform_right_divide_source =
        "uniform sampler2D colorTexture; uniform vec2 offset; varying vec2 uv; "
        "void main() { gl_FragColor = texture2D(colorTexture, uv / offset.yx); }";
    const char* varying_uniform_left_divide_source =
        "uniform sampler2D colorTexture; uniform vec2 offset; varying vec2 uv; "
        "void main() { gl_FragColor = texture2D(colorTexture, offset.yx / uv); }";
    const char* varying_repeated_partial_source =
        "uniform sampler2D unusedTexture; uniform sampler2D activeTexture; "
        "varying vec2 uv; void main() { gl_FragColor = "
        "texture2D(activeTexture, uv) + texture2D(activeTexture, uv) + "
        "texture2D(activeTexture, uv); }";
    const char* varying_eight_sampler_source =
        "uniform sampler2D s0; uniform sampler2D s1; "
        "uniform sampler2D s2; uniform sampler2D s3; "
        "uniform sampler2D s4; uniform sampler2D s5; "
        "uniform sampler2D s6; uniform sampler2D s7; varying vec2 uv; "
        "void main() { gl_FragColor = texture2D(s7, uv) + "
        "texture2D(s6, uv) + texture2D(s5, uv) + texture2D(s4, uv) + "
        "texture2D(s3, uv) + texture2D(s2, uv) + texture2D(s1, uv) + "
        "texture2D(s0, uv); }";
    const char* varying_local_affine_eight_sampler_source =
        "uniform sampler2D s0; uniform sampler2D s1; "
        "uniform sampler2D s2; uniform sampler2D s3; "
        "uniform sampler2D s4; uniform sampler2D s5; "
        "uniform sampler2D s6; uniform sampler2D s7; varying vec2 uv; "
        "void main() { vec2 baseUv = uv + vec2(0.125, -0.125); "
        "vec2 sampleUv = baseUv - vec2(0.0, 0.0); "
        "gl_FragColor = texture2D(s7, sampleUv + vec2(0.0, 0.0)) + "
        "texture2D(s6, sampleUv + vec2(0.0, 0.0)) + "
        "texture2D(s5, sampleUv + vec2(0.0, 0.0)) + "
        "texture2D(s4, sampleUv + vec2(0.0, 0.0)) + "
        "texture2D(s3, sampleUv + vec2(0.0, 0.0)) + "
        "texture2D(s2, sampleUv + vec2(0.0, 0.0)) + "
        "texture2D(s1, sampleUv + vec2(0.0, 0.0)) + "
        "texture2D(s0, sampleUv + vec2(0.0, 0.0)); }";
    float scalar_splat = 0.75f;
    const float tint_values[4] = {0.5f, 1.0f, 0.25f, 1.0f};
    const float bias_values[4] = {0.1f, 0.2f, 0.1f, 0.0f};
    const float left_color_values[4] = {0.5f, 0.75f, 1.0f, 1.0f};
    const float unit_color_values[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    const float divisor_values[4] = {2.0f, 2.0f, 2.0f, 1.0f};

    assert(ringl_context_create(&desc, &context) == 0);
    assert(ringl_make_current(context) == 0);
    shader = ringl_create_shader(RINGL_FRAGMENT_SHADER);
    assert(shader != 0u);
    ringl_shader_source(shader, source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + sizeof(instructions));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    memcpy(instructions, blob + sizeof(header), sizeof(instructions));

    assert(header.magic == RSH1_MAGIC);
    assert(header.stage == 2u);
    assert(header.instruction_count == 15u);
    assert(header.register_count == 10u);
    assert(header.input_count == 4u);
    assert(header.output_count == 4u);
    assert(header.resource_count == 2u);

    for (component = 0u; component < 4u; ++component) {
        const Instruction* input = &instructions[component];
        const Instruction* sample = &instructions[6u + component];
        const Instruction* store = &instructions[10u + component];
        assert(input->opcode == RSH1_LOAD_INPUT_F32);
        assert(input->destination == 6u + component);
        assert(input->immediate == component);
        assert(sample->opcode == RSH1_SAMPLE_IMAGE_2D_F32);
        assert(sample->flags == component);
        assert(sample->destination == 2u + component);
        assert(sample->source0 == 0u);
        assert(sample->source1 == 1u);
        assert(sample->resource == 0u);
        assert(sample->immediate == 1u);
        assert(store->opcode == RSH1_STORE_OUTPUT_F32);
        assert(store->source0 == 2u + component);
        assert(store->immediate == component);
    }

    ringl_shader_source(shader, scalar_splat_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + sizeof(instructions));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(instructions, blob + sizeof(header), sizeof(instructions));
    memcpy(&expected_bits, &scalar_splat, sizeof(expected_bits));
    assert(instructions[4].immediate == expected_bits);
    assert(instructions[5].immediate == expected_bits);

    ringl_shader_source(shader, two_sampler_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + sizeof(two_sampler_instructions));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    memcpy(two_sampler_instructions, blob + sizeof(header),
           sizeof(two_sampler_instructions));
    assert(header.instruction_count == 25u);
    assert(header.register_count == 20u);
    assert(header.resource_count == 4u);
    for (component = 0u; component < 4u; ++component) {
        const Instruction* second_sample =
            &two_sampler_instructions[8u + component];
        const Instruction* first_sample =
            &two_sampler_instructions[12u + component];
        const Instruction* add = &two_sampler_instructions[16u + component];
        const Instruction* store = &two_sampler_instructions[20u + component];

        assert(second_sample->opcode == RSH1_SAMPLE_IMAGE_2D_F32);
        assert(second_sample->resource == 2u && second_sample->immediate == 3u);
        assert(first_sample->opcode == RSH1_SAMPLE_IMAGE_2D_F32);
        assert(first_sample->resource == 0u && first_sample->immediate == 1u);
        assert(add->opcode == RSH1_ADD_F32);
        assert(add->source0 == 4u + component);
        assert(add->source1 == 8u + component);
        assert(store->opcode == RSH1_STORE_OUTPUT_F32);
        assert(store->source0 == 16u + component);
    }

    /* One declared sampler may be sampled repeatedly. The resource pair stays
     * dense and typed while every call receives independent coordinates and
     * result registers. */
    ringl_shader_source(shader, repeated_single_sampler_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + sizeof(two_sampler_instructions));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    memcpy(two_sampler_instructions, blob + sizeof(header),
           sizeof(two_sampler_instructions));
    assert(header.instruction_count == 25u);
    assert(header.register_count == 20u);
    assert(header.resource_count == 2u);
    for (component = 0u; component < 4u; ++component) {
        assert(two_sampler_instructions[8u + component].resource == 0u);
        assert(two_sampler_instructions[8u + component].immediate == 1u);
        assert(two_sampler_instructions[12u + component].resource == 0u);
        assert(two_sampler_instructions[12u + component].immediate == 1u);
    }

    /* Active resource pairs are dense, not declaration-indexed: the unused
     * first sampler has no RSH1 resource and the repeated second sampler
     * becomes pair [0, 1]. */
    ringl_shader_source(shader, partial_sampler_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + sizeof(two_sampler_instructions));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    memcpy(two_sampler_instructions, blob + sizeof(header),
           sizeof(two_sampler_instructions));
    assert(header.resource_count == 2u);
    for (component = 0u; component < 4u; ++component) {
        assert(two_sampler_instructions[8u + component].resource == 0u);
        assert(two_sampler_instructions[8u + component].immediate == 1u);
        assert(two_sampler_instructions[12u + component].resource == 0u);
        assert(two_sampler_instructions[12u + component].immediate == 1u);
    }

    ringl_shader_source(shader, three_sampler_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + sizeof(three_sampler_instructions));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    memcpy(three_sampler_instructions, blob + sizeof(header),
           sizeof(three_sampler_instructions));
    assert(header.instruction_count == 35u);
    assert(header.register_count == 30u);
    assert(header.resource_count == 6u);
    for (component = 0u; component < 4u; ++component) {
        const Instruction* third_sample =
            &three_sampler_instructions[10u + component];
        const Instruction* first_sample =
            &three_sampler_instructions[14u + component];
        const Instruction* second_sample =
            &three_sampler_instructions[18u + component];
        const Instruction* first_add =
            &three_sampler_instructions[22u + component];
        const Instruction* second_add =
            &three_sampler_instructions[26u + component];
        const Instruction* store =
            &three_sampler_instructions[30u + component];

        assert(third_sample->opcode == RSH1_SAMPLE_IMAGE_2D_F32);
        assert(third_sample->resource == 4u && third_sample->immediate == 5u);
        assert(first_sample->opcode == RSH1_SAMPLE_IMAGE_2D_F32);
        assert(first_sample->resource == 0u && first_sample->immediate == 1u);
        assert(second_sample->opcode == RSH1_SAMPLE_IMAGE_2D_F32);
        assert(second_sample->resource == 2u && second_sample->immediate == 3u);
        assert(first_add->opcode == RSH1_ADD_F32);
        assert(first_add->source0 == 6u + component);
        assert(first_add->source1 == 10u + component);
        assert(second_add->opcode == RSH1_ADD_F32);
        assert(second_add->source0 == 22u + component);
        assert(second_add->source1 == 14u + component);
        assert(store->opcode == RSH1_STORE_OUTPUT_F32);
        assert(store->source0 == 26u + component);
    }

    /* The declared eight-sampler maximum remains within both the locally
     * generated RSH1 bounds and RinGPU's public 256-register limit. */
    ringl_shader_source(shader, eight_sampler_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 85u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.instruction_count == 85u);
    assert(header.register_count == 80u);
    assert(header.resource_count == 16u);

    ringl_shader_source(shader, varying_two_sampler_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 21u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.instruction_count == 21u);
    assert(header.register_count == 16u);
    assert(header.resource_count == 4u);
    for (component = 0u; component < 4u; ++component) {
        const Instruction* first_sample =
            (const Instruction*)(blob + sizeof(header)) + 4u + component;
        const Instruction* second_sample =
            (const Instruction*)(blob + sizeof(header)) + 8u + component;
        const Instruction* add =
            (const Instruction*)(blob + sizeof(header)) + 12u + component;

        assert(first_sample->resource == 0u && first_sample->immediate == 1u);
        assert(second_sample->resource == 2u && second_sample->immediate == 3u);
        assert(add->source0 == 2u + component);
        assert(add->source1 == 6u + component);
    }

    /* One sampled RGBA result may be tinted by a finite vec4 literal. The
     * constants and component-wise MUL_F32 operations remain executable RSH1
     * instructions rather than a host-side texture rewrite. */
    ringl_shader_source(shader, varying_tinted_texture_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 21u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.instruction_count == 21u);
    assert(header.register_count == 16u);
    assert(header.resource_count == 2u);
    for (component = 0u; component < 4u; ++component) {
        uint32_t tint_bits;
        const Instruction* sample =
            (const Instruction*)(blob + sizeof(header)) + 4u + component;
        const Instruction* constant =
            (const Instruction*)(blob + sizeof(header)) + 8u + component;
        const Instruction* multiply =
            (const Instruction*)(blob + sizeof(header)) + 12u + component;
        const Instruction* store =
            (const Instruction*)(blob + sizeof(header)) + 16u + component;

        memcpy(&tint_bits, &tint_values[component], sizeof(tint_bits));
        assert(sample->opcode == RSH1_SAMPLE_IMAGE_2D_F32);
        assert(constant->opcode == RSH1_CONST_F32);
        assert(constant->destination == 8u + component);
        assert(constant->immediate == tint_bits);
        assert(multiply->opcode == RSH1_MUL_F32);
        assert(multiply->destination == 12u + component);
        assert(multiply->source0 == 2u + component);
        assert(multiply->source1 == 8u + component);
        assert(store->opcode == RSH1_STORE_OUTPUT_F32);
        assert(store->source0 == 12u + component);
    }

    /* Multiple samples require an explicit parenthesized addition before the
     * tint, so the bounded profile never guesses GLSL precedence. */
    ringl_shader_source(shader, varying_two_sampler_tinted_texture_source,
                        -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 29u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.instruction_count == 29u);
    assert(header.register_count == 24u);
    assert(header.resource_count == 4u);
    for (component = 0u; component < 4u; ++component) {
        uint32_t tint_bits;
        const Instruction* add =
            (const Instruction*)(blob + sizeof(header)) + 12u + component;
        const Instruction* constant =
            (const Instruction*)(blob + sizeof(header)) + 16u + component;
        const Instruction* multiply =
            (const Instruction*)(blob + sizeof(header)) + 20u + component;
        const Instruction* store =
            (const Instruction*)(blob + sizeof(header)) + 24u + component;

        memcpy(&tint_bits, &tint_values[component], sizeof(tint_bits));
        assert(add->opcode == RSH1_ADD_F32);
        assert(add->destination == 10u + component);
        assert(add->source0 == 2u + component);
        assert(add->source1 == 6u + component);
        assert(constant->opcode == RSH1_CONST_F32);
        assert(constant->destination == 16u + component);
        assert(constant->immediate == tint_bits);
        assert(multiply->opcode == RSH1_MUL_F32);
        assert(multiply->destination == 20u + component);
        assert(multiply->source0 == 10u + component);
        assert(multiply->source1 == 16u + component);
        assert(store->opcode == RSH1_STORE_OUTPUT_F32);
        assert(store->source0 == 20u + component);
    }

    ringl_shader_source(shader, varying_biased_texture_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 21u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.instruction_count == 21u);
    assert(header.register_count == 16u);
    assert(header.resource_count == 2u);
    for (component = 0u; component < 4u; ++component) {
        uint32_t bias_bits;
        const Instruction* constant =
            (const Instruction*)(blob + sizeof(header)) + 8u + component;
        const Instruction* add =
            (const Instruction*)(blob + sizeof(header)) + 12u + component;

        memcpy(&bias_bits, &bias_values[component], sizeof(bias_bits));
        assert(constant->opcode == RSH1_CONST_F32);
        assert(constant->immediate == bias_bits);
        assert(add->opcode == RSH1_ADD_F32);
        assert(add->destination == 12u + component);
        assert(add->source0 == 2u + component);
        assert(add->source1 == 8u + component);
    }

    ringl_shader_source(shader, varying_subtracted_texture_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 21u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    for (component = 0u; component < 4u; ++component) {
        uint32_t bias_bits;
        const Instruction* constant =
            (const Instruction*)(blob + sizeof(header)) + 8u + component;
        const Instruction* subtract =
            (const Instruction*)(blob + sizeof(header)) + 12u + component;

        memcpy(&bias_bits, &bias_values[component], sizeof(bias_bits));
        assert(constant->opcode == RSH1_CONST_F32);
        assert(constant->immediate == bias_bits);
        assert(subtract->opcode == RSH1_SUB_F32);
        assert(subtract->source0 == 2u + component);
        assert(subtract->source1 == 8u + component);
    }

    /* A finite color literal on the left keeps noncommutative subtraction in
     * RSH1 operand order, rather than being rewritten as sampled-color minus
     * literal. */
    ringl_shader_source(shader, varying_left_subtracted_texture_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 21u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    for (component = 0u; component < 4u; ++component) {
        uint32_t left_color_bits;
        const Instruction* constant =
            (const Instruction*)(blob + sizeof(header)) + 8u + component;
        const Instruction* subtract =
            (const Instruction*)(blob + sizeof(header)) + 12u + component;

        memcpy(&left_color_bits, &left_color_values[component],
               sizeof(left_color_bits));
        assert(constant->opcode == RSH1_CONST_F32);
        assert(constant->immediate == left_color_bits);
        assert(subtract->opcode == RSH1_SUB_F32);
        assert(subtract->source0 == 8u + component);
        assert(subtract->source1 == 2u + component);
    }

    /* The live texture result is a legal divisor. RinGPU's fragment preflight
     * rejects any zero component before a target write, while the IR retains
     * the literal as the left operand. */
    ringl_shader_source(shader, varying_left_divided_texture_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 21u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    for (component = 0u; component < 4u; ++component) {
        uint32_t unit_color_bits;
        const Instruction* constant =
            (const Instruction*)(blob + sizeof(header)) + 8u + component;
        const Instruction* divide =
            (const Instruction*)(blob + sizeof(header)) + 12u + component;

        memcpy(&unit_color_bits, &unit_color_values[component],
               sizeof(unit_color_bits));
        assert(constant->opcode == RSH1_CONST_F32);
        assert(constant->immediate == unit_color_bits);
        assert(divide->opcode == RSH1_DIV_F32);
        assert(divide->source0 == 8u + component);
        assert(divide->source1 == 2u + component);
    }

    ringl_shader_source(shader, varying_divided_texture_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 21u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    for (component = 0u; component < 4u; ++component) {
        uint32_t divisor_bits;
        const Instruction* constant =
            (const Instruction*)(blob + sizeof(header)) + 8u + component;
        const Instruction* divide =
            (const Instruction*)(blob + sizeof(header)) + 12u + component;

        memcpy(&divisor_bits, &divisor_values[component],
               sizeof(divisor_bits));
        assert(constant->immediate == divisor_bits);
        assert(divide->opcode == RSH1_DIV_F32);
        assert(divide->source0 == 2u + component);
        assert(divide->source1 == 8u + component);
    }

    /* A zero literal divisor is rejected during lowering, before it could
     * reach RinGPU's fragment executor. */
    ringl_shader_source(shader, varying_zero_divisor_texture_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) != 0);

    /* Two declared vec2 varyings occupy the four perspective RSH1 inputs;
     * each texture call chooses its own pair instead of silently reusing the
     * first coordinate. */
    ringl_shader_source(shader, varying_two_coordinate_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 21u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.instruction_count == 21u);
    assert(header.register_count == 16u);
    assert(header.resource_count == 4u);
    for (component = 0u; component < 4u; ++component) {
        const Instruction* first_sample =
            (const Instruction*)(blob + sizeof(header)) + 4u + component;
        const Instruction* second_sample =
            (const Instruction*)(blob + sizeof(header)) + 8u + component;

        assert(first_sample->source0 == 0u && first_sample->source1 == 1u);
        assert(second_sample->source0 == 14u && second_sample->source1 == 15u);
        assert(first_sample->resource == 0u && first_sample->immediate == 1u);
        assert(second_sample->resource == 2u && second_sample->immediate == 3u);
    }

    /* texture2D coordinate selectors are a scalar-register permutation. The
     * first pair is reversed while the second pair's `st` form remains in
     * physical order, so the lowerer cannot accidentally ignore selectors. */
    ringl_shader_source(shader, varying_coordinate_swizzle_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 21u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.instruction_count == 21u);
    assert(header.register_count == 16u);
    for (component = 0u; component < 4u; ++component) {
        const Instruction* first_sample =
            (const Instruction*)(blob + sizeof(header)) + 4u + component;
        const Instruction* second_sample =
            (const Instruction*)(blob + sizeof(header)) + 8u + component;

        assert(first_sample->opcode == RSH1_SAMPLE_IMAGE_2D_F32);
        assert(first_sample->source0 == 1u && first_sample->source1 == 0u);
        assert(second_sample->opcode == RSH1_SAMPLE_IMAGE_2D_F32);
        assert(second_sample->source0 == 14u && second_sample->source1 == 15u);
    }

    /* Two perspective coordinates can be combined in RSH1 before sampling;
     * the physical second pair remains distinct rather than being folded into
     * the first input pair. */
    ringl_shader_source(shader, varying_coordinate_add_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 15u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.instruction_count == 15u);
    assert(header.register_count == 12u);
    assert(header.resource_count == 2u);
    for (component = 0u; component < 2u; ++component) {
        const Instruction* combine =
            (const Instruction*)(blob + sizeof(header)) + 4u + component;

        assert(combine->opcode == RSH1_ADD_F32);
        assert(combine->destination == 10u + component);
        assert(combine->source0 == component);
        assert(combine->source1 == 6u + component);
    }
    for (component = 0u; component < 4u; ++component) {
        const Instruction* sample =
            (const Instruction*)(blob + sizeof(header)) + 6u + component;

        assert(sample->opcode == RSH1_SAMPLE_IMAGE_2D_F32);
        assert(sample->source0 == 10u && sample->source1 == 11u);
        assert(sample->resource == 0u && sample->immediate == 1u);
    }

    /* The same two perspective inputs may be combined by a named local vec2
     * before sampling.  This is a real RSH1 ADD path, not a source rewrite
     * back into the call expression. */
    ringl_shader_source(shader, varying_local_coordinate_add_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 15u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.instruction_count == 15u);
    assert(header.register_count == 12u);
    assert(header.resource_count == 2u);
    for (component = 0u; component < 2u; ++component) {
        const Instruction* combine =
            (const Instruction*)(blob + sizeof(header)) + 4u + component;

        assert(combine->opcode == RSH1_ADD_F32);
        assert(combine->destination == 10u + component);
        assert(combine->source0 == component);
        assert(combine->source1 == 6u + component);
    }
    for (component = 0u; component < 4u; ++component) {
        const Instruction* sample =
            (const Instruction*)(blob + sizeof(header)) + 6u + component;

        assert(sample->opcode == RSH1_SAMPLE_IMAGE_2D_F32);
        assert(sample->source0 == 10u && sample->source1 == 11u);
        assert(sample->resource == 0u && sample->immediate == 1u);
    }

    ringl_shader_source(shader, varying_local_coordinate_sub_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 15u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    for (component = 0u; component < 2u; ++component) {
        const Instruction* combine =
            (const Instruction*)(blob + sizeof(header)) + 4u + component;

        assert(combine->opcode == RSH1_SUB_F32);
        assert(combine->source0 == 6u + component);
        assert(combine->source1 == component);
    }

    /* A two-varying local result may feed the existing finite affine local
     * chain.  The second operation consumes the first RSH1 result in source
     * order instead of folding the expressions on the host. */
    ringl_shader_source(shader, varying_local_coordinate_affine_chain_source,
                        -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 19u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.instruction_count == 19u);
    assert(header.register_count == 16u);
    assert(header.resource_count == 2u);
    for (component = 0u; component < 2u; ++component) {
        const Instruction* combine =
            (const Instruction*)(blob + sizeof(header)) + 4u + component;
        const Instruction* constant =
            (const Instruction*)(blob + sizeof(header)) + 6u + component;
        const Instruction* affine =
            (const Instruction*)(blob + sizeof(header)) + 8u + component;
        const Instruction* sample =
            (const Instruction*)(blob + sizeof(header)) + 10u + component;

        assert(combine->opcode == RSH1_ADD_F32);
        assert(combine->destination == 10u + component);
        assert(combine->source0 == component);
        assert(combine->source1 == 6u + component);
        assert(constant->opcode == RSH1_CONST_F32);
        assert(constant->destination == 12u + component);
        assert(affine->opcode == RSH1_SUB_F32);
        assert(affine->destination == 14u + component);
        assert(affine->source0 == 10u + component);
        assert(affine->source1 == 12u + component);
        assert(sample->opcode == RSH1_SAMPLE_IMAGE_2D_F32);
        assert(sample->source0 == 14u && sample->source1 == 15u);
    }

    /* A directly initialized local vec2 remains an interpolated coordinate
     * in RSH1; it is not folded to a constant at compile time. */
    ringl_shader_source(shader, varying_local_alias_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 21u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.instruction_count == 21u);
    assert(header.register_count == 16u);
    assert(header.resource_count == 4u);
    for (component = 0u; component < 4u; ++component) {
        const Instruction* first_sample =
            (const Instruction*)(blob + sizeof(header)) + 4u + component;
        const Instruction* second_sample =
            (const Instruction*)(blob + sizeof(header)) + 8u + component;

        assert(first_sample->opcode == RSH1_SAMPLE_IMAGE_2D_F32);
        assert(first_sample->source0 == 0u && first_sample->source1 == 1u);
        assert(first_sample->resource == 0u && first_sample->immediate == 1u);
        assert(second_sample->opcode == RSH1_SAMPLE_IMAGE_2D_F32);
        assert(second_sample->source0 == 0u && second_sample->source1 == 1u);
        assert(second_sample->resource == 2u && second_sample->immediate == 3u);
    }

    /* A local finite affine coordinate is evaluated once before all samples,
     * which keeps the bounded eight-call instruction profile below RSH1's
     * ceiling while still using live interpolated inputs. */
    ringl_shader_source(shader, varying_local_affine_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 25u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.instruction_count == 25u);
    assert(header.register_count == 20u);
    assert(header.resource_count == 4u);
    for (component = 0u; component < 4u; ++component) {
        const Instruction* local_constant =
            (const Instruction*)(blob + sizeof(header)) + 4u + component / 2u;
        const Instruction* local_coordinate =
            (const Instruction*)(blob + sizeof(header)) + 6u + component / 2u;
        const Instruction* first_sample =
            (const Instruction*)(blob + sizeof(header)) + 8u + component;
        const Instruction* second_sample =
            (const Instruction*)(blob + sizeof(header)) + 12u + component;

        assert(local_constant->opcode == RSH1_CONST_F32);
        assert(local_constant->destination == 16u + component / 2u);
        assert(local_coordinate->opcode == RSH1_ADD_F32);
        assert(local_coordinate->destination == 18u + component / 2u);
        assert(local_coordinate->source0 == component / 2u);
        assert(local_coordinate->source1 == 16u + component / 2u);
        assert(first_sample->source0 == 18u && first_sample->source1 == 19u);
        assert(second_sample->source0 == 18u && second_sample->source1 == 19u);
    }

    /* A local affine coordinate and a call-local affine offset are executed
     * sequentially through separate temporary registers, preserving GLSL's
     * Float32 evaluation order rather than algebraically folding them. */
    ringl_shader_source(shader, varying_local_affine_call_offset_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 21u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.instruction_count == 21u);
    assert(header.register_count == 16u);
    assert(header.resource_count == 2u);
    for (component = 0u; component < 4u; ++component) {
        const Instruction* local_coordinate =
            (const Instruction*)(blob + sizeof(header)) + 6u + component / 2u;
        const Instruction* call_coordinate =
            (const Instruction*)(blob + sizeof(header)) + 10u + component / 2u;
        const Instruction* sample =
            (const Instruction*)(blob + sizeof(header)) + 12u + component;

        assert(local_coordinate->destination == 10u + component / 2u);
        assert(local_coordinate->source0 == component / 2u);
        assert(call_coordinate->destination == 14u + component / 2u);
        assert(call_coordinate->source0 == 10u + component / 2u);
        assert(sample->source0 == 14u && sample->source1 == 15u);
    }

    /* Two local affine values execute in source order. The second operation
     * reads the first result rather than folding both Float32 operations. */
    ringl_shader_source(shader, varying_local_affine_chain_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 21u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.instruction_count == 21u);
    assert(header.register_count == 16u);
    assert(header.resource_count == 2u);
    for (component = 0u; component < 4u; ++component) {
        const Instruction* first_coordinate =
            (const Instruction*)(blob + sizeof(header)) + 6u + component / 2u;
        const Instruction* second_coordinate =
            (const Instruction*)(blob + sizeof(header)) + 10u + component / 2u;
        const Instruction* sample =
            (const Instruction*)(blob + sizeof(header)) + 12u + component;

        assert(first_coordinate->destination == 10u + component / 2u);
        assert(first_coordinate->source0 == component / 2u);
        assert(second_coordinate->destination == 14u + component / 2u);
        assert(second_coordinate->source0 == 10u + component / 2u);
        assert(sample->source0 == 14u && sample->source1 == 15u);
    }

    /* Six local affine values execute in declaration order. Each operation
     * consumes the preceding result without algebraically folding Float32
     * operations. */
    ringl_shader_source(shader, varying_six_local_affine_chain_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 37u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.instruction_count == 37u);
    assert(header.register_count == 32u);
    assert(header.resource_count == 2u);
    for (uint32_t local_index = 0u; local_index < 6u; ++local_index) {
        for (component = 0u; component < 2u; ++component) {
            const Instruction* coordinate =
                (const Instruction*)(blob + sizeof(header)) +
                6u + local_index * 4u + component;
            uint32_t expected_source = local_index == 0u
                ? component : 10u + (local_index - 1u) * 4u + component;

            assert(coordinate->destination ==
                   10u + local_index * 4u + component);
            assert(coordinate->source0 == expected_source);
        }
    }
    for (component = 0u; component < 4u; ++component) {
        const Instruction* sample =
            (const Instruction*)(blob + sizeof(header)) + 28u + component;

        assert(sample->source0 == 30u && sample->source1 == 31u);
    }
    assert(((const Instruction*)(blob + sizeof(header)) + 6u)->opcode ==
           RSH1_MUL_F32);
    assert(((const Instruction*)(blob + sizeof(header)) + 10u)->opcode ==
           RSH1_DIV_F32);

    /* Eight locals are accepted when the final one-call program still fits
     * RSH1, rather than being rejected by the former fixed six-local limit. */
    ringl_shader_source(shader, varying_eight_local_affine_chain_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 45u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.instruction_count == 45u);
    assert(header.register_count == 40u);
    assert(header.resource_count == 2u);
    for (uint32_t local_index = 0u; local_index < 8u; ++local_index) {
        for (component = 0u; component < 2u; ++component) {
            const Instruction* coordinate =
                (const Instruction*)(blob + sizeof(header)) +
                6u + local_index * 4u + component;
            uint32_t expected_source = local_index == 0u
                ? component : 10u + (local_index - 1u) * 4u + component;

            assert(coordinate->destination ==
                   10u + local_index * 4u + component);
            assert(coordinate->source0 == expected_source);
        }
    }
    for (component = 0u; component < 4u; ++component) {
        const Instruction* sample =
            (const Instruction*)(blob + sizeof(header)) + 36u + component;

        assert(sample->source0 == 38u && sample->source1 == 39u);
    }

    /* The parser ceiling never bypasses the executable RSH1 limits. Eight
     * locals plus eight call-local offsets must fail before an IR blob is
     * published. */
    ringl_shader_source(shader, varying_capacity_rejected_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) != 0);

    /* Each sample may apply one finite vec2 offset to the interpolated
     * coordinate. The temporary coordinate registers are deliberately
     * reused only after the preceding sample, so this also verifies the
     * bounded ADD/SUB execution path accepted by RinGPU's RSH1 validator. */
    ringl_shader_source(shader, varying_affine_offsets_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 29u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.instruction_count == 29u);
    assert(header.register_count == 20u);
    assert(header.resource_count == 4u);
    {
        float first_u = 0.5f;
        float first_v = -0.5f;
        float second_u = 0.25f;
        float second_v = 0.75f;
        uint32_t first_u_bits;
        uint32_t first_v_bits;
        uint32_t second_u_bits;
        uint32_t second_v_bits;

        memcpy(&first_u_bits, &first_u, sizeof(first_u_bits));
        memcpy(&first_v_bits, &first_v, sizeof(first_v_bits));
        memcpy(&second_u_bits, &second_u, sizeof(second_u_bits));
        memcpy(&second_v_bits, &second_v, sizeof(second_v_bits));
        assert(((const Instruction*)(blob + sizeof(header)) + 4u)->immediate ==
               first_u_bits);
        assert(((const Instruction*)(blob + sizeof(header)) + 5u)->immediate ==
               first_v_bits);
        assert(((const Instruction*)(blob + sizeof(header)) + 12u)->immediate ==
               second_u_bits);
        assert(((const Instruction*)(blob + sizeof(header)) + 13u)->immediate ==
               second_v_bits);
    }
    for (component = 0u; component < 4u; ++component) {
        const Instruction* first_constant =
            (const Instruction*)(blob + sizeof(header)) + 4u + component / 2u;
        const Instruction* first_coordinate =
            (const Instruction*)(blob + sizeof(header)) + 6u + component / 2u;
        const Instruction* first_sample =
            (const Instruction*)(blob + sizeof(header)) + 8u + component;
        const Instruction* second_constant =
            (const Instruction*)(blob + sizeof(header)) + 12u + component / 2u;
        const Instruction* second_coordinate =
            (const Instruction*)(blob + sizeof(header)) + 14u + component / 2u;
        const Instruction* second_sample =
            (const Instruction*)(blob + sizeof(header)) + 16u + component;

        assert(first_constant->opcode == RSH1_CONST_F32);
        assert(first_constant->destination == 16u + component / 2u);
        assert(first_coordinate->opcode == RSH1_ADD_F32);
        assert(first_coordinate->destination == 18u + component / 2u);
        assert(first_coordinate->source0 == component / 2u);
        assert(first_coordinate->source1 == 16u + component / 2u);
        assert(first_sample->source0 == 18u && first_sample->source1 == 19u);
        assert(first_sample->resource == 0u && first_sample->immediate == 1u);
        assert(second_constant->opcode == RSH1_CONST_F32);
        assert(second_constant->destination == 16u + component / 2u);
        assert(second_coordinate->opcode == RSH1_SUB_F32);
        assert(second_coordinate->destination == 18u + component / 2u);
        assert(second_coordinate->source0 == component / 2u);
        assert(second_coordinate->source1 == 16u + component / 2u);
        assert(second_sample->source0 == 18u && second_sample->source1 == 19u);
        assert(second_sample->resource == 2u && second_sample->immediate == 3u);
    }

    /* A declared coordinate vec2 is lowered through the same RSH1 constants
     * as the literal form. The standalone shader path has WebGL's initial
     * zero uniform value; the native draw test below verifies a later public
     * uniform2f update replaces these exact constants atomically. */
    ringl_shader_source(shader, varying_uniform_offset_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 17u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.instruction_count == 17u);
    assert(header.register_count == 12u);
    assert(header.resource_count == 2u);
    for (component = 0u; component < 2u; ++component) {
        const Instruction* constant =
            (const Instruction*)(blob + sizeof(header)) + 4u + component;
        const Instruction* add =
            (const Instruction*)(blob + sizeof(header)) + 6u + component;
        const Instruction* sample =
            (const Instruction*)(blob + sizeof(header)) + 8u + component;

        assert(constant->opcode == RSH1_CONST_F32);
        assert(constant->destination == 8u + component);
        assert(constant->immediate == 0u);
        assert(add->opcode == RSH1_ADD_F32);
        assert(add->destination == 10u + component);
        assert(add->source0 == component && add->source1 == 8u + component);
        assert(sample->source0 == 10u && sample->source1 == 11u);
    }

    ringl_shader_source(shader, varying_uniform_left_add_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 17u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    for (component = 0u; component < 2u; ++component) {
        const Instruction* add =
            (const Instruction*)(blob + sizeof(header)) + 6u + component;

        assert(add->opcode == RSH1_ADD_F32);
        assert(add->source0 == component);
        assert(add->source1 == 8u + component);
    }

    ringl_shader_source(shader, varying_uniform_left_multiply_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 17u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    for (component = 0u; component < 2u; ++component) {
        const Instruction* multiply =
            (const Instruction*)(blob + sizeof(header)) + 6u + component;

        assert(multiply->opcode == RSH1_MUL_F32);
        assert(multiply->source0 == component);
        assert(multiply->source1 == 8u + component);
    }

    /* A uniform divisor retains WebGL's zero default so linking succeeds;
     * a later zero update is rejected by the executor before target publish.
     * The RSH1 module must nevertheless keep varying/uniform operand order. */
    ringl_shader_source(shader, varying_uniform_right_divide_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 17u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    for (component = 0u; component < 2u; ++component) {
        const Instruction* constant =
            (const Instruction*)(blob + sizeof(header)) + 4u + component;
        const Instruction* divide =
            (const Instruction*)(blob + sizeof(header)) + 6u + component;

        assert(constant->opcode == RSH1_CONST_F32);
        assert(constant->destination == 8u + component);
        assert(constant->immediate == 0u);
        assert(divide->opcode == RSH1_DIV_F32);
        assert(divide->destination == 10u + component);
        assert(divide->source0 == component);
        assert(divide->source1 == 8u + component);
    }

    /* The same generic preflight makes a uniform-led division executable.
     * Its operands must be constant / varying rather than silently reversed. */
    ringl_shader_source(shader, varying_uniform_left_divide_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 17u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    for (component = 0u; component < 2u; ++component) {
        const Instruction* divide =
            (const Instruction*)(blob + sizeof(header)) + 6u + component;

        assert(divide->opcode == RSH1_DIV_F32);
        assert(divide->destination == 10u + component);
        assert(divide->source0 == 8u + component);
        assert(divide->source1 == component);
    }

    /* A coordinate uniform may also be the left operand. The constants still
     * come from the program-owned uniform state, but subtraction must retain
     * GLSL operand order rather than silently treating it as `uv - offset`. */
    ringl_shader_source(shader, varying_uniform_left_subtract_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 17u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.instruction_count == 17u);
    assert(header.register_count == 12u);
    assert(header.resource_count == 2u);
    for (component = 0u; component < 2u; ++component) {
        const Instruction* constant =
            (const Instruction*)(blob + sizeof(header)) + 4u + component;
        const Instruction* subtract =
            (const Instruction*)(blob + sizeof(header)) + 6u + component;
        const Instruction* sample =
            (const Instruction*)(blob + sizeof(header)) + 8u + component;

        assert(constant->opcode == RSH1_CONST_F32);
        assert(constant->destination == 8u + component);
        assert(constant->immediate == 0u);
        assert(subtract->opcode == RSH1_SUB_F32);
        assert(subtract->destination == 10u + component);
        assert(subtract->source0 == 8u + component);
        assert(subtract->source1 == component);
        assert(sample->source0 == 10u && sample->source1 == 11u);
    }

    /* Repeated calls over a non-first declaration compact to one RSH1
     * image/sampler pair while retaining independent samples and sums. */
    ringl_shader_source(shader, varying_repeated_partial_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 29u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.instruction_count == 29u);
    assert(header.register_count == 24u);
    assert(header.resource_count == 2u);
    for (component = 0u; component < 4u; ++component) {
        const Instruction* first_sample =
            (const Instruction*)(blob + sizeof(header)) + 4u + component;
        const Instruction* second_sample =
            (const Instruction*)(blob + sizeof(header)) + 8u + component;
        const Instruction* third_sample =
            (const Instruction*)(blob + sizeof(header)) + 12u + component;
        const Instruction* first_add =
            (const Instruction*)(blob + sizeof(header)) + 16u + component;
        const Instruction* second_add =
            (const Instruction*)(blob + sizeof(header)) + 20u + component;
        const Instruction* store =
            (const Instruction*)(blob + sizeof(header)) + 24u + component;

        assert(first_sample->resource == 0u && first_sample->immediate == 1u);
        assert(second_sample->resource == 0u && second_sample->immediate == 1u);
        assert(third_sample->resource == 0u && third_sample->immediate == 1u);
        assert(first_add->source0 == 2u + component);
        assert(first_add->source1 == 6u + component);
        assert(second_add->source0 == 14u + component);
        assert(second_add->source1 == 10u + component);
        assert(store->source0 == 18u + component);
    }

    /* The shared-coordinate profile reaches the declaration/call ceiling
     * without exceeding the RSH1 limits. Calls remain in source order while
     * active resource pairs remain declaration ordered. */
    ringl_shader_source(shader, varying_eight_sampler_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 69u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.instruction_count == 69u);
    assert(header.register_count == 64u);
    assert(header.resource_count == 16u);
    for (component = 0u; component < 4u; ++component) {
        const Instruction* first_sample =
            (const Instruction*)(blob + sizeof(header)) + 4u + component;
        const Instruction* final_sample =
            (const Instruction*)(blob + sizeof(header)) + 32u + component;

        assert(first_sample->resource == 14u && first_sample->immediate == 15u);
        assert(final_sample->resource == 0u && final_sample->immediate == 1u);
    }

    /* Two local affine forms and per-call affine offsets may coexist at the
     * eight-call ceiling. The local coordinates execute in source order, and
     * the final register quartet is reused only after each sample consumes it. */
    ringl_shader_source(shader, varying_local_affine_eight_sampler_source,
                        -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 109u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.instruction_count == 109u);
    assert(header.register_count == 76u);
    assert(header.resource_count == 16u);
    for (component = 0u; component < 4u; ++component) {
        const Instruction* local_coordinate =
            (const Instruction*)(blob + sizeof(header)) + 6u + component / 2u;
        const Instruction* second_local_coordinate =
            (const Instruction*)(blob + sizeof(header)) + 10u + component / 2u;
        const Instruction* call_coordinate =
            (const Instruction*)(blob + sizeof(header)) + 14u + component / 2u;
        const Instruction* first_sample =
            (const Instruction*)(blob + sizeof(header)) + 16u + component;
        const Instruction* final_sample =
            (const Instruction*)(blob + sizeof(header)) + 72u + component;

        assert(local_coordinate->destination == 66u + component / 2u);
        assert(local_coordinate->source0 == component / 2u);
        assert(second_local_coordinate->destination == 70u + component / 2u);
        assert(second_local_coordinate->source0 == 66u + component / 2u);
        assert(call_coordinate->destination == 74u + component / 2u);
        assert(call_coordinate->source0 == 70u + component / 2u);
        assert(first_sample->source0 == 74u && first_sample->source1 == 75u);
        assert(final_sample->source0 == 74u && final_sample->source1 == 75u);
    }

    /* Three independent perspective vec2 inputs stay distinct in RSH1. The
     * third pair is loaded into the bounded native varying tail instead of
     * being folded onto either earlier coordinate. */
    ringl_shader_source(shader, varying_three_coordinate_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 31u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.instruction_count == 31u);
    assert(header.register_count == 26u);
    assert(header.input_count == 6u);
    assert(header.output_count == 4u);
    assert(header.resource_count == 6u);
    for (component = 0u; component < 4u; ++component) {
        const Instruction* first_sample =
            (const Instruction*)(blob + sizeof(header)) + 6u + component;
        const Instruction* second_sample =
            (const Instruction*)(blob + sizeof(header)) + 10u + component;
        const Instruction* third_sample =
            (const Instruction*)(blob + sizeof(header)) + 14u + component;

        assert(first_sample->opcode == RSH1_SAMPLE_IMAGE_2D_F32);
        assert(first_sample->source0 == 0u && first_sample->source1 == 1u);
        assert(second_sample->opcode == RSH1_SAMPLE_IMAGE_2D_F32);
        assert(second_sample->source0 == 22u && second_sample->source1 == 23u);
        assert(third_sample->opcode == RSH1_SAMPLE_IMAGE_2D_F32);
        assert(third_sample->source0 == 24u && third_sample->source1 == 25u);
    }

    /* The fourth perspective coordinate has its own two RSH1 inputs. It
     * must not alias the pre-existing six-scalar native route. */
    ringl_shader_source(shader, varying_four_coordinate_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 41u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.instruction_count == 41u);
    assert(header.register_count == 36u);
    assert(header.input_count == 8u);
    assert(header.output_count == 4u);
    assert(header.resource_count == 8u);
    for (component = 0u; component < 4u; ++component) {
        const Instruction* first_sample =
            (const Instruction*)(blob + sizeof(header)) + 8u + component;
        const Instruction* second_sample =
            (const Instruction*)(blob + sizeof(header)) + 12u + component;
        const Instruction* third_sample =
            (const Instruction*)(blob + sizeof(header)) + 16u + component;
        const Instruction* fourth_sample =
            (const Instruction*)(blob + sizeof(header)) + 20u + component;

        assert(first_sample->source0 == 0u && first_sample->source1 == 1u);
        assert(second_sample->source0 == 30u && second_sample->source1 == 31u);
        assert(third_sample->source0 == 32u && third_sample->source1 == 33u);
        assert(fourth_sample->source0 == 34u && fourth_sample->source1 == 35u);
    }

    /* The published eight-varying contract is sixteen scalar coordinates.
     * Every pair must retain its own RSH1 input registers; truncating pairs
     * five through eight or aliasing them to the fourth pair would sample a
     * different image before the native backend gets a chance to reject it. */
    ringl_shader_source(shader, varying_eight_coordinate_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 81u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.instruction_count == 81u);
    assert(header.register_count == 76u);
    assert(header.input_count == 16u);
    assert(header.output_count == 4u);
    assert(header.resource_count == 16u);
    {
        static const uint16_t expected_coordinates[8][2] = {
            {0u, 1u}, {62u, 63u}, {64u, 65u}, {66u, 67u},
            {68u, 69u}, {70u, 71u}, {72u, 73u}, {74u, 75u}
        };
        uint32_t sample_index;

        for (sample_index = 0u; sample_index < 8u; ++sample_index) {
            for (component = 0u; component < 4u; ++component) {
                const Instruction* sample =
                    (const Instruction*)(blob + sizeof(header)) +
                    16u + sample_index * 4u + component;

                assert(sample->opcode == RSH1_SAMPLE_IMAGE_2D_F32);
                assert(sample->source0 ==
                       expected_coordinates[sample_index][0]);
                assert(sample->source1 ==
                       expected_coordinates[sample_index][1]);
                assert(sample->resource == sample_index * 2u);
                assert(sample->immediate == sample_index * 2u + 1u);
            }
        }
    }

    /* A four-UV local can combine any two physical pairs while direct samples
     * continue to read their own registers. */
    ringl_shader_source(shader, varying_four_coordinate_local_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 43u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.instruction_count == 43u);
    assert(header.register_count == 40u);
    assert(header.input_count == 8u);
    for (component = 0u; component < 2u; ++component) {
        const Instruction* combine =
            (const Instruction*)(blob + sizeof(header)) + 8u + component;

        assert(combine->opcode == RSH1_ADD_F32);
        assert(combine->source0 == 30u + component);
        assert(combine->source1 == 34u + component);
        assert(combine->destination == 38u + component);
    }
    for (component = 0u; component < 4u; ++component) {
        const Instruction* first_sample =
            (const Instruction*)(blob + sizeof(header)) + 10u + component;
        const Instruction* second_sample =
            (const Instruction*)(blob + sizeof(header)) + 14u + component;
        const Instruction* third_sample =
            (const Instruction*)(blob + sizeof(header)) + 18u + component;
        const Instruction* fourth_sample =
            (const Instruction*)(blob + sizeof(header)) + 22u + component;

        assert(first_sample->source0 == 0u && first_sample->source1 == 1u);
        assert(second_sample->source0 == 38u && second_sample->source1 == 39u);
        assert(third_sample->source0 == 32u && third_sample->source1 == 33u);
        assert(fourth_sample->source0 == 38u && fourth_sample->source1 == 39u);
    }

    ringl_shader_source(shader, varying_three_coordinate_local_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 33u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.instruction_count == 33u);
    assert(header.register_count == 30u);
    assert(header.input_count == 6u);
    for (component = 0u; component < 4u; ++component) {
        const Instruction* first_sample =
            (const Instruction*)(blob + sizeof(header)) + 8u + component;
        const Instruction* second_sample =
            (const Instruction*)(blob + sizeof(header)) + 12u + component;
        const Instruction* third_sample =
            (const Instruction*)(blob + sizeof(header)) + 16u + component;

        assert(first_sample->source0 == 28u && first_sample->source1 == 29u);
        assert(second_sample->source0 == 28u && second_sample->source1 == 29u);
        assert(third_sample->source0 == 24u && third_sample->source1 == 25u);
    }

    /* A three-UV local may use the second and third physical input pairs;
     * direct first-Uv sampling must remain separate from that result. */
    ringl_shader_source(shader, varying_three_coordinate_third_pair_local_source,
                        -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 33u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.instruction_count == 33u);
    assert(header.register_count == 30u);
    assert(header.input_count == 6u);
    for (component = 0u; component < 2u; ++component) {
        const Instruction* combine =
            (const Instruction*)(blob + sizeof(header)) + 6u + component;

        assert(combine->opcode == RSH1_SUB_F32);
        assert(combine->source0 == 22u + component);
        assert(combine->source1 == 24u + component);
    }
    for (component = 0u; component < 4u; ++component) {
        const Instruction* first_sample =
            (const Instruction*)(blob + sizeof(header)) + 8u + component;
        const Instruction* second_sample =
            (const Instruction*)(blob + sizeof(header)) + 12u + component;
        const Instruction* third_sample =
            (const Instruction*)(blob + sizeof(header)) + 16u + component;

        assert(first_sample->source0 == 0u && first_sample->source1 == 1u);
        assert(second_sample->source0 == 28u && second_sample->source1 == 29u);
        assert(third_sample->source0 == 28u && third_sample->source1 == 29u);
    }

    /* A local built from second/third UVs may feed a finite affine local;
     * the second stage must read the first RSH1 result in source order. */
    ringl_shader_source(shader,
                        varying_three_coordinate_third_pair_local_chain_source,
                        -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 35u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.instruction_count == 35u);
    assert(header.register_count == 34u);
    assert(header.input_count == 6u);
    for (component = 0u; component < 2u; ++component) {
        const Instruction* combine =
            (const Instruction*)(blob + sizeof(header)) + 6u + component;
        const Instruction* affine =
            (const Instruction*)(blob + sizeof(header)) + 8u + component;

        assert(combine->opcode == RSH1_SUB_F32);
        assert(combine->source0 == 22u + component);
        assert(combine->source1 == 24u + component);
        assert(affine->opcode == RSH1_ADD_F32);
        assert(affine->source0 == 28u + component);
        assert(affine->source1 == component);
        assert(affine->destination == 32u + component);
    }
    for (component = 0u; component < 4u; ++component) {
        const Instruction* first_sample =
            (const Instruction*)(blob + sizeof(header)) + 10u + component;
        const Instruction* second_sample =
            (const Instruction*)(blob + sizeof(header)) + 14u + component;
        const Instruction* third_sample =
            (const Instruction*)(blob + sizeof(header)) + 18u + component;

        assert(first_sample->source0 == 0u && first_sample->source1 == 1u);
        assert(second_sample->source0 == 32u && second_sample->source1 == 33u);
        assert(third_sample->source0 == 32u && third_sample->source1 == 33u);
    }

    /* The historic three-UV shape matcher intentionally rejects reusing a
     * pair in a second local combination. The generic sampler lowerer must
     * still accept this typed, finite RSH1 expression and retain all three
     * fragment inputs plus the real image/sampler resource pair. */
    ringl_shader_source(shader, varying_three_coordinate_reused_pair_source,
                        -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size > sizeof(header));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.input_count == 6u && header.output_count == 4u &&
           header.resource_count == 2u);
    assert(header.instruction_count >= 16u && header.register_count >= 14u);

    ringl_context_destroy(context);
    return 0;
}
