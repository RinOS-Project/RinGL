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
    const char* varying_unsupported_multiple_local_source =
        "uniform sampler2D colorTexture; varying vec2 uv; "
        "void main() { vec2 sampleUv = uv; vec2 otherUv = sampleUv; "
        "gl_FragColor = texture2D(colorTexture, otherUv); }";
    const char* varying_affine_offsets_source =
        "uniform sampler2D firstTexture; uniform sampler2D secondTexture; "
        "varying vec2 uv; void main() { gl_FragColor = "
        "texture2D(firstTexture, uv + vec2(5e-1, -5e-1)) + "
        "texture2D(secondTexture, uv - vec2(2.5e-1, 7.5e-1)); }";
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
        "void main() { vec2 sampleUv = uv + vec2(0.125, -0.125); "
        "gl_FragColor = texture2D(s7, sampleUv + vec2(0.0, 0.0)) + "
        "texture2D(s6, sampleUv + vec2(0.0, 0.0)) + "
        "texture2D(s5, sampleUv + vec2(0.0, 0.0)) + "
        "texture2D(s4, sampleUv + vec2(0.0, 0.0)) + "
        "texture2D(s3, sampleUv + vec2(0.0, 0.0)) + "
        "texture2D(s2, sampleUv + vec2(0.0, 0.0)) + "
        "texture2D(s1, sampleUv + vec2(0.0, 0.0)) + "
        "texture2D(s0, sampleUv + vec2(0.0, 0.0)); }";
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

    /* Further local values remain closed until their RSH1 execution is
     * implemented; parser acceptance cannot make them executable. */
    ringl_shader_source(shader, varying_unsupported_multiple_local_source,
                        -1);
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

    /* The local affine and per-call affine forms may coexist at the eight
     * call ceiling. The local coordinate is hoisted once, while the second
     * register quartet is reused only after each sample has consumed it. */
    ringl_shader_source(shader, varying_local_affine_eight_sampler_source,
                        -1);
    ringl_compile_shader(shader);
    assert(ringl_get_shader_compile_status(shader) == RINGL_TRUE);
    assert(ringl_lower_shader_rsh1(shader) == 0);
    size = ringl_get_shader_rsh1_size(shader);
    assert(size == sizeof(header) + 105u * sizeof(Instruction));
    assert(ringl_copy_shader_rsh1(shader, blob, sizeof(blob)) == size);
    memcpy(&header, blob, sizeof(header));
    assert(header.instruction_count == 105u);
    assert(header.register_count == 72u);
    assert(header.resource_count == 16u);
    for (component = 0u; component < 4u; ++component) {
        const Instruction* local_coordinate =
            (const Instruction*)(blob + sizeof(header)) + 6u + component / 2u;
        const Instruction* call_coordinate =
            (const Instruction*)(blob + sizeof(header)) + 10u + component / 2u;
        const Instruction* first_sample =
            (const Instruction*)(blob + sizeof(header)) + 12u + component;
        const Instruction* final_sample =
            (const Instruction*)(blob + sizeof(header)) + 68u + component;

        assert(local_coordinate->destination == 66u + component / 2u);
        assert(local_coordinate->source0 == component / 2u);
        assert(call_coordinate->destination == 70u + component / 2u);
        assert(call_coordinate->source0 == 66u + component / 2u);
        assert(first_sample->source0 == 70u && first_sample->source1 == 71u);
        assert(final_sample->source0 == 70u && final_sample->source1 == 71u);
    }

    ringl_context_destroy(context);
    return 0;
}
