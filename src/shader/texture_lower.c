/* SPDX-License-Identifier: MIT */
#include "texture_lower.h"
#include "rsh1_abi.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* skip_space(const char* p, const char* end)
{
    while (p < end && isspace((unsigned char)*p))
        ++p;
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
              c == 'e' || c == 'E'))
            break;
        temp[n++] = c;
        ++p;
    }
    if (n == 0u)
        return 0;
    temp[n] = '\0';
    *value = strtof(temp, &parsed_end);
    if (parsed_end == temp || *parsed_end != '\0')
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

int ringl_glsl_lower_texture2d_rsh1(
    const char* source,
    size_t source_length,
    const char* sampler_name,
    uint32_t sampler_count,
    RinGLGlslLowerResult* result)
{
    RinGLRsh1HeaderV1 header;
    RinGLRsh1InstructionV1 ins[11];
    const char* end;
    const char* call;
    const char* cursor;
    char sampler[64];
    char ctor[16];
    float u;
    float v;
    uint32_t u_bits;
    uint32_t v_bits;
    uint32_t component;
    size_t total;

    if (source == NULL || sampler_name == NULL || result == NULL)
        return -1;
    memset(result, 0, sizeof(*result));
    if (sampler_count != 1u) {
        (void)snprintf(result->diagnostic, sizeof(result->diagnostic),
                       "initial texture2D lowering supports exactly one sampler2D uniform");
        return 1;
    }

    end = source + source_length;
    call = strstr(source, "texture2D");
    if (call == NULL || call >= end) {
        (void)snprintf(result->diagnostic, sizeof(result->diagnostic),
                       "texture2D call not found");
        return 1;
    }
    if (strstr(call + 9u, "texture2D") != NULL) {
        (void)snprintf(result->diagnostic, sizeof(result->diagnostic),
                       "multiple texture2D calls are not supported yet");
        return 1;
    }

    cursor = call + 9u;
    if (!expect_char(&cursor, end, '(') ||
        !parse_ident(&cursor, end, sampler, sizeof(sampler)) ||
        strcmp(sampler, sampler_name) != 0 ||
        !expect_char(&cursor, end, ',') ||
        !parse_ident(&cursor, end, ctor, sizeof(ctor)) ||
        strcmp(ctor, "vec2") != 0 ||
        !expect_char(&cursor, end, '(') ||
        !parse_float(&cursor, end, &u) ||
        !expect_char(&cursor, end, ',') ||
        !parse_float(&cursor, end, &v) ||
        !expect_char(&cursor, end, ')') ||
        !expect_char(&cursor, end, ')')) {
        (void)snprintf(result->diagnostic, sizeof(result->diagnostic),
                       "initial texture2D lowering requires sampler2D and constant vec2 coordinates");
        return 1;
    }

    memcpy(&u_bits, &u, sizeof(u_bits));
    memcpy(&v_bits, &v, sizeof(v_bits));

    init_instruction(&ins[0], RINGL_RSH1_OP_CONST_F32);
    ins[0].destination = 0u;
    ins[0].immediate = u_bits;
    init_instruction(&ins[1], RINGL_RSH1_OP_CONST_F32);
    ins[1].destination = 1u;
    ins[1].immediate = v_bits;

    for (component = 0u; component < 4u; ++component) {
        init_instruction(&ins[2u + component],
                         RINGL_RSH1_OP_SAMPLE_IMAGE_2D_F32);
        ins[2u + component].flags = (uint16_t)component;
        ins[2u + component].destination = (uint16_t)(2u + component);
        ins[2u + component].source0 = 0u;
        ins[2u + component].source1 = 1u;
        ins[2u + component].resource = 0u;
        ins[2u + component].immediate = 1u;

        init_instruction(&ins[6u + component], RINGL_RSH1_OP_STORE_OUTPUT_F32);
        ins[6u + component].source0 = (uint16_t)(2u + component);
        ins[6u + component].immediate = component;
    }
    init_instruction(&ins[10], RINGL_RSH1_OP_RETURN);

    memset(&header, 0, sizeof(header));
    header.magic = RINGL_RSH1_MAGIC;
    header.version = RINGL_RSH1_VERSION;
    header.header_size = sizeof(header);
    header.stage = RINGL_RSH1_STAGE_FRAGMENT;
    header.instruction_count = 11u;
    header.register_count = 6u;
    header.output_count = 4u;
    header.resource_count = 2u;
    total = sizeof(header) + sizeof(ins);
    header.total_size = (uint32_t)total;

    memcpy(result->bytes, &header, sizeof(header));
    memcpy(result->bytes + sizeof(header), ins, sizeof(ins));
    result->ok = 1u;
    result->instruction_count = header.instruction_count;
    result->register_count = header.register_count;
    result->input_count = 0u;
    result->output_count = header.output_count;
    result->byte_size = header.total_size;
    return 0;
}
