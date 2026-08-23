/* SPDX-License-Identifier: MIT */
#include "varying_lower.h"
#include "rsh1_abi.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ringl/ringl.h>

static void init_instruction(RinGLRsh1InstructionV1* ins, uint16_t opcode)
{
    memset(ins, 0, sizeof(*ins));
    ins->opcode = opcode;
    ins->destination = RINGL_RSH1_UNUSED;
    ins->source0 = RINGL_RSH1_UNUSED;
    ins->source1 = RINGL_RSH1_UNUSED;
    ins->resource = RINGL_RSH1_UNUSED;
}

static char* compact_source(const char* source, size_t length)
{
    char* compact;
    size_t read_index;
    size_t write_index = 0u;

    if (source == NULL || strstr(source, "//") != NULL)
        return NULL;
    compact = malloc(length + 1u);
    if (compact == NULL)
        return NULL;
    for (read_index = 0u; read_index < length; ++read_index) {
        unsigned char c = (unsigned char)source[read_index];
        if (!isspace(c))
            compact[write_index++] = (char)c;
    }
    compact[write_index] = '\0';
    return compact;
}

static int read_decl_name(const char* source, const char* prefix,
                          uint32_t occurrence, char* name, size_t capacity)
{
    const char* p = source;
    size_t prefix_length = strlen(prefix);
    uint32_t index;

    for (index = 0u; index <= occurrence; ++index) {
        p = strstr(p, prefix);
        if (p == NULL)
            return 0;
        p += prefix_length;
    }
    index = 0u;
    while (*p != '\0' && *p != ';') {
        if ((!isalnum((unsigned char)*p) && *p != '_') ||
            index + 1u >= capacity)
            return 0;
        name[index++] = *p++;
    }
    if (*p != ';' || index == 0u)
        return 0;
    name[index] = '\0';
    return 1;
}

#define RINGL_VARYING_TEXTURE_MAX_SAMPLERS RINGL_GLSL_MAX_SAMPLER_UNIFORMS
#define RINGL_VARYING_TEXTURE_MAX_CALLS RINGL_GLSL_MAX_SAMPLER_UNIFORMS
/* This is a parsing ceiling, not a guaranteed executable shape. The exact
 * RSH1 instruction/register budgets are checked after the calls are known. */
#define RINGL_VARYING_TEXTURE_MAX_LOCAL_COORDINATES 8u

typedef struct VaryingTextureCall {
    uint32_t sampler_index;
    uint32_t coordinate_input_location;
    uint32_t secondary_coordinate_input_location;
    uint32_t coordinate_kind;
    float offset_u;
    float offset_v;
} VaryingTextureCall;

enum VaryingTextureCoordinateKind {
    RINGL_VARYING_TEXTURE_COORD_DIRECT = 0,
    RINGL_VARYING_TEXTURE_COORD_ADD_OFFSET,
    RINGL_VARYING_TEXTURE_COORD_SUB_OFFSET,
    RINGL_VARYING_TEXTURE_COORD_MUL_SCALE,
    RINGL_VARYING_TEXTURE_COORD_DIV_SCALE,
    RINGL_VARYING_TEXTURE_COORD_ADD_COORDINATE,
    RINGL_VARYING_TEXTURE_COORD_SUB_COORDINATE,
};

static int consume_text(const char** cursor, const char* text)
{
    size_t length;

    if (cursor == NULL || *cursor == NULL || text == NULL)
        return 0;
    length = strlen(text);
    if (strncmp(*cursor, text, length) != 0)
        return 0;
    *cursor += length;
    return 1;
}

static int read_identifier(const char** cursor, char* name, size_t capacity)
{
    const char* p;
    size_t length = 0u;

    if (cursor == NULL || *cursor == NULL || name == NULL || capacity == 0u)
        return 0;
    p = *cursor;
    if (!isalpha((unsigned char)*p) && *p != '_')
        return 0;
    do {
        if (length + 1u >= capacity)
            return 0;
        name[length++] = *p++;
    } while (isalnum((unsigned char)*p) || *p == '_');
    name[length] = '\0';
    *cursor = p;
    return 1;
}

static int sampler_index_for_name(char names[][64], uint32_t count,
                                  const char* name, uint32_t* index_out)
{
    uint32_t index;

    if (names == NULL || name == NULL || index_out == NULL)
        return 0;
    for (index = 0u; index < count; ++index) {
        if (strcmp(names[index], name) == 0) {
            *index_out = index;
            return 1;
        }
    }
    return 0;
}

static int parse_finite_float(const char** cursor, float* value)
{
    const char* p;
    char text[64];
    char* parsed_end;
    size_t length = 0u;

    if (cursor == NULL || *cursor == NULL || value == NULL)
        return 0;
    p = *cursor;
    while (isdigit((unsigned char)*p) || *p == '.' || *p == '+' ||
           *p == '-' || *p == 'e' || *p == 'E') {
        if (length + 1u >= sizeof(text))
            return 0;
        text[length++] = *p++;
    }
    if (length == 0u)
        return 0;
    text[length] = '\0';
    *value = strtof(text, &parsed_end);
    if (parsed_end == text || *parsed_end != '\0' || !isfinite(*value))
        return 0;
    *cursor = p;
    return 1;
}

static int parse_varying_texture_offset(const char** cursor,
                                        uint32_t* coordinate_kind,
                                        float* offset_u,
                                        float* offset_v)
{
    if (cursor == NULL || *cursor == NULL || coordinate_kind == NULL ||
        offset_u == NULL || offset_v == NULL) {
        return 0;
    }
    *coordinate_kind = RINGL_VARYING_TEXTURE_COORD_DIRECT;
    *offset_u = 0.0f;
    *offset_v = 0.0f;
    if (**cursor != '+' && **cursor != '-' && **cursor != '*' &&
        **cursor != '/')
        return 1;
    *coordinate_kind = **cursor == '+'
        ? RINGL_VARYING_TEXTURE_COORD_ADD_OFFSET
        : **cursor == '-'
        ? RINGL_VARYING_TEXTURE_COORD_SUB_OFFSET
        : **cursor == '*'
        ? RINGL_VARYING_TEXTURE_COORD_MUL_SCALE
        : RINGL_VARYING_TEXTURE_COORD_DIV_SCALE;
    ++*cursor;
    if (!consume_text(cursor, "vec2(") ||
        !parse_finite_float(cursor, offset_u) || !consume_text(cursor, ",") ||
        !parse_finite_float(cursor, offset_v) || !consume_text(cursor, ")")) {
        return 0;
    }
    return *coordinate_kind != RINGL_VARYING_TEXTURE_COORD_DIV_SCALE ||
           (*offset_u != 0.0f && *offset_v != 0.0f);
}

static int parse_varying_texture_call(const char** cursor,
                                      char sampler_names[][64],
                                      uint32_t sampler_count,
                                      char coordinate_names[][64],
                                      const uint32_t* coordinate_input_locations,
                                      uint32_t coordinate_count,
                                      VaryingTextureCall* call)
{
    char sampler[64];
    char coordinate[64];
    char secondary_coordinate[64];
    uint32_t coordinate_index;

    if (cursor == NULL || sampler_names == NULL ||
        coordinate_names == NULL || coordinate_input_locations == NULL ||
        coordinate_count == 0u || call == NULL ||
        !consume_text(cursor, "texture2D(") ||
        !read_identifier(cursor, sampler, sizeof(sampler)) ||
        !consume_text(cursor, ",") ||
        !read_identifier(cursor, coordinate, sizeof(coordinate)) ||
        !sampler_index_for_name(sampler_names, sampler_count, sampler,
                                &call->sampler_index) ||
        !sampler_index_for_name(coordinate_names, coordinate_count, coordinate,
                                &coordinate_index)) {
        return 0;
    }
    call->coordinate_input_location =
        coordinate_input_locations[coordinate_index];
    if (call->coordinate_input_location != 0u &&
        call->coordinate_input_location != 2u) {
        return 0;
    }
    call->secondary_coordinate_input_location = UINT32_MAX;
    if ((*(*cursor) == '+' || *(*cursor) == '-') &&
        strncmp((*cursor) + 1u, "vec2(", strlen("vec2(")) != 0 &&
        isalpha((unsigned char)(*cursor)[1])) {
        int subtract = **cursor == '-';
        uint32_t secondary_index;

        ++*cursor;
        if (!read_identifier(cursor, secondary_coordinate,
                             sizeof(secondary_coordinate)) ||
            !sampler_index_for_name(coordinate_names, coordinate_count,
                                    secondary_coordinate, &secondary_index)) {
            return 0;
        }
        call->secondary_coordinate_input_location =
            coordinate_input_locations[secondary_index];
        if (call->secondary_coordinate_input_location != 0u &&
            call->secondary_coordinate_input_location != 2u)
            return 0;
        call->coordinate_kind = subtract
            ? RINGL_VARYING_TEXTURE_COORD_SUB_COORDINATE
            : RINGL_VARYING_TEXTURE_COORD_ADD_COORDINATE;
    } else if (!parse_varying_texture_offset(cursor, &call->coordinate_kind,
                                      &call->offset_u, &call->offset_v) ||
               !consume_text(cursor, ")")) {
        return 0;
    }
    if (call->secondary_coordinate_input_location != UINT32_MAX &&
        !consume_text(cursor, ")"))
        return 0;
    return 1;
}

/* The bounded texture profile accepts local vec2 values derived in sequence
 * from the shared varying by finite component-wise operations. It is not a
 * host-side substitution: samples keep reading RSH1's interpolated inputs. */
static int parse_varying_texture_local_coordinate(
    const char** cursor, const char* source_coordinate, char* coordinate,
    size_t coordinate_capacity, uint32_t* coordinate_kind, float* offset_u,
    float* offset_v)
{
    char source[64];

    if (cursor == NULL || *cursor == NULL || source_coordinate == NULL ||
        coordinate == NULL || coordinate_capacity == 0u ||
        coordinate_kind == NULL || offset_u == NULL || offset_v == NULL ||
        !consume_text(cursor, "vec2") ||
        !read_identifier(cursor, coordinate, coordinate_capacity) ||
        strcmp(coordinate, source_coordinate) == 0 ||
        !consume_text(cursor, "=") ||
        !read_identifier(cursor, source, sizeof(source)) ||
        strcmp(source, source_coordinate) != 0 ||
        !parse_varying_texture_offset(cursor, coordinate_kind, offset_u,
                                      offset_v) ||
        !consume_text(cursor, ";")) {
        return 0;
    }
    return 1;
}

/* Keep the two-varying extension deliberately narrow: one local vec2 may
 * combine the two declared perspective coordinates, then every sample reads
 * that named result.  This accepts the natural GLSL spelling without making
 * the bounded lowerer pretend to support arbitrary local vector expressions. */
static int parse_varying_texture_two_coordinate_local(
    const char** cursor, char varying_names[][64], char* coordinate,
    size_t coordinate_capacity, uint32_t* coordinate_kind,
    uint32_t* primary_input_location, uint32_t* secondary_input_location)
{
    char primary[64];
    char secondary[64];

    if (cursor == NULL || *cursor == NULL || varying_names == NULL ||
        coordinate == NULL || coordinate_capacity == 0u ||
        coordinate_kind == NULL || primary_input_location == NULL ||
        secondary_input_location == NULL || !consume_text(cursor, "vec2") ||
        !read_identifier(cursor, coordinate, coordinate_capacity) ||
        strcmp(coordinate, varying_names[0]) == 0 ||
        strcmp(coordinate, varying_names[1]) == 0 ||
        !consume_text(cursor, "=") ||
        !read_identifier(cursor, primary, sizeof(primary)) ||
        (**cursor != '+' && **cursor != '-')) {
        return 0;
    }
    *coordinate_kind = **cursor == '+'
        ? RINGL_VARYING_TEXTURE_COORD_ADD_COORDINATE
        : RINGL_VARYING_TEXTURE_COORD_SUB_COORDINATE;
    ++*cursor;
    if (!read_identifier(cursor, secondary, sizeof(secondary)) ||
        !consume_text(cursor, ";")) {
        return 0;
    }
    if (strcmp(primary, varying_names[0]) == 0)
        *primary_input_location = 0u;
    else if (strcmp(primary, varying_names[1]) == 0)
        *primary_input_location = 2u;
    else
        return 0;
    if (strcmp(secondary, varying_names[0]) == 0)
        *secondary_input_location = 0u;
    else if (strcmp(secondary, varying_names[1]) == 0)
        *secondary_input_location = 2u;
    else
        return 0;
    return *primary_input_location != *secondary_input_location;
}

static int parse_varying_texture_color_operator(const char** cursor,
                                                uint16_t* opcode)
{
    if (cursor == NULL || *cursor == NULL || opcode == NULL ||
        (**cursor != '*' && **cursor != '+' && **cursor != '-' &&
         **cursor != '/')) {
        return 0;
    }
    *opcode = **cursor == '*'
        ? RINGL_RSH1_OP_MUL_F32
        : **cursor == '+'
        ? RINGL_RSH1_OP_ADD_F32
        : **cursor == '-'
        ? RINGL_RSH1_OP_SUB_F32 : RINGL_RSH1_OP_DIV_F32;
    ++*cursor;
    return 1;
}

static int parse_varying_texture_color_literal(const char** cursor,
                                               float color[4])
{
    uint32_t component;

    if (cursor == NULL || *cursor == NULL || color == NULL ||
        !consume_text(cursor, "vec4(")) {
        return 0;
    }
    for (component = 0u; component < 4u; ++component) {
        if (!parse_finite_float(cursor, &color[component]) ||
            (component + 1u < 4u && !consume_text(cursor, ","))) {
            return 0;
        }
    }
    return consume_text(cursor, ")");
}

static int parse_varying_texture_color_operation(const char** cursor,
                                                 uint16_t* opcode,
                                                 float color[4])
{
    uint32_t component;

    if (!parse_varying_texture_color_operator(cursor, opcode) ||
        !parse_varying_texture_color_literal(cursor, color)) {
        return 0;
    }
    if (*opcode == RINGL_RSH1_OP_DIV_F32) {
        for (component = 0u; component < 4u; ++component) {
            if (color[component] == 0.0f) return 0;
        }
    }
    return 1;
}

static void emit_varying_texture_offset(RinGLRsh1InstructionV1* ins,
                                        uint32_t* instruction_cursor,
                                        uint32_t temporary_base,
                                        uint32_t source_u,
                                        uint32_t source_v,
                                        uint32_t coordinate_kind,
                                        float offset_u, float offset_v)
{
    uint32_t offset_u_bits;
    uint32_t offset_v_bits;
    uint16_t opcode = coordinate_kind == RINGL_VARYING_TEXTURE_COORD_ADD_OFFSET
        ? RINGL_RSH1_OP_ADD_F32
        : coordinate_kind == RINGL_VARYING_TEXTURE_COORD_SUB_OFFSET
        ? RINGL_RSH1_OP_SUB_F32
        : coordinate_kind == RINGL_VARYING_TEXTURE_COORD_MUL_SCALE
        ? RINGL_RSH1_OP_MUL_F32 : RINGL_RSH1_OP_DIV_F32;

    memcpy(&offset_u_bits, &offset_u, sizeof(offset_u_bits));
    memcpy(&offset_v_bits, &offset_v, sizeof(offset_v_bits));
    init_instruction(&ins[*instruction_cursor], RINGL_RSH1_OP_CONST_F32);
    ins[*instruction_cursor].destination = (uint16_t)temporary_base;
    ins[(*instruction_cursor)++].immediate = offset_u_bits;
    init_instruction(&ins[*instruction_cursor], RINGL_RSH1_OP_CONST_F32);
    ins[*instruction_cursor].destination = (uint16_t)(temporary_base + 1u);
    ins[(*instruction_cursor)++].immediate = offset_v_bits;
    init_instruction(&ins[*instruction_cursor], opcode);
    ins[*instruction_cursor].destination = (uint16_t)(temporary_base + 2u);
    ins[*instruction_cursor].source0 = (uint16_t)source_u;
    ins[(*instruction_cursor)++].source1 = (uint16_t)temporary_base;
    init_instruction(&ins[*instruction_cursor], opcode);
    ins[*instruction_cursor].destination = (uint16_t)(temporary_base + 3u);
    ins[*instruction_cursor].source0 = (uint16_t)source_v;
    ins[(*instruction_cursor)++].source1 = (uint16_t)(temporary_base + 1u);
}

static void emit_varying_texture_coordinate_combine(
    RinGLRsh1InstructionV1* ins, uint32_t* instruction_cursor,
    uint32_t temporary_base, uint32_t source_u, uint32_t source_v,
    uint32_t secondary_u, uint32_t secondary_v, uint32_t coordinate_kind)
{
    uint16_t opcode = coordinate_kind ==
            RINGL_VARYING_TEXTURE_COORD_ADD_COORDINATE
        ? RINGL_RSH1_OP_ADD_F32 : RINGL_RSH1_OP_SUB_F32;

    init_instruction(&ins[*instruction_cursor], opcode);
    ins[*instruction_cursor].destination = (uint16_t)(temporary_base + 2u);
    ins[*instruction_cursor].source0 = (uint16_t)source_u;
    ins[(*instruction_cursor)++].source1 = (uint16_t)secondary_u;
    init_instruction(&ins[*instruction_cursor], opcode);
    ins[*instruction_cursor].destination = (uint16_t)(temporary_base + 3u);
    ins[*instruction_cursor].source0 = (uint16_t)source_v;
    ins[(*instruction_cursor)++].source1 = (uint16_t)secondary_v;
}

static int lower_vertex(const char* source, RinGLGlslLowerResult* result)
{
    RinGLRsh1HeaderV1 header;
    RinGLRsh1InstructionV1 ins[15];
    char position[64];
    char texcoord[64];
    char varying[64];
    char expected[512];
    uint32_t zero_bits = 0u;
    float one = 1.0f;
    uint32_t one_bits;
    size_t total;

    if (!read_decl_name(source, "attributevec2", 0u, position, sizeof(position)) ||
        !read_decl_name(source, "attributevec2", 1u, texcoord, sizeof(texcoord)) ||
        !read_decl_name(source, "varyingvec2", 0u, varying, sizeof(varying)))
        return 1;
    (void)snprintf(expected, sizeof(expected),
                   "attributevec2%s;attributevec2%s;varyingvec2%s;"
                   "voidmain(){gl_Position=vec4(%s,0.0,1.0);%s=%s;}",
                   position, texcoord, varying, position, varying, texcoord);
    if (strcmp(source, expected) != 0)
        return 1;

    memcpy(&one_bits, &one, sizeof(one_bits));
    init_instruction(&ins[0], RINGL_RSH1_OP_LOAD_INPUT_F32);
    ins[0].destination = 0u; ins[0].immediate = 0u;
    init_instruction(&ins[1], RINGL_RSH1_OP_LOAD_INPUT_F32);
    ins[1].destination = 1u; ins[1].immediate = 1u;
    init_instruction(&ins[2], RINGL_RSH1_OP_LOAD_INPUT_F32);
    ins[2].destination = 2u; ins[2].immediate = 2u;
    init_instruction(&ins[3], RINGL_RSH1_OP_LOAD_INPUT_F32);
    ins[3].destination = 3u; ins[3].immediate = 3u;
    init_instruction(&ins[4], RINGL_RSH1_OP_CONST_F32);
    ins[4].destination = 4u; ins[4].immediate = zero_bits;
    init_instruction(&ins[5], RINGL_RSH1_OP_CONST_F32);
    ins[5].destination = 5u; ins[5].immediate = one_bits;

    init_instruction(&ins[6], RINGL_RSH1_OP_STORE_OUTPUT_F32);
    ins[6].source0 = 0u; ins[6].immediate = 0u;
    init_instruction(&ins[7], RINGL_RSH1_OP_STORE_OUTPUT_F32);
    ins[7].source0 = 1u; ins[7].immediate = 1u;
    init_instruction(&ins[8], RINGL_RSH1_OP_STORE_OUTPUT_F32);
    ins[8].source0 = 4u; ins[8].immediate = 2u;
    init_instruction(&ins[9], RINGL_RSH1_OP_STORE_OUTPUT_F32);
    ins[9].source0 = 5u; ins[9].immediate = 3u;
    init_instruction(&ins[10], RINGL_RSH1_OP_STORE_OUTPUT_F32);
    ins[10].source0 = 2u; ins[10].immediate = 4u;
    init_instruction(&ins[11], RINGL_RSH1_OP_STORE_OUTPUT_F32);
    ins[11].source0 = 3u; ins[11].immediate = 5u;
    init_instruction(&ins[12], RINGL_RSH1_OP_STORE_OUTPUT_F32);
    ins[12].source0 = 4u; ins[12].immediate = 6u;
    init_instruction(&ins[13], RINGL_RSH1_OP_STORE_OUTPUT_F32);
    ins[13].source0 = 5u; ins[13].immediate = 7u;
    init_instruction(&ins[14], RINGL_RSH1_OP_RETURN);

    memset(&header, 0, sizeof(header));
    header.magic = RINGL_RSH1_MAGIC;
    header.version = RINGL_RSH1_VERSION;
    header.header_size = sizeof(header);
    header.stage = RINGL_RSH1_STAGE_VERTEX;
    header.instruction_count = 15u;
    header.register_count = 6u;
    header.input_count = 4u;
    header.output_count = 8u;
    total = sizeof(header) + sizeof(ins);
    header.total_size = (uint32_t)total;
    memcpy(result->bytes, &header, sizeof(header));
    memcpy(result->bytes + sizeof(header), ins, sizeof(ins));
    result->ok = 1u;
    result->instruction_count = header.instruction_count;
    result->register_count = header.register_count;
    result->input_count = header.input_count;
    result->output_count = header.output_count;
    result->byte_size = header.total_size;
    return 0;
}

/* This profile deliberately has one shared, perspective-interpolated vec2.
 * The emitted layout preserves the historic one- and two-call bytecode while
 * allowing bounded repeated and selectively active sampler chains:
 *
 *   [UV inputs][optional local affine coordinate][RGBA samples]
 *   [RGBA left-to-right sums][RGBA stores][return]
 *
 * Up to six local affine coordinates are evaluated in source order before the
 * samples. Each sample may additionally use one finite offset; its four
 * temporary registers are overwritten only after that sample has consumed
 * them. The eight-call profile stays below both RSH1 and RinGPU limits.
 */
static int lower_fragment_texture_chain(const char* source,
                                        RinGLGlslLowerResult* result)
{
    RinGLRsh1HeaderV1 header;
    RinGLRsh1InstructionV1 ins[RINGL_RSH1_MAX_INSTRUCTIONS];
    char sampler_names[RINGL_VARYING_TEXTURE_MAX_SAMPLERS][64];
    char varying_names[2][64];
    char coordinate_names[2][64];
    char local_coordinate_names[RINGL_VARYING_TEXTURE_MAX_LOCAL_COORDINATES][64];
    uint32_t coordinate_input_locations[2] = {0u, 2u};
    uint32_t local_coordinate_kinds[RINGL_VARYING_TEXTURE_MAX_LOCAL_COORDINATES] = {0u};
    float local_offset_u[RINGL_VARYING_TEXTURE_MAX_LOCAL_COORDINATES] = {0.0f};
    float local_offset_v[RINGL_VARYING_TEXTURE_MAX_LOCAL_COORDINATES] = {0.0f};
    VaryingTextureCall calls[RINGL_VARYING_TEXTURE_MAX_CALLS];
    uint32_t sampler_resource_indices[RINGL_VARYING_TEXTURE_MAX_SAMPLERS] = {0u};
    uint32_t sampler_binding_indices[RINGL_VARYING_TEXTURE_MAX_SAMPLERS] = {0u};
    const char* cursor;
    uint32_t sampler_count = 0u;
    uint32_t varying_count = 0u;
    uint32_t coordinate_name_count;
    uint32_t local_coordinate_count = 0u;
    uint32_t two_coordinate_local = 0u;
    uint32_t local_primary_input_location = 0u;
    uint32_t local_secondary_input_location = 2u;
    uint32_t sampler_binding_count = 0u;
    uint32_t call_count = 0u;
    uint32_t sample_base;
    uint32_t instruction_cursor;
    uint32_t add_base;
    uint32_t add_register_base;
    uint32_t store_base;
    uint32_t final_base;
    uint32_t sampled_final_base;
    uint32_t padding_base;
    uint32_t coordinate_temp_base;
    uint32_t color_instruction_base = 0u;
    uint32_t color_constant_base = 0u;
    uint32_t color_result_base = 0u;
    uint32_t has_call_offset = 0u;
    uint32_t parenthesized_color_operation = 0u;
    uint32_t color_operation_enabled = 0u;
    uint32_t color_operation_on_left = 0u;
    uint16_t color_operation = RINGL_RSH1_OP_MUL_F32;
    uint32_t local_temporary_register_count = 0u;
    uint32_t temporary_register_count = 0u;
    uint32_t local_coordinate_u = 0u;
    uint32_t local_coordinate_v = 1u;
    uint32_t call_index;
    uint32_t sampler_index;
    uint32_t component;
    float color[4] = {0.0f};
    size_t total;

    if (source == NULL || result == NULL)
        return 1;
    cursor = source;
    while (strncmp(cursor, "uniformsampler2D", strlen("uniformsampler2D")) ==
           0) {
        if (sampler_count == RINGL_VARYING_TEXTURE_MAX_SAMPLERS ||
            !consume_text(&cursor, "uniformsampler2D") ||
            !read_identifier(&cursor, sampler_names[sampler_count],
                             sizeof(sampler_names[sampler_count])) ||
            !consume_text(&cursor, ";")) {
            return 1;
        }
        for (sampler_index = 0u; sampler_index < sampler_count;
             ++sampler_index) {
            if (strcmp(sampler_names[sampler_index],
                       sampler_names[sampler_count]) == 0) {
                return 1;
            }
        }
        ++sampler_count;
    }
    while (strncmp(cursor, "varyingvec2", strlen("varyingvec2")) == 0) {
        uint32_t index;

        if (varying_count == 2u || !consume_text(&cursor, "varyingvec2") ||
            !read_identifier(&cursor, varying_names[varying_count],
                             sizeof(varying_names[varying_count])) ||
            !consume_text(&cursor, ";")) {
            return 1;
        }
        for (index = 0u; index < varying_count; ++index) {
            if (strcmp(varying_names[index], varying_names[varying_count]) ==
                0) {
                return 1;
            }
        }
        ++varying_count;
    }
    if (sampler_count == 0u || varying_count == 0u ||
        !consume_text(&cursor, "voidmain(){")) {
        return 1;
    }
    for (call_index = 0u; call_index < varying_count; ++call_index) {
        (void)snprintf(coordinate_names[call_index],
                       sizeof(coordinate_names[call_index]), "%s",
                       varying_names[call_index]);
    }
    coordinate_name_count = varying_count;
    if (varying_count == 1u) {
        const char* local_source = varying_names[0];

        while (strncmp(cursor, "vec2", strlen("vec2")) == 0) {
            if (local_coordinate_count ==
                    RINGL_VARYING_TEXTURE_MAX_LOCAL_COORDINATES ||
                !parse_varying_texture_local_coordinate(
                    &cursor, local_source,
                    local_coordinate_names[local_coordinate_count],
                    sizeof(local_coordinate_names[local_coordinate_count]),
                    &local_coordinate_kinds[local_coordinate_count],
                    &local_offset_u[local_coordinate_count],
                    &local_offset_v[local_coordinate_count])) {
                return 1;
            }
            local_source = local_coordinate_names[local_coordinate_count++];
        }
        if (local_coordinate_count != 0u) {
            (void)snprintf(coordinate_names[0], sizeof(coordinate_names[0]),
                           "%s", local_coordinate_names[
                               local_coordinate_count - 1u]);
        }
    } else if (strncmp(cursor, "vec2", strlen("vec2")) == 0) {
        const char* local_source;

        if (!parse_varying_texture_two_coordinate_local(
                &cursor, varying_names, local_coordinate_names[0],
                sizeof(local_coordinate_names[0]),
                &local_coordinate_kinds[0], &local_primary_input_location,
                &local_secondary_input_location)) {
            return 1;
        }
        local_source = local_coordinate_names[0];
        coordinate_name_count = 1u;
        local_coordinate_count = 1u;
        two_coordinate_local = 1u;
        while (strncmp(cursor, "vec2", strlen("vec2")) == 0) {
            if (local_coordinate_count ==
                    RINGL_VARYING_TEXTURE_MAX_LOCAL_COORDINATES ||
                !parse_varying_texture_local_coordinate(
                    &cursor, local_source,
                    local_coordinate_names[local_coordinate_count],
                    sizeof(local_coordinate_names[local_coordinate_count]),
                    &local_coordinate_kinds[local_coordinate_count],
                    &local_offset_u[local_coordinate_count],
                    &local_offset_v[local_coordinate_count])) {
                return 1;
            }
            local_source = local_coordinate_names[local_coordinate_count++];
        }
        (void)snprintf(coordinate_names[0], sizeof(coordinate_names[0]),
                       "%s", local_coordinate_names[
                           local_coordinate_count - 1u]);
    }
    if (!consume_text(&cursor, "gl_FragColor="))
        return 1;
    if (strncmp(cursor, "vec4(", strlen("vec4(")) == 0) {
        if (!parse_varying_texture_color_literal(&cursor, color) ||
            !parse_varying_texture_color_operator(&cursor, &color_operation) ||
            /* A texture-dependent divisor cannot be proven nonzero during
             * lowering, so do not submit it to a later execution failure. */
            color_operation == RINGL_RSH1_OP_DIV_F32) {
            return 1;
        }
        color_operation_enabled = 1u;
        color_operation_on_left = 1u;
        if (*cursor == '(') {
            ++cursor;
            parenthesized_color_operation = 1u;
        }
        for (;;) {
            if (call_count == RINGL_VARYING_TEXTURE_MAX_CALLS ||
                !parse_varying_texture_call(
                    &cursor, sampler_names, sampler_count, coordinate_names,
                    coordinate_input_locations, coordinate_name_count,
                    &calls[call_count])) {
                return 1;
            }
            ++call_count;
            if (*cursor != '+' || !parenthesized_color_operation)
                break;
            ++cursor;
        }
        if ((parenthesized_color_operation && !consume_text(&cursor, ")")) ||
            strcmp(cursor, ";}") != 0) {
            return 1;
        }
    } else {
        if (*cursor == '(') {
            ++cursor;
            parenthesized_color_operation = 1u;
        }
        for (;;) {
            if (call_count == RINGL_VARYING_TEXTURE_MAX_CALLS ||
                !parse_varying_texture_call(
                    &cursor, sampler_names, sampler_count, coordinate_names,
                    coordinate_input_locations, coordinate_name_count,
                    &calls[call_count])) {
                return 1;
            }
            ++call_count;
            if (*cursor != '+' ||
                (!parenthesized_color_operation &&
                 strncmp(cursor + 1u, "vec4(", strlen("vec4(")) == 0)) {
                break;
            }
            ++cursor;
        }
        if (parenthesized_color_operation) {
            if (!consume_text(&cursor, ")") ||
                !parse_varying_texture_color_operation(
                    &cursor, &color_operation, color)) {
                return 1;
            }
            color_operation_enabled = 1u;
        } else if (*cursor == '*' || *cursor == '+' || *cursor == '-' ||
                   *cursor == '/') {
            if (call_count != 1u ||
                !parse_varying_texture_color_operation(
                    &cursor, &color_operation, color)) {
                return 1;
            }
            color_operation_enabled = 1u;
        }
        if (strcmp(cursor, ";}") != 0)
            return 1;
    }

    for (sampler_index = 0u; sampler_index < sampler_count; ++sampler_index) {
        for (call_index = 0u; call_index < call_count; ++call_index) {
            if (calls[call_index].sampler_index != sampler_index)
                continue;
            sampler_resource_indices[sampler_index] = sampler_binding_count;
            sampler_binding_indices[sampler_binding_count++] = sampler_index;
            break;
        }
    }
    if (sampler_binding_count == 0u)
        return 1;

    sample_base = 4u;
    instruction_cursor = sample_base;
    for (call_index = 0u; call_index < local_coordinate_count; ++call_index) {
        if (local_coordinate_kinds[call_index] !=
            RINGL_VARYING_TEXTURE_COORD_DIRECT) {
            local_temporary_register_count += 4u;
            instruction_cursor += two_coordinate_local && call_index == 0u
                ? 2u : 4u;
        }
    }
    temporary_register_count = local_temporary_register_count;
    for (call_index = 0u; call_index < call_count; ++call_index) {
        if (calls[call_index].coordinate_kind !=
            RINGL_VARYING_TEXTURE_COORD_DIRECT) {
            has_call_offset = 1u;
            instruction_cursor +=
                calls[call_index].secondary_coordinate_input_location !=
                        UINT32_MAX
                ? 2u : 4u;
        }
        instruction_cursor += 4u;
    }
    add_base = instruction_cursor;
    store_base = add_base + (call_count - 1u) * 4u;
    add_register_base = 2u + call_count * 4u;
    final_base = call_count == 1u
        ? 2u : add_register_base + (call_count - 2u) * 4u;
    sampled_final_base = final_base;
    padding_base = final_base + 4u;
    coordinate_temp_base = padding_base + 2u;
    if (has_call_offset)
        temporary_register_count += 4u;
    if (store_base + 5u > RINGL_RSH1_MAX_INSTRUCTIONS ||
        8u * call_count + temporary_register_count >
            RINGL_RSH1_MAX_REGISTERS) {
        return 1;
    }
    if (color_operation_enabled) {
        if (store_base + 13u > RINGL_RSH1_MAX_INSTRUCTIONS ||
            8u * call_count + temporary_register_count + 8u >
                RINGL_RSH1_MAX_REGISTERS) {
            return 1;
        }
        color_instruction_base = store_base;
        color_constant_base = 8u * call_count + temporary_register_count;
        color_result_base = color_constant_base + 4u;
        store_base += 8u;
        final_base = color_result_base;
    }
    instruction_cursor = sample_base;
    memset(ins, 0, sizeof(ins));
    for (component = 0u; component < 4u; ++component) {
        init_instruction(&ins[component], RINGL_RSH1_OP_LOAD_INPUT_F32);
        ins[component].destination = component < 2u
            ? (uint16_t)component
            : (uint16_t)(padding_base + component - 2u);
        ins[component].immediate = component;
    }
    {
        uint32_t local_temporary_base = coordinate_temp_base;

        if (two_coordinate_local) {
            uint32_t primary_u = local_primary_input_location == 0u
                ? 0u : padding_base;
            uint32_t secondary_u = local_secondary_input_location == 0u
                ? 0u : padding_base;

            emit_varying_texture_coordinate_combine(
                ins, &instruction_cursor, local_temporary_base, primary_u,
                primary_u + 1u, secondary_u, secondary_u + 1u,
                local_coordinate_kinds[0]);
            local_coordinate_u = local_temporary_base + 2u;
            local_coordinate_v = local_temporary_base + 3u;
            local_temporary_base += 4u;
            for (call_index = 1u; call_index < local_coordinate_count;
                 ++call_index) {
                if (local_coordinate_kinds[call_index] ==
                    RINGL_VARYING_TEXTURE_COORD_DIRECT) {
                    continue;
                }
                emit_varying_texture_offset(
                    ins, &instruction_cursor, local_temporary_base,
                    local_coordinate_u, local_coordinate_v,
                    local_coordinate_kinds[call_index],
                    local_offset_u[call_index], local_offset_v[call_index]);
                local_coordinate_u = local_temporary_base + 2u;
                local_coordinate_v = local_temporary_base + 3u;
                local_temporary_base += 4u;
            }
        } else {
            for (call_index = 0u; call_index < local_coordinate_count;
                 ++call_index) {
                if (local_coordinate_kinds[call_index] ==
                    RINGL_VARYING_TEXTURE_COORD_DIRECT) {
                    continue;
                }
                emit_varying_texture_offset(
                    ins, &instruction_cursor, local_temporary_base,
                    local_coordinate_u, local_coordinate_v,
                    local_coordinate_kinds[call_index],
                    local_offset_u[call_index], local_offset_v[call_index]);
                local_coordinate_u = local_temporary_base + 2u;
                local_coordinate_v = local_temporary_base + 3u;
                local_temporary_base += 4u;
            }
        }
    }
    for (call_index = 0u; call_index < call_count; ++call_index) {
        uint32_t resource_pair =
            sampler_resource_indices[calls[call_index].sampler_index] * 2u;
        uint32_t coordinate_u;
        uint32_t coordinate_v;

        if (local_coordinate_count != 0u) {
            coordinate_u = local_coordinate_u;
            coordinate_v = local_coordinate_v;
        } else if (calls[call_index].coordinate_input_location == 0u) {
            coordinate_u = 0u;
            coordinate_v = 1u;
        } else {
            coordinate_u = padding_base;
            coordinate_v = padding_base + 1u;
        }

        if (calls[call_index].coordinate_kind !=
            RINGL_VARYING_TEXTURE_COORD_DIRECT) {
            uint32_t call_temp_base = coordinate_temp_base +
                local_temporary_register_count;

            if (calls[call_index].secondary_coordinate_input_location !=
                UINT32_MAX) {
                uint32_t secondary_u =
                    calls[call_index].secondary_coordinate_input_location == 0u
                    ? 0u : padding_base;
                uint32_t secondary_v = secondary_u + 1u;

                emit_varying_texture_coordinate_combine(
                    ins, &instruction_cursor, call_temp_base, coordinate_u,
                    coordinate_v, secondary_u, secondary_v,
                    calls[call_index].coordinate_kind);
            } else {
                emit_varying_texture_offset(ins, &instruction_cursor,
                                            call_temp_base, coordinate_u,
                                            coordinate_v,
                                            calls[call_index].coordinate_kind,
                                            calls[call_index].offset_u,
                                            calls[call_index].offset_v);
            }
            coordinate_u = call_temp_base + 2u;
            coordinate_v = call_temp_base + 3u;
        }

        for (component = 0u; component < 4u; ++component) {
            RinGLRsh1InstructionV1* sample =
                &ins[instruction_cursor + component];

            init_instruction(sample, RINGL_RSH1_OP_SAMPLE_IMAGE_2D_F32);
            sample->flags = (uint16_t)component;
            sample->destination = (uint16_t)(2u + call_index * 4u + component);
            sample->source0 = (uint16_t)coordinate_u;
            sample->source1 = (uint16_t)coordinate_v;
            sample->resource = (uint16_t)resource_pair;
            sample->immediate = resource_pair + 1u;
        }
        instruction_cursor += 4u;
    }
    for (call_index = 0u; call_index + 1u < call_count; ++call_index) {
        uint32_t previous_base = call_index == 0u
            ? 2u : add_register_base + (call_index - 1u) * 4u;
        uint32_t next_base = 2u + (call_index + 1u) * 4u;
        uint32_t destination_base = add_register_base + call_index * 4u;

        for (component = 0u; component < 4u; ++component) {
            RinGLRsh1InstructionV1* add =
                &ins[add_base + call_index * 4u + component];

            init_instruction(add, RINGL_RSH1_OP_ADD_F32);
            add->destination = (uint16_t)(destination_base + component);
            add->source0 = (uint16_t)(previous_base + component);
            add->source1 = (uint16_t)(next_base + component);
        }
    }
    if (color_operation_enabled) {
        for (component = 0u; component < 4u; ++component) {
            uint32_t color_bits;
            RinGLRsh1InstructionV1* constant =
                &ins[color_instruction_base + component];
            RinGLRsh1InstructionV1* operation =
                &ins[color_instruction_base + 4u + component];

            memcpy(&color_bits, &color[component], sizeof(color_bits));
            init_instruction(constant, RINGL_RSH1_OP_CONST_F32);
            constant->destination = (uint16_t)(color_constant_base + component);
            constant->immediate = color_bits;
            init_instruction(operation, color_operation);
            operation->destination = (uint16_t)(color_result_base + component);
            operation->source0 = color_operation_on_left
                ? (uint16_t)(color_constant_base + component)
                : (uint16_t)(sampled_final_base + component);
            operation->source1 = color_operation_on_left
                ? (uint16_t)(sampled_final_base + component)
                : (uint16_t)(color_constant_base + component);
        }
    }
    for (component = 0u; component < 4u; ++component) {
        RinGLRsh1InstructionV1* store = &ins[store_base + component];

        init_instruction(store, RINGL_RSH1_OP_STORE_OUTPUT_F32);
        store->source0 = (uint16_t)(final_base + component);
        store->immediate = component;
    }
    init_instruction(&ins[store_base + 4u], RINGL_RSH1_OP_RETURN);

    memset(&header, 0, sizeof(header));
    header.magic = RINGL_RSH1_MAGIC;
    header.version = RINGL_RSH1_VERSION;
    header.header_size = sizeof(header);
    header.stage = RINGL_RSH1_STAGE_FRAGMENT;
    header.instruction_count = store_base + 5u;
    header.register_count = 8u * call_count + temporary_register_count +
        (color_operation_enabled ? 8u : 0u);
    header.input_count = 4u;
    header.output_count = 4u;
    header.resource_count = sampler_binding_count * 2u;
    total = sizeof(header) +
        (size_t)header.instruction_count * sizeof(ins[0]);
    header.total_size = (uint32_t)total;
    memcpy(result->bytes, &header, sizeof(header));
    memcpy(result->bytes + sizeof(header), ins,
           (size_t)header.instruction_count * sizeof(ins[0]));
    result->ok = 1u;
    result->instruction_count = header.instruction_count;
    result->register_count = header.register_count;
    result->input_count = header.input_count;
    result->output_count = header.output_count;
    result->byte_size = header.total_size;
    result->sampler_binding_count = sampler_binding_count;
    memcpy(result->sampler_binding_indices, sampler_binding_indices,
           (size_t)sampler_binding_count * sizeof(sampler_binding_indices[0]));
    return 0;
}

static int lower_vertex_color(const char* source,
                              uint32_t color_width,
                              RinGLGlslLowerResult* result)
{
    RinGLRsh1HeaderV1 header;
    RinGLRsh1InstructionV1 ins[17];
    char attribute_tag[16];
    char varying_tag[16];
    char position[64];
    char color[64];
    char varying[64];
    char expected[512];
    uint32_t zero_bits = 0u;
    float one = 1.0f;
    uint32_t one_bits;
    uint32_t input_count;
    uint32_t instruction_count;
    uint32_t input;
    size_t instruction_bytes;
    size_t total;

    if ((color_width != 3u && color_width != 4u) ||
        snprintf(attribute_tag, sizeof(attribute_tag), "attributevec%u",
                 color_width) < 0 ||
        snprintf(varying_tag, sizeof(varying_tag), "varyingvec%u",
                 color_width) < 0 ||
        !read_decl_name(source, "attributevec2", 0u, position,
                        sizeof(position)) ||
        !read_decl_name(source, attribute_tag, 0u, color, sizeof(color)) ||
        !read_decl_name(source, varying_tag, 0u, varying,
                        sizeof(varying))) {
        return 1;
    }
    (void)snprintf(expected, sizeof(expected),
                   "attributevec2%s;attributevec%u%s;varyingvec%u%s;"
                   "voidmain(){gl_Position=vec4(%s,0.0,1.0);%s=%s;}",
                   position, color_width, color, color_width, varying,
                   position, varying, color);
    if (strcmp(source, expected) != 0)
        return 1;

    input_count = 2u + color_width;
    instruction_count = input_count + 11u;
    for (input = 0u; input < input_count; ++input) {
        init_instruction(&ins[input], RINGL_RSH1_OP_LOAD_INPUT_F32);
        ins[input].destination = (uint16_t)input;
        ins[input].immediate = input;
    }
    memcpy(&one_bits, &one, sizeof(one_bits));
    init_instruction(&ins[input_count], RINGL_RSH1_OP_CONST_F32);
    ins[input_count].destination = (uint16_t)input_count;
    ins[input_count].immediate = zero_bits;
    init_instruction(&ins[input_count + 1u], RINGL_RSH1_OP_CONST_F32);
    ins[input_count + 1u].destination = (uint16_t)(input_count + 1u);
    ins[input_count + 1u].immediate = one_bits;
    for (input = 0u; input < 8u; ++input) {
        RinGLRsh1InstructionV1* store =
            &ins[input_count + 2u + input];
        init_instruction(store, RINGL_RSH1_OP_STORE_OUTPUT_F32);
        if (input < 2u)
            store->source0 = (uint16_t)input;
        else if (input == 2u)
            store->source0 = (uint16_t)input_count;
        else if (input == 3u)
            store->source0 = (uint16_t)(input_count + 1u);
        else if (input < 4u + color_width)
            store->source0 = (uint16_t)(input - 2u);
        else
            store->source0 = (uint16_t)(input_count + 1u);
        store->immediate = input;
    }
    init_instruction(&ins[instruction_count - 1u], RINGL_RSH1_OP_RETURN);

    memset(&header, 0, sizeof(header));
    header.magic = RINGL_RSH1_MAGIC;
    header.version = RINGL_RSH1_VERSION;
    header.header_size = sizeof(header);
    header.stage = RINGL_RSH1_STAGE_VERTEX;
    header.instruction_count = instruction_count;
    header.register_count = input_count + 2u;
    header.input_count = input_count;
    header.output_count = 8u;
    instruction_bytes = (size_t)instruction_count * sizeof(ins[0]);
    total = sizeof(header) + instruction_bytes;
    header.total_size = (uint32_t)total;
    memcpy(result->bytes, &header, sizeof(header));
    memcpy(result->bytes + sizeof(header), ins, instruction_bytes);
    result->ok = 1u;
    result->instruction_count = header.instruction_count;
    result->register_count = header.register_count;
    result->input_count = header.input_count;
    result->output_count = header.output_count;
    result->byte_size = header.total_size;
    return 0;
}

static int lower_fragment_color(const char* source,
                                uint32_t color_width,
                                RinGLGlslLowerResult* result)
{
    RinGLRsh1HeaderV1 header;
    RinGLRsh1InstructionV1 ins[9];
    char varying_tag[16];
    char varying[64];
    char expected[256];
    uint32_t component;
    size_t total;

    if ((color_width != 3u && color_width != 4u) ||
        snprintf(varying_tag, sizeof(varying_tag), "varyingvec%u",
                 color_width) < 0 ||
        !read_decl_name(source, varying_tag, 0u, varying,
                        sizeof(varying))) {
        return 1;
    }
    if (color_width == 4u) {
        (void)snprintf(expected, sizeof(expected),
                       "varyingvec4%s;voidmain(){gl_FragColor=%s;}",
                       varying, varying);
    } else {
        (void)snprintf(expected, sizeof(expected),
                       "varyingvec3%s;voidmain(){gl_FragColor=vec4(%s,1.0);}",
                       varying, varying);
    }
    if (strcmp(source, expected) != 0)
        return 1;
    for (component = 0u; component < 4u; ++component) {
        init_instruction(&ins[component], RINGL_RSH1_OP_LOAD_INPUT_F32);
        ins[component].destination = (uint16_t)component;
        ins[component].immediate = component;
    }
    for (component = 0u; component < 4u; ++component) {
        init_instruction(&ins[4u + component], RINGL_RSH1_OP_STORE_OUTPUT_F32);
        ins[4u + component].source0 = (uint16_t)component;
        ins[4u + component].immediate = component;
    }
    init_instruction(&ins[8], RINGL_RSH1_OP_RETURN);

    memset(&header, 0, sizeof(header));
    header.magic = RINGL_RSH1_MAGIC;
    header.version = RINGL_RSH1_VERSION;
    header.header_size = sizeof(header);
    header.stage = RINGL_RSH1_STAGE_FRAGMENT;
    header.instruction_count = 9u;
    header.register_count = 4u;
    header.input_count = 4u;
    header.output_count = 4u;
    total = sizeof(header) + sizeof(ins);
    header.total_size = (uint32_t)total;
    memcpy(result->bytes, &header, sizeof(header));
    memcpy(result->bytes + sizeof(header), ins, sizeof(ins));
    result->ok = 1u;
    result->instruction_count = header.instruction_count;
    result->register_count = header.register_count;
    result->input_count = header.input_count;
    result->output_count = header.output_count;
    result->byte_size = header.total_size;
    return 0;
}

static int lower_vertex_two_vec2(const char* source,
                                 RinGLGlslLowerResult* result)
{
    RinGLRsh1HeaderV1 header;
    RinGLRsh1InstructionV1 ins[17];
    char position[64];
    char first_attribute[64];
    char second_attribute[64];
    char first_varying[64];
    char second_varying[64];
    char expected[768];
    uint32_t zero_bits = 0u;
    float one = 1.0f;
    uint32_t one_bits;
    uint32_t input;
    size_t total;

    if (!read_decl_name(source, "attributevec2", 0u, position,
                        sizeof(position)) ||
        !read_decl_name(source, "attributevec2", 1u, first_attribute,
                        sizeof(first_attribute)) ||
        !read_decl_name(source, "attributevec2", 2u, second_attribute,
                        sizeof(second_attribute)) ||
        !read_decl_name(source, "varyingvec2", 0u, first_varying,
                        sizeof(first_varying)) ||
        !read_decl_name(source, "varyingvec2", 1u, second_varying,
                        sizeof(second_varying))) {
        return 1;
    }
    (void)snprintf(expected, sizeof(expected),
                   "attributevec2%s;attributevec2%s;attributevec2%s;"
                   "varyingvec2%s;varyingvec2%s;"
                   "voidmain(){gl_Position=vec4(%s,0.0,1.0);%s=%s;%s=%s;}",
                   position, first_attribute, second_attribute, first_varying,
                   second_varying, position, first_varying, first_attribute,
                   second_varying, second_attribute);
    if (strcmp(source, expected) != 0)
        return 1;

    for (input = 0u; input < 6u; ++input) {
        init_instruction(&ins[input], RINGL_RSH1_OP_LOAD_INPUT_F32);
        ins[input].destination = (uint16_t)input;
        ins[input].immediate = input;
    }
    memcpy(&one_bits, &one, sizeof(one_bits));
    init_instruction(&ins[6], RINGL_RSH1_OP_CONST_F32);
    ins[6].destination = 6u;
    ins[6].immediate = zero_bits;
    init_instruction(&ins[7], RINGL_RSH1_OP_CONST_F32);
    ins[7].destination = 7u;
    ins[7].immediate = one_bits;
    for (input = 0u; input < 8u; ++input) {
        init_instruction(&ins[8u + input], RINGL_RSH1_OP_STORE_OUTPUT_F32);
        if (input < 2u)
            ins[8u + input].source0 = (uint16_t)input;
        else if (input == 2u)
            ins[8u + input].source0 = 6u;
        else if (input == 3u)
            ins[8u + input].source0 = 7u;
        else
            ins[8u + input].source0 = (uint16_t)(input - 2u);
        ins[8u + input].immediate = input;
    }
    init_instruction(&ins[16], RINGL_RSH1_OP_RETURN);

    memset(&header, 0, sizeof(header));
    header.magic = RINGL_RSH1_MAGIC;
    header.version = RINGL_RSH1_VERSION;
    header.header_size = sizeof(header);
    header.stage = RINGL_RSH1_STAGE_VERTEX;
    header.instruction_count = 17u;
    header.register_count = 8u;
    header.input_count = 6u;
    header.output_count = 8u;
    total = sizeof(header) + sizeof(ins);
    header.total_size = (uint32_t)total;
    memcpy(result->bytes, &header, sizeof(header));
    memcpy(result->bytes + sizeof(header), ins, sizeof(ins));
    result->ok = 1u;
    result->instruction_count = header.instruction_count;
    result->register_count = header.register_count;
    result->input_count = header.input_count;
    result->output_count = header.output_count;
    result->byte_size = header.total_size;
    return 0;
}

static int lower_fragment_two_vec2(const char* source,
                                   RinGLGlslLowerResult* result)
{
    RinGLRsh1HeaderV1 header;
    RinGLRsh1InstructionV1 ins[9];
    char first_varying[64];
    char second_varying[64];
    char expected[384];
    uint32_t component;
    size_t total;

    if (!read_decl_name(source, "varyingvec2", 0u, first_varying,
                        sizeof(first_varying)) ||
        !read_decl_name(source, "varyingvec2", 1u, second_varying,
                        sizeof(second_varying))) {
        return 1;
    }
    (void)snprintf(expected, sizeof(expected),
                   "varyingvec2%s;varyingvec2%s;"
                   "voidmain(){gl_FragColor=vec4(%s,%s);}",
                   first_varying, second_varying, first_varying,
                   second_varying);
    if (strcmp(source, expected) != 0)
        return 1;
    for (component = 0u; component < 4u; ++component) {
        init_instruction(&ins[component], RINGL_RSH1_OP_LOAD_INPUT_F32);
        ins[component].destination = (uint16_t)component;
        ins[component].immediate = component;
        init_instruction(&ins[4u + component], RINGL_RSH1_OP_STORE_OUTPUT_F32);
        ins[4u + component].source0 = (uint16_t)component;
        ins[4u + component].immediate = component;
    }
    init_instruction(&ins[8], RINGL_RSH1_OP_RETURN);

    memset(&header, 0, sizeof(header));
    header.magic = RINGL_RSH1_MAGIC;
    header.version = RINGL_RSH1_VERSION;
    header.header_size = sizeof(header);
    header.stage = RINGL_RSH1_STAGE_FRAGMENT;
    header.instruction_count = 9u;
    header.register_count = 4u;
    header.input_count = 4u;
    header.output_count = 4u;
    total = sizeof(header) + sizeof(ins);
    header.total_size = (uint32_t)total;
    memcpy(result->bytes, &header, sizeof(header));
    memcpy(result->bytes + sizeof(header), ins, sizeof(ins));
    result->ok = 1u;
    result->instruction_count = header.instruction_count;
    result->register_count = header.register_count;
    result->input_count = header.input_count;
    result->output_count = header.output_count;
    result->byte_size = header.total_size;
    return 0;
}

int ringl_glsl_lower_varying_rsh1(uint32_t shader_type,
                                  const char* source,
                                  size_t source_length,
                                  RinGLGlslLowerResult* result)
{
    char* compact;
    int rc;

    if (source == NULL || result == NULL)
        return -1;
    memset(result, 0, sizeof(*result));
    compact = compact_source(source, source_length);
    if (compact == NULL) {
        (void)snprintf(result->diagnostic, sizeof(result->diagnostic),
                       "varying lowering could not normalize shader source");
        return 1;
    }
    if (strstr(compact, "varyingvec4") != NULL &&
        shader_type == RINGL_VERTEX_SHADER)
        rc = lower_vertex_color(compact, 4u, result);
    else if (strstr(compact, "varyingvec4") != NULL &&
             shader_type == RINGL_FRAGMENT_SHADER)
        rc = lower_fragment_color(compact, 4u, result);
    else if (strstr(compact, "varyingvec3") != NULL &&
             shader_type == RINGL_VERTEX_SHADER)
        rc = lower_vertex_color(compact, 3u, result);
    else if (strstr(compact, "varyingvec3") != NULL &&
             shader_type == RINGL_FRAGMENT_SHADER)
        rc = lower_fragment_color(compact, 3u, result);
    else if (strstr(compact, "varyingvec2") != NULL &&
             shader_type == RINGL_VERTEX_SHADER) {
        rc = lower_vertex_two_vec2(compact, result);
        if (rc != 0)
            rc = lower_vertex(compact, result);
    } else if (strstr(compact, "varyingvec2") != NULL &&
               shader_type == RINGL_FRAGMENT_SHADER) {
        rc = lower_fragment_two_vec2(compact, result);
        if (rc != 0)
            rc = lower_fragment_texture_chain(compact, result);
    }
    else if (shader_type == RINGL_VERTEX_SHADER)
        rc = lower_vertex(compact, result);
    else if (shader_type == RINGL_FRAGMENT_SHADER)
        rc = lower_fragment_texture_chain(compact, result);
    else
        rc = 1;
    free(compact);
    if (rc != 0 && result->diagnostic[0] == '\0')
        (void)snprintf(result->diagnostic, sizeof(result->diagnostic),
                       "shader is outside the initial varying lowering profile");
    return rc;
}
