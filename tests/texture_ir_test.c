/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <ringl/ringl.h>

#define RSH1_MAGIC UINT32_C(0x31485352)
#define RSH1_LOAD_INPUT_F32 45u
#define RSH1_SAMPLE_IMAGE_2D_F32 55u
#define RSH1_STORE_OUTPUT_F32 46u
#define RSH1_ADD_F32 20u

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
    const char* unused_sampler_source =
        "uniform sampler2D firstTexture;\n"
        "uniform sampler2D secondTexture;\n"
        "void main() {\n"
        "  gl_FragColor = texture2D(firstTexture, vec2(0.25)) + "
        "texture2D(firstTexture, vec2(0.75));\n"
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
    float scalar_splat = 0.75f;

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

    /* RinGPU validates each declared resource as typed and used. Do not hide
     * an unused declaration by silently manufacturing a dummy resource use. */
    ringl_shader_source(shader, unused_sampler_source, -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) != 0);

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

    ringl_context_destroy(context);
    return 0;
}
