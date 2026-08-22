/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <ringl/ringl.h>

#define RSH1_MAGIC UINT32_C(0x31485352)
#define RSH1_LOAD_INPUT_F32 45u
#define RSH1_SAMPLE_IMAGE_2D_F32 55u
#define RSH1_STORE_OUTPUT_F32 46u

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
    uint8_t blob[512];
    uint32_t size;
    uint32_t expected_bits;
    Header header;
    Instruction instructions[15];
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

    ringl_context_destroy(context);
    return 0;
}
