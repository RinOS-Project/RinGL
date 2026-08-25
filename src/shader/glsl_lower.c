/* SPDX-License-Identifier: MIT */
#include "glsl_lower.h"
#include "rsh1_abi.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ringl/ringl.h>

typedef enum Tok {
    T_EOF = 0,
    T_IDENT,
    T_NUMBER,
    T_VOID,
    T_FLOAT,
    T_VEC2,
    T_VEC3,
    T_VEC4,
    T_MAT2,
    T_MAT3,
    T_MAT4,
    T_INT,
    T_IVEC2,
    T_IVEC3,
    T_IVEC4,
    T_ATTRIBUTE,
    T_UNIFORM,
    T_VARYING,
    T_PRECISION,
    T_LOWP,
    T_MEDIUMP,
    T_HIGHP,
    T_LPAREN,
    T_RPAREN,
    T_LBRACE,
    T_RBRACE,
    T_SEMI,
    T_COMMA,
    T_ASSIGN,
    T_PLUS,
    T_MINUS,
    T_STAR,
    T_SLASH,
    T_DOT,
    T_HASH,
    T_COLON,
    T_LBRACKET,
    T_RBRACKET,
    T_BAD
} Tok;

typedef struct Token {
    Tok kind;
    const char* begin;
    size_t length;
} Token;

typedef struct Value {
    uint16_t regs[16];
    uint8_t width;
    uint8_t matrix;
    uint8_t is_i32;
} Value;

typedef struct Symbol {
    char name[64];
    uint16_t regs[16];
    uint16_t input;
    uint8_t width;
    uint8_t attribute;
    uint8_t uniform;
    uint8_t initialized;
    uint8_t matrix;
    uint8_t is_i32;
} Symbol;

typedef struct Lower {
    const char* source;
    size_t length;
    size_t offset;
    Token token;
    uint32_t shader_type;
    Symbol symbols[64];
    uint32_t symbol_count;
    uint16_t next_reg;
    uint16_t next_input;
    uint16_t output_count;
    RinGLRsh1InstructionV1 ins[RINGL_RSH1_MAX_INSTRUCTIONS];
    uint32_t ins_count;
    const RinGLGlslUniformValue* uniforms;
    uint32_t uniform_count;
    uint32_t standard_derivatives_enabled;
    uint32_t frag_depth_enabled;
    uint32_t uses_frag_depth;
    uint32_t draw_buffers_enabled;
    uint32_t uses_draw_buffers;
    RinGLGlslLowerResult* result;
} Lower;

static uint16_t new_reg(Lower* lower);
static int emit(Lower* lower, uint16_t opcode, uint16_t dst,
                uint16_t source0, uint16_t source1, uint32_t immediate);

static Value invalid_value(void)
{
    Value value;
    memset(&value, 0, sizeof(value));
    return value;
}

static void fail(Lower* lower, const char* message)
{
    if (lower->result->diagnostic[0] == '\0') {
        (void)snprintf(lower->result->diagnostic,
                       sizeof(lower->result->diagnostic), "%s", message);
    }
}

static int text_is(const Token* token, const char* text)
{
    size_t length = strlen(text);
    return token->kind == T_IDENT && token->length == length &&
           memcmp(token->begin, text, length) == 0;
}

static void skip(Lower* lower)
{
    for (;;) {
        while (lower->offset < lower->length &&
               isspace((unsigned char)lower->source[lower->offset])) {
            lower->offset++;
        }
        if (lower->offset + 1u < lower->length &&
            lower->source[lower->offset] == '/' &&
            lower->source[lower->offset + 1u] == '/') {
            lower->offset += 2u;
            while (lower->offset < lower->length &&
                   lower->source[lower->offset] != '\n') {
                lower->offset++;
            }
            continue;
        }
        break;
    }
}

static Tok keyword(const char* begin, size_t length)
{
    if (length == 4u && memcmp(begin, "void", 4u) == 0)
        return T_VOID;
    if (length == 5u && memcmp(begin, "float", 5u) == 0)
        return T_FLOAT;
    if (length == 4u && memcmp(begin, "vec2", 4u) == 0)
        return T_VEC2;
    if (length == 4u && memcmp(begin, "vec3", 4u) == 0)
        return T_VEC3;
    if (length == 4u && memcmp(begin, "vec4", 4u) == 0)
        return T_VEC4;
    if (length == 4u && memcmp(begin, "mat2", 4u) == 0)
        return T_MAT2;
    if (length == 4u && memcmp(begin, "mat3", 4u) == 0)
        return T_MAT3;
    if (length == 4u && memcmp(begin, "mat4", 4u) == 0)
        return T_MAT4;
    if (length == 3u && memcmp(begin, "int", 3u) == 0)
        return T_INT;
    if (length == 5u && memcmp(begin, "ivec2", 5u) == 0)
        return T_IVEC2;
    if (length == 5u && memcmp(begin, "ivec3", 5u) == 0)
        return T_IVEC3;
    if (length == 5u && memcmp(begin, "ivec4", 5u) == 0)
        return T_IVEC4;
    if (length == 9u && memcmp(begin, "attribute", 9u) == 0)
        return T_ATTRIBUTE;
    if (length == 7u && memcmp(begin, "uniform", 7u) == 0)
        return T_UNIFORM;
    if (length == 7u && memcmp(begin, "varying", 7u) == 0)
        return T_VARYING;
    if (length == 9u && memcmp(begin, "precision", 9u) == 0)
        return T_PRECISION;
    if (length == 4u && memcmp(begin, "lowp", 4u) == 0)
        return T_LOWP;
    if (length == 7u && memcmp(begin, "mediump", 7u) == 0)
        return T_MEDIUMP;
    if (length == 5u && memcmp(begin, "highp", 5u) == 0)
        return T_HIGHP;
    return T_IDENT;
}

static void next(Lower* lower)
{
    Token token = {0};
    char c;

    skip(lower);
    if (lower->offset >= lower->length) {
        lower->token.kind = T_EOF;
        return;
    }
    token.begin = lower->source + lower->offset;
    c = lower->source[lower->offset++];
    if (isalpha((unsigned char)c) || c == '_') {
        size_t start = lower->offset - 1u;
        while (lower->offset < lower->length) {
            c = lower->source[lower->offset];
            if (!isalnum((unsigned char)c) && c != '_')
                break;
            lower->offset++;
        }
        token.begin = lower->source + start;
        token.length = lower->offset - start;
        token.kind = keyword(token.begin, token.length);
        lower->token = token;
        return;
    }
    if (isdigit((unsigned char)c) ||
        (c == '.' && lower->offset < lower->length &&
         isdigit((unsigned char)lower->source[lower->offset]))) {
        size_t start = lower->offset - 1u;
        int dot_seen = c == '.';
        while (lower->offset < lower->length) {
            c = lower->source[lower->offset];
            if (isdigit((unsigned char)c)) {
                lower->offset++;
                continue;
            }
            if (c == '.' && !dot_seen) {
                dot_seen = 1;
                lower->offset++;
                continue;
            }
            break;
        }
        token.begin = lower->source + start;
        token.length = lower->offset - start;
        token.kind = T_NUMBER;
        lower->token = token;
        return;
    }
    token.length = 1u;
    switch (c) {
    case '(': token.kind = T_LPAREN; break;
    case ')': token.kind = T_RPAREN; break;
    case '{': token.kind = T_LBRACE; break;
    case '}': token.kind = T_RBRACE; break;
    case ';': token.kind = T_SEMI; break;
    case ',': token.kind = T_COMMA; break;
    case '=': token.kind = T_ASSIGN; break;
    case '+': token.kind = T_PLUS; break;
    case '-': token.kind = T_MINUS; break;
    case '*': token.kind = T_STAR; break;
    case '/': token.kind = T_SLASH; break;
    case '.': token.kind = T_DOT; break;
    case '#': token.kind = T_HASH; break;
    case ':': token.kind = T_COLON; break;
    case '[': token.kind = T_LBRACKET; break;
    case ']': token.kind = T_RBRACKET; break;
    default: token.kind = T_BAD; break;
    }
    lower->token = token;
}

static int take(Lower* lower, Tok kind)
{
    if (lower->token.kind != kind)
        return 0;
    next(lower);
    return 1;
}

static int need(Lower* lower, Tok kind, const char* message)
{
    if (!take(lower, kind)) {
        fail(lower, message);
        return 0;
    }
    return 1;
}

static Symbol* find_symbol(Lower* lower, const Token* token)
{
    uint32_t index;
    for (index = 0u; index < lower->symbol_count; ++index) {
        size_t length = strlen(lower->symbols[index].name);
        if (length == token->length &&
            memcmp(lower->symbols[index].name, token->begin, length) == 0) {
            return &lower->symbols[index];
        }
    }
    return NULL;
}

static Symbol* add_symbol(Lower* lower, const Token* token,
                          int attribute, uint8_t width,
                          uint8_t matrix_dimension)
{
    Symbol* symbol;
    uint32_t index;

    if (lower->symbol_count >= 64u || token->length == 0u ||
        token->length >= 64u || width == 0u || width > 4u ||
        matrix_dimension > 4u || matrix_dimension == 1u ||
        (matrix_dimension != 0u && width != matrix_dimension)) {
        fail(lower, "symbol limit exceeded");
        return NULL;
    }
    if (attribute && (uint32_t)lower->next_input + width > UINT16_MAX) {
        fail(lower, "input slot limit exceeded");
        return NULL;
    }
    symbol = &lower->symbols[lower->symbol_count++];
    memset(symbol, 0, sizeof(*symbol));
    memcpy(symbol->name, token->begin, token->length);
    symbol->name[token->length] = '\0';
    symbol->attribute = (uint8_t)attribute;
    symbol->width = width;
    symbol->matrix = matrix_dimension;
    symbol->input = attribute ? lower->next_input : RINGL_RSH1_UNUSED;
    if (attribute)
        lower->next_input = (uint16_t)(lower->next_input + width);
    for (index = 0u; index < (symbol->matrix
                                  ? (uint32_t)symbol->matrix * symbol->matrix
                                  : 4u); ++index)
        symbol->regs[index] = RINGL_RSH1_UNUSED;
    symbol->initialized = (uint8_t)attribute;
    return symbol;
}

static const RinGLGlslUniformValue* find_uniform_value(
    const Lower* lower, const Token* name)
{
    uint32_t index;

    if (lower == NULL || name == NULL)
        return NULL;
    for (index = 0u; index < lower->uniform_count; ++index) {
        const RinGLGlslUniformValue* uniform = &lower->uniforms[index];
        size_t length;

        if (uniform->name == NULL)
            continue;
        length = strlen(uniform->name);
        if (length == name->length &&
            memcmp(uniform->name, name->begin, length) == 0) {
            return uniform;
        }
    }
    return NULL;
}

static int initialize_uniform(Lower* lower, Symbol* symbol,
                              const Token* name, uint32_t type)
{
    const RinGLGlslUniformValue* uniform;
    uint32_t zero_values[16] = { 0u };
    const uint32_t* values = zero_values;
    int is_i32 = type == RINGL_INT || type == RINGL_INT_VEC2 ||
                 type == RINGL_INT_VEC3 || type == RINGL_INT_VEC4;
    uint32_t index;

    if (lower == NULL || symbol == NULL || name == NULL)
        return 0;
    uniform = find_uniform_value(lower, name);
    if (uniform != NULL && uniform->type != type) {
        fail(lower, "uniform reflection type mismatch");
        return 0;
    }
    if (uniform != NULL)
        values = is_i32 ? (const uint32_t*)uniform->i32_values
                         : (const uint32_t*)uniform->values;
    symbol->uniform = 1u;
    symbol->is_i32 = (uint8_t)is_i32;
    for (index = 0u; index < (symbol->matrix
                                  ? (uint32_t)symbol->matrix * symbol->matrix
                                  : symbol->width); ++index) {
        uint16_t reg = new_reg(lower);
        uint32_t bits = values[index];

        if (reg == RINGL_RSH1_UNUSED ||
            !emit(lower, is_i32 ? RINGL_RSH1_OP_CONST_I32 : RINGL_RSH1_OP_CONST_F32, reg,
                  RINGL_RSH1_UNUSED, RINGL_RSH1_UNUSED, bits)) {
            return 0;
        }
        symbol->regs[index] = reg;
    }
    symbol->initialized = 1u;
    return 1;
}

static uint16_t new_reg(Lower* lower)
{
    if (lower->next_reg >= RINGL_RSH1_MAX_REGISTERS) {
        fail(lower, "RSH1 register limit exceeded");
        return RINGL_RSH1_UNUSED;
    }
    return lower->next_reg++;
}

static int emit(Lower* lower, uint16_t opcode, uint16_t dst,
                uint16_t source0, uint16_t source1, uint32_t immediate)
{
    RinGLRsh1InstructionV1* instruction;
    if (lower->ins_count >= RINGL_RSH1_MAX_INSTRUCTIONS) {
        fail(lower, "RSH1 instruction limit exceeded");
        return 0;
    }
    instruction = &lower->ins[lower->ins_count++];
    memset(instruction, 0, sizeof(*instruction));
    instruction->opcode = opcode;
    instruction->destination = dst;
    instruction->source0 = source0;
    instruction->source1 = source1;
    instruction->resource = RINGL_RSH1_UNUSED;
    instruction->immediate = immediate;
    return 1;
}

static Value expression(Lower* lower);

static Value number_value(Lower* lower)
{
    Value value = invalid_value();
    char temp[64];
    char* end = NULL;
    uint32_t bits;
    uint16_t reg;
    int integer = 1;
    size_t index;

    if (lower->token.length >= sizeof(temp)) {
        fail(lower, "numeric literal too long");
        return value;
    }
    memcpy(temp, lower->token.begin, lower->token.length);
    temp[lower->token.length] = '\0';
    for (index = 0u; index < lower->token.length; ++index) {
        if (temp[index] == '.') {
            integer = 0;
            break;
        }
    }
    if (integer) {
        long number = strtol(temp, &end, 10);
        int32_t i32_number;
        if (end == temp || *end != '\0' || number < INT32_MIN ||
            number > INT32_MAX) {
            fail(lower, "integer literal is out of range");
            return value;
        }
        i32_number = (int32_t)number;
        memcpy(&bits, &i32_number, sizeof(bits));
    } else {
        float number = strtof(temp, &end);
        if (end == temp || *end != '\0') {
            fail(lower, "invalid numeric literal");
            return value;
        }
        memcpy(&bits, &number, sizeof(bits));
    }
    next(lower);
    reg = new_reg(lower);
    if (reg == RINGL_RSH1_UNUSED ||
        !emit(lower, integer ? RINGL_RSH1_OP_CONST_I32 : RINGL_RSH1_OP_CONST_F32,
              reg, RINGL_RSH1_UNUSED,
              RINGL_RSH1_UNUSED, bits)) {
        return value;
    }
    value.regs[0] = reg;
    value.width = 1u;
    value.is_i32 = (uint8_t)integer;
    return value;
}

static Value apply_swizzle(Lower* lower, Value value)
{
    while (take(lower, T_DOT)) {
        Token swizzle = lower->token;
        uint16_t selected[4];
        uint8_t family = 0u;
        uint32_t index;

        /* GLSL permits a read-only swizzle of one through four components,
         * including repeated components (for example xx and bgra). The
         * selector must use exactly one of xyzw, rgba, or stpq: accepting a
         * mixed alphabet would assign a meaning that GLSL deliberately does
         * not give it. Keep this as register selection rather than emitting
         * fake vector instructions; RSH1 remains a scalar IR. */
        if (value.matrix || value.width == 1u || swizzle.kind != T_IDENT ||
            swizzle.length == 0u || swizzle.length > 4u) {
            fail(lower, "invalid vector component selection");
            return invalid_value();
        }
        for (index = 0u; index < swizzle.length; ++index) {
            char component = swizzle.begin[index];
            uint8_t component_family;
            uint32_t component_index;

            switch (component) {
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
            default:
                fail(lower, "invalid vector component selection");
                return invalid_value();
            }
            if ((family != 0u && family != component_family) ||
                component_index >= value.width) {
                fail(lower, "vector component is outside the declared width");
                return invalid_value();
            }
            family = component_family;
            selected[index] = value.regs[component_index];
        }
        for (index = 0u; index < swizzle.length; ++index)
            value.regs[index] = selected[index];
        value.width = (uint8_t)swizzle.length;
        next(lower);
    }
    return value;
}

static Value symbol_value(Lower* lower)
{
    Value value = invalid_value();
    Token name = lower->token;
    Symbol* symbol = find_symbol(lower, &name);
    uint32_t index;

    if (symbol == NULL || !symbol->initialized) {
        fail(lower, "use of unavailable value");
        return value;
    }
    next(lower);
    value.width = symbol->width;
    value.matrix = symbol->matrix;
    value.is_i32 = symbol->is_i32;
    if (symbol->attribute) {
        for (index = 0u; index < symbol->width; ++index) {
            uint16_t reg = new_reg(lower);
            if (reg == RINGL_RSH1_UNUSED ||
                !emit(lower, RINGL_RSH1_OP_LOAD_INPUT_F32, reg,
                      RINGL_RSH1_UNUSED, RINGL_RSH1_UNUSED,
                      (uint32_t)symbol->input + index)) {
                return invalid_value();
            }
            value.regs[index] = reg;
        }
    } else {
        for (index = 0u; index < (symbol->matrix
                                      ? (uint32_t)symbol->matrix * symbol->matrix
                                      : symbol->width); ++index)
            value.regs[index] = symbol->regs[index];
    }
    return apply_swizzle(lower, value);
}

static Value constructor_value(Lower* lower, uint8_t target_width,
                               int target_is_i32)
{
    Value result = invalid_value();
    uint32_t width = 0u;

    next(lower);
    if (!need(lower, T_LPAREN, "expected '(' after vector constructor"))
        return result;
    if (lower->token.kind == T_RPAREN) {
        fail(lower, "vector constructor requires arguments");
        return result;
    }
    for (;;) {
        Value argument = expression(lower);
        uint32_t index;
        if (argument.width == 0u || argument.matrix)
            return result;
        if (argument.is_i32 != (uint8_t)target_is_i32) {
            fail(lower, "vector constructor component type mismatch");
            return result;
        }
        if (width + argument.width > target_width) {
            fail(lower, "too many vector constructor components");
            return result;
        }
        for (index = 0u; index < argument.width; ++index)
            result.regs[width++] = argument.regs[index];
        if (!take(lower, T_COMMA))
            break;
    }
    if (!need(lower, T_RPAREN, "expected ')' after vector constructor"))
        return invalid_value();
    if (width != target_width) {
        fail(lower, "vector constructor component count mismatch");
        return invalid_value();
    }
    result.width = target_width;
    result.is_i32 = (uint8_t)target_is_i32;
    return result;
}

static Value conversion_value(Lower* lower, int target_is_i32)
{
    Value value;
    uint16_t result;

    next(lower);
    if (!need(lower, T_LPAREN, "expected '(' after scalar conversion"))
        return invalid_value();
    value = expression(lower);
    if (value.width == 0u || value.matrix || value.width != 1u) {
        fail(lower, "scalar conversion requires a scalar value");
        return invalid_value();
    }
    if (!need(lower, T_RPAREN, "expected ')' after scalar conversion"))
        return invalid_value();
    if (value.is_i32 == (uint8_t)target_is_i32)
        return value;
    result = new_reg(lower);
    if (result == RINGL_RSH1_UNUSED ||
        !emit(lower, target_is_i32 ? RINGL_RSH1_OP_F32_TO_I32
                                   : RINGL_RSH1_OP_I32_TO_F32,
              result, value.regs[0], RINGL_RSH1_UNUSED, 0u)) {
        return invalid_value();
    }
    value.regs[0] = result;
    value.is_i32 = (uint8_t)target_is_i32;
    return value;
}

static Value derivative_value(Lower* lower, uint16_t opcode)
{
    Value value;
    uint32_t index;

    if (lower->shader_type != RINGL_FRAGMENT_SHADER ||
        lower->standard_derivatives_enabled == 0u) {
        fail(lower, "derivatives require enabled fragment GL_OES_standard_derivatives");
        return invalid_value();
    }
    next(lower);
    if (!need(lower, T_LPAREN, "expected '(' after derivative builtin"))
        return invalid_value();
    value = expression(lower);
    if (value.width == 0u || value.matrix || value.is_i32) {
        fail(lower, "derivative builtin requires scalar or vector float");
        return invalid_value();
    }
    if (!need(lower, T_RPAREN, "expected ')' after derivative builtin"))
        return invalid_value();
    for (index = 0u; index < value.width; ++index) {
        uint16_t destination = new_reg(lower);

        if (destination == RINGL_RSH1_UNUSED ||
            !emit(lower, opcode, destination, value.regs[index],
                  RINGL_RSH1_UNUSED, 0u)) {
            return invalid_value();
        }
        value.regs[index] = destination;
    }
    return value;
}

/* gl_PointCoord belongs to fixed point rasterization, rather than to a
 * user-declared varying. Materialize its two components as RSH1 builtins so
 * the generic RinGPU backend supplies the coordinate for each fragment. */
static Value point_coord_value(Lower* lower)
{
    Value value = invalid_value();
    uint16_t x;
    uint16_t y;

    if (lower->shader_type != RINGL_FRAGMENT_SHADER) {
        fail(lower, "gl_PointCoord is only available in fragment shaders");
        return value;
    }
    next(lower);
    x = new_reg(lower);
    y = new_reg(lower);
    if (x == RINGL_RSH1_UNUSED || y == RINGL_RSH1_UNUSED ||
        !emit(lower, RINGL_RSH1_OP_LOAD_BUILTIN_F32, x,
              RINGL_RSH1_UNUSED, RINGL_RSH1_UNUSED,
              RINGL_RSH1_BUILTIN_POINT_COORD_X) ||
        !emit(lower, RINGL_RSH1_OP_LOAD_BUILTIN_F32, y,
              RINGL_RSH1_UNUSED, RINGL_RSH1_UNUSED,
              RINGL_RSH1_BUILTIN_POINT_COORD_Y)) {
        return invalid_value();
    }
    value.regs[0] = x;
    value.regs[1] = y;
    value.width = 2u;
    return apply_swizzle(lower, value);
}

static Value primary(Lower* lower)
{
    Value value;
    if (lower->token.kind == T_NUMBER)
        return number_value(lower);
    if (lower->token.kind == T_FLOAT)
        return conversion_value(lower, 0);
    if (lower->token.kind == T_INT)
        return conversion_value(lower, 1);
    if (lower->token.kind == T_VEC2)
        return constructor_value(lower, 2u, 0);
    if (lower->token.kind == T_VEC3)
        return constructor_value(lower, 3u, 0);
    if (lower->token.kind == T_VEC4)
        return constructor_value(lower, 4u, 0);
    if (lower->token.kind == T_IVEC2)
        return constructor_value(lower, 2u, 1);
    if (lower->token.kind == T_IVEC3)
        return constructor_value(lower, 3u, 1);
    if (lower->token.kind == T_IVEC4)
        return constructor_value(lower, 4u, 1);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "dFdx"))
        return derivative_value(lower, RINGL_RSH1_OP_DFDX_F32);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "dFdy"))
        return derivative_value(lower, RINGL_RSH1_OP_DFDY_F32);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "fwidth"))
        return derivative_value(lower, RINGL_RSH1_OP_FWIDTH_F32);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "gl_PointCoord"))
        return point_coord_value(lower);
    if (lower->token.kind == T_IDENT)
        return symbol_value(lower);
    if (take(lower, T_LPAREN)) {
        value = expression(lower);
        if (value.width == 0u || !need(lower, T_RPAREN, "expected ')'"))
            return invalid_value();
        return value;
    }
    fail(lower, "expected expression");
    return invalid_value();
}

static Value unary(Lower* lower)
{
    Value value;
    uint32_t index;
    if (take(lower, T_PLUS))
        return unary(lower);
    if (!take(lower, T_MINUS))
        return primary(lower);

    value = unary(lower);
    if (value.width == 0u || value.matrix) {
        fail(lower, "unary arithmetic does not accept a matrix");
        return invalid_value();
    }
    for (index = 0u; index < value.width; ++index) {
        uint16_t zero = new_reg(lower);
        uint16_t result = new_reg(lower);
        if (zero == RINGL_RSH1_UNUSED || result == RINGL_RSH1_UNUSED ||
            !emit(lower, value.is_i32 ? RINGL_RSH1_OP_CONST_I32
                                      : RINGL_RSH1_OP_CONST_F32,
                  zero, RINGL_RSH1_UNUSED,
                  RINGL_RSH1_UNUSED, 0u) ||
            !emit(lower, value.is_i32 ? RINGL_RSH1_OP_SUB_I32
                                      : RINGL_RSH1_OP_SUB_F32,
                  result, zero, value.regs[index],
                  0u)) {
            return invalid_value();
        }
        value.regs[index] = result;
    }
    return value;
}

static Value matrix_times_vector(Lower* lower, const Value* matrix,
                                 const Value* vector)
{
    Value result = invalid_value();
    uint32_t row;
    uint32_t dimension;

    if (matrix == NULL || vector == NULL || !matrix->matrix ||
        vector->matrix || vector->width != matrix->matrix)
        return result;
    if (matrix->is_i32 || vector->is_i32) {
        fail(lower, "matrix multiplication requires floating-point values");
        return result;
    }
    dimension = matrix->matrix;
    for (row = 0u; row < dimension; ++row) {
        uint16_t sum = RINGL_RSH1_UNUSED;
        uint32_t column;

        for (column = 0u; column < dimension; ++column) {
            uint16_t product = new_reg(lower);

            if (product == RINGL_RSH1_UNUSED ||
                !emit(lower, RINGL_RSH1_OP_MUL_F32, product,
                      matrix->regs[column * dimension + row], vector->regs[column],
                      0u)) {
                return invalid_value();
            }
            if (column != 0u) {
                uint16_t combined = new_reg(lower);
                if (combined == RINGL_RSH1_UNUSED ||
                    !emit(lower, RINGL_RSH1_OP_ADD_F32, combined, sum,
                          product, 0u)) {
                    return invalid_value();
                }
                sum = combined;
            } else {
                sum = product;
            }
        }
        result.regs[row] = sum;
    }
    result.width = (uint8_t)dimension;
    return result;
}

/* GLSL scalar/vector arithmetic is represented as scalar RSH1 instructions.
 * Keep the expansion at this frontend boundary: the RSH1 ABI has no hidden
 * vector operation and the generated module must remain independently
 * executable by every RinGPU backend. GLSL ES applies a scalar operand to
 * every component of the other operand for all four arithmetic operators;
 * retain that rule here instead of rejecting valid vecN +/- scalar shaders. */
static Value componentwise_binary(Lower* lower, const Value* left,
                                  const Value* right, uint16_t opcode,
                                  int allow_scalar_broadcast)
{
    Value result = invalid_value();
    uint8_t width;
    uint32_t index;

    if (left == NULL || right == NULL || left->matrix || right->matrix ||
        left->width == 0u || right->width == 0u) {
        fail(lower, "matrix arithmetic is not supported by this expression profile");
        return result;
    }
    if (left->is_i32 != right->is_i32) {
        fail(lower, "arithmetic operands must have matching scalar types");
        return result;
    }
    if (left->is_i32) {
        switch (opcode) {
        case RINGL_RSH1_OP_ADD_F32: opcode = RINGL_RSH1_OP_ADD_I32; break;
        case RINGL_RSH1_OP_SUB_F32: opcode = RINGL_RSH1_OP_SUB_I32; break;
        case RINGL_RSH1_OP_MUL_F32: opcode = RINGL_RSH1_OP_MUL_I32; break;
        case RINGL_RSH1_OP_DIV_F32: opcode = RINGL_RSH1_OP_DIV_I32; break;
        default:
            fail(lower, "unsupported integer arithmetic operation");
            return result;
        }
    }
    if (left->width == right->width) {
        width = left->width;
    } else if (allow_scalar_broadcast && left->width == 1u) {
        width = right->width;
    } else if (allow_scalar_broadcast && right->width == 1u) {
        width = left->width;
    } else {
        fail(lower, "vector arithmetic requires matching component counts");
        return result;
    }

    for (index = 0u; index < width; ++index) {
        uint16_t destination = new_reg(lower);
        uint16_t left_reg = left->regs[left->width == 1u ? 0u : index];
        uint16_t right_reg = right->regs[right->width == 1u ? 0u : index];

        if (destination == RINGL_RSH1_UNUSED ||
            !emit(lower, opcode, destination, left_reg, right_reg, 0u)) {
            return invalid_value();
        }
        result.regs[index] = destination;
    }
    result.width = width;
    result.is_i32 = left->is_i32;
    return result;
}

static Value multiplicative(Lower* lower)
{
    Value left = unary(lower);
    while (left.width != 0u &&
           (lower->token.kind == T_STAR || lower->token.kind == T_SLASH)) {
        Tok operation = lower->token.kind;
        Value right;
        next(lower);
        right = unary(lower);
        if (operation == T_STAR && left.matrix) {
            left = matrix_times_vector(lower, &left, &right);
            if (left.width == 0u)
                fail(lower, "matrix multiplication requires a matching vector right operand");
            continue;
        }
        left = componentwise_binary(
            lower, &left, &right,
            operation == T_STAR ? RINGL_RSH1_OP_MUL_F32 : RINGL_RSH1_OP_DIV_F32,
            1);
    }
    return left;
}

static Value expression(Lower* lower)
{
    Value left = multiplicative(lower);
    while (left.width != 0u &&
           (lower->token.kind == T_PLUS || lower->token.kind == T_MINUS)) {
        Tok operation = lower->token.kind;
        Value right;
        next(lower);
        right = multiplicative(lower);
        left = componentwise_binary(
            lower, &left, &right,
            operation == T_PLUS ? RINGL_RSH1_OP_ADD_F32 : RINGL_RSH1_OP_SUB_F32,
            1);
    }
    return left;
}

static int local_decl(Lower* lower, uint8_t width, int is_i32)
{
    Token name;
    Symbol* symbol;
    next(lower);
    if (lower->token.kind != T_IDENT) {
        fail(lower, "expected local name");
        return 0;
    }
    name = lower->token;
    if (find_symbol(lower, &name) != NULL) {
        fail(lower, "duplicate local");
        return 0;
    }
    symbol = add_symbol(lower, &name, 0, width, 0u);
    if (symbol == NULL)
        return 0;
    symbol->is_i32 = (uint8_t)is_i32;
    next(lower);
    if (take(lower, T_ASSIGN)) {
        Value value = expression(lower);
        if (value.matrix || value.width != width ||
            value.is_i32 != (uint8_t)is_i32) {
            fail(lower, "local initializer component count mismatch");
            return 0;
        }
        memcpy(symbol->regs, value.regs,
               (size_t)width * sizeof(value.regs[0]));
        symbol->initialized = 1u;
    }
    return need(lower, T_SEMI, "expected ';' after local");
}

static int store_output(Lower* lower, const Value* value,
                        uint32_t first_output)
{
    uint32_t index;
    for (index = 0u; index < value->width; ++index) {
        if (!emit(lower, RINGL_RSH1_OP_STORE_OUTPUT_F32,
                  RINGL_RSH1_UNUSED, value->regs[index],
                  RINGL_RSH1_UNUSED, first_output + index)) {
            return 0;
        }
    }
    if (lower->output_count < first_output + value->width)
        lower->output_count = first_output + value->width;
    return 1;
}

static int assignment(Lower* lower)
{
    Token target = lower->token;
    Symbol* symbol = NULL;
    Value value;
    int output = 0;
    uint32_t first_output = 0u;
    int frag_data = 0;
    int frag_depth = 0;
    int point_size = 0;

    if (target.kind != T_IDENT) {
        fail(lower, "expected assignment");
        return 0;
    }
    if (text_is(&target, "gl_Position"))
        output = lower->shader_type == RINGL_VERTEX_SHADER;
    else if (text_is(&target, "gl_PointSize")) {
        if (lower->shader_type != RINGL_VERTEX_SHADER) {
            fail(lower, "gl_PointSize is only writable in vertex shaders");
            return 0;
        }
        output = 1;
        point_size = 1;
        first_output = 4u;
    }
    else if (text_is(&target, "gl_FragColor"))
        output = lower->shader_type == RINGL_FRAGMENT_SHADER;
    else if (text_is(&target, "gl_FragDepthEXT")) {
        if (lower->shader_type != RINGL_FRAGMENT_SHADER ||
            lower->frag_depth_enabled == 0u) {
            fail(lower, "gl_FragDepthEXT requires GL_EXT_frag_depth");
            return 0;
        }
        output = 1;
        frag_depth = 1;
        first_output = lower->draw_buffers_enabled != 0u
            ? RINGL_MAX_COLOR_ATTACHMENTS * 4u : 4u;
    }
    else if (text_is(&target, "gl_FragData")) {
        if (lower->shader_type != RINGL_FRAGMENT_SHADER ||
            lower->draw_buffers_enabled == 0u) {
            fail(lower, "gl_FragData requires enabled GL_EXT_draw_buffers");
            return 0;
        }
        output = 1;
        frag_data = 1;
    }
    else
        symbol = find_symbol(lower, &target);
    if (!output && symbol == NULL) {
        fail(lower, "unknown assignment target");
        return 0;
    }
    next(lower);
    if (frag_data) {
        if (!need(lower, T_LBRACKET, "expected '[' after gl_FragData") ||
            lower->token.kind != T_NUMBER || lower->token.length != 1u ||
            lower->token.begin[0] < '0' ||
            lower->token.begin[0] >=
                (char)('0' + RINGL_MAX_COLOR_ATTACHMENTS)) {
            fail(lower, "gl_FragData index is outside the supported range");
            return 0;
        }
        first_output = (uint32_t)(lower->token.begin[0] - '0') * 4u;
        next(lower);
        if (!need(lower, T_RBRACKET,
                  "expected ']' after gl_FragData index")) {
            return 0;
        }
        lower->uses_draw_buffers = 1u;
    }
    if (!need(lower, T_ASSIGN, "expected '='"))
        return 0;
    value = expression(lower);
    if (value.width == 0u)
        return 0;
    if (!need(lower, T_SEMI, "expected ';' after assignment"))
        return 0;
    if (output) {
        if (point_size && (value.matrix || value.is_i32 || value.width != 1u)) {
            fail(lower, "gl_PointSize output must be float");
            return 0;
        }
        if (frag_depth && (value.matrix || value.is_i32 || value.width != 1u)) {
            fail(lower, "gl_FragDepthEXT output must be float");
            return 0;
        }
        if (!frag_depth && (value.matrix || value.is_i32 ||
                            (value.width != 1u && value.width != 4u))) {
            fail(lower, "shader output must be scalar or vec4");
            return 0;
        }
        if (frag_data && value.width != 4u) {
            fail(lower, "gl_FragData output must be vec4");
            return 0;
        }
        if (frag_depth)
            lower->uses_frag_depth = 1u;
        return store_output(lower, &value, first_output);
    }
    if (symbol->attribute || symbol->uniform) {
        fail(lower, "attribute or uniform is read-only");
        return 0;
    }
    if (symbol->width != value.width || symbol->is_i32 != value.is_i32) {
        fail(lower, "assignment width mismatch");
        return 0;
    }
    memcpy(symbol->regs, value.regs,
           (size_t)value.width * sizeof(value.regs[0]));
    symbol->initialized = 1u;
    return 1;
}

/* The RSH1 execution domain for this bounded GLES profile is binary32. GLSL
 * ES default precision statements are still parsed as source-language
 * declarations, but do not create an unobservable alternate lowering path. */
static int precision_decl(Lower* lower)
{
    next(lower);
    if (lower->token.kind != T_LOWP && lower->token.kind != T_MEDIUMP &&
        lower->token.kind != T_HIGHP) {
        fail(lower, "precision declaration requires lowp, mediump, or highp");
        return 0;
    }
    next(lower);
    if (lower->token.kind != T_FLOAT && lower->token.kind != T_INT) {
        fail(lower, "precision declaration type is not supported");
        return 0;
    }
    next(lower);
    return need(lower, T_SEMI, "expected ';' after precision declaration");
}

static int extension_decl(Lower* lower)
{
    next(lower);
    if (!text_is(&lower->token, "extension")) {
        fail(lower, "only #extension is supported");
        return 0;
    }
    next(lower);
    if (!text_is(&lower->token, "GL_OES_standard_derivatives") &&
        !text_is(&lower->token, "GL_EXT_frag_depth") &&
        !text_is(&lower->token, "GL_EXT_draw_buffers")) {
        fail(lower, "unsupported GLSL extension");
        return 0;
    }
    {
        int standard_derivatives = text_is(
            &lower->token, "GL_OES_standard_derivatives");
        int frag_depth = text_is(&lower->token, "GL_EXT_frag_depth");
    next(lower);
    if (!need(lower, T_COLON, "expected ':' in #extension directive"))
        return 0;
    if (!text_is(&lower->token, "enable") &&
        !text_is(&lower->token, "require")) {
        fail(lower, "extension must be enabled or required");
        return 0;
    }
    if (lower->shader_type != RINGL_FRAGMENT_SHADER) {
        fail(lower, "extension requires a fragment shader");
        return 0;
    }
        if (standard_derivatives)
            lower->standard_derivatives_enabled = 1u;
        else if (frag_depth)
            lower->frag_depth_enabled = 1u;
        else
            lower->draw_buffers_enabled = 1u;
    }
    next(lower);
    return 1;
}

static int parse_all(Lower* lower)
{
    int main_seen = 0;
    next(lower);
    while (lower->token.kind != T_EOF) {
        if (lower->token.kind == T_HASH) {
            if (!extension_decl(lower))
                return 0;
            continue;
        }
        if (lower->token.kind == T_PRECISION) {
            if (!precision_decl(lower))
                return 0;
            continue;
        }
        if (lower->token.kind == T_ATTRIBUTE) {
            Token name;
            uint8_t width;
            if (lower->shader_type != RINGL_VERTEX_SHADER) {
                fail(lower, "fragment attribute");
                return 0;
            }
            next(lower);
            if (lower->token.kind == T_FLOAT)
                width = 1u;
            else if (lower->token.kind == T_VEC2)
                width = 2u;
            else if (lower->token.kind == T_VEC3)
                width = 3u;
            else if (lower->token.kind == T_VEC4)
                width = 4u;
            else {
                fail(lower, "expected float, vec2, vec3, or vec4 attribute type");
                return 0;
            }
            next(lower);
            if (lower->token.kind != T_IDENT) {
                fail(lower, "expected attribute name");
                return 0;
            }
            name = lower->token;
            if (find_symbol(lower, &name) != NULL ||
                add_symbol(lower, &name, 1, width, 0u) == NULL) {
                return 0;
            }
            next(lower);
            if (!need(lower, T_SEMI, "expected ';' after attribute"))
                return 0;
            continue;
        }
        if (lower->token.kind == T_UNIFORM) {
            Token name;
            Symbol* symbol;

            next(lower);
            uint32_t uniform_type;

            if (lower->token.kind == T_FLOAT) {
                uniform_type = RINGL_FLOAT;
            } else if (lower->token.kind == T_INT) {
                uniform_type = RINGL_INT;
            } else if (lower->token.kind == T_VEC2) {
                uniform_type = RINGL_FLOAT_VEC2;
            } else if (lower->token.kind == T_IVEC2) {
                uniform_type = RINGL_INT_VEC2;
            } else if (lower->token.kind == T_VEC3) {
                uniform_type = RINGL_FLOAT_VEC3;
            } else if (lower->token.kind == T_IVEC3) {
                uniform_type = RINGL_INT_VEC3;
            } else if (lower->token.kind == T_VEC4) {
                uniform_type = RINGL_FLOAT_VEC4;
            } else if (lower->token.kind == T_IVEC4) {
                uniform_type = RINGL_INT_VEC4;
            } else if (lower->token.kind == T_MAT2 &&
                       lower->shader_type == RINGL_VERTEX_SHADER) {
                uniform_type = RINGL_FLOAT_MAT2;
            } else if (lower->token.kind == T_MAT3 &&
                       lower->shader_type == RINGL_VERTEX_SHADER) {
                uniform_type = RINGL_FLOAT_MAT3;
            } else if (lower->token.kind == T_MAT4 &&
                       lower->shader_type == RINGL_VERTEX_SHADER) {
                uniform_type = RINGL_FLOAT_MAT4;
            } else {
                fail(lower, "only vertex mat2-4 and scalar/vector float or int uniforms are supported");
                return 0;
            }
            next(lower);
            if (lower->token.kind != T_IDENT) {
                fail(lower, "expected uniform name");
                return 0;
            }
            name = lower->token;
            if (find_symbol(lower, &name) != NULL ||
                (symbol = add_symbol(lower, &name, 0,
                                     uniform_type == RINGL_FLOAT || uniform_type == RINGL_INT ? 1u
                                     : uniform_type == RINGL_FLOAT_VEC2 || uniform_type == RINGL_INT_VEC2 ? 2u
                                     : uniform_type == RINGL_FLOAT_VEC3 || uniform_type == RINGL_INT_VEC3 ? 3u
                                     : uniform_type == RINGL_FLOAT_MAT2 ? 2u
                                     : uniform_type == RINGL_FLOAT_MAT3 ? 3u
                                     : 4u,
                                     uniform_type == RINGL_FLOAT_MAT2 ? 2u
                                     : uniform_type == RINGL_FLOAT_MAT3 ? 3u
                                     : uniform_type == RINGL_FLOAT_MAT4 ? 4u : 0u)) == NULL ||
                !initialize_uniform(lower, symbol, &name, uniform_type)) {
                return 0;
            }
            next(lower);
            if (!need(lower, T_SEMI, "expected ';' after uniform"))
                return 0;
            continue;
        }
        if (lower->token.kind == T_VARYING) {
            Token name;
            Symbol* symbol;
            uint8_t width;

            if (lower->shader_type != RINGL_FRAGMENT_SHADER) {
                fail(lower, "generic varying lowering only supports fragment shaders");
                return 0;
            }
            next(lower);
            if (lower->token.kind == T_VEC2)
                width = 2u;
            else if (lower->token.kind == T_VEC3)
                width = 3u;
            else if (lower->token.kind == T_VEC4)
                width = 4u;
            else {
                fail(lower, "expected varying vec2, vec3, or vec4");
                return 0;
            }
            next(lower);
            if (lower->token.kind != T_IDENT) {
                fail(lower, "expected varying name");
                return 0;
            }
            name = lower->token;
            if (find_symbol(lower, &name) != NULL ||
                (symbol = add_symbol(lower, &name, 1, width, 0u)) == NULL) {
                return 0;
            }
            (void)symbol;
            next(lower);
            if (!need(lower, T_SEMI, "expected ';' after varying"))
                return 0;
            continue;
        }
        if (lower->token.kind == T_VOID) {
            next(lower);
            if (!text_is(&lower->token, "main") || main_seen) {
                fail(lower, "expected unique main");
                return 0;
            }
            main_seen = 1;
            next(lower);
            if (!need(lower, T_LPAREN, "expected '('") ||
                !need(lower, T_RPAREN, "expected ')'") ||
                !need(lower, T_LBRACE, "expected '{'")) {
                return 0;
            }
            while (lower->token.kind != T_RBRACE &&
                   lower->token.kind != T_EOF) {
                if (lower->token.kind == T_FLOAT ||
                    lower->token.kind == T_VEC2 ||
                    lower->token.kind == T_VEC3 ||
                    lower->token.kind == T_VEC4 ||
                    lower->token.kind == T_INT ||
                    lower->token.kind == T_IVEC2 ||
                    lower->token.kind == T_IVEC3 ||
                    lower->token.kind == T_IVEC4) {
                    uint8_t width = lower->token.kind == T_FLOAT ||
                                    lower->token.kind == T_INT ? 1u
                        : lower->token.kind == T_VEC2 || lower->token.kind == T_IVEC2 ? 2u
                        : lower->token.kind == T_VEC3 || lower->token.kind == T_IVEC3 ? 3u : 4u;
                    int is_i32 = lower->token.kind == T_INT ||
                                 lower->token.kind == T_IVEC2 ||
                                 lower->token.kind == T_IVEC3 ||
                                 lower->token.kind == T_IVEC4;
                    if (!local_decl(lower, width, is_i32))
                        return 0;
                } else if (!assignment(lower)) {
                    return 0;
                }
            }
            if (!need(lower, T_RBRACE, "expected '}'"))
                return 0;
            continue;
        }
        fail(lower, "unsupported lowering syntax");
        return 0;
    }
    if (!main_seen) {
        fail(lower, "missing main");
        return 0;
    }
    return 1;
}

/*
 * The RinGPU software rasterizer consumes a fixed vertex ABI: clip position
 * occupies outputs 0..3, with a programmable point size in output 4 and four
 * interpolants in outputs 5..8. GLSL ES shaders without an explicit point
 * size still receive the defined 1-pixel default. Preserve scalar lowering
 * as-is; it is used by the standalone IR API rather than the graphics
 * pipeline.
 */
static int append_raster_defaults(Lower* lower)
{
    uint16_t zero;
    uint16_t one;
    float one_value = 1.0f;
    uint32_t one_bits;

    if (lower->shader_type != RINGL_VERTEX_SHADER ||
        (lower->output_count != 4u && lower->output_count != 5u)) {
        return 1;
    }
    zero = new_reg(lower);
    one = new_reg(lower);
    if (zero == RINGL_RSH1_UNUSED || one == RINGL_RSH1_UNUSED) {
        return 0;
    }
    memcpy(&one_bits, &one_value, sizeof(one_bits));
    if (!emit(lower, RINGL_RSH1_OP_CONST_F32, zero, RINGL_RSH1_UNUSED,
              RINGL_RSH1_UNUSED, 0u) ||
        !emit(lower, RINGL_RSH1_OP_CONST_F32, one, RINGL_RSH1_UNUSED,
              RINGL_RSH1_UNUSED, one_bits) ||
        (lower->output_count == 4u &&
         !emit(lower, RINGL_RSH1_OP_STORE_OUTPUT_F32, RINGL_RSH1_UNUSED,
               one, RINGL_RSH1_UNUSED, 4u)) ||
        !emit(lower, RINGL_RSH1_OP_STORE_OUTPUT_F32, RINGL_RSH1_UNUSED,
              zero, RINGL_RSH1_UNUSED, 5u) ||
        !emit(lower, RINGL_RSH1_OP_STORE_OUTPUT_F32, RINGL_RSH1_UNUSED,
              zero, RINGL_RSH1_UNUSED, 6u) ||
        !emit(lower, RINGL_RSH1_OP_STORE_OUTPUT_F32, RINGL_RSH1_UNUSED,
              zero, RINGL_RSH1_UNUSED, 7u) ||
        !emit(lower, RINGL_RSH1_OP_STORE_OUTPUT_F32, RINGL_RSH1_UNUSED,
              one, RINGL_RSH1_UNUSED, 8u)) {
        return 0;
    }
    lower->output_count = 9u;
    return 1;
}

/* RSH1 validates that every declared output is written. WebGL instead defines
 * unwritten gl_FragData entries as zero, so materialize those stores before
 * publishing the fixed four-target output ABI. */
static int append_draw_buffer_defaults(Lower* lower)
{
    uint8_t written[RINGL_MAX_COLOR_ATTACHMENTS * 4u] = { 0u };
    uint16_t zero;
    uint32_t instruction_index;
    uint32_t output_index;

    if (lower->shader_type != RINGL_FRAGMENT_SHADER ||
        lower->uses_draw_buffers == 0u) {
        return 1;
    }
    for (instruction_index = 0u; instruction_index < lower->ins_count;
         ++instruction_index) {
        const RinGLRsh1InstructionV1* instruction =
            &lower->ins[instruction_index];

        if (instruction->opcode == RINGL_RSH1_OP_STORE_OUTPUT_F32 &&
            instruction->immediate < RINGL_MAX_COLOR_ATTACHMENTS * 4u) {
            written[instruction->immediate] = 1u;
        }
    }
    zero = RINGL_RSH1_UNUSED;
    for (output_index = 0u;
         output_index < RINGL_MAX_COLOR_ATTACHMENTS * 4u; ++output_index) {
        if (written[output_index] != 0u)
            continue;
        if (zero == RINGL_RSH1_UNUSED) {
            zero = new_reg(lower);
            if (zero == RINGL_RSH1_UNUSED ||
                !emit(lower, RINGL_RSH1_OP_CONST_F32, zero,
                      RINGL_RSH1_UNUSED, RINGL_RSH1_UNUSED, 0u)) {
                return 0;
            }
        }
        if (!emit(lower, RINGL_RSH1_OP_STORE_OUTPUT_F32,
                  RINGL_RSH1_UNUSED, zero, RINGL_RSH1_UNUSED,
                  output_index)) {
            return 0;
        }
    }
    lower->output_count = RINGL_MAX_COLOR_ATTACHMENTS * 4u;
    if (lower->uses_frag_depth != 0u)
        lower->output_count++;
    return 1;
}

/* Emit type-bearing loads for the fixed interpolant ABI.  They are unused by
 * constant fragment shaders, but RinGPU validates every declared input when
 * it builds a native graphics pipeline. */
static int append_fragment_interpolant_inputs(Lower* lower)
{
    uint32_t color_output_count;
    uint32_t index;

    color_output_count = lower->output_count;
    /* GL_EXT_frag_depth appends one terminal scalar after the RGBA outputs;
     * it does not alter the vertex-to-fragment interpolant interface. */
    if ((color_output_count % 4u) == 1u)
        color_output_count--;
    if (lower->shader_type != RINGL_FRAGMENT_SHADER ||
        (color_output_count != 4u &&
         color_output_count != RINGL_MAX_COLOR_ATTACHMENTS * 4u) ||
        lower->next_input != 0u) {
        return 1;
    }
    for (index = 0u; index < 4u; ++index) {
        uint16_t reg = new_reg(lower);
        if (reg == RINGL_RSH1_UNUSED ||
            !emit(lower, RINGL_RSH1_OP_LOAD_INPUT_F32, reg,
                  RINGL_RSH1_UNUSED, RINGL_RSH1_UNUSED, index)) {
            return 0;
        }
    }
    lower->next_input = 4u;
    return 1;
}

int ringl_glsl_lower_rsh1_with_uniforms(
    uint32_t shader_type, const char* source, size_t source_length,
    const RinGLGlslUniformValue* uniforms, uint32_t uniform_count,
    RinGLGlslLowerResult* result)
{
    Lower lower;
    RinGLRsh1HeaderV1 header;
    size_t total;

    if (source == NULL || result == NULL ||
        uniform_count > RINGL_GLSL_MAX_UNIFORMS ||
        (uniform_count != 0u && uniforms == NULL))
        return -1;
    memset(result, 0, sizeof(*result));
    memset(&lower, 0, sizeof(lower));
    lower.source = source;
    lower.length = source_length;
    lower.shader_type = shader_type;
    lower.uniforms = uniforms;
    lower.uniform_count = uniform_count;
    lower.result = result;
    if (!parse_all(&lower))
        return 1;
    if (!append_draw_buffer_defaults(&lower))
        return 1;
    if (!append_raster_defaults(&lower))
        return 1;
    if (!append_fragment_interpolant_inputs(&lower))
        return 1;
    if (!emit(&lower, RINGL_RSH1_OP_RETURN, RINGL_RSH1_UNUSED,
              RINGL_RSH1_UNUSED, RINGL_RSH1_UNUSED, 0u)) {
        return 1;
    }
    if (lower.next_reg == 0u)
        lower.next_reg = 1u;

    memset(&header, 0, sizeof(header));
    header.magic = RINGL_RSH1_MAGIC;
    header.version = RINGL_RSH1_VERSION;
    header.header_size = sizeof(header);
    header.stage = shader_type == RINGL_VERTEX_SHADER
        ? RINGL_RSH1_STAGE_VERTEX : RINGL_RSH1_STAGE_FRAGMENT;
    header.instruction_count = lower.ins_count;
    header.register_count = lower.next_reg;
    header.input_count = lower.next_input;
    header.output_count = lower.output_count;
    header.entry_instruction = 0u;
    total = sizeof(header) +
            (size_t)lower.ins_count * sizeof(lower.ins[0]);
    header.total_size = (uint32_t)total;
    memcpy(result->bytes, &header, sizeof(header));
    memcpy(result->bytes + sizeof(header), lower.ins,
           (size_t)lower.ins_count * sizeof(lower.ins[0]));
    result->ok = 1u;
    result->instruction_count = lower.ins_count;
    result->register_count = lower.next_reg;
    result->input_count = lower.next_input;
    result->output_count = lower.output_count;
    result->byte_size = (uint32_t)total;
    return 0;
}

int ringl_glsl_lower_rsh1(uint32_t shader_type, const char* source,
                          size_t source_length,
                          RinGLGlslLowerResult* result)
{
    return ringl_glsl_lower_rsh1_with_uniforms(
        shader_type, source, source_length, NULL, 0u, result);
}
