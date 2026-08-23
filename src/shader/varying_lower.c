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

typedef struct VaryingTextureCall {
    uint32_t sampler_index;
    uint32_t coordinate_kind;
    float offset_u;
    float offset_v;
} VaryingTextureCall;

enum VaryingTextureCoordinateKind {
    RINGL_VARYING_TEXTURE_COORD_DIRECT = 0,
    RINGL_VARYING_TEXTURE_COORD_ADD_OFFSET,
    RINGL_VARYING_TEXTURE_COORD_SUB_OFFSET,
};

_Static_assert(8u * RINGL_VARYING_TEXTURE_MAX_CALLS + 4u <=
                   RINGL_RSH1_MAX_REGISTERS,
               "varying texture profile exceeds the RSH1 register ceiling");
_Static_assert(12u * RINGL_VARYING_TEXTURE_MAX_CALLS + 5u <=
                   RINGL_RSH1_MAX_INSTRUCTIONS,
               "varying texture profile exceeds the RSH1 instruction ceiling");

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

static int parse_varying_texture_call(const char** cursor,
                                      char sampler_names[][64],
                                      uint32_t sampler_count,
                                      const char* varying,
                                      VaryingTextureCall* call)
{
    char sampler[64];
    char coordinate[64];
    uint32_t coordinate_kind = RINGL_VARYING_TEXTURE_COORD_DIRECT;
    float offset_u = 0.0f;
    float offset_v = 0.0f;

    if (cursor == NULL || sampler_names == NULL || varying == NULL ||
        call == NULL || !consume_text(cursor, "texture2D(") ||
        !read_identifier(cursor, sampler, sizeof(sampler)) ||
        !consume_text(cursor, ",") ||
        !read_identifier(cursor, coordinate, sizeof(coordinate)) ||
        strcmp(coordinate, varying) != 0 ||
        !sampler_index_for_name(sampler_names, sampler_count, sampler,
                                &call->sampler_index)) {
        return 0;
    }
    if (**cursor == '+' || **cursor == '-') {
        coordinate_kind = **cursor == '+'
            ? RINGL_VARYING_TEXTURE_COORD_ADD_OFFSET
            : RINGL_VARYING_TEXTURE_COORD_SUB_OFFSET;
        ++*cursor;
        if (!consume_text(cursor, "vec2(") ||
            !parse_finite_float(cursor, &offset_u) ||
            !consume_text(cursor, ",") ||
            !parse_finite_float(cursor, &offset_v) ||
            !consume_text(cursor, ")")) {
            return 0;
        }
    }
    if (!consume_text(cursor, ")"))
        return 0;
    call->coordinate_kind = coordinate_kind;
    call->offset_u = offset_u;
    call->offset_v = offset_v;
    return 1;
}

/* The bounded texture profile accepts exactly one local vec2 alias. It is not
 * a host-side substitution: samples keep reading RSH1's interpolated inputs. */
static int parse_varying_texture_local_alias(const char** cursor,
                                             const char* varying,
                                             char* coordinate,
                                             size_t coordinate_capacity)
{
    char source[64];

    if (cursor == NULL || *cursor == NULL || varying == NULL ||
        coordinate == NULL || coordinate_capacity == 0u ||
        !consume_text(cursor, "vec2") ||
        !read_identifier(cursor, coordinate, coordinate_capacity) ||
        strcmp(coordinate, varying) == 0 ||
        !consume_text(cursor, "=") ||
        !read_identifier(cursor, source, sizeof(source)) ||
        strcmp(source, varying) != 0 || !consume_text(cursor, ";")) {
        return 0;
    }
    return 1;
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
 *   [UV inputs][optional affine coordinate][RGBA samples]
 *   [RGBA left-to-right sums][RGBA stores][return]
 *
 * An affine coordinate is deliberately bounded to one finite vec2 literal
 * added to or subtracted from the shared varying. The four temporary scalar
 * registers are overwritten for each call after its sample has consumed
 * them, so the eight-call profile stays below both RSH1 and RinGPU limits.
 */
static int lower_fragment_texture_chain(const char* source,
                                        RinGLGlslLowerResult* result)
{
    RinGLRsh1HeaderV1 header;
    RinGLRsh1InstructionV1 ins[RINGL_RSH1_MAX_INSTRUCTIONS];
    char sampler_names[RINGL_VARYING_TEXTURE_MAX_SAMPLERS][64];
    char varying[64];
    char coordinate[64];
    VaryingTextureCall calls[RINGL_VARYING_TEXTURE_MAX_CALLS];
    uint32_t sampler_resource_indices[RINGL_VARYING_TEXTURE_MAX_SAMPLERS] = {0u};
    uint32_t sampler_binding_indices[RINGL_VARYING_TEXTURE_MAX_SAMPLERS] = {0u};
    const char* cursor;
    uint32_t sampler_count = 0u;
    uint32_t sampler_binding_count = 0u;
    uint32_t call_count = 0u;
    uint32_t sample_base;
    uint32_t instruction_cursor;
    uint32_t add_base;
    uint32_t add_register_base;
    uint32_t store_base;
    uint32_t final_base;
    uint32_t padding_base;
    uint32_t coordinate_temp_base;
    uint32_t has_offset = 0u;
    uint32_t call_index;
    uint32_t sampler_index;
    uint32_t component;
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
    if (sampler_count == 0u || !consume_text(&cursor, "varyingvec2") ||
        !read_identifier(&cursor, varying, sizeof(varying)) ||
        !consume_text(&cursor, ";") || !consume_text(&cursor, "voidmain(){")) {
        return 1;
    }
    (void)snprintf(coordinate, sizeof(coordinate), "%s", varying);
    if (strncmp(cursor, "vec2", strlen("vec2")) == 0 &&
        !parse_varying_texture_local_alias(&cursor, varying, coordinate,
                                           sizeof(coordinate))) {
        return 1;
    }
    if (!consume_text(&cursor, "gl_FragColor="))
        return 1;
    for (;;) {
        if (call_count == RINGL_VARYING_TEXTURE_MAX_CALLS ||
            !parse_varying_texture_call(&cursor, sampler_names, sampler_count,
                                        coordinate, &calls[call_count])) {
            return 1;
        }
        ++call_count;
        if (*cursor != '+')
            break;
        ++cursor;
    }
    if (strcmp(cursor, ";}") != 0)
        return 1;

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
    for (call_index = 0u; call_index < call_count; ++call_index) {
        if (calls[call_index].coordinate_kind !=
            RINGL_VARYING_TEXTURE_COORD_DIRECT) {
            has_offset = 1u;
            instruction_cursor += 4u;
        }
        instruction_cursor += 4u;
    }
    add_base = instruction_cursor;
    store_base = add_base + (call_count - 1u) * 4u;
    add_register_base = 2u + call_count * 4u;
    final_base = call_count == 1u
        ? 2u : add_register_base + (call_count - 2u) * 4u;
    padding_base = final_base + 4u;
    coordinate_temp_base = padding_base + 2u;
    instruction_cursor = sample_base;
    memset(ins, 0, sizeof(ins));
    for (component = 0u; component < 4u; ++component) {
        init_instruction(&ins[component], RINGL_RSH1_OP_LOAD_INPUT_F32);
        ins[component].destination = component < 2u
            ? (uint16_t)component
            : (uint16_t)(padding_base + component - 2u);
        ins[component].immediate = component;
    }
    for (call_index = 0u; call_index < call_count; ++call_index) {
        uint32_t resource_pair =
            sampler_resource_indices[calls[call_index].sampler_index] * 2u;
        uint32_t coordinate_u = 0u;
        uint32_t coordinate_v = 1u;

        if (calls[call_index].coordinate_kind !=
            RINGL_VARYING_TEXTURE_COORD_DIRECT) {
            uint32_t offset_u_bits;
            uint32_t offset_v_bits;
            uint16_t opcode = calls[call_index].coordinate_kind ==
                    RINGL_VARYING_TEXTURE_COORD_ADD_OFFSET
                ? RINGL_RSH1_OP_ADD_F32
                : RINGL_RSH1_OP_SUB_F32;

            memcpy(&offset_u_bits, &calls[call_index].offset_u,
                   sizeof(offset_u_bits));
            memcpy(&offset_v_bits, &calls[call_index].offset_v,
                   sizeof(offset_v_bits));
            init_instruction(&ins[instruction_cursor], RINGL_RSH1_OP_CONST_F32);
            ins[instruction_cursor].destination =
                (uint16_t)coordinate_temp_base;
            ins[instruction_cursor++].immediate = offset_u_bits;
            init_instruction(&ins[instruction_cursor], RINGL_RSH1_OP_CONST_F32);
            ins[instruction_cursor].destination =
                (uint16_t)(coordinate_temp_base + 1u);
            ins[instruction_cursor++].immediate = offset_v_bits;
            init_instruction(&ins[instruction_cursor], opcode);
            ins[instruction_cursor].destination =
                (uint16_t)(coordinate_temp_base + 2u);
            ins[instruction_cursor].source0 = 0u;
            ins[instruction_cursor++].source1 =
                (uint16_t)coordinate_temp_base;
            init_instruction(&ins[instruction_cursor], opcode);
            ins[instruction_cursor].destination =
                (uint16_t)(coordinate_temp_base + 3u);
            ins[instruction_cursor].source0 = 1u;
            ins[instruction_cursor++].source1 =
                (uint16_t)(coordinate_temp_base + 1u);
            coordinate_u = coordinate_temp_base + 2u;
            coordinate_v = coordinate_temp_base + 3u;
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
    header.register_count = 8u * call_count + has_offset * 4u;
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
