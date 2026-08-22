/* SPDX-License-Identifier: MIT */
#include "texture_lower.h"
#include "glsl_parser.h"
#include "rsh1_abi.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TextureCall {
    uint32_t sampler_index;
    float u;
    float v;
} TextureCall;

#define RINGL_TEXTURE_MAX_SAMPLERS RINGL_GLSL_MAX_SAMPLER_UNIFORMS

_Static_assert(10u * RINGL_TEXTURE_MAX_SAMPLERS <=
                   RINGL_RSH1_MAX_REGISTERS,
               "texture profile exceeds the RSH1 register ceiling");
_Static_assert(10u * RINGL_TEXTURE_MAX_SAMPLERS + 5u <=
                   RINGL_RSH1_MAX_INSTRUCTIONS,
               "texture profile exceeds the RSH1 instruction ceiling");

static const char* skip_space(const char* p, const char* end)
{
    while (p < end) {
        if (isspace((unsigned char)*p)) {
            ++p;
            continue;
        }
        if (*p == '/' && p + 1u < end && p[1] == '/') {
            p += 2u;
            while (p < end && *p != '\n')
                ++p;
            continue;
        }
        break;
    }
    return p;
}

static int parse_ident(const char** cursor, const char* end,
                       char* out, size_t capacity)
{
    const char* p = skip_space(*cursor, end);
    size_t n = 0u;

    if (p >= end || (!isalpha((unsigned char)*p) && *p != '_'))
        return 0;
    while (p < end && (isalnum((unsigned char)*p) || *p == '_')) {
        if (n + 1u >= capacity)
            return 0;
        out[n++] = *p++;
    }
    out[n] = '\0';
    *cursor = p;
    return 1;
}

static int expect_char(const char** cursor, const char* end, char expected)
{
    const char* p = skip_space(*cursor, end);
    if (p >= end || *p != expected)
        return 0;
    *cursor = p + 1;
    return 1;
}

static int parse_float(const char** cursor, const char* end, float* value)
{
    const char* p = skip_space(*cursor, end);
    char temp[64];
    char* parsed_end = NULL;
    size_t n = 0u;

    if (p >= end)
        return 0;
    while (p < end && n + 1u < sizeof(temp)) {
        char c = *p;
        if (!(isdigit((unsigned char)c) || c == '.' || c == '+' || c == '-' ||
              c == 'e' || c == 'E')) {
            break;
        }
        temp[n++] = c;
        ++p;
    }
    if (n == 0u)
        return 0;
    temp[n] = '\0';
    *value = strtof(temp, &parsed_end);
    if (parsed_end == temp || *parsed_end != '\0' || !isfinite(*value))
        return 0;
    *cursor = p;
    return 1;
}

static void init_instruction(RinGLRsh1InstructionV1* ins, uint16_t opcode)
{
    memset(ins, 0, sizeof(*ins));
    ins->opcode = opcode;
    ins->destination = RINGL_RSH1_UNUSED;
    ins->source0 = RINGL_RSH1_UNUSED;
    ins->source1 = RINGL_RSH1_UNUSED;
    ins->resource = RINGL_RSH1_UNUSED;
}

static int sampler_index_for(const char* sampler_names, size_t stride,
                             uint32_t sampler_count, const char* name,
                             uint32_t* index_out)
{
    uint32_t index;

    if (sampler_names == NULL || stride == 0u || name == NULL ||
        index_out == NULL)
        return 0;
    for (index = 0u; index < sampler_count; ++index) {
        const char* candidate = sampler_names + (size_t)index * stride;
        if (candidate[0] != '\0' && strcmp(candidate, name) == 0) {
            *index_out = index;
            return 1;
        }
    }
    return 0;
}

static int parse_texture_call(const char** cursor, const char* end,
                              const char* sampler_names, size_t stride,
                              uint32_t sampler_count, TextureCall* call)
{
    char ident[64];
    char ctor[16];
    int scalar_splat;

    if (call == NULL || !parse_ident(cursor, end, ident, sizeof(ident)) ||
        strcmp(ident, "texture2D") != 0 ||
        !expect_char(cursor, end, '(') ||
        !parse_ident(cursor, end, ident, sizeof(ident)) ||
        !sampler_index_for(sampler_names, stride, sampler_count, ident,
                           &call->sampler_index) ||
        !expect_char(cursor, end, ',') ||
        !parse_ident(cursor, end, ctor, sizeof(ctor)) ||
        strcmp(ctor, "vec2") != 0 || !expect_char(cursor, end, '(') ||
        !parse_float(cursor, end, &call->u)) {
        return 0;
    }
    scalar_splat = !expect_char(cursor, end, ',');
    if ((scalar_splat && !expect_char(cursor, end, ')')) ||
        (!scalar_splat && (!parse_float(cursor, end, &call->v) ||
                           !expect_char(cursor, end, ')'))) ||
        !expect_char(cursor, end, ')')) {
        return 0;
    }
    if (scalar_splat)
        call->v = call->u;
    return 1;
}

static int find_fragment_assignment(const char* source, const char* end,
                                    const char** assignment_out)
{
    const char* cursor = source;

    while (cursor < end) {
        char ident[64];
        const char* after = cursor;

        if (parse_ident(&after, end, ident, sizeof(ident))) {
            if (strcmp(ident, "gl_FragColor") == 0 &&
                expect_char(&after, end, '=')) {
                *assignment_out = after;
                return 1;
            }
            cursor = after;
        } else {
            ++cursor;
        }
    }
    return 0;
}

static uint32_t count_texture_calls(const char* source, const char* end)
{
    const char* cursor = source;
    uint32_t count = 0u;

    while (cursor < end) {
        char ident[64];
        const char* after = cursor;

        if (parse_ident(&after, end, ident, sizeof(ident))) {
            if (strcmp(ident, "texture2D") == 0)
                ++count;
            cursor = after;
        } else {
            ++cursor;
        }
    }
    return count;
}

static void store_result(RinGLGlslLowerResult* result,
                         const RinGLRsh1HeaderV1* header,
                         const RinGLRsh1InstructionV1* instructions)
{
    memcpy(result->bytes, header, sizeof(*header));
    memcpy(result->bytes + sizeof(*header), instructions,
           (size_t)header->instruction_count * sizeof(*instructions));
    result->ok = 1u;
    result->instruction_count = header->instruction_count;
    result->register_count = header->register_count;
    result->input_count = header->input_count;
    result->output_count = header->output_count;
    result->byte_size = header->total_size;
}

/* The fragment profile uses one constant coordinate pair and one RGBA sample
 * per declared sampler. Results are accumulated left-to-right. The register
 * layout is deliberately formulaic so every supported count has a fixed,
 * auditable RSH1 shape:
 *
 *   [coordinates 2N][sampled RGBA 4N][raster inputs 4][sums 4(N - 1)]
 *
 * This preserves the historic N=1 and N=2 bytecode layouts exactly while
 * making the already-general RinGPU binding table reachable up to its RinGL
 * sampler declaration limit. */
static void emit_texture_sum(const TextureCall* calls, uint32_t sampler_count,
                             RinGLGlslLowerResult* result)
{
    RinGLRsh1HeaderV1 header;
    RinGLRsh1InstructionV1 instructions[RINGL_RSH1_MAX_INSTRUCTIONS];
    uint32_t sample_base = sampler_count * 2u;
    uint32_t input_base = sampler_count * 6u;
    uint32_t add_base = 4u + sampler_count * 6u;
    uint32_t store_base = sampler_count * 10u;
    uint32_t final_base;
    uint32_t sampler;
    uint32_t component;

    memset(instructions, 0, sizeof(instructions));
    for (component = 0u; component < 4u; ++component) {
        init_instruction(&instructions[component], RINGL_RSH1_OP_LOAD_INPUT_F32);
        instructions[component].destination =
            (uint16_t)(input_base + component);
        instructions[component].immediate = component;
    }
    for (sampler = 0u; sampler < sampler_count; ++sampler) {
        uint32_t u_bits;
        uint32_t v_bits;
        uint32_t coordinate_instruction = 4u + sampler * 2u;

        memcpy(&u_bits, &calls[sampler].u, sizeof(u_bits));
        memcpy(&v_bits, &calls[sampler].v, sizeof(v_bits));
        init_instruction(&instructions[coordinate_instruction],
                         RINGL_RSH1_OP_CONST_F32);
        instructions[coordinate_instruction].destination =
            (uint16_t)(sampler * 2u);
        instructions[coordinate_instruction].immediate = u_bits;
        init_instruction(&instructions[coordinate_instruction + 1u],
                         RINGL_RSH1_OP_CONST_F32);
        instructions[coordinate_instruction + 1u].destination =
            (uint16_t)(sampler * 2u + 1u);
        instructions[coordinate_instruction + 1u].immediate = v_bits;
    }
    for (sampler = 0u; sampler < sampler_count; ++sampler) {
        uint32_t resource = calls[sampler].sampler_index * 2u;
        for (component = 0u; component < 4u; ++component) {
            uint32_t instruction = 4u + sampler_count * 2u +
                sampler * 4u + component;
            init_instruction(&instructions[instruction],
                             RINGL_RSH1_OP_SAMPLE_IMAGE_2D_F32);
            instructions[instruction].flags = (uint16_t)component;
            instructions[instruction].destination =
                (uint16_t)(sample_base + sampler * 4u + component);
            instructions[instruction].source0 = (uint16_t)(sampler * 2u);
            instructions[instruction].source1 =
                (uint16_t)(sampler * 2u + 1u);
            instructions[instruction].resource = (uint16_t)resource;
            instructions[instruction].immediate = resource + 1u;
        }
    }

    final_base = sample_base;
    for (sampler = 1u; sampler < sampler_count; ++sampler) {
        uint32_t destination_base = input_base + sampler * 4u;
        for (component = 0u; component < 4u; ++component) {
            uint32_t instruction = add_base + (sampler - 1u) * 4u + component;
            init_instruction(&instructions[instruction], RINGL_RSH1_OP_ADD_F32);
            instructions[instruction].destination =
                (uint16_t)(destination_base + component);
            instructions[instruction].source0 =
                (uint16_t)(final_base + component);
            instructions[instruction].source1 =
                (uint16_t)(sample_base + sampler * 4u + component);
        }
        final_base = destination_base;
    }
    for (component = 0u; component < 4u; ++component) {
        init_instruction(&instructions[store_base + component],
                         RINGL_RSH1_OP_STORE_OUTPUT_F32);
        instructions[store_base + component].source0 =
            (uint16_t)(final_base + component);
        instructions[store_base + component].immediate = component;
    }
    init_instruction(&instructions[store_base + 4u], RINGL_RSH1_OP_RETURN);
    memset(&header, 0, sizeof(header));
    header.magic = RINGL_RSH1_MAGIC;
    header.version = RINGL_RSH1_VERSION;
    header.header_size = sizeof(header);
    header.total_size = sizeof(header) +
        (size_t)(store_base + 5u) * sizeof(instructions[0]);
    header.stage = RINGL_RSH1_STAGE_FRAGMENT;
    header.instruction_count = store_base + 5u;
    header.register_count = sampler_count * 10u;
    header.input_count = 4u;
    header.output_count = 4u;
    header.resource_count = sampler_count * 2u;
    store_result(result, &header, instructions);
}

int ringl_glsl_lower_texture2d_rsh1(
    const char* source,
    size_t source_length,
    const char* sampler_names,
    size_t sampler_name_stride,
    uint32_t sampler_count,
    RinGLGlslLowerResult* result)
{
    const char* end;
    const char* cursor;
    TextureCall calls[RINGL_TEXTURE_MAX_SAMPLERS];
    uint32_t seen_samplers = 0u;
    uint32_t call_index;

    if (source == NULL || sampler_names == NULL || sampler_name_stride == 0u ||
        result == NULL)
        return -1;
    memset(result, 0, sizeof(*result));
    if (sampler_count == 0u || sampler_count > RINGL_TEXTURE_MAX_SAMPLERS) {
        (void)snprintf(result->diagnostic, sizeof(result->diagnostic),
                       "bounded texture2D lowering supports one to eight sampler2D uniforms");
        return 1;
    }
    end = source + source_length;
    if (!find_fragment_assignment(source, end, &cursor)) {
        (void)snprintf(result->diagnostic, sizeof(result->diagnostic),
                       "bounded texture2D lowering requires a gl_FragColor texture2D assignment");
        return 1;
    }
    for (call_index = 0u; call_index < sampler_count; ++call_index) {
        uint32_t sampler_bit;

        if ((call_index != 0u && !expect_char(&cursor, end, '+')) ||
            !parse_texture_call(&cursor, end, sampler_names,
                                sampler_name_stride, sampler_count,
                                &calls[call_index])) {
            (void)snprintf(result->diagnostic, sizeof(result->diagnostic),
                           "bounded texture2D lowering requires one constant-coordinate texture2D call per sampler combined with '+'");
            return 1;
        }
        sampler_bit = UINT32_C(1) << calls[call_index].sampler_index;
        if ((seen_samplers & sampler_bit) != 0u) {
            (void)snprintf(result->diagnostic, sizeof(result->diagnostic),
                           "bounded texture2D lowering requires each declared sampler exactly once");
            return 1;
        }
        seen_samplers |= sampler_bit;
    }
    if (!expect_char(&cursor, end, ';') ||
        count_texture_calls(source, end) != sampler_count) {
        (void)snprintf(result->diagnostic, sizeof(result->diagnostic),
                       "bounded texture2D lowering requires exactly one constant-coordinate texture2D call per sampler");
        return 1;
    }
    emit_texture_sum(calls, sampler_count, result);
    return 0;
}
