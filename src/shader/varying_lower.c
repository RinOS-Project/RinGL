/* SPDX-License-Identifier: MIT */
#include "varying_lower.h"
#include "rsh1_abi.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
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

/* The structural varying lowerers compare a whitespace-free source shape.
 * Remove only fully validated default-precision declarations before those
 * comparisons; ringl_glsl_parse() has already rejected every other form.
 * RSH1 evaluates the accepted scalar domain in binary32, so these statements
 * change GLSL source precision but require no hidden backend mode. */
static void strip_precision_declarations(char* source)
{
    char* read = source;
    char* write = source;

    while (*read != '\0') {
        const char* qualifier = NULL;
        const char* type = NULL;
        char* cursor;

        if (strncmp(read, "precision", strlen("precision")) == 0) {
            cursor = read + strlen("precision");
            if (strncmp(cursor, "lowp", strlen("lowp")) == 0)
                qualifier = "lowp";
            else if (strncmp(cursor, "mediump", strlen("mediump")) == 0)
                qualifier = "mediump";
            else if (strncmp(cursor, "highp", strlen("highp")) == 0)
                qualifier = "highp";
            if (qualifier != NULL) {
                cursor += strlen(qualifier);
                if (strncmp(cursor, "float", strlen("float")) == 0)
                    type = "float";
                else if (strncmp(cursor, "int", strlen("int")) == 0)
                    type = "int";
                else if (strncmp(cursor, "sampler2D", strlen("sampler2D")) == 0)
                    type = "sampler2D";
                if (type != NULL) {
                    cursor += strlen(type);
                    if (*cursor == ';') {
                        read = cursor + 1u;
                        continue;
                    }
                }
            }
        }
        *write++ = *read++;
    }
    *write = '\0';
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
    strip_precision_declarations(compact);
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

static int append_compact_source(char* output, size_t capacity,
                                 size_t* length, const char* format, ...)
{
    va_list arguments;
    int written;

    if (output == NULL || length == NULL || format == NULL ||
        *length >= capacity) {
        return 0;
    }
    va_start(arguments, format);
    written = vsnprintf(output + *length, capacity - *length, format,
                        arguments);
    va_end(arguments);
    if (written < 0 || (size_t)written >= capacity - *length)
        return 0;
    *length += (size_t)written;
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

/* The bounded varying profiles still need ordinary GLSL read selectors at
 * their source boundary. Keep the selector as a register permutation: RSH1
 * has scalar registers, so emitting a synthetic vector operation would hide
 * the component order from every backend. A caller may omit the selector,
 * but when present it must produce the exact width required by the enclosing
 * GLSL expression. */
static int parse_optional_read_swizzle(const char** cursor,
                                       uint32_t source_width,
                                       uint32_t result_width,
                                       uint32_t components[4])
{
    uint32_t current_width;
    uint32_t index;

    if (cursor == NULL || *cursor == NULL || components == NULL ||
        source_width == 0u || source_width > 4u || result_width == 0u ||
        result_width > 4u) {
        return 0;
    }
    for (index = 0u; index < source_width; ++index)
        components[index] = index;
    current_width = source_width;
    while (**cursor == '.') {
        const char* p = *cursor + 1u;
        uint32_t selected[4];
        uint32_t length = 0u;
        uint8_t family = 0u;

        while (isalpha((unsigned char)*p)) {
            uint8_t component_family;
            uint32_t component_index;

            if (length == 4u)
                return 0;
            switch (*p++) {
            case 'x': component_family = 1u; component_index = 0u; break;
            case 'y': component_family = 1u; component_index = 1u; break;
            case 'z': component_family = 1u; component_index = 2u; break;
            case 'w': component_family = 1u; component_index = 3u; break;
            case 'r': component_family = 2u; component_index = 0u; break;
            case 'g': component_family = 2u; component_index = 1u; break;
            case 'b': component_family = 2u; component_index = 2u; break;
            case 'a': component_family = 2u; component_index = 3u; break;
            case 's': component_family = 3u; component_index = 0u; break;
            case 't': component_family = 3u; component_index = 1u; break;
            case 'p': component_family = 3u; component_index = 2u; break;
            case 'q': component_family = 3u; component_index = 3u; break;
            default: return 0;
            }
            if ((family != 0u && family != component_family) ||
                component_index >= current_width) {
                return 0;
            }
            family = component_family;
            selected[length++] = components[component_index];
        }
        if (length == 0u)
            return 0;
        for (index = 0u; index < length; ++index)
            components[index] = selected[index];
        current_width = length;
        *cursor = p;
    }
    if (current_width != result_width)
        return 0;
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
    if (call->coordinate_input_location != UINT32_MAX &&
        (call->coordinate_input_location > 6u ||
         (call->coordinate_input_location & 1u) != 0u)) {
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
        if (call->coordinate_input_location == UINT32_MAX ||
            call->secondary_coordinate_input_location == UINT32_MAX ||
            call->secondary_coordinate_input_location > 6u ||
            (call->secondary_coordinate_input_location & 1u) != 0u)
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

static int parse_varying_texture_local_coordinate_or_varying(
    const char** cursor, const char* source_coordinate,
    char varying_names[][64], uint32_t varying_count,
    uint32_t allowed_secondary_input_location, char* coordinate,
    size_t coordinate_capacity, uint32_t* coordinate_kind, float* offset_u,
    float* offset_v, uint32_t* secondary_input_location)
{
    char source[64];
    char secondary[64];
    uint32_t index;

    if (cursor == NULL || *cursor == NULL || source_coordinate == NULL ||
        varying_names == NULL || varying_count != 3u || coordinate == NULL ||
        coordinate_capacity == 0u || coordinate_kind == NULL ||
        offset_u == NULL || offset_v == NULL || secondary_input_location == NULL ||
        !consume_text(cursor, "vec2") ||
        !read_identifier(cursor, coordinate, coordinate_capacity) ||
        strcmp(coordinate, source_coordinate) == 0 ||
        !consume_text(cursor, "=") ||
        !read_identifier(cursor, source, sizeof(source)) ||
        strcmp(source, source_coordinate) != 0) {
        return 0;
    }
    *secondary_input_location = UINT32_MAX;
    if ((*(*cursor) == '+' || *(*cursor) == '-') &&
        isalpha((unsigned char)(*cursor)[1])) {
        int subtract = **cursor == '-';

        ++*cursor;
        if (!read_identifier(cursor, secondary, sizeof(secondary)) ||
            !consume_text(cursor, ";")) {
            return 0;
        }
        for (index = 0u; index < varying_count; ++index) {
            if (strcmp(secondary, varying_names[index]) == 0)
                break;
        }
        if (index == varying_count || index * 2u != allowed_secondary_input_location)
            return 0;
        *coordinate_kind = subtract
            ? RINGL_VARYING_TEXTURE_COORD_SUB_COORDINATE
            : RINGL_VARYING_TEXTURE_COORD_ADD_COORDINATE;
        *secondary_input_location = allowed_secondary_input_location;
        return 1;
    }
    return parse_varying_texture_offset(cursor, coordinate_kind, offset_u,
                                        offset_v) && consume_text(cursor, ";");
}

/* Keep the multi-varying extension deliberately narrow: one local vec2 may
 * combine any two distinct declared perspective coordinates, then samples
 * read that named result. This accepts the natural GLSL spelling without
 * pretending to support arbitrary local vector expressions. */
static int parse_varying_texture_two_coordinate_local(
    const char** cursor, char varying_names[][64], uint32_t varying_count,
    char* coordinate,
    size_t coordinate_capacity, uint32_t* coordinate_kind,
    uint32_t* primary_input_location, uint32_t* secondary_input_location,
    uint32_t* tertiary_coordinate_kind, uint32_t* tertiary_input_location)
{
    char primary[64];
    char secondary[64];
    char tertiary[64];
    uint32_t index;

    if (cursor == NULL || *cursor == NULL || varying_names == NULL ||
        varying_count < 2u || varying_count > 4u || coordinate == NULL ||
        coordinate_capacity == 0u ||
        coordinate_kind == NULL || primary_input_location == NULL ||
        secondary_input_location == NULL || tertiary_coordinate_kind == NULL ||
        tertiary_input_location == NULL || !consume_text(cursor, "vec2") ||
        !read_identifier(cursor, coordinate, coordinate_capacity) ||
        !consume_text(cursor, "=") ||
        !read_identifier(cursor, primary, sizeof(primary)) ||
        (**cursor != '+' && **cursor != '-')) {
        return 0;
    }
    for (index = 0u; index < varying_count; ++index) {
        if (strcmp(coordinate, varying_names[index]) == 0)
            return 0;
    }
    *coordinate_kind = **cursor == '+'
        ? RINGL_VARYING_TEXTURE_COORD_ADD_COORDINATE
        : RINGL_VARYING_TEXTURE_COORD_SUB_COORDINATE;
    ++*cursor;
    if (!read_identifier(cursor, secondary, sizeof(secondary))) {
        return 0;
    }
    for (index = 0u; index < varying_count; ++index) {
        if (strcmp(primary, varying_names[index]) == 0) {
            *primary_input_location = index * 2u;
            break;
        }
    }
    if (index == varying_count)
        return 0;
    for (index = 0u; index < varying_count; ++index) {
        if (strcmp(secondary, varying_names[index]) == 0) {
            *secondary_input_location = index * 2u;
            break;
        }
    }
    if (index == varying_count || *primary_input_location == *secondary_input_location)
        return 0;
    *tertiary_input_location = UINT32_MAX;
    if (varying_count == 3u && (**cursor == '+' || **cursor == '-') &&
        isalpha((unsigned char)(*cursor)[1])) {
        int subtract = **cursor == '-';

        ++*cursor;
        if (!read_identifier(cursor, tertiary, sizeof(tertiary)) ||
            !consume_text(cursor, ";")) {
            return 0;
        }
        for (index = 0u; index < varying_count; ++index) {
            if (strcmp(tertiary, varying_names[index]) == 0)
                break;
        }
        if (index == varying_count || index * 2u == *primary_input_location ||
            index * 2u == *secondary_input_location) {
            return 0;
        }
        *tertiary_coordinate_kind = subtract
            ? RINGL_VARYING_TEXTURE_COORD_SUB_COORDINATE
            : RINGL_VARYING_TEXTURE_COORD_ADD_COORDINATE;
        *tertiary_input_location = index * 2u;
        return 1;
    }
    return consume_text(cursor, ";");
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
                                                 float color[4],
                                                 const char* uniform_name)
{
    uint32_t component;
    uint32_t components[4];

    if (!parse_varying_texture_color_operator(cursor, opcode)) {
        return 0;
    }
    if (!parse_varying_texture_color_literal(cursor, color)) {
        char name[64];
        float uniform_color[4];

        if (uniform_name == NULL || !read_identifier(cursor, name,
                                                      sizeof(name)) ||
            strcmp(name, uniform_name) != 0) {
            return 0;
        }
        memcpy(uniform_color, color, sizeof(uniform_color));
        if (!parse_optional_read_swizzle(cursor, 4u, 4u, components)) {
            return 0;
        }
        for (component = 0u; component < 4u; ++component)
            color[component] = uniform_color[components[component]];
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

/* This is the WebGL transformed-texture vertex route with a program-owned
 * matrix. It keeps the scalar attribute/varying ABI used by the texture
 * fragment lowerer: clip position occupies outputs 0..3 and one through four
 * UV pairs follow in declaration order. A single UV pair reserves the final
 * two scalar slots to retain the fixed eight-output RinGPU interface; two,
 * three, and four pairs occupy 8, 10, and 12 outputs respectively. The
 * profile may instead append one `attribute vec4`/`varying vec4` color after
 * one or two UV pairs, so the complete interpolation interface remains at
 * most eight scalars. The matrix values are materialized as RSH1 constants at
 * link time and on uniform updates, exactly as the no-varying mat4 lowerer
 * does; RinGPU executes the multiply rather than the embedding
 * pre-transforming geometry.
 * Both common position spellings are accepted:
 *
 *   vec4 position; vec2 texCoord0..3; transform * position
 *   vec2 position; vec2 texCoord0..3; transform * vec4(position, 0, 1)
 */
static int lower_vertex_transformed_texture(
    const char* source, const RinGLGlslUniformValue* uniforms,
    uint32_t uniform_count, RinGLGlslLowerResult* result)
{
    RinGLRsh1HeaderV1 header;
    RinGLRsh1InstructionV1 ins[RINGL_RSH1_MAX_INSTRUCTIONS];
    char position[64];
    char texcoords[4][64];
    char color_attribute[64];
    char transform[64];
    char varyings[4][64];
    char color_varying[64];
    char expected[2048];
    uint16_t position_regs[4];
    uint16_t transformed_position_regs[4];
    uint16_t texcoord_regs[8];
    uint16_t color_regs[4];
    uint16_t matrix_regs[16];
    uint16_t zero_reg;
    uint16_t one_reg;
    uint16_t next_reg = 0u;
    uint32_t position_width;
    uint32_t texcoord_count = 0u;
    uint32_t has_vertex_color = 0u;
    uint32_t input_count;
    uint32_t varying_scalar_count;
    uint32_t output_count;
    uint32_t instruction_cursor = 0u;
    uint32_t matrix_index;
    uint32_t row;
    size_t expected_length = 0u;
    float values[16] = {0.0f};
    size_t total;

    if (source == NULL || result == NULL ||
        (uniform_count != 0u && uniforms == NULL)) {
        return 1;
    }
    if (strncmp(source, "attributevec4", strlen("attributevec4")) == 0 &&
        read_decl_name(source, "attributevec4", 0u, position,
                       sizeof(position))) {
        position_width = 4u;
    } else {
        position_width = 2u;
        if (!read_decl_name(source, "attributevec2", 0u, position,
                            sizeof(position))) {
            return 1;
        }
    }
    while (texcoord_count < 4u &&
           read_decl_name(source, "attributevec2",
                          texcoord_count +
                              (position_width == 2u ? 1u : 0u),
                          texcoords[texcoord_count],
                          sizeof(texcoords[texcoord_count]))) {
        ++texcoord_count;
    }
    if (texcoord_count == 0u)
        return 1;
    if (read_decl_name(source, "attributevec4",
                       position_width == 4u ? 1u : 0u, color_attribute,
                       sizeof(color_attribute))) {
        if (texcoord_count > 2u ||
            !read_decl_name(source, "varyingvec4", 0u, color_varying,
                            sizeof(color_varying))) {
            return 1;
        }
        has_vertex_color = 1u;
    }
    if (!read_decl_name(source, "uniformmat4", 0u, transform,
                        sizeof(transform))) {
        return 1;
    }
    for (matrix_index = 0u; matrix_index < texcoord_count; ++matrix_index) {
        if (!read_decl_name(source, "varyingvec2", matrix_index,
                            varyings[matrix_index],
                            sizeof(varyings[matrix_index]))) {
            return 1;
        }
    }
    if (!append_compact_source(expected, sizeof(expected), &expected_length,
                               position_width == 4u ? "attributevec4%s;"
                                                    : "attributevec2%s;",
                               position)) {
        return 1;
    }
    for (matrix_index = 0u; matrix_index < texcoord_count; ++matrix_index) {
        if (!append_compact_source(expected, sizeof(expected),
                                   &expected_length, "attributevec2%s;",
                                   texcoords[matrix_index])) {
            return 1;
        }
    }
    if (has_vertex_color &&
        !append_compact_source(expected, sizeof(expected), &expected_length,
                               "attributevec4%s;", color_attribute)) {
        return 1;
    }
    if (!append_compact_source(expected, sizeof(expected), &expected_length,
                               "uniformmat4%s;", transform)) {
        return 1;
    }
    for (matrix_index = 0u; matrix_index < texcoord_count; ++matrix_index) {
        if (!append_compact_source(expected, sizeof(expected),
                                   &expected_length, "varyingvec2%s;",
                                   varyings[matrix_index])) {
            return 1;
        }
    }
    if (has_vertex_color &&
        !append_compact_source(expected, sizeof(expected), &expected_length,
                               "varyingvec4%s;", color_varying)) {
        return 1;
    }
    if (!append_compact_source(expected, sizeof(expected), &expected_length,
                               "voidmain(){gl_Position=%s*", transform) ||
        !append_compact_source(expected, sizeof(expected), &expected_length,
                               position_width == 4u ? "%s;" : "vec4(%s,0.0,1.0);",
                               position)) {
        return 1;
    }
    for (matrix_index = 0u; matrix_index < texcoord_count; ++matrix_index) {
        if (!append_compact_source(expected, sizeof(expected),
                                   &expected_length, "%s=%s;",
                                   varyings[matrix_index],
                                   texcoords[matrix_index])) {
            return 1;
        }
    }
    if (has_vertex_color &&
        !append_compact_source(expected, sizeof(expected), &expected_length,
                               "%s=%s;", color_varying, color_attribute)) {
        return 1;
    }
    if (!append_compact_source(expected, sizeof(expected), &expected_length,
                               "}")) {
        return 1;
    }
    if (strcmp(source, expected) != 0)
        return 1;
    for (matrix_index = 0u; matrix_index < uniform_count; ++matrix_index) {
        if (uniforms[matrix_index].name == NULL ||
            strcmp(uniforms[matrix_index].name, transform) != 0) {
            continue;
        }
        if (uniforms[matrix_index].type != RINGL_FLOAT_MAT4)
            return 1;
        memcpy(values, uniforms[matrix_index].values, sizeof(values));
        break;
    }

    memset(ins, 0, sizeof(ins));
    input_count = position_width + texcoord_count * 2u +
        (has_vertex_color ? 4u : 0u);
    if (position_width == 4u) {
        for (matrix_index = 0u; matrix_index < input_count; ++matrix_index) {
            init_instruction(&ins[instruction_cursor], RINGL_RSH1_OP_LOAD_INPUT_F32);
            ins[instruction_cursor].destination = next_reg;
            ins[instruction_cursor++].immediate = matrix_index;
            if (matrix_index < 4u)
                position_regs[matrix_index] = next_reg;
            else if (matrix_index < position_width + texcoord_count * 2u)
                texcoord_regs[matrix_index - position_width] = next_reg;
            else
                color_regs[matrix_index - position_width -
                           texcoord_count * 2u] = next_reg;
            ++next_reg;
        }
        zero_reg = RINGL_RSH1_UNUSED;
        one_reg = RINGL_RSH1_UNUSED;
    } else {
        for (matrix_index = 0u; matrix_index < input_count; ++matrix_index) {
            init_instruction(&ins[instruction_cursor], RINGL_RSH1_OP_LOAD_INPUT_F32);
            ins[instruction_cursor].destination = next_reg;
            ins[instruction_cursor++].immediate = matrix_index;
            if (matrix_index < 2u)
                position_regs[matrix_index] = next_reg;
            else if (matrix_index < position_width + texcoord_count * 2u)
                texcoord_regs[matrix_index - position_width] = next_reg;
            else
                color_regs[matrix_index - position_width -
                           texcoord_count * 2u] = next_reg;
            ++next_reg;
        }
        zero_reg = next_reg++;
        init_instruction(&ins[instruction_cursor], RINGL_RSH1_OP_CONST_F32);
        ins[instruction_cursor++].destination = zero_reg;
        one_reg = next_reg++;
        init_instruction(&ins[instruction_cursor], RINGL_RSH1_OP_CONST_F32);
        ins[instruction_cursor].destination = one_reg;
        {
            float one = 1.0f;
            memcpy(&ins[instruction_cursor].immediate, &one,
                   sizeof(ins[instruction_cursor].immediate));
        }
        ++instruction_cursor;
        position_regs[2] = zero_reg;
        position_regs[3] = one_reg;
    }
    for (matrix_index = 0u; matrix_index < 16u; ++matrix_index) {
        uint32_t bits;

        memcpy(&bits, &values[matrix_index], sizeof(bits));
        matrix_regs[matrix_index] = next_reg++;
        init_instruction(&ins[instruction_cursor], RINGL_RSH1_OP_CONST_F32);
        ins[instruction_cursor].destination = matrix_regs[matrix_index];
        ins[instruction_cursor++].immediate = bits;
    }
    for (row = 0u; row < 4u; ++row) {
        uint16_t products[4];
        uint16_t left_sum;
        uint16_t right_sum;
        uint16_t output;
        uint32_t column;

        for (column = 0u; column < 4u; ++column) {
            products[column] = next_reg++;
            init_instruction(&ins[instruction_cursor], RINGL_RSH1_OP_MUL_F32);
            ins[instruction_cursor].destination = products[column];
            ins[instruction_cursor].source0 = matrix_regs[column * 4u + row];
            ins[instruction_cursor++].source1 = position_regs[column];
        }
        left_sum = next_reg++;
        init_instruction(&ins[instruction_cursor], RINGL_RSH1_OP_ADD_F32);
        ins[instruction_cursor].destination = left_sum;
        ins[instruction_cursor].source0 = products[0];
        ins[instruction_cursor++].source1 = products[1];
        right_sum = next_reg++;
        init_instruction(&ins[instruction_cursor], RINGL_RSH1_OP_ADD_F32);
        ins[instruction_cursor].destination = right_sum;
        ins[instruction_cursor].source0 = products[2];
        ins[instruction_cursor++].source1 = products[3];
        output = next_reg++;
        init_instruction(&ins[instruction_cursor], RINGL_RSH1_OP_ADD_F32);
        ins[instruction_cursor].destination = output;
        ins[instruction_cursor].source0 = left_sum;
        ins[instruction_cursor++].source1 = right_sum;
        /* Preserve the four original attribute registers until every matrix
         * row has consumed them. Reusing position_regs here would make row 1
         * and later multiply against a prior output for non-diagonal
         * transforms. */
        transformed_position_regs[row] = output;
    }
    if (zero_reg == RINGL_RSH1_UNUSED) {
        zero_reg = next_reg++;
        init_instruction(&ins[instruction_cursor], RINGL_RSH1_OP_CONST_F32);
        ins[instruction_cursor++].destination = zero_reg;
        one_reg = next_reg++;
        init_instruction(&ins[instruction_cursor], RINGL_RSH1_OP_CONST_F32);
        ins[instruction_cursor].destination = one_reg;
        {
            float one = 1.0f;
            memcpy(&ins[instruction_cursor].immediate, &one,
                   sizeof(ins[instruction_cursor].immediate));
        }
        ++instruction_cursor;
    }
    varying_scalar_count = texcoord_count * 2u +
        (has_vertex_color ? 4u : 0u);
    if (varying_scalar_count > 8u)
        return 1;
    output_count = 4u + varying_scalar_count;
    if (varying_scalar_count == 2u)
        output_count = 8u;
    for (row = 0u; row < output_count; ++row) {
        uint16_t source_reg;

        if (row < 4u) {
            source_reg = transformed_position_regs[row];
        } else if (row < 4u + texcoord_count * 2u) {
            source_reg = texcoord_regs[row - 4u];
        } else if (row < 4u + varying_scalar_count) {
            source_reg = color_regs[row - 4u - texcoord_count * 2u];
        } else {
            source_reg = row == 6u ? zero_reg : one_reg;
        }

        init_instruction(&ins[instruction_cursor], RINGL_RSH1_OP_STORE_OUTPUT_F32);
        ins[instruction_cursor].source0 = source_reg;
        ins[instruction_cursor++].immediate = row;
    }
    init_instruction(&ins[instruction_cursor++], RINGL_RSH1_OP_RETURN);

    if (instruction_cursor > RINGL_RSH1_MAX_INSTRUCTIONS ||
        next_reg > RINGL_RSH1_MAX_REGISTERS) {
        return 1;
    }
    memset(&header, 0, sizeof(header));
    header.magic = RINGL_RSH1_MAGIC;
    header.version = RINGL_RSH1_VERSION;
    header.header_size = sizeof(header);
    header.stage = RINGL_RSH1_STAGE_VERTEX;
    header.instruction_count = instruction_cursor;
    header.register_count = next_reg;
    header.input_count = input_count;
    header.output_count = output_count;
    total = sizeof(header) + (size_t)instruction_cursor * sizeof(ins[0]);
    header.total_size = (uint32_t)total;
    memcpy(result->bytes, &header, sizeof(header));
    memcpy(result->bytes + sizeof(header), ins,
           (size_t)instruction_cursor * sizeof(ins[0]));
    result->ok = 1u;
    result->instruction_count = header.instruction_count;
    result->register_count = header.register_count;
    result->input_count = header.input_count;
    result->output_count = header.output_count;
    result->byte_size = header.total_size;
    return 0;
}

/* This profile accepts one or two perspective-interpolated UV pairs followed
 * by one perspective-interpolated RGBA vertex color. It is the common WebGL
 * `texture2D(...) * vertexColor` route, or two sampled textures added before
 * that modulation, optionally followed by one linked `uniform vec4` tint
 * and/or one `uniform float` opacity. Interpolation inputs, image samples,
 * additions, component-wise products, scalar opacity broadcasts, and final
 * stores are RSH1 instructions. The source shape is exact so a later
 * color/texture expression cannot be mistaken for this bounded native
 * execution path. */
static int lower_fragment_textured_vertex_color(
    const char* source, const RinGLGlslUniformValue* uniforms,
    uint32_t uniform_count, RinGLGlslLowerResult* result)
{
    RinGLRsh1HeaderV1 header;
    RinGLRsh1InstructionV1 ins[64];
    char samplers[2][64];
    char uvs[2][64];
    char color[64];
    char tint_name[64];
    char opacity_name[64];
    char expected[1024];
    float tint[4] = {0.0f};
    float opacity = 0.0f;
    uint32_t has_tint = 0u;
    uint32_t has_opacity = 0u;
    uint32_t sampler_count = 1u;
    uint32_t input_count;
    uint32_t sample_base;
    uint32_t color_input_base;
    uint32_t color_components[4];
    uint32_t tint_components[4] = {0u, 1u, 2u, 3u};
    uint32_t instruction_cursor;
    uint32_t next_register;
    uint32_t store_base;
    uint32_t final_base;
    uint32_t component;
    uint32_t sampler_index;
    size_t expected_length = 0u;
    size_t total;
    const char* color_cursor;
    const char* color_selector;
    const char* tint_selector = NULL;
    size_t color_selector_length;
    size_t tint_selector_length = 0u;

    if (source == NULL || result == NULL ||
        (uniform_count != 0u && uniforms == NULL) ||
        !read_decl_name(source, "uniformsampler2D", 0u, samplers[0],
                        sizeof(samplers[0])) ||
        !read_decl_name(source, "varyingvec2", 0u, uvs[0],
                        sizeof(uvs[0])) ||
        !read_decl_name(source, "varyingvec4", 0u, color, sizeof(color))) {
        return 1;
    }
    if (read_decl_name(source, "uniformsampler2D", 1u, samplers[1],
                       sizeof(samplers[1]))) {
        sampler_count = 2u;
        if (!read_decl_name(source, "varyingvec2", 1u, uvs[1],
                            sizeof(uvs[1]))) {
            return 1;
        }
    }
    if (read_decl_name(source, "uniformvec4", 0u, tint_name,
                       sizeof(tint_name))) {
        uint32_t index;

        has_tint = 1u;
        for (index = 0u; index < uniform_count; ++index) {
            if (uniforms[index].name == NULL ||
                strcmp(uniforms[index].name, tint_name) != 0) {
                continue;
            }
            if (uniforms[index].type != RINGL_FLOAT_VEC4)
                return 1;
            memcpy(tint, uniforms[index].values, sizeof(tint));
            break;
        }
        for (index = 0u; index < 4u; ++index) {
            if (!isfinite(tint[index]))
                return 1;
        }
    }
    if (read_decl_name(source, "uniformfloat", 0u, opacity_name,
                       sizeof(opacity_name))) {
        uint32_t index;

        has_opacity = 1u;
        for (index = 0u; index < uniform_count; ++index) {
            if (uniforms[index].name == NULL ||
                strcmp(uniforms[index].name, opacity_name) != 0) {
                continue;
            }
            if (uniforms[index].type != RINGL_FLOAT)
                return 1;
            opacity = uniforms[index].values[0];
            break;
        }
        if (!isfinite(opacity))
            return 1;
    }
    for (sampler_index = 0u; sampler_index < sampler_count; ++sampler_index) {
        if (!append_compact_source(expected, sizeof(expected), &expected_length,
                                   "uniformsampler2D%s;",
                                   samplers[sampler_index])) {
            return 1;
        }
    }
    if (has_tint &&
        !append_compact_source(expected, sizeof(expected), &expected_length,
                               "uniformvec4%s;", tint_name)) {
        return 1;
    }
    if (has_opacity &&
        !append_compact_source(expected, sizeof(expected), &expected_length,
                               "uniformfloat%s;", opacity_name)) {
        return 1;
    }
    for (sampler_index = 0u; sampler_index < sampler_count; ++sampler_index) {
        if (!append_compact_source(expected, sizeof(expected), &expected_length,
                                   "varyingvec2%s;", uvs[sampler_index])) {
            return 1;
        }
    }
    if (!append_compact_source(expected, sizeof(expected), &expected_length,
                               "varyingvec4%s;voidmain(){gl_FragColor=",
                               color)) {
        return 1;
    }
    if (sampler_count == 2u) {
        if (!append_compact_source(expected, sizeof(expected), &expected_length,
                                   "(texture2D(%s,%s)+texture2D(%s,%s))",
                                   samplers[0], uvs[0], samplers[1], uvs[1])) {
            return 1;
        }
    } else if (!append_compact_source(expected, sizeof(expected),
                                      &expected_length, "texture2D(%s,%s)",
                                      samplers[0], uvs[0])) {
        return 1;
    }
    if (strncmp(source, expected, expected_length) != 0)
        return 1;
    color_cursor = source + expected_length;
    if (!consume_text(&color_cursor, "*") ||
        !consume_text(&color_cursor, color)) {
        return 1;
    }
    color_selector = color_cursor;
    if (!parse_optional_read_swizzle(&color_cursor, 4u, 4u,
                                     color_components)) {
        return 1;
    }
    color_selector_length = (size_t)(color_cursor - color_selector);
    if (has_tint) {
        if (!consume_text(&color_cursor, "*") ||
            !consume_text(&color_cursor, tint_name)) {
            return 1;
        }
        tint_selector = color_cursor;
        if (!parse_optional_read_swizzle(&color_cursor, 4u, 4u,
                                         tint_components)) {
            return 1;
        }
        tint_selector_length = (size_t)(color_cursor - tint_selector);
    }
    if (has_opacity &&
        (!consume_text(&color_cursor, "*") ||
         !consume_text(&color_cursor, opacity_name))) {
        return 1;
    }
    if (!append_compact_source(expected, sizeof(expected), &expected_length,
                               "*%s", color) ||
        (color_selector_length != 0u &&
         !append_compact_source(expected, sizeof(expected), &expected_length,
                                "%.*s", (int)color_selector_length,
                                color_selector)) ||
        (has_tint &&
         !append_compact_source(expected, sizeof(expected), &expected_length,
                                "*%s", tint_name)) ||
        (tint_selector_length != 0u &&
         !append_compact_source(expected, sizeof(expected), &expected_length,
                                "%.*s", (int)tint_selector_length,
                                tint_selector)) ||
        (has_opacity &&
         !append_compact_source(expected, sizeof(expected), &expected_length,
                                "*%s", opacity_name)) ||
        !append_compact_source(expected, sizeof(expected), &expected_length,
                               ";}")) {
        return 1;
    }
    if (strcmp(source, expected) != 0)
        return 1;

    memset(ins, 0, sizeof(ins));
    input_count = sampler_count * 2u + 4u;
    for (component = 0u; component < input_count; ++component) {
        init_instruction(&ins[component], RINGL_RSH1_OP_LOAD_INPUT_F32);
        ins[component].destination = (uint16_t)component;
        ins[component].immediate = component;
    }
    sample_base = input_count;
    for (sampler_index = 0u; sampler_index < sampler_count; ++sampler_index) {
        uint32_t resource_base = sampler_index * 2u;

        for (component = 0u; component < 4u; ++component) {
            uint32_t instruction = sample_base + sampler_index * 4u + component;

            init_instruction(&ins[instruction], RINGL_RSH1_OP_SAMPLE_IMAGE_2D_F32);
            ins[instruction].flags = (uint16_t)component;
            ins[instruction].destination = (uint16_t)instruction;
            ins[instruction].source0 = (uint16_t)(sampler_index * 2u);
            ins[instruction].source1 = (uint16_t)(sampler_index * 2u + 1u);
            ins[instruction].resource = (uint16_t)resource_base;
            ins[instruction].immediate = resource_base + 1u;
        }
    }
    instruction_cursor = sample_base + sampler_count * 4u;
    next_register = instruction_cursor;
    final_base = sample_base;
    if (sampler_count == 2u) {
        for (component = 0u; component < 4u; ++component) {
            init_instruction(&ins[instruction_cursor + component],
                             RINGL_RSH1_OP_ADD_F32);
            ins[instruction_cursor + component].destination =
                (uint16_t)(next_register + component);
            ins[instruction_cursor + component].source0 =
                (uint16_t)(sample_base + component);
            ins[instruction_cursor + component].source1 =
                (uint16_t)(sample_base + 4u + component);
        }
        final_base = next_register;
        next_register += 4u;
        instruction_cursor += 4u;
    }
    color_input_base = sampler_count * 2u;
    for (component = 0u; component < 4u; ++component) {
        init_instruction(&ins[instruction_cursor + component], RINGL_RSH1_OP_MUL_F32);
        ins[instruction_cursor + component].destination =
            (uint16_t)(next_register + component);
        ins[instruction_cursor + component].source0 =
            (uint16_t)(final_base + component);
        ins[instruction_cursor + component].source1 =
            (uint16_t)(color_input_base + color_components[component]);
    }
    final_base = next_register;
    next_register += 4u;
    instruction_cursor += 4u;
    if (has_tint) {
        for (component = 0u; component < 4u; ++component) {
            uint32_t tint_bits;

            memcpy(&tint_bits, &tint[tint_components[component]],
                   sizeof(tint_bits));
            init_instruction(&ins[instruction_cursor + component],
                             RINGL_RSH1_OP_CONST_F32);
            ins[instruction_cursor + component].destination =
                (uint16_t)(next_register + component);
            ins[instruction_cursor + component].immediate = tint_bits;
        }
        instruction_cursor += 4u;
        for (component = 0u; component < 4u; ++component) {
            init_instruction(&ins[instruction_cursor + component],
                             RINGL_RSH1_OP_MUL_F32);
            ins[instruction_cursor + component].destination =
                (uint16_t)(next_register + 4u + component);
            ins[instruction_cursor + component].source0 =
                (uint16_t)(final_base + component);
            ins[instruction_cursor + component].source1 =
                (uint16_t)(next_register + component);
        }
        final_base = next_register + 4u;
        next_register += 8u;
        instruction_cursor += 4u;
    }
    if (has_opacity) {
        uint32_t opacity_bits;
        uint32_t opacity_register = next_register++;

        memcpy(&opacity_bits, &opacity, sizeof(opacity_bits));
        init_instruction(&ins[instruction_cursor], RINGL_RSH1_OP_CONST_F32);
        ins[instruction_cursor].destination = (uint16_t)opacity_register;
        ins[instruction_cursor++].immediate = opacity_bits;
        for (component = 0u; component < 4u; ++component) {
            init_instruction(&ins[instruction_cursor + component],
                             RINGL_RSH1_OP_MUL_F32);
            ins[instruction_cursor + component].destination =
                (uint16_t)(next_register + component);
            ins[instruction_cursor + component].source0 =
                (uint16_t)(final_base + component);
            ins[instruction_cursor + component].source1 =
                (uint16_t)opacity_register;
        }
        final_base = next_register;
        next_register += 4u;
        instruction_cursor += 4u;
    }
    store_base = instruction_cursor;
    for (component = 0u; component < 4u; ++component) {
        init_instruction(&ins[store_base + component],
                         RINGL_RSH1_OP_STORE_OUTPUT_F32);
        ins[store_base + component].source0 = (uint16_t)(final_base + component);
        ins[store_base + component].immediate = component;
    }
    init_instruction(&ins[store_base + 4u], RINGL_RSH1_OP_RETURN);

    memset(&header, 0, sizeof(header));
    header.magic = RINGL_RSH1_MAGIC;
    header.version = RINGL_RSH1_VERSION;
    header.header_size = sizeof(header);
    header.stage = RINGL_RSH1_STAGE_FRAGMENT;
    header.instruction_count = store_base + 5u;
    header.register_count = next_register;
    header.input_count = input_count;
    header.output_count = 4u;
    header.resource_count = sampler_count * 2u;
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
    result->sampler_binding_count = sampler_count;
    for (sampler_index = 0u; sampler_index < sampler_count; ++sampler_index)
        result->sampler_binding_indices[sampler_index] = sampler_index;
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
static int lower_fragment_texture_chain(
    const char* source, const RinGLGlslUniformValue* uniforms,
    uint32_t uniform_count, RinGLGlslLowerResult* result)
{
    RinGLRsh1HeaderV1 header;
    RinGLRsh1InstructionV1 ins[RINGL_RSH1_MAX_INSTRUCTIONS];
    char sampler_names[RINGL_VARYING_TEXTURE_MAX_SAMPLERS][64];
    char color_uniform_name[64] = {0};
    char varying_names[4][64] = {{0}};
    char coordinate_names[5][64];
    char local_coordinate_names[RINGL_VARYING_TEXTURE_MAX_LOCAL_COORDINATES][64];
    uint32_t coordinate_input_locations[5] = {
        0u, 2u, 4u, 6u, UINT32_MAX
    };
    uint32_t local_coordinate_kinds[RINGL_VARYING_TEXTURE_MAX_LOCAL_COORDINATES] = {0u};
    uint32_t local_secondary_input_locations[
        RINGL_VARYING_TEXTURE_MAX_LOCAL_COORDINATES] = {0u};
    float local_offset_u[RINGL_VARYING_TEXTURE_MAX_LOCAL_COORDINATES] = {0.0f};
    float local_offset_v[RINGL_VARYING_TEXTURE_MAX_LOCAL_COORDINATES] = {0.0f};
    VaryingTextureCall calls[RINGL_VARYING_TEXTURE_MAX_CALLS];
    uint32_t sampler_resource_indices[RINGL_VARYING_TEXTURE_MAX_SAMPLERS] = {0u};
    uint32_t sampler_binding_indices[RINGL_VARYING_TEXTURE_MAX_SAMPLERS] = {0u};
    const char* cursor;
    uint32_t sampler_count = 0u;
    uint32_t has_color_uniform = 0u;
    uint32_t varying_count = 0u;
    uint32_t coordinate_name_count;
    uint32_t local_coordinate_count = 0u;
    uint32_t two_coordinate_local = 0u;
    uint32_t local_primary_input_location = 0u;
    uint32_t local_secondary_input_location = 2u;
    uint32_t local_tertiary_coordinate_kind = RINGL_VARYING_TEXTURE_COORD_DIRECT;
    uint32_t local_tertiary_input_location = UINT32_MAX;
    uint32_t allowed_third_input_location = UINT32_MAX;
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

    if (source == NULL || result == NULL ||
        (uniform_count != 0u && uniforms == NULL))
        return 1;
    for (call_index = 0u;
         call_index < RINGL_VARYING_TEXTURE_MAX_LOCAL_COORDINATES;
         ++call_index) {
        local_secondary_input_locations[call_index] = UINT32_MAX;
    }
    cursor = source;
    while (strncmp(cursor, "uniformsampler2D", strlen("uniformsampler2D")) ==
               0 ||
           strncmp(cursor, "uniformvec4", strlen("uniformvec4")) == 0) {
        if (strncmp(cursor, "uniformsampler2D",
                    strlen("uniformsampler2D")) == 0) {
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
        } else {
            if (has_color_uniform || !consume_text(&cursor, "uniformvec4") ||
                !read_identifier(&cursor, color_uniform_name,
                                 sizeof(color_uniform_name)) ||
                !consume_text(&cursor, ";")) {
                return 1;
            }
            has_color_uniform = 1u;
        }
    }
    while (strncmp(cursor, "varyingvec2", strlen("varyingvec2")) == 0) {
        uint32_t index;

        if (varying_count == 4u || !consume_text(&cursor, "varyingvec2") ||
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
    if (has_color_uniform) {
        for (call_index = 0u; call_index < uniform_count; ++call_index) {
            if (uniforms[call_index].name == NULL ||
                strcmp(uniforms[call_index].name, color_uniform_name) != 0) {
                continue;
            }
            if (uniforms[call_index].type != RINGL_FLOAT_VEC4)
                return 1;
            memcpy(color, uniforms[call_index].values, sizeof(color));
            break;
        }
    }
    for (call_index = 0u; call_index < varying_count; ++call_index) {
        /* read_identifier() accepted this fixed-width name. Copy its complete
         * bounded representation rather than routing a proven 63-byte string
         * through snprintf(), whose generic truncation diagnostic obscures the
         * parser invariant under -Werror. */
        memcpy(coordinate_names[call_index], varying_names[call_index],
               sizeof(coordinate_names[call_index]));
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
    } else if ((varying_count == 2u || varying_count == 3u ||
                varying_count == 4u) &&
               strncmp(cursor, "vec2", strlen("vec2")) == 0) {
        const char* local_source;

        if (!parse_varying_texture_two_coordinate_local(
                &cursor, varying_names, varying_count,
                local_coordinate_names[0],
                sizeof(local_coordinate_names[0]),
                &local_coordinate_kinds[0], &local_primary_input_location,
                &local_secondary_input_location, &local_tertiary_coordinate_kind,
                &local_tertiary_input_location)) {
            return 1;
        }
        local_source = local_coordinate_names[0];
        coordinate_name_count = varying_count == 2u ? 1u : varying_count + 1u;
        local_coordinate_count = 1u;
        two_coordinate_local = 1u;
        if (local_tertiary_input_location != UINT32_MAX) {
            (void)snprintf(local_coordinate_names[1],
                           sizeof(local_coordinate_names[1]), "%s",
                           local_coordinate_names[0]);
            local_coordinate_kinds[1] = local_tertiary_coordinate_kind;
            local_secondary_input_locations[1] = local_tertiary_input_location;
            local_coordinate_count = 2u;
            local_source = local_coordinate_names[1];
        }
        if (varying_count == 3u) {
            for (call_index = 0u; call_index < varying_count; ++call_index) {
                uint32_t input_location = call_index * 2u;

                if (input_location != local_primary_input_location &&
                    input_location != local_secondary_input_location) {
                    allowed_third_input_location = input_location;
                    break;
                }
            }
            if (local_tertiary_input_location != UINT32_MAX)
                allowed_third_input_location = UINT32_MAX;
        }
        if (varying_count >= 3u) {
            (void)snprintf(coordinate_names[varying_count],
                           sizeof(coordinate_names[varying_count]),
                           "%s", local_coordinate_names[0]);
            /* This appended slot is a named local, not another physical
             * pair. Keeping it separate prevents a local from aliasing the
             * fourth pair in the four-UV profile. */
            coordinate_input_locations[varying_count] = UINT32_MAX;
        }
        while (strncmp(cursor, "vec2", strlen("vec2")) == 0) {
            if (local_coordinate_count ==
                    RINGL_VARYING_TEXTURE_MAX_LOCAL_COORDINATES ||
                !(varying_count == 3u
                      ? parse_varying_texture_local_coordinate_or_varying(
                            &cursor, local_source, varying_names, varying_count,
                            allowed_third_input_location,
                            local_coordinate_names[local_coordinate_count],
                            sizeof(local_coordinate_names[local_coordinate_count]),
                            &local_coordinate_kinds[local_coordinate_count],
                            &local_offset_u[local_coordinate_count],
                            &local_offset_v[local_coordinate_count],
                            &local_secondary_input_locations[
                                local_coordinate_count])
                      : parse_varying_texture_local_coordinate(
                            &cursor, local_source,
                            local_coordinate_names[local_coordinate_count],
                            sizeof(local_coordinate_names[local_coordinate_count]),
                            &local_coordinate_kinds[local_coordinate_count],
                            &local_offset_u[local_coordinate_count],
                            &local_offset_v[local_coordinate_count]))) {
                return 1;
            }
            if (local_secondary_input_locations[local_coordinate_count] !=
                UINT32_MAX) {
                allowed_third_input_location = UINT32_MAX;
            }
            local_source = local_coordinate_names[local_coordinate_count++];
        }
        if (varying_count >= 3u) {
            (void)snprintf(coordinate_names[varying_count],
                           sizeof(coordinate_names[varying_count]),
                           "%s", local_coordinate_names[
                               local_coordinate_count - 1u]);
            coordinate_input_locations[varying_count] = UINT32_MAX;
        } else {
            (void)snprintf(coordinate_names[0], sizeof(coordinate_names[0]),
                           "%s", local_coordinate_names[
                               local_coordinate_count - 1u]);
        }
    }
    if (!consume_text(&cursor, "gl_FragColor="))
        return 1;
    if (strncmp(cursor, "vec4(", strlen("vec4(")) == 0) {
        if (!parse_varying_texture_color_literal(&cursor, color) ||
            !parse_varying_texture_color_operator(&cursor, &color_operation)) {
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
                    &cursor, &color_operation, color,
                    has_color_uniform ? color_uniform_name : NULL)) {
                return 1;
            }
            color_operation_enabled = 1u;
        } else if (*cursor == '*' || *cursor == '+' || *cursor == '-' ||
                   *cursor == '/') {
            if (call_count != 1u ||
                !parse_varying_texture_color_operation(
                    &cursor, &color_operation, color,
                    has_color_uniform ? color_uniform_name : NULL)) {
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

    sample_base = varying_count >= 3u ? varying_count * 2u : 4u;
    instruction_cursor = sample_base;
    for (call_index = 0u; call_index < local_coordinate_count; ++call_index) {
        if (local_coordinate_kinds[call_index] !=
            RINGL_VARYING_TEXTURE_COORD_DIRECT) {
            local_temporary_register_count += 4u;
            instruction_cursor += (two_coordinate_local && call_index == 0u) ||
                    local_secondary_input_locations[call_index] != UINT32_MAX
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
    coordinate_temp_base = padding_base + sample_base - 2u;
    /* Inputs beyond the first two coordinate components occupy the padding
     * register tail. Reserve the extra pairs before local temporaries. */
    if (varying_count > 2u)
        temporary_register_count += (varying_count - 2u) * 2u;
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
    for (component = 0u; component < sample_base; ++component) {
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
                ? 0u : padding_base + local_primary_input_location - 2u;
            uint32_t secondary_u = local_secondary_input_location == 0u
                ? 0u : padding_base + local_secondary_input_location - 2u;

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
                if (local_secondary_input_locations[call_index] != UINT32_MAX) {
                    uint32_t secondary_u =
                        local_secondary_input_locations[call_index] == 0u
                        ? 0u : padding_base +
                            local_secondary_input_locations[call_index] - 2u;

                    emit_varying_texture_coordinate_combine(
                        ins, &instruction_cursor, local_temporary_base,
                        local_coordinate_u, local_coordinate_v, secondary_u,
                        secondary_u + 1u, local_coordinate_kinds[call_index]);
                } else {
                    emit_varying_texture_offset(
                        ins, &instruction_cursor, local_temporary_base,
                        local_coordinate_u, local_coordinate_v,
                        local_coordinate_kinds[call_index],
                        local_offset_u[call_index], local_offset_v[call_index]);
                }
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

        if (calls[call_index].coordinate_input_location == UINT32_MAX ||
            (varying_count <= 2u && local_coordinate_count != 0u)) {
            coordinate_u = local_coordinate_u;
            coordinate_v = local_coordinate_v;
        } else {
            coordinate_u = calls[call_index].coordinate_input_location == 0u
                ? 0u : padding_base +
                    calls[call_index].coordinate_input_location - 2u;
            coordinate_v = coordinate_u + 1u;
        }

        if (calls[call_index].coordinate_kind !=
            RINGL_VARYING_TEXTURE_COORD_DIRECT) {
            uint32_t call_temp_base = coordinate_temp_base +
                local_temporary_register_count;

            if (calls[call_index].secondary_coordinate_input_location !=
                UINT32_MAX) {
                uint32_t secondary_u =
                    calls[call_index].secondary_coordinate_input_location == 0u
                    ? 0u : padding_base +
                        calls[call_index].secondary_coordinate_input_location - 2u;
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
    header.input_count = sample_base;
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
    char prefix[256];
    const char* cursor;
    uint32_t source_components[4];
    uint32_t component;
    int prefix_length;
    size_t total;

    if ((color_width != 3u && color_width != 4u) ||
        snprintf(varying_tag, sizeof(varying_tag), "varyingvec%u",
                 color_width) < 0 ||
        !read_decl_name(source, varying_tag, 0u, varying,
                        sizeof(varying))) {
        return 1;
    }
    if (color_width == 4u)
        prefix_length = snprintf(prefix, sizeof(prefix),
                                 "varyingvec4%s;voidmain(){gl_FragColor=%s",
                                 varying, varying);
    else
        prefix_length = snprintf(prefix, sizeof(prefix),
                                 "varyingvec3%s;voidmain(){gl_FragColor=vec4(%s",
                                 varying, varying);
    if (prefix_length < 0 || (size_t)prefix_length >= sizeof(prefix) ||
        strncmp(source, prefix, (size_t)prefix_length) != 0) {
        return 1;
    }
    cursor = source + prefix_length;
    if (!parse_optional_read_swizzle(&cursor, color_width, color_width,
                                     source_components)) {
        return 1;
    }
    if ((color_width == 4u && strcmp(cursor, ";}") != 0) ||
        (color_width == 3u && strcmp(cursor, ",1.0);}") != 0)) {
        return 1;
    }
    for (component = 0u; component < 4u; ++component) {
        init_instruction(&ins[component], RINGL_RSH1_OP_LOAD_INPUT_F32);
        ins[component].destination = (uint16_t)component;
        ins[component].immediate = component;
    }
    for (component = 0u; component < 4u; ++component) {
        init_instruction(&ins[4u + component], RINGL_RSH1_OP_STORE_OUTPUT_F32);
        ins[4u + component].source0 = component < color_width
            ? (uint16_t)source_components[component]
            : (uint16_t)component;
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

static int lower_vertex_three_vec2(const char* source,
                                   RinGLGlslLowerResult* result)
{
    RinGLRsh1HeaderV1 header;
    RinGLRsh1InstructionV1 ins[21];
    char position[64];
    char first_attribute[64];
    char second_attribute[64];
    char third_attribute[64];
    char first_varying[64];
    char second_varying[64];
    char third_varying[64];
    char expected[1024];
    uint32_t zero_bits = 0u;
    float one = 1.0f;
    uint32_t one_bits;
    uint32_t input;
    size_t total;

    if (!read_decl_name(source, "attributevec2", 0u, position, sizeof(position)) ||
        !read_decl_name(source, "attributevec2", 1u, first_attribute, sizeof(first_attribute)) ||
        !read_decl_name(source, "attributevec2", 2u, second_attribute, sizeof(second_attribute)) ||
        !read_decl_name(source, "attributevec2", 3u, third_attribute, sizeof(third_attribute)) ||
        !read_decl_name(source, "varyingvec2", 0u, first_varying, sizeof(first_varying)) ||
        !read_decl_name(source, "varyingvec2", 1u, second_varying, sizeof(second_varying)) ||
        !read_decl_name(source, "varyingvec2", 2u, third_varying, sizeof(third_varying))) {
        return 1;
    }
    (void)snprintf(expected, sizeof(expected),
        "attributevec2%s;attributevec2%s;attributevec2%s;attributevec2%s;"
        "varyingvec2%s;varyingvec2%s;varyingvec2%s;"
        "voidmain(){gl_Position=vec4(%s,0.0,1.0);%s=%s;%s=%s;%s=%s;}",
        position, first_attribute, second_attribute, third_attribute,
        first_varying, second_varying, third_varying, position,
        first_varying, first_attribute, second_varying, second_attribute,
        third_varying, third_attribute);
    if (strcmp(source, expected) != 0) return 1;
    for (input = 0u; input < 8u; ++input) {
        init_instruction(&ins[input], RINGL_RSH1_OP_LOAD_INPUT_F32);
        ins[input].destination = (uint16_t)input;
        ins[input].immediate = input;
    }
    memcpy(&one_bits, &one, sizeof(one_bits));
    init_instruction(&ins[8], RINGL_RSH1_OP_CONST_F32);
    ins[8].destination = 8u;
    ins[8].immediate = zero_bits;
    init_instruction(&ins[9], RINGL_RSH1_OP_CONST_F32);
    ins[9].destination = 9u;
    ins[9].immediate = one_bits;
    for (input = 0u; input < 10u; ++input) {
        init_instruction(&ins[10u + input], RINGL_RSH1_OP_STORE_OUTPUT_F32);
        if (input < 2u) ins[10u + input].source0 = (uint16_t)input;
        else if (input == 2u) ins[10u + input].source0 = 8u;
        else if (input == 3u) ins[10u + input].source0 = 9u;
        else ins[10u + input].source0 = (uint16_t)(input - 2u);
        ins[10u + input].immediate = input;
    }
    init_instruction(&ins[20], RINGL_RSH1_OP_RETURN);
    memset(&header, 0, sizeof(header));
    header.magic = RINGL_RSH1_MAGIC;
    header.version = RINGL_RSH1_VERSION;
    header.header_size = sizeof(header);
    header.stage = RINGL_RSH1_STAGE_VERTEX;
    header.instruction_count = 21u;
    header.register_count = 10u;
    header.input_count = 8u;
    header.output_count = 10u;
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

/* Four independent UV pairs are lowered to the native eight-scalar varying
 * route. The position remains xyzw in output slots 0..3, followed by each
 * pair in declaration order; this is the exact RSH1 shape consumed by the
 * RinGPU/Aquamarine private transport. */
static int lower_vertex_four_vec2(const char* source,
                                  RinGLGlslLowerResult* result)
{
    RinGLRsh1HeaderV1 header;
    RinGLRsh1InstructionV1 ins[25];
    char position[64];
    char first_attribute[64];
    char second_attribute[64];
    char third_attribute[64];
    char fourth_attribute[64];
    char first_varying[64];
    char second_varying[64];
    char third_varying[64];
    char fourth_varying[64];
    char expected[1536];
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
        !read_decl_name(source, "attributevec2", 3u, third_attribute,
                        sizeof(third_attribute)) ||
        !read_decl_name(source, "attributevec2", 4u, fourth_attribute,
                        sizeof(fourth_attribute)) ||
        !read_decl_name(source, "varyingvec2", 0u, first_varying,
                        sizeof(first_varying)) ||
        !read_decl_name(source, "varyingvec2", 1u, second_varying,
                        sizeof(second_varying)) ||
        !read_decl_name(source, "varyingvec2", 2u, third_varying,
                        sizeof(third_varying)) ||
        !read_decl_name(source, "varyingvec2", 3u, fourth_varying,
                        sizeof(fourth_varying))) {
        return 1;
    }
    (void)snprintf(expected, sizeof(expected),
        "attributevec2%s;attributevec2%s;attributevec2%s;attributevec2%s;"
        "attributevec2%s;varyingvec2%s;varyingvec2%s;varyingvec2%s;"
        "varyingvec2%s;voidmain(){gl_Position=vec4(%s,0.0,1.0);%s=%s;"
        "%s=%s;%s=%s;%s=%s;}",
        position, first_attribute, second_attribute, third_attribute,
        fourth_attribute, first_varying, second_varying, third_varying,
        fourth_varying, position, first_varying, first_attribute,
        second_varying, second_attribute, third_varying, third_attribute,
        fourth_varying, fourth_attribute);
    if (strcmp(source, expected) != 0)
        return 1;

    for (input = 0u; input < 10u; ++input) {
        init_instruction(&ins[input], RINGL_RSH1_OP_LOAD_INPUT_F32);
        ins[input].destination = (uint16_t)input;
        ins[input].immediate = input;
    }
    memcpy(&one_bits, &one, sizeof(one_bits));
    init_instruction(&ins[10], RINGL_RSH1_OP_CONST_F32);
    ins[10].destination = 10u;
    ins[10].immediate = zero_bits;
    init_instruction(&ins[11], RINGL_RSH1_OP_CONST_F32);
    ins[11].destination = 11u;
    ins[11].immediate = one_bits;
    for (input = 0u; input < 12u; ++input) {
        init_instruction(&ins[12u + input], RINGL_RSH1_OP_STORE_OUTPUT_F32);
        if (input < 2u)
            ins[12u + input].source0 = (uint16_t)input;
        else if (input == 2u)
            ins[12u + input].source0 = 10u;
        else if (input == 3u)
            ins[12u + input].source0 = 11u;
        else
            ins[12u + input].source0 = (uint16_t)(input - 2u);
        ins[12u + input].immediate = input;
    }
    init_instruction(&ins[24], RINGL_RSH1_OP_RETURN);

    memset(&header, 0, sizeof(header));
    header.magic = RINGL_RSH1_MAGIC;
    header.version = RINGL_RSH1_VERSION;
    header.header_size = sizeof(header);
    header.stage = RINGL_RSH1_STAGE_VERTEX;
    header.instruction_count = 25u;
    header.register_count = 12u;
    header.input_count = 10u;
    header.output_count = 12u;
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

int ringl_glsl_lower_varying_rsh1_with_uniforms(
    uint32_t shader_type, const char* source, size_t source_length,
    const RinGLGlslUniformValue* uniforms, uint32_t uniform_count,
    RinGLGlslLowerResult* result)
{
    char* compact;
    int rc;

    if (source == NULL || result == NULL ||
        (uniform_count != 0u && uniforms == NULL))
        return -1;
    memset(result, 0, sizeof(*result));
    compact = compact_source(source, source_length);
    if (compact == NULL) {
        (void)snprintf(result->diagnostic, sizeof(result->diagnostic),
                       "varying lowering could not normalize shader source");
        return 1;
    }
    if (strstr(compact, "varyingvec2") != NULL &&
        shader_type == RINGL_VERTEX_SHADER) {
        rc = lower_vertex_transformed_texture(compact, uniforms, uniform_count,
                                              result);
        if (rc != 0)
            rc = lower_vertex_four_vec2(compact, result);
        if (rc != 0)
            rc = lower_vertex_three_vec2(compact, result);
        if (rc != 0)
            rc = lower_vertex_two_vec2(compact, result);
        if (rc != 0)
            rc = lower_vertex(compact, result);
    } else if (strstr(compact, "varyingvec4") != NULL &&
        shader_type == RINGL_VERTEX_SHADER)
        rc = lower_vertex_color(compact, 4u, result);
    else if (strstr(compact, "varyingvec4") != NULL &&
             shader_type == RINGL_FRAGMENT_SHADER) {
        rc = lower_fragment_color(compact, 4u, result);
        if (rc != 0)
            rc = lower_fragment_textured_vertex_color(compact, uniforms,
                                                       uniform_count, result);
    }
    else if (strstr(compact, "varyingvec3") != NULL &&
             shader_type == RINGL_VERTEX_SHADER)
        rc = lower_vertex_color(compact, 3u, result);
    else if (strstr(compact, "varyingvec3") != NULL &&
             shader_type == RINGL_FRAGMENT_SHADER)
        rc = lower_fragment_color(compact, 3u, result);
    else if (strstr(compact, "varyingvec2") != NULL &&
               shader_type == RINGL_FRAGMENT_SHADER) {
        rc = lower_fragment_two_vec2(compact, result);
        if (rc != 0)
            rc = lower_fragment_texture_chain(compact, uniforms, uniform_count,
                                              result);
    }
    else if (shader_type == RINGL_VERTEX_SHADER)
        rc = lower_vertex(compact, result);
    else if (shader_type == RINGL_FRAGMENT_SHADER)
        rc = lower_fragment_texture_chain(compact, uniforms, uniform_count,
                                          result);
    else
        rc = 1;
    free(compact);
    if (rc != 0 && result->diagnostic[0] == '\0')
        (void)snprintf(result->diagnostic, sizeof(result->diagnostic),
                       "shader is outside the initial varying lowering profile");
    return rc;
}

int ringl_glsl_lower_varying_rsh1(uint32_t shader_type,
                                  const char* source,
                                  size_t source_length,
                                  RinGLGlslLowerResult* result)
{
    return ringl_glsl_lower_varying_rsh1_with_uniforms(
        shader_type, source, source_length, NULL, 0u, result);
}
