/* SPDX-License-Identifier: MIT */
#include "glsl_lower.h"
#include "rsh1_abi.h"

#include "../ringl_internal.h"

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
    T_BOOL,
    T_TRUE,
    T_FALSE,
    T_BVEC2,
    T_BVEC3,
    T_BVEC4,
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
    T_IF,
    T_ELSE,
    T_EQ,
    T_NE,
    T_LT,
    T_LE,
    T_GT,
    T_GE,
    T_NOT,
    T_AND,
    T_XOR,
    T_OR,
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
    uint8_t is_bool;
} Value;

typedef struct Symbol {
    char name[64];
    uint16_t regs[16];
    uint16_t input;
    uint16_t output;
    uint8_t width;
    uint8_t attribute;
    uint8_t uniform;
    uint8_t varying;
    uint8_t initialized;
    uint8_t matrix;
    uint8_t is_i32;
    uint8_t is_bool;
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
    uint16_t next_varying_output;
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

/* RSH1 is a finite binary32 execution contract. Keep an overflowing source
 * literal from becoming an Inf/NaN constant that is only rejected after a
 * caller has attempted a draw. */
static int finite_f32(float number)
{
    uint32_t bits;

    memcpy(&bits, &number, sizeof(bits));
    return (bits & UINT32_C(0x7f800000)) != UINT32_C(0x7f800000);
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
    if (length == 4u && memcmp(begin, "bool", 4u) == 0)
        return T_BOOL;
    if (length == 4u && memcmp(begin, "true", 4u) == 0)
        return T_TRUE;
    if (length == 5u && memcmp(begin, "false", 5u) == 0)
        return T_FALSE;
    if (length == 5u && memcmp(begin, "bvec2", 5u) == 0)
        return T_BVEC2;
    if (length == 5u && memcmp(begin, "bvec3", 5u) == 0)
        return T_BVEC3;
    if (length == 5u && memcmp(begin, "bvec4", 5u) == 0)
        return T_BVEC4;
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
    if (length == 2u && memcmp(begin, "if", 2u) == 0)
        return T_IF;
    if (length == 4u && memcmp(begin, "else", 4u) == 0)
        return T_ELSE;
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
        int exponent_seen = 0;
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
            if ((c == 'e' || c == 'E') && !exponent_seen) {
                size_t exponent = lower->offset + 1u;

                exponent_seen = 1;
                if (exponent < lower->length &&
                    (lower->source[exponent] == '+' ||
                     lower->source[exponent] == '-')) {
                    ++exponent;
                }
                if (exponent >= lower->length ||
                    !isdigit((unsigned char)lower->source[exponent])) {
                    token.kind = T_BAD;
                    token.begin = lower->source + start;
                    token.length = exponent - start;
                    lower->offset = exponent;
                    lower->token = token;
                    return;
                }
                lower->offset = exponent + 1u;
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
    case '=':
        if (lower->offset < lower->length &&
            lower->source[lower->offset] == '=') {
            lower->offset++;
            token.length = 2u;
            token.kind = T_EQ;
        } else {
            token.kind = T_ASSIGN;
        }
        break;
    case '!':
        if (lower->offset < lower->length &&
            lower->source[lower->offset] == '=') {
            lower->offset++;
            token.length = 2u;
            token.kind = T_NE;
        } else {
            token.kind = T_NOT;
        }
        break;
    case '&':
        if (lower->offset < lower->length &&
            lower->source[lower->offset] == '&') {
            lower->offset++;
            token.length = 2u;
            token.kind = T_AND;
        } else {
            token.kind = T_BAD;
        }
        break;
    case '^':
        if (lower->offset < lower->length &&
            lower->source[lower->offset] == '^') {
            lower->offset++;
            token.length = 2u;
            token.kind = T_XOR;
        } else {
            token.kind = T_BAD;
        }
        break;
    case '|':
        if (lower->offset < lower->length &&
            lower->source[lower->offset] == '|') {
            lower->offset++;
            token.length = 2u;
            token.kind = T_OR;
        } else {
            token.kind = T_BAD;
        }
        break;
    case '<':
        if (lower->offset < lower->length &&
            lower->source[lower->offset] == '=') {
            lower->offset++;
            token.length = 2u;
            token.kind = T_LE;
        } else {
            token.kind = T_LT;
        }
        break;
    case '>':
        if (lower->offset < lower->length &&
            lower->source[lower->offset] == '=') {
            lower->offset++;
            token.length = 2u;
            token.kind = T_GE;
        } else {
            token.kind = T_GT;
        }
        break;
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
    symbol->output = RINGL_RSH1_UNUSED;
    if (attribute)
        lower->next_input = (uint16_t)(lower->next_input + width);
    for (index = 0u; index < (symbol->matrix
                                  ? (uint32_t)symbol->matrix * symbol->matrix
                                  : 4u); ++index)
        symbol->regs[index] = RINGL_RSH1_UNUSED;
    symbol->initialized = (uint8_t)attribute;
    return symbol;
}

/* A vertex varying is a writable RSH1 output after clip position; a fragment
 * varying is a read-only RSH1 input. Keep this distinction in the generic
 * lowerer instead of routing mixed vec2/vec3/vec4 interfaces through the old
 * profile-specific emitters. */
static Symbol* add_varying_symbol(Lower* lower, const Token* token,
                                  uint8_t width)
{
    Symbol* symbol;

    if (lower == NULL || token == NULL ||
        (width != 2u && width != 3u && width != 4u))
        return NULL;
    symbol = add_symbol(lower, token,
                        lower->shader_type == RINGL_FRAGMENT_SHADER,
                        width, 0u);
    if (symbol == NULL)
        return NULL;
    symbol->varying = 1u;
    if (lower->shader_type == RINGL_VERTEX_SHADER) {
        if (lower->next_varying_output < 4u ||
            lower->next_varying_output > 4u + RINGL_MAX_VARYING_COMPONENTS ||
            width > 4u + RINGL_MAX_VARYING_COMPONENTS -
                        lower->next_varying_output) {
            fail(lower, "varying component limit exceeded");
            return NULL;
        }
        symbol->output = lower->next_varying_output;
        lower->next_varying_output =
            (uint16_t)(lower->next_varying_output + width);
    } else {
        /* RSH1 records a dense fragment interface. Materialize every declared
         * component, including a declaration the source never reads, so the
         * native linker can validate its type instead of seeing a header-only
         * input slot. */
        for (uint32_t index = 0u; index < width; ++index) {
            uint16_t reg = new_reg(lower);
            if (reg == RINGL_RSH1_UNUSED ||
                !emit(lower, RINGL_RSH1_OP_LOAD_INPUT_F32, reg,
                      RINGL_RSH1_UNUSED, RINGL_RSH1_UNUSED,
                      (uint32_t)symbol->input + index)) {
                return NULL;
            }
            symbol->regs[index] = reg;
        }
    }
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
    int is_i32 = type == RINGL_INT || type == RINGL_BOOL ||
                 type == RINGL_BOOL_VEC2 || type == RINGL_BOOL_VEC3 ||
                 type == RINGL_BOOL_VEC4 || type == RINGL_INT_VEC2 ||
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
    symbol->is_bool = type == RINGL_BOOL || type == RINGL_BOOL_VEC2 ||
                      type == RINGL_BOOL_VEC3 || type == RINGL_BOOL_VEC4;
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
static Value componentwise_binary(Lower* lower, const Value* left,
                                   const Value* right, uint16_t opcode,
                                   int allow_scalar_broadcast);
static Value float_constant_value(Lower* lower, float number);

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
        if (temp[index] == '.' || temp[index] == 'e' || temp[index] == 'E') {
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
        if (end == temp || *end != '\0' || !finite_f32(number)) {
            fail(lower, "numeric literal must be finite binary32");
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

static Value bool_constant_value(Lower* lower, int boolean)
{
    Value value = invalid_value();
    uint16_t reg = new_reg(lower);

    next(lower);
    if (reg == RINGL_RSH1_UNUSED ||
        !emit(lower, RINGL_RSH1_OP_CONST_I32, reg, RINGL_RSH1_UNUSED,
              RINGL_RSH1_UNUSED, boolean ? 1u : 0u)) {
        return value;
    }
    value.regs[0] = reg;
    value.width = 1u;
    value.is_i32 = 1u;
    value.is_bool = 1u;
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
    value.is_bool = symbol->is_bool;
    if (symbol->attribute && !symbol->varying) {
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
                               int target_is_i32, int target_is_bool)
{
    Value result = invalid_value();
    uint32_t argument_count = 0u;
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
        if (argument.is_i32 != (uint8_t)target_is_i32 ||
            argument.is_bool != (uint8_t)target_is_bool) {
            fail(lower, "vector constructor component type mismatch");
            return result;
        }
        if (width + argument.width > target_width) {
            fail(lower, "too many vector constructor components");
            return result;
        }
        for (index = 0u; index < argument.width; ++index)
            result.regs[width++] = argument.regs[index];
        argument_count++;
        if (!take(lower, T_COMMA))
            break;
    }
    if (!need(lower, T_RPAREN, "expected ')' after vector constructor"))
        return invalid_value();
    /* GLSL permits one same-basic-type scalar to initialize every vector
     * component. Keep it as a register alias: the scalar RSH1 ABI has no
     * vector instruction, and no browser/Aquamarine-side broadcast is needed.
     * Multiple arguments still require the exact component count below. */
    if (argument_count == 1u && width == 1u) {
        uint32_t index;

        for (index = 1u; index < target_width; ++index)
            result.regs[index] = result.regs[0];
        width = target_width;
    }
    if (width != target_width) {
        fail(lower, "vector constructor component count mismatch");
        return invalid_value();
    }
    result.width = target_width;
    result.is_i32 = (uint8_t)target_is_i32;
    result.is_bool = (uint8_t)target_is_bool;
    return result;
}

/* RSH1 deliberately stores vector values as scalar registers. Keep Boolean
 * vector builtins on that same path: comparison instructions produce the
 * normalized 0/1 values required by the WebGL uniform contract, so no
 * Aquamarine- or backend-specific Boolean operation is needed. */
static Value boolean_invert_value(Lower* lower, const Value* value)
{
    Value result = invalid_value();
    uint16_t zero;
    uint32_t index;

    if (value->width == 0u || value->matrix || !value->is_i32 ||
        !value->is_bool) {
        fail(lower, "not requires a Boolean scalar or vector");
        return result;
    }
    zero = new_reg(lower);
    if (zero == RINGL_RSH1_UNUSED ||
        !emit(lower, RINGL_RSH1_OP_CONST_I32, zero, RINGL_RSH1_UNUSED,
              RINGL_RSH1_UNUSED, 0u)) {
        return result;
    }
    for (index = 0u; index < value->width; ++index) {
        uint16_t destination = new_reg(lower);

        if (destination == RINGL_RSH1_UNUSED ||
            !emit(lower, RINGL_RSH1_OP_CMP_EQ_I32, destination,
                  value->regs[index], zero, 0u)) {
            return invalid_value();
        }
        result.regs[index] = destination;
    }
    result.width = value->width;
    result.is_i32 = 1u;
    result.is_bool = 1u;
    return result;
}

static Value boolean_not_value(Lower* lower)
{
    Value value;

    next(lower);
    if (!need(lower, T_LPAREN, "expected '(' after not"))
        return invalid_value();
    value = expression(lower);
    if (value.width == 0u || !need(lower, T_RPAREN, "expected ')' after not"))
        return invalid_value();
    return boolean_invert_value(lower, &value);
}

static Value boolean_compare_value(Lower* lower, uint16_t opcode,
                                   const char* name)
{
    Value left;
    Value right;
    Value result = invalid_value();
    uint32_t index;

    next(lower);
    if (!need(lower, T_LPAREN, "expected '(' after Boolean comparison builtin"))
        return result;
    left = expression(lower);
    if (left.width == 0u || !need(lower, T_COMMA,
                                  "expected ',' in Boolean comparison builtin")) {
        return result;
    }
    right = expression(lower);
    if (right.width == 0u ||
        !need(lower, T_RPAREN, "expected ')' after Boolean comparison builtin")) {
        return result;
    }
    if (left.matrix || right.matrix || left.width < 2u || left.width > 4u ||
        left.width != right.width || !left.is_i32 || !right.is_i32 ||
        !left.is_bool || !right.is_bool) {
        fail(lower, name);
        return result;
    }
    for (index = 0u; index < left.width; ++index) {
        uint16_t destination = new_reg(lower);

        if (destination == RINGL_RSH1_UNUSED ||
            !emit(lower, opcode, destination, left.regs[index],
                  right.regs[index], 0u)) {
            return invalid_value();
        }
        result.regs[index] = destination;
    }
    result.width = left.width;
    result.is_i32 = 1u;
    result.is_bool = 1u;
    return result;
}

static Value boolean_reduce_value(Lower* lower, int require_all)
{
    Value value;
    Value result = invalid_value();
    uint16_t sum;
    uint16_t expected;
    uint16_t destination;
    uint32_t index;

    next(lower);
    if (!need(lower, T_LPAREN, "expected '(' after Boolean reduction builtin"))
        return result;
    value = expression(lower);
    if (value.width == 0u ||
        !need(lower, T_RPAREN, "expected ')' after Boolean reduction builtin")) {
        return result;
    }
    if (value.matrix || value.width < 2u || value.width > 4u ||
        !value.is_i32 || !value.is_bool) {
        fail(lower, "any and all require a Boolean vector");
        return result;
    }
    sum = value.regs[0];
    for (index = 1u; index < value.width; ++index) {
        uint16_t next_sum = new_reg(lower);

        if (next_sum == RINGL_RSH1_UNUSED ||
            !emit(lower, RINGL_RSH1_OP_ADD_I32, next_sum, sum,
                  value.regs[index], 0u)) {
            return result;
        }
        sum = next_sum;
    }
    expected = new_reg(lower);
    destination = new_reg(lower);
    if (expected == RINGL_RSH1_UNUSED || destination == RINGL_RSH1_UNUSED ||
        !emit(lower, RINGL_RSH1_OP_CONST_I32, expected, RINGL_RSH1_UNUSED,
              RINGL_RSH1_UNUSED, require_all ? value.width : 0u) ||
        !emit(lower, require_all ? RINGL_RSH1_OP_CMP_EQ_I32
                                 : RINGL_RSH1_OP_CMP_NE_I32,
              destination, sum, expected, 0u)) {
        return result;
    }
    result.regs[0] = destination;
    result.width = 1u;
    result.is_i32 = 1u;
    result.is_bool = 1u;
    return result;
}

/* Matrices stay column-major all the way to matrix_times_vector(): element
 * (column, row) is regs[column * dimension + row].  Do the constructor
 * expansion here, rather than leave matrixCompMult to an embedding, so the
 * same RSH1 scalar instructions run on every RinGPU backend. */
static Value matrix_constructor_value(Lower* lower, uint8_t dimension)
{
    Value arguments[16];
    Value result = invalid_value();
    uint32_t argument_count = 0u;
    uint32_t component_count = 0u;
    uint32_t index;

    next(lower);
    if (!need(lower, T_LPAREN, "expected '(' after matrix constructor"))
        return result;
    if (lower->token.kind == T_RPAREN) {
        fail(lower, "matrix constructor requires arguments");
        return result;
    }
    for (;;) {
        Value argument;

        if (argument_count == sizeof(arguments) / sizeof(arguments[0])) {
            fail(lower, "too many matrix constructor arguments");
            return result;
        }
        argument = expression(lower);
        if (argument.width == 0u)
            return result;
        if (argument.is_i32) {
            fail(lower, "matrix constructor requires floating-point values");
            return result;
        }
        if (argument.matrix &&
            (argument_count != 0u || argument.matrix != dimension)) {
            fail(lower, "matrix constructor only accepts a matching matrix copy");
            return result;
        }
        if (!argument.matrix && component_count + argument.width >
                                    (uint32_t)dimension * dimension) {
            fail(lower, "too many matrix constructor components");
            return result;
        }
        arguments[argument_count++] = argument;
        if (!argument.matrix)
            component_count += argument.width;
        if (!take(lower, T_COMMA))
            break;
    }
    if (!need(lower, T_RPAREN, "expected ')' after matrix constructor"))
        return invalid_value();

    if (argument_count == 1u && arguments[0].matrix) {
        result = arguments[0];
        return result;
    }
    if (argument_count == 1u && component_count == 1u) {
        Value zero = float_constant_value(lower, 0.0f);

        if (zero.width == 0u)
            return invalid_value();
        for (index = 0u; index < (uint32_t)dimension * dimension; ++index)
            result.regs[index] = index / dimension == index % dimension
                ? arguments[0].regs[0] : zero.regs[0];
    } else {
        uint32_t destination = 0u;
        uint32_t argument_index;

        if (component_count != (uint32_t)dimension * dimension) {
            fail(lower, "matrix constructor component count mismatch");
            return invalid_value();
        }
        for (argument_index = 0u; argument_index < argument_count;
             ++argument_index) {
            const Value* argument = &arguments[argument_index];
            uint32_t component;

            for (component = 0u; component < argument->width; ++component)
                result.regs[destination++] = argument->regs[component];
        }
    }
    result.width = dimension;
    result.matrix = dimension;
    return result;
}

static Value matrix_component_multiply_value(Lower* lower)
{
    Value left;
    Value right;
    Value result = invalid_value();
    uint32_t index;
    uint32_t component_count;

    next(lower);
    if (!need(lower, T_LPAREN, "expected '(' after matrixCompMult"))
        return result;
    left = expression(lower);
    if (left.width == 0u || !need(lower, T_COMMA,
                                  "expected ',' in matrixCompMult"))
        return result;
    right = expression(lower);
    if (right.width == 0u ||
        !need(lower, T_RPAREN, "expected ')' after matrixCompMult")) {
        return result;
    }
    if (!left.matrix || !right.matrix || left.matrix != right.matrix ||
        left.is_i32 || right.is_i32) {
        fail(lower, "matrixCompMult requires matching floating-point matrices");
        return result;
    }
    component_count = (uint32_t)left.matrix * left.matrix;
    for (index = 0u; index < component_count; ++index) {
        uint16_t destination = new_reg(lower);

        if (destination == RINGL_RSH1_UNUSED ||
            !emit(lower, RINGL_RSH1_OP_MUL_F32, destination,
                  left.regs[index], right.regs[index], 0u)) {
            return invalid_value();
        }
        result.regs[index] = destination;
    }
    result.width = left.width;
    result.matrix = left.matrix;
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
    value.is_bool = 0u;
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

/* Common GLSL floating-point builtins are scalarized here rather than
 * delegated to an embedding. This keeps their ordinary expression semantics
 * in the RSH1 module that RinGPU validates and executes. */
static int float_vector_value(Lower* lower, const Value* value,
                              const char* message)
{
    if (value == NULL || value->width == 0u || value->matrix || value->is_i32) {
        fail(lower, message);
        return 0;
    }
    return 1;
}

static Value float_constant_value(Lower* lower, float number)
{
    Value value = invalid_value();
    uint16_t reg;
    uint32_t bits;

    memcpy(&bits, &number, sizeof(bits));
    reg = new_reg(lower);
    if (reg == RINGL_RSH1_UNUSED ||
        !emit(lower, RINGL_RSH1_OP_CONST_F32, reg,
              RINGL_RSH1_UNUSED, RINGL_RSH1_UNUSED, bits)) {
        return value;
    }
    value.regs[0] = reg;
    value.width = 1u;
    return value;
}

static Value negate_float_value(Lower* lower, const Value* value)
{
    Value zero;

    if (!float_vector_value(lower, value,
                            "math builtin requires floating-point values")) {
        return invalid_value();
    }
    zero = float_constant_value(lower, 0.0f);
    if (zero.width == 0u)
        return invalid_value();
    return componentwise_binary(lower, &zero, value, RINGL_RSH1_OP_SUB_F32,
                                1);
}

static Value floor_float_value(Lower* lower, const Value* value)
{
    Value result = invalid_value();
    uint32_t index;

    if (!float_vector_value(lower, value,
                            "floor builtin requires floating-point values")) {
        return result;
    }
    for (index = 0u; index < value->width; ++index) {
        uint16_t destination = new_reg(lower);

        if (destination == RINGL_RSH1_UNUSED ||
            !emit(lower, RINGL_RSH1_OP_FLOOR_F32, destination,
                  value->regs[index], RINGL_RSH1_UNUSED, 0u)) {
            return invalid_value();
        }
        result.regs[index] = destination;
    }
    result.width = value->width;
    return result;
}

static Value unary_math_argument(Lower* lower)
{
    Value value;

    next(lower);
    if (!need(lower, T_LPAREN, "expected '(' after math builtin"))
        return invalid_value();
    value = expression(lower);
    if (!need(lower, T_RPAREN, "expected ')' after math builtin"))
        return invalid_value();
    if (!float_vector_value(lower, &value,
                            "math builtin requires floating-point values")) {
        return invalid_value();
    }
    return value;
}

static Value floor_value(Lower* lower)
{
    Value value = unary_math_argument(lower);

    return value.width == 0u ? value : floor_float_value(lower, &value);
}

static Value ceil_value(Lower* lower)
{
    Value value = unary_math_argument(lower);
    Value negative;
    Value rounded;

    if (value.width == 0u)
        return value;
    negative = negate_float_value(lower, &value);
    rounded = floor_float_value(lower, &negative);
    return rounded.width == 0u ? rounded : negate_float_value(lower, &rounded);
}

static Value fract_value(Lower* lower)
{
    Value value = unary_math_argument(lower);
    Value rounded;

    if (value.width == 0u)
        return value;
    rounded = floor_float_value(lower, &value);
    return rounded.width == 0u ? rounded
                              : componentwise_binary(lower, &value, &rounded,
                                                     RINGL_RSH1_OP_SUB_F32, 0);
}

static Value abs_value(Lower* lower)
{
    Value value = unary_math_argument(lower);
    Value negative;

    if (value.width == 0u)
        return value;
    negative = negate_float_value(lower, &value);
    return negative.width == 0u ? negative
                                : componentwise_binary(lower, &value, &negative,
                                                       RINGL_RSH1_OP_MAX_F32, 0);
}

static Value sqrt_float_value(Lower* lower, const Value* value)
{
    Value result = invalid_value();
    uint32_t index;

    if (!float_vector_value(lower, value,
                            "sqrt builtin requires floating-point values")) {
        return result;
    }
    for (index = 0u; index < value->width; ++index) {
        uint16_t destination = new_reg(lower);

        if (destination == RINGL_RSH1_UNUSED ||
            !emit(lower, RINGL_RSH1_OP_SQRT_F32, destination,
                  value->regs[index], RINGL_RSH1_UNUSED, 0u)) {
            return invalid_value();
        }
        result.regs[index] = destination;
    }
    result.width = value->width;
    return result;
}

static Value sqrt_value(Lower* lower)
{
    Value value = unary_math_argument(lower);

    return value.width == 0u ? value : sqrt_float_value(lower, &value);
}

static Value trig_float_value(Lower* lower, const Value* value,
                              uint16_t opcode, const char* diagnostic)
{
    Value result = invalid_value();
    uint32_t index;

    if (!float_vector_value(lower, value, diagnostic))
        return result;
    for (index = 0u; index < value->width; ++index) {
        uint16_t destination = new_reg(lower);

        if (destination == RINGL_RSH1_UNUSED ||
            !emit(lower, opcode, destination, value->regs[index],
                  RINGL_RSH1_UNUSED, 0u)) {
            return invalid_value();
        }
        result.regs[index] = destination;
    }
    result.width = value->width;
    return result;
}

static Value unary_trig_value(Lower* lower, uint16_t opcode,
                              const char* diagnostic)
{
    Value value = unary_math_argument(lower);

    return value.width == 0u ? value
                             : trig_float_value(lower, &value, opcode,
                                                diagnostic);
}

static Value angle_scale_value(Lower* lower, float factor)
{
    Value value = unary_math_argument(lower);
    Value scale;

    if (value.width == 0u)
        return value;
    scale = float_constant_value(lower, factor);
    return scale.width == 0u ? scale
                             : componentwise_binary(lower, &value, &scale,
                                                    RINGL_RSH1_OP_MUL_F32, 1);
}

static Value tan_value(Lower* lower)
{
    Value value = unary_math_argument(lower);
    Value sine;
    Value cosine;

    if (value.width == 0u)
        return value;
    sine = trig_float_value(lower, &value, RINGL_RSH1_OP_SIN_F32,
                            "tan builtin requires floating-point values");
    cosine = trig_float_value(lower, &value, RINGL_RSH1_OP_COS_F32,
                              "tan builtin requires floating-point values");
    if (sine.width == 0u || cosine.width == 0u)
        return invalid_value();
    return componentwise_binary(lower, &sine, &cosine, RINGL_RSH1_OP_DIV_F32,
                                 0);
}

static Value atan_value(Lower* lower)
{
    Value value;
    Value x;
    Value result = invalid_value();
    uint32_t index;

    next(lower);
    if (!need(lower, T_LPAREN, "expected '(' after atan builtin"))
        return result;
    value = expression(lower);
    if (take(lower, T_COMMA)) {
        x = expression(lower);
        if (!need(lower, T_RPAREN, "expected ')' after atan arguments"))
            return result;
        if (!float_vector_value(lower, &value,
                                "atan builtin requires floating-point values") ||
            !float_vector_value(lower, &x,
                                "atan builtin requires floating-point values") ||
            value.width != x.width) {
            fail(lower, "atan(y, x) requires matching floating-point values");
            return result;
        }
        for (index = 0u; index < value.width; ++index) {
            uint16_t destination = new_reg(lower);

            if (destination == RINGL_RSH1_UNUSED ||
                !emit(lower, RINGL_RSH1_OP_ATAN2_F32, destination,
                      value.regs[index], x.regs[index], 0u)) {
                return invalid_value();
            }
            result.regs[index] = destination;
        }
        result.width = value.width;
        return result;
    }
    if (!need(lower, T_RPAREN, "expected ')' after atan argument"))
        return result;
    return trig_float_value(lower, &value, RINGL_RSH1_OP_ATAN_F32,
                            "atan builtin requires floating-point values");
}

static Value exponential_float_value(Lower* lower, const Value* value,
                                     uint16_t opcode, const char* diagnostic)
{
    Value result = invalid_value();
    uint32_t index;

    if (!float_vector_value(lower, value, diagnostic))
        return result;
    for (index = 0u; index < value->width; ++index) {
        uint16_t destination = new_reg(lower);

        if (destination == RINGL_RSH1_UNUSED ||
            !emit(lower, opcode, destination, value->regs[index],
                  RINGL_RSH1_UNUSED, 0u)) {
            return invalid_value();
        }
        result.regs[index] = destination;
    }
    result.width = value->width;
    return result;
}

static Value unary_exponential_value(Lower* lower, uint16_t opcode,
                                     const char* diagnostic)
{
    Value value = unary_math_argument(lower);

    return value.width == 0u ? value
                             : exponential_float_value(lower, &value, opcode,
                                                      diagnostic);
}

static Value exp_value(Lower* lower)
{
    const float log2e = 1.44269504088896340736f;
    Value value = unary_math_argument(lower);
    Value scale;
    Value scaled;

    if (value.width == 0u)
        return value;
    scale = float_constant_value(lower, log2e);
    scaled = scale.width == 0u ? scale
                               : componentwise_binary(lower, &value, &scale,
                                                      RINGL_RSH1_OP_MUL_F32,
                                                      1);
    return scaled.width == 0u
        ? scaled
        : exponential_float_value(lower, &scaled, RINGL_RSH1_OP_EXP2_F32,
                                  "exp builtin requires floating-point values");
}

static Value log_value(Lower* lower)
{
    const float ln2 = 0.69314718055994530942f;
    Value value = unary_math_argument(lower);
    Value logarithm;
    Value scale;

    if (value.width == 0u)
        return value;
    logarithm = exponential_float_value(lower, &value, RINGL_RSH1_OP_LOG2_F32,
                                        "log builtin requires floating-point values");
    if (logarithm.width == 0u)
        return logarithm;
    scale = float_constant_value(lower, ln2);
    return scale.width == 0u ? scale
                             : componentwise_binary(lower, &logarithm, &scale,
                                                    RINGL_RSH1_OP_MUL_F32, 1);
}

static Value pow_value(Lower* lower)
{
    Value base;
    Value exponent;
    Value result = invalid_value();
    uint32_t index;

    next(lower);
    if (!need(lower, T_LPAREN, "expected '(' after pow builtin"))
        return result;
    base = expression(lower);
    if (!need(lower, T_COMMA, "expected ',' after pow base") ||
        (exponent = expression(lower)).width == 0u ||
        !need(lower, T_RPAREN, "expected ')' after pow arguments")) {
        return result;
    }
    if (!float_vector_value(lower, &base,
                            "pow builtin requires floating-point values") ||
        !float_vector_value(lower, &exponent,
                            "pow builtin requires floating-point values") ||
        base.width != exponent.width) {
        fail(lower, "pow requires matching floating-point values");
        return result;
    }
    for (index = 0u; index < base.width; ++index) {
        uint16_t destination = new_reg(lower);

        if (destination == RINGL_RSH1_UNUSED ||
            !emit(lower, RINGL_RSH1_OP_POW_F32, destination, base.regs[index],
                  exponent.regs[index], 0u)) {
            return invalid_value();
        }
        result.regs[index] = destination;
    }
    result.width = base.width;
    return result;
}

static Value inversesqrt_value(Lower* lower)
{
    Value value = unary_math_argument(lower);
    Value root;
    Value one;

    if (value.width == 0u)
        return value;
    root = sqrt_float_value(lower, &value);
    if (root.width == 0u)
        return root;
    one = float_constant_value(lower, 1.0f);
    return one.width == 0u ? one
                           : componentwise_binary(lower, &one, &root,
                                                  RINGL_RSH1_OP_DIV_F32, 1);
}

static Value squared_length_value(Lower* lower, const Value* value)
{
    Value result = invalid_value();
    uint16_t sum = RINGL_RSH1_UNUSED;
    uint32_t index;

    if (!float_vector_value(lower, value,
                            "length builtin requires floating-point values")) {
        return result;
    }
    for (index = 0u; index < value->width; ++index) {
        uint16_t product = new_reg(lower);

        if (product == RINGL_RSH1_UNUSED ||
            !emit(lower, RINGL_RSH1_OP_MUL_F32, product, value->regs[index],
                  value->regs[index], 0u)) {
            return invalid_value();
        }
        if (index != 0u) {
            uint16_t combined = new_reg(lower);

            if (combined == RINGL_RSH1_UNUSED ||
                !emit(lower, RINGL_RSH1_OP_ADD_F32, combined, sum, product,
                      0u)) {
                return invalid_value();
            }
            sum = combined;
        } else {
            sum = product;
        }
    }
    result.regs[0] = sum;
    result.width = 1u;
    return result;
}

static Value length_float_value(Lower* lower, const Value* value)
{
    Value squared = squared_length_value(lower, value);

    return squared.width == 0u ? squared : sqrt_float_value(lower, &squared);
}

static Value length_value(Lower* lower)
{
    Value value = unary_math_argument(lower);

    return value.width == 0u ? value : length_float_value(lower, &value);
}

static Value normalize_value(Lower* lower)
{
    Value value = unary_math_argument(lower);
    Value magnitude;

    if (value.width == 0u)
        return value;
    magnitude = length_float_value(lower, &value);
    return magnitude.width == 0u ? magnitude
                                : componentwise_binary(lower, &value,
                                                       &magnitude,
                                                       RINGL_RSH1_OP_DIV_F32,
                                                       1);
}

static Value min_max_value(Lower* lower, uint16_t opcode)
{
    Value left;
    Value right;

    next(lower);
    if (!need(lower, T_LPAREN, "expected '(' after min/max builtin"))
        return invalid_value();
    left = expression(lower);
    if (!need(lower, T_COMMA, "expected ',' in min/max builtin"))
        return invalid_value();
    right = expression(lower);
    if (!need(lower, T_RPAREN, "expected ')' after min/max builtin"))
        return invalid_value();
    if (!float_vector_value(lower, &left,
                            "min/max builtin requires floating-point values") ||
        !float_vector_value(lower, &right,
                            "min/max builtin requires floating-point values") ||
        (left.width != right.width && right.width != 1u)) {
        fail(lower, "min/max builtin component count mismatch");
        return invalid_value();
    }
    return componentwise_binary(lower, &left, &right, opcode, 1);
}

static Value clamp_value(Lower* lower)
{
    Value value;
    Value minimum;
    Value maximum;
    Value result;

    next(lower);
    if (!need(lower, T_LPAREN, "expected '(' after clamp builtin"))
        return invalid_value();
    value = expression(lower);
    if (!need(lower, T_COMMA, "expected ',' after clamp value"))
        return invalid_value();
    minimum = expression(lower);
    if (!need(lower, T_COMMA, "expected ',' after clamp minimum"))
        return invalid_value();
    maximum = expression(lower);
    if (!need(lower, T_RPAREN, "expected ')' after clamp builtin"))
        return invalid_value();
    if (!float_vector_value(lower, &value,
                            "clamp builtin requires floating-point values") ||
        !float_vector_value(lower, &minimum,
                            "clamp builtin requires floating-point values") ||
        !float_vector_value(lower, &maximum,
                            "clamp builtin requires floating-point values") ||
        (minimum.width != value.width && minimum.width != 1u) ||
        (maximum.width != value.width && maximum.width != 1u)) {
        fail(lower, "clamp builtin component count mismatch");
        return invalid_value();
    }
    result = componentwise_binary(lower, &value, &minimum,
                                  RINGL_RSH1_OP_MAX_F32, 1);
    if (result.width == 0u)
        return invalid_value();
    return componentwise_binary(lower, &result, &maximum,
                                RINGL_RSH1_OP_MIN_F32, 1);
}

static Value mix_value(Lower* lower)
{
    Value left;
    Value right;
    Value amount;
    Value result = invalid_value();
    uint32_t one_bits;
    float one = 1.0f;
    uint32_t index;

    next(lower);
    if (!need(lower, T_LPAREN, "expected '(' after mix builtin"))
        return result;
    left = expression(lower);
    if (!need(lower, T_COMMA, "expected ',' after first mix value"))
        return result;
    right = expression(lower);
    if (!need(lower, T_COMMA, "expected ',' after second mix value"))
        return result;
    amount = expression(lower);
    if (!need(lower, T_RPAREN, "expected ')' after mix builtin"))
        return result;
    if (!float_vector_value(lower, &left,
                            "mix builtin requires floating-point values") ||
        !float_vector_value(lower, &right,
                            "mix builtin requires floating-point values") ||
        !float_vector_value(lower, &amount,
                            "mix builtin requires floating-point values") ||
        left.width != right.width ||
        (amount.width != left.width && amount.width != 1u)) {
        fail(lower, "mix builtin component count mismatch");
        return result;
    }
    memcpy(&one_bits, &one, sizeof(one_bits));
    for (index = 0u; index < left.width; ++index) {
        uint16_t one_reg = new_reg(lower);
        uint16_t inverse_amount = new_reg(lower);
        uint16_t left_product = new_reg(lower);
        uint16_t right_product = new_reg(lower);
        uint16_t destination = new_reg(lower);
        uint16_t amount_reg = amount.regs[amount.width == 1u ? 0u : index];

        if (one_reg == RINGL_RSH1_UNUSED ||
            inverse_amount == RINGL_RSH1_UNUSED ||
            left_product == RINGL_RSH1_UNUSED ||
            right_product == RINGL_RSH1_UNUSED ||
            destination == RINGL_RSH1_UNUSED ||
            !emit(lower, RINGL_RSH1_OP_CONST_F32, one_reg,
                  RINGL_RSH1_UNUSED, RINGL_RSH1_UNUSED, one_bits) ||
            !emit(lower, RINGL_RSH1_OP_SUB_F32, inverse_amount, one_reg,
                  amount_reg, 0u) ||
            !emit(lower, RINGL_RSH1_OP_MUL_F32, left_product,
                  left.regs[index], inverse_amount, 0u) ||
            !emit(lower, RINGL_RSH1_OP_MUL_F32, right_product,
                  right.regs[index], amount_reg, 0u) ||
            !emit(lower, RINGL_RSH1_OP_ADD_F32, destination, left_product,
                  right_product, 0u)) {
            return invalid_value();
        }
        result.regs[index] = destination;
    }
    result.width = left.width;
    return result;
}

static Value dot_float_vector_values(Lower* lower, const Value* left,
                                     const Value* right,
                                     const char* diagnostic)
{
    Value result = invalid_value();
    uint16_t sum = RINGL_RSH1_UNUSED;
    uint32_t index;

    if (!float_vector_value(lower, left, diagnostic) ||
        !float_vector_value(lower, right, diagnostic) || left->width < 2u ||
        left->width != right->width) {
        fail(lower, diagnostic);
        return result;
    }
    for (index = 0u; index < left->width; ++index) {
        uint16_t product = new_reg(lower);

        if (product == RINGL_RSH1_UNUSED ||
            !emit(lower, RINGL_RSH1_OP_MUL_F32, product, left->regs[index],
                  right->regs[index], 0u)) {
            return invalid_value();
        }
        if (index != 0u) {
            uint16_t combined = new_reg(lower);

            if (combined == RINGL_RSH1_UNUSED ||
                !emit(lower, RINGL_RSH1_OP_ADD_F32, combined, sum, product,
                      0u)) {
                return invalid_value();
            }
            sum = combined;
        } else {
            sum = product;
        }
    }
    result.regs[0] = sum;
    result.width = 1u;
    return result;
}

static Value dot_value(Lower* lower)
{
    Value left;
    Value right;

    next(lower);
    if (!need(lower, T_LPAREN, "expected '(' after dot builtin"))
        return invalid_value();
    left = expression(lower);
    if (!need(lower, T_COMMA, "expected ',' in dot builtin"))
        return invalid_value();
    right = expression(lower);
    if (!need(lower, T_RPAREN, "expected ')' after dot builtin"))
        return invalid_value();
    return dot_float_vector_values(
        lower, &left, &right,
        "dot builtin requires matching vec2, vec3, or vec4 values");
}

static int matching_geometric_vectors(Lower* lower, const Value* left,
                                      const Value* right,
                                      const char* diagnostic)
{
    if (!float_vector_value(lower, left, diagnostic) ||
        !float_vector_value(lower, right, diagnostic) || left->width < 2u ||
        left->width != right->width) {
        fail(lower, diagnostic);
        return 0;
    }
    return 1;
}

static Value distance_value(Lower* lower)
{
    Value left;
    Value right;
    Value difference;

    next(lower);
    if (!need(lower, T_LPAREN, "expected '(' after distance builtin"))
        return invalid_value();
    left = expression(lower);
    if (!need(lower, T_COMMA, "expected ',' in distance builtin"))
        return invalid_value();
    right = expression(lower);
    if (!need(lower, T_RPAREN, "expected ')' after distance builtin"))
        return invalid_value();
    if (!float_vector_value(lower, &left,
                            "distance builtin requires matching floating-point values") ||
        !float_vector_value(lower, &right,
                            "distance builtin requires matching floating-point values") ||
        left.width != right.width) {
        fail(lower, "distance builtin requires matching floating-point values");
        return invalid_value();
    }
    difference = componentwise_binary(lower, &left, &right,
                                      RINGL_RSH1_OP_SUB_F32, 0);
    return difference.width == 0u ? difference
                                  : length_float_value(lower, &difference);
}

static Value cross_value(Lower* lower)
{
    Value left;
    Value right;
    Value result = invalid_value();
    static const uint8_t left_first[3] = { 1u, 2u, 0u };
    static const uint8_t right_first[3] = { 2u, 0u, 1u };
    static const uint8_t left_second[3] = { 2u, 0u, 1u };
    static const uint8_t right_second[3] = { 1u, 2u, 0u };
    uint32_t index;

    next(lower);
    if (!need(lower, T_LPAREN, "expected '(' after cross builtin"))
        return result;
    left = expression(lower);
    if (!need(lower, T_COMMA, "expected ',' in cross builtin"))
        return result;
    right = expression(lower);
    if (!need(lower, T_RPAREN, "expected ')' after cross builtin"))
        return result;
    if (!float_vector_value(lower, &left,
                            "cross builtin requires matching vec3 values") ||
        !float_vector_value(lower, &right,
                            "cross builtin requires matching vec3 values") ||
        left.width != 3u || right.width != 3u) {
        fail(lower, "cross builtin requires matching vec3 values");
        return result;
    }
    for (index = 0u; index < 3u; ++index) {
        uint16_t first_product = new_reg(lower);
        uint16_t second_product = new_reg(lower);
        uint16_t destination = new_reg(lower);

        if (first_product == RINGL_RSH1_UNUSED ||
            second_product == RINGL_RSH1_UNUSED ||
            destination == RINGL_RSH1_UNUSED ||
            !emit(lower, RINGL_RSH1_OP_MUL_F32, first_product,
                  left.regs[left_first[index]], right.regs[right_first[index]],
                  0u) ||
            !emit(lower, RINGL_RSH1_OP_MUL_F32, second_product,
                  left.regs[left_second[index]],
                  right.regs[right_second[index]], 0u) ||
            !emit(lower, RINGL_RSH1_OP_SUB_F32, destination, first_product,
                  second_product, 0u)) {
            return invalid_value();
        }
        result.regs[index] = destination;
    }
    result.width = 3u;
    return result;
}

static Value reflect_value(Lower* lower)
{
    Value incident;
    Value normal;
    Value projection;
    Value two;
    Value scale;
    Value normal_component;

    next(lower);
    if (!need(lower, T_LPAREN, "expected '(' after reflect builtin"))
        return invalid_value();
    incident = expression(lower);
    if (!need(lower, T_COMMA, "expected ',' in reflect builtin"))
        return invalid_value();
    normal = expression(lower);
    if (!need(lower, T_RPAREN, "expected ')' after reflect builtin"))
        return invalid_value();
    if (!matching_geometric_vectors(
            lower, &incident, &normal,
            "reflect builtin requires matching vec2, vec3, or vec4 values")) {
        return invalid_value();
    }
    projection = dot_float_vector_values(
        lower, &normal, &incident,
        "reflect builtin requires matching vec2, vec3, or vec4 values");
    two = float_constant_value(lower, 2.0f);
    if (projection.width == 0u || two.width == 0u)
        return invalid_value();
    scale = componentwise_binary(lower, &two, &projection,
                                 RINGL_RSH1_OP_MUL_F32, 0);
    if (scale.width == 0u)
        return invalid_value();
    normal_component = componentwise_binary(lower, &normal, &scale,
                                            RINGL_RSH1_OP_MUL_F32, 1);
    return normal_component.width == 0u
        ? normal_component
        : componentwise_binary(lower, &incident, &normal_component,
                               RINGL_RSH1_OP_SUB_F32, 0);
}

static Value faceforward_value(Lower* lower)
{
    Value normal;
    Value incident;
    Value reference_normal;
    Value projection;
    Value two;
    Value one;
    Value zero;
    Value result_factor = invalid_value();
    Value result;
    uint16_t negative;
    uint16_t negative_float;
    uint16_t doubled;

    next(lower);
    if (!need(lower, T_LPAREN, "expected '(' after faceforward builtin"))
        return invalid_value();
    normal = expression(lower);
    if (!need(lower, T_COMMA, "expected ',' after faceforward normal"))
        return invalid_value();
    incident = expression(lower);
    if (!need(lower, T_COMMA, "expected ',' after faceforward incident"))
        return invalid_value();
    reference_normal = expression(lower);
    if (!need(lower, T_RPAREN, "expected ')' after faceforward builtin"))
        return invalid_value();
    if (!matching_geometric_vectors(
            lower, &normal, &incident,
            "faceforward builtin requires matching vec2, vec3, or vec4 values") ||
        !matching_geometric_vectors(
            lower, &normal, &reference_normal,
            "faceforward builtin requires matching vec2, vec3, or vec4 values")) {
        return invalid_value();
    }
    projection = dot_float_vector_values(
        lower, &reference_normal, &incident,
        "faceforward builtin requires matching vec2, vec3, or vec4 values");
    two = float_constant_value(lower, 2.0f);
    one = float_constant_value(lower, 1.0f);
    zero = float_constant_value(lower, 0.0f);
    negative = new_reg(lower);
    negative_float = new_reg(lower);
    doubled = new_reg(lower);
    if (projection.width == 0u || two.width == 0u || one.width == 0u ||
        zero.width == 0u ||
        negative == RINGL_RSH1_UNUSED ||
        negative_float == RINGL_RSH1_UNUSED || doubled == RINGL_RSH1_UNUSED ||
        !emit(lower, RINGL_RSH1_OP_CMP_LT_F32, negative, projection.regs[0],
              zero.regs[0], 0u) ||
        !emit(lower, RINGL_RSH1_OP_I32_TO_F32, negative_float, negative,
              RINGL_RSH1_UNUSED, 0u) ||
        !emit(lower, RINGL_RSH1_OP_MUL_F32, doubled, two.regs[0],
              negative_float, 0u) ||
        !emit(lower, RINGL_RSH1_OP_SUB_F32, doubled, doubled, one.regs[0],
              0u)) {
        return invalid_value();
    }
    result_factor.regs[0] = doubled;
    result_factor.width = 1u;
    result = componentwise_binary(lower, &normal, &result_factor,
                                  RINGL_RSH1_OP_MUL_F32, 1);
    return result;
}

static Value refract_value(Lower* lower)
{
    Value incident;
    Value normal;
    Value eta;
    Value projection;
    Value eta_squared;
    Value projection_squared;
    Value one;
    Value zero;
    Value one_minus_projection_squared;
    Value coefficient;
    Value discriminant;
    Value safe_discriminant;
    Value root;
    Value eta_incident;
    Value eta_projection;
    Value normal_scale;
    Value normal_component;
    Value candidate;
    Value transmission_mask = invalid_value();
    uint16_t negative_discriminant;
    uint16_t negative_discriminant_float;
    uint16_t mask_register;

    next(lower);
    if (!need(lower, T_LPAREN, "expected '(' after refract builtin"))
        return invalid_value();
    incident = expression(lower);
    if (!need(lower, T_COMMA, "expected ',' after refract incident"))
        return invalid_value();
    normal = expression(lower);
    if (!need(lower, T_COMMA, "expected ',' after refract normal"))
        return invalid_value();
    eta = expression(lower);
    if (!need(lower, T_RPAREN, "expected ')' after refract builtin"))
        return invalid_value();
    if (!matching_geometric_vectors(
            lower, &incident, &normal,
            "refract builtin requires matching vec2, vec3, or vec4 values") ||
        !float_vector_value(lower, &eta,
                            "refract builtin requires a scalar float eta") ||
        eta.width != 1u) {
        fail(lower, "refract builtin requires matching vectors and scalar eta");
        return invalid_value();
    }
    projection = dot_float_vector_values(
        lower, &normal, &incident,
        "refract builtin requires matching vec2, vec3, or vec4 values");
    one = float_constant_value(lower, 1.0f);
    zero = float_constant_value(lower, 0.0f);
    if (projection.width == 0u || one.width == 0u || zero.width == 0u)
        return invalid_value();
    eta_squared = componentwise_binary(lower, &eta, &eta,
                                       RINGL_RSH1_OP_MUL_F32, 0);
    projection_squared = componentwise_binary(lower, &projection, &projection,
                                              RINGL_RSH1_OP_MUL_F32, 0);
    if (eta_squared.width == 0u || projection_squared.width == 0u)
        return invalid_value();
    one_minus_projection_squared = componentwise_binary(
        lower, &one, &projection_squared, RINGL_RSH1_OP_SUB_F32, 0);
    if (one_minus_projection_squared.width == 0u)
        return invalid_value();
    coefficient = componentwise_binary(lower, &eta_squared,
                                       &one_minus_projection_squared,
                                       RINGL_RSH1_OP_MUL_F32, 0);
    if (coefficient.width == 0u)
        return invalid_value();
    discriminant = componentwise_binary(lower, &one, &coefficient,
                                        RINGL_RSH1_OP_SUB_F32, 0);
    if (discriminant.width == 0u)
        return invalid_value();
    safe_discriminant = componentwise_binary(lower, &discriminant, &zero,
                                              RINGL_RSH1_OP_MAX_F32, 0);
    if (safe_discriminant.width == 0u)
        return invalid_value();
    root = sqrt_float_value(lower, &safe_discriminant);
    eta_incident = componentwise_binary(lower, &incident, &eta,
                                        RINGL_RSH1_OP_MUL_F32, 1);
    eta_projection = componentwise_binary(lower, &eta, &projection,
                                          RINGL_RSH1_OP_MUL_F32, 0);
    if (root.width == 0u || eta_incident.width == 0u ||
        eta_projection.width == 0u) {
        return invalid_value();
    }
    normal_scale = componentwise_binary(lower, &eta_projection, &root,
                                        RINGL_RSH1_OP_ADD_F32, 0);
    if (normal_scale.width == 0u)
        return invalid_value();
    normal_component = componentwise_binary(lower, &normal, &normal_scale,
                                            RINGL_RSH1_OP_MUL_F32, 1);
    if (normal_component.width == 0u)
        return invalid_value();
    candidate = componentwise_binary(lower, &eta_incident, &normal_component,
                                     RINGL_RSH1_OP_SUB_F32, 0);
    if (candidate.width == 0u)
        return invalid_value();
    negative_discriminant = new_reg(lower);
    negative_discriminant_float = new_reg(lower);
    mask_register = new_reg(lower);
    if (negative_discriminant == RINGL_RSH1_UNUSED ||
        negative_discriminant_float == RINGL_RSH1_UNUSED ||
        mask_register == RINGL_RSH1_UNUSED ||
        !emit(lower, RINGL_RSH1_OP_CMP_LT_F32, negative_discriminant,
              discriminant.regs[0], zero.regs[0], 0u) ||
        !emit(lower, RINGL_RSH1_OP_I32_TO_F32, negative_discriminant_float,
              negative_discriminant, RINGL_RSH1_UNUSED, 0u) ||
        !emit(lower, RINGL_RSH1_OP_SUB_F32, mask_register, one.regs[0],
              negative_discriminant_float, 0u)) {
        return invalid_value();
    }
    transmission_mask.regs[0] = mask_register;
    transmission_mask.width = 1u;
    return componentwise_binary(lower, &candidate, &transmission_mask,
                                 RINGL_RSH1_OP_MUL_F32, 1);
}

static Value mod_value(Lower* lower)
{
    Value value;
    Value divisor;
    Value quotient;
    Value rounded;
    Value product;

    next(lower);
    if (!need(lower, T_LPAREN, "expected '(' after mod builtin"))
        return invalid_value();
    value = expression(lower);
    if (!need(lower, T_COMMA, "expected ',' in mod builtin"))
        return invalid_value();
    divisor = expression(lower);
    if (!need(lower, T_RPAREN, "expected ')' after mod builtin"))
        return invalid_value();
    if (!float_vector_value(lower, &value,
                            "mod builtin requires floating-point values") ||
        !float_vector_value(lower, &divisor,
                            "mod builtin requires floating-point values") ||
        (divisor.width != value.width && divisor.width != 1u)) {
        fail(lower, "mod builtin component count mismatch");
        return invalid_value();
    }
    quotient = componentwise_binary(lower, &value, &divisor,
                                    RINGL_RSH1_OP_DIV_F32, 1);
    if (quotient.width == 0u)
        return invalid_value();
    rounded = floor_float_value(lower, &quotient);
    if (rounded.width == 0u)
        return invalid_value();
    product = componentwise_binary(lower, &divisor, &rounded,
                                   RINGL_RSH1_OP_MUL_F32, 1);
    return product.width == 0u ? product
                              : componentwise_binary(lower, &value, &product,
                                                     RINGL_RSH1_OP_SUB_F32, 0);
}

static Value sign_value(Lower* lower)
{
    Value value = unary_math_argument(lower);
    Value zero;
    Value result = invalid_value();
    uint32_t index;

    if (value.width == 0u)
        return value;
    zero = float_constant_value(lower, 0.0f);
    if (zero.width == 0u)
        return invalid_value();
    for (index = 0u; index < value.width; ++index) {
        uint16_t negative = new_reg(lower);
        uint16_t positive = new_reg(lower);
        uint16_t negative_float = new_reg(lower);
        uint16_t positive_float = new_reg(lower);
        uint16_t destination = new_reg(lower);

        if (negative == RINGL_RSH1_UNUSED ||
            positive == RINGL_RSH1_UNUSED ||
            negative_float == RINGL_RSH1_UNUSED ||
            positive_float == RINGL_RSH1_UNUSED ||
            destination == RINGL_RSH1_UNUSED ||
            !emit(lower, RINGL_RSH1_OP_CMP_LT_F32, negative,
                  value.regs[index], zero.regs[0], 0u) ||
            !emit(lower, RINGL_RSH1_OP_CMP_GT_F32, positive,
                  value.regs[index], zero.regs[0], 0u) ||
            !emit(lower, RINGL_RSH1_OP_I32_TO_F32, negative_float,
                  negative, RINGL_RSH1_UNUSED, 0u) ||
            !emit(lower, RINGL_RSH1_OP_I32_TO_F32, positive_float,
                  positive, RINGL_RSH1_UNUSED, 0u) ||
            !emit(lower, RINGL_RSH1_OP_SUB_F32, destination,
                  positive_float, negative_float, 0u)) {
            return invalid_value();
        }
        result.regs[index] = destination;
    }
    result.width = value.width;
    return result;
}

static Value step_value(Lower* lower)
{
    Value edge;
    Value value;
    Value one;
    Value result = invalid_value();
    uint32_t index;

    next(lower);
    if (!need(lower, T_LPAREN, "expected '(' after step builtin"))
        return result;
    edge = expression(lower);
    if (!need(lower, T_COMMA, "expected ',' in step builtin"))
        return result;
    value = expression(lower);
    if (!need(lower, T_RPAREN, "expected ')' after step builtin"))
        return result;
    if (!float_vector_value(lower, &edge,
                            "step builtin requires floating-point values") ||
        !float_vector_value(lower, &value,
                            "step builtin requires floating-point values") ||
        (edge.width != value.width && edge.width != 1u)) {
        fail(lower, "step builtin component count mismatch");
        return result;
    }
    one = float_constant_value(lower, 1.0f);
    if (one.width == 0u)
        return result;
    for (index = 0u; index < value.width; ++index) {
        uint16_t below_edge = new_reg(lower);
        uint16_t below_edge_float = new_reg(lower);
        uint16_t destination = new_reg(lower);
        uint16_t edge_reg = edge.regs[edge.width == 1u ? 0u : index];

        if (below_edge == RINGL_RSH1_UNUSED ||
            below_edge_float == RINGL_RSH1_UNUSED ||
            destination == RINGL_RSH1_UNUSED ||
            !emit(lower, RINGL_RSH1_OP_CMP_LT_F32, below_edge,
                  value.regs[index], edge_reg, 0u) ||
            !emit(lower, RINGL_RSH1_OP_I32_TO_F32, below_edge_float,
                  below_edge, RINGL_RSH1_UNUSED, 0u) ||
            !emit(lower, RINGL_RSH1_OP_SUB_F32, destination, one.regs[0],
                  below_edge_float, 0u)) {
            return invalid_value();
        }
        result.regs[index] = destination;
    }
    result.width = value.width;
    return result;
}

static Value smoothstep_value(Lower* lower)
{
    Value edge0;
    Value edge1;
    Value value;
    Value zero;
    Value one;
    Value two;
    Value three;
    Value result = invalid_value();
    uint32_t index;

    next(lower);
    if (!need(lower, T_LPAREN, "expected '(' after smoothstep builtin"))
        return result;
    edge0 = expression(lower);
    if (!need(lower, T_COMMA, "expected ',' after first smoothstep edge"))
        return result;
    edge1 = expression(lower);
    if (!need(lower, T_COMMA, "expected ',' after second smoothstep edge"))
        return result;
    value = expression(lower);
    if (!need(lower, T_RPAREN, "expected ')' after smoothstep builtin"))
        return result;
    if (!float_vector_value(lower, &edge0,
                            "smoothstep builtin requires floating-point values") ||
        !float_vector_value(lower, &edge1,
                            "smoothstep builtin requires floating-point values") ||
        !float_vector_value(lower, &value,
                            "smoothstep builtin requires floating-point values") ||
        (edge0.width != value.width && edge0.width != 1u) ||
        (edge1.width != value.width && edge1.width != 1u)) {
        fail(lower, "smoothstep builtin component count mismatch");
        return result;
    }
    zero = float_constant_value(lower, 0.0f);
    one = float_constant_value(lower, 1.0f);
    two = float_constant_value(lower, 2.0f);
    three = float_constant_value(lower, 3.0f);
    if (zero.width == 0u || one.width == 0u || two.width == 0u ||
        three.width == 0u) {
        return result;
    }
    for (index = 0u; index < value.width; ++index) {
        uint16_t edge0_reg = edge0.regs[edge0.width == 1u ? 0u : index];
        uint16_t edge1_reg = edge1.regs[edge1.width == 1u ? 0u : index];
        uint16_t span = new_reg(lower);
        uint16_t offset = new_reg(lower);
        uint16_t unclamped = new_reg(lower);
        uint16_t lower_bound = new_reg(lower);
        uint16_t parameter = new_reg(lower);
        uint16_t squared = new_reg(lower);
        uint16_t doubled = new_reg(lower);
        uint16_t polynomial = new_reg(lower);
        uint16_t destination = new_reg(lower);

        if (span == RINGL_RSH1_UNUSED || offset == RINGL_RSH1_UNUSED ||
            unclamped == RINGL_RSH1_UNUSED ||
            lower_bound == RINGL_RSH1_UNUSED ||
            parameter == RINGL_RSH1_UNUSED ||
            squared == RINGL_RSH1_UNUSED || doubled == RINGL_RSH1_UNUSED ||
            polynomial == RINGL_RSH1_UNUSED ||
            destination == RINGL_RSH1_UNUSED ||
            !emit(lower, RINGL_RSH1_OP_SUB_F32, span, edge1_reg, edge0_reg,
                  0u) ||
            !emit(lower, RINGL_RSH1_OP_SUB_F32, offset, value.regs[index],
                  edge0_reg, 0u) ||
            !emit(lower, RINGL_RSH1_OP_DIV_F32, unclamped, offset, span,
                  0u) ||
            !emit(lower, RINGL_RSH1_OP_MAX_F32, lower_bound, unclamped,
                  zero.regs[0], 0u) ||
            !emit(lower, RINGL_RSH1_OP_MIN_F32, parameter, lower_bound,
                  one.regs[0], 0u) ||
            !emit(lower, RINGL_RSH1_OP_MUL_F32, squared, parameter,
                  parameter, 0u) ||
            !emit(lower, RINGL_RSH1_OP_MUL_F32, doubled, two.regs[0],
                  parameter, 0u) ||
            !emit(lower, RINGL_RSH1_OP_SUB_F32, polynomial, three.regs[0],
                  doubled, 0u) ||
            !emit(lower, RINGL_RSH1_OP_MUL_F32, destination, squared,
                  polynomial, 0u)) {
            return invalid_value();
        }
        result.regs[index] = destination;
    }
    result.width = value.width;
    return result;
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

/* gl_FragCoord is supplied by the rasterizer in window coordinates. Keep the
 * four components as builtins instead of a synthetic varying: in particular
 * .w is the interpolated reciprocal clip W, not an affine vertex value. */
static Value frag_coord_value(Lower* lower)
{
    Value value = invalid_value();
    uint16_t x;
    uint16_t y;
    uint16_t z;
    uint16_t w;

    if (lower->shader_type != RINGL_FRAGMENT_SHADER) {
        fail(lower, "gl_FragCoord is only available in fragment shaders");
        return value;
    }
    next(lower);
    x = new_reg(lower);
    y = new_reg(lower);
    z = new_reg(lower);
    w = new_reg(lower);
    if (x == RINGL_RSH1_UNUSED || y == RINGL_RSH1_UNUSED ||
        z == RINGL_RSH1_UNUSED || w == RINGL_RSH1_UNUSED ||
        !emit(lower, RINGL_RSH1_OP_LOAD_BUILTIN_F32, x,
              RINGL_RSH1_UNUSED, RINGL_RSH1_UNUSED,
              RINGL_RSH1_BUILTIN_FRAG_COORD_X) ||
        !emit(lower, RINGL_RSH1_OP_LOAD_BUILTIN_F32, y,
              RINGL_RSH1_UNUSED, RINGL_RSH1_UNUSED,
              RINGL_RSH1_BUILTIN_FRAG_COORD_Y) ||
        !emit(lower, RINGL_RSH1_OP_LOAD_BUILTIN_F32, z,
              RINGL_RSH1_UNUSED, RINGL_RSH1_UNUSED,
              RINGL_RSH1_BUILTIN_FRAG_COORD_Z) ||
        !emit(lower, RINGL_RSH1_OP_LOAD_BUILTIN_F32, w,
              RINGL_RSH1_UNUSED, RINGL_RSH1_UNUSED,
              RINGL_RSH1_BUILTIN_FRAG_COORD_W)) {
        return invalid_value();
    }
    value.regs[0] = x;
    value.regs[1] = y;
    value.regs[2] = z;
    value.regs[3] = w;
    value.width = 4u;
    return apply_swizzle(lower, value);
}

static Value primary(Lower* lower)
{
    Value value;
    if (lower->token.kind == T_NUMBER)
        return number_value(lower);
    if (lower->token.kind == T_TRUE)
        return bool_constant_value(lower, 1);
    if (lower->token.kind == T_FALSE)
        return bool_constant_value(lower, 0);
    if (lower->token.kind == T_FLOAT)
        return conversion_value(lower, 0);
    if (lower->token.kind == T_INT)
        return conversion_value(lower, 1);
    if (lower->token.kind == T_VEC2)
        return constructor_value(lower, 2u, 0, 0);
    if (lower->token.kind == T_VEC3)
        return constructor_value(lower, 3u, 0, 0);
    if (lower->token.kind == T_VEC4)
        return constructor_value(lower, 4u, 0, 0);
    if (lower->token.kind == T_MAT2)
        return matrix_constructor_value(lower, 2u);
    if (lower->token.kind == T_MAT3)
        return matrix_constructor_value(lower, 3u);
    if (lower->token.kind == T_MAT4)
        return matrix_constructor_value(lower, 4u);
    if (lower->token.kind == T_IVEC2)
        return constructor_value(lower, 2u, 1, 0);
    if (lower->token.kind == T_IVEC3)
        return constructor_value(lower, 3u, 1, 0);
    if (lower->token.kind == T_IVEC4)
        return constructor_value(lower, 4u, 1, 0);
    if (lower->token.kind == T_BVEC2)
        return constructor_value(lower, 2u, 1, 1);
    if (lower->token.kind == T_BVEC3)
        return constructor_value(lower, 3u, 1, 1);
    if (lower->token.kind == T_BVEC4)
        return constructor_value(lower, 4u, 1, 1);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "not"))
        return boolean_not_value(lower);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "equal"))
        return boolean_compare_value(
            lower, RINGL_RSH1_OP_CMP_EQ_I32,
            "equal requires Boolean vectors with matching dimensions");
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "notEqual"))
        return boolean_compare_value(
            lower, RINGL_RSH1_OP_CMP_NE_I32,
            "notEqual requires Boolean vectors with matching dimensions");
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "any"))
        return boolean_reduce_value(lower, 0);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "all"))
        return boolean_reduce_value(lower, 1);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "dFdx"))
        return derivative_value(lower, RINGL_RSH1_OP_DFDX_F32);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "dFdy"))
        return derivative_value(lower, RINGL_RSH1_OP_DFDY_F32);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "fwidth"))
        return derivative_value(lower, RINGL_RSH1_OP_FWIDTH_F32);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "min"))
        return min_max_value(lower, RINGL_RSH1_OP_MIN_F32);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "max"))
        return min_max_value(lower, RINGL_RSH1_OP_MAX_F32);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "clamp"))
        return clamp_value(lower);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "mix"))
        return mix_value(lower);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "dot"))
        return dot_value(lower);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "distance"))
        return distance_value(lower);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "cross"))
        return cross_value(lower);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "reflect"))
        return reflect_value(lower);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "faceforward"))
        return faceforward_value(lower);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "refract"))
        return refract_value(lower);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "floor"))
        return floor_value(lower);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "ceil"))
        return ceil_value(lower);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "fract"))
        return fract_value(lower);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "mod"))
        return mod_value(lower);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "abs"))
        return abs_value(lower);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "sqrt"))
        return sqrt_value(lower);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "inversesqrt"))
        return inversesqrt_value(lower);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "length"))
        return length_value(lower);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "normalize"))
        return normalize_value(lower);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "radians"))
        return angle_scale_value(lower, 0.01745329251994329577f);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "degrees"))
        return angle_scale_value(lower, 57.2957795130823208768f);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "sin"))
        return unary_trig_value(lower, RINGL_RSH1_OP_SIN_F32,
                                "sin builtin requires floating-point values");
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "cos"))
        return unary_trig_value(lower, RINGL_RSH1_OP_COS_F32,
                                "cos builtin requires floating-point values");
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "tan"))
        return tan_value(lower);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "asin"))
        return unary_trig_value(lower, RINGL_RSH1_OP_ASIN_F32,
                                "asin builtin requires floating-point values");
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "acos"))
        return unary_trig_value(lower, RINGL_RSH1_OP_ACOS_F32,
                                "acos builtin requires floating-point values");
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "atan"))
        return atan_value(lower);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "exp"))
        return exp_value(lower);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "log"))
        return log_value(lower);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "exp2"))
        return unary_exponential_value(lower, RINGL_RSH1_OP_EXP2_F32,
                                       "exp2 builtin requires floating-point values");
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "log2"))
        return unary_exponential_value(lower, RINGL_RSH1_OP_LOG2_F32,
                                       "log2 builtin requires floating-point values");
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "pow"))
        return pow_value(lower);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "matrixCompMult"))
        return matrix_component_multiply_value(lower);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "sign"))
        return sign_value(lower);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "step"))
        return step_value(lower);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "smoothstep"))
        return smoothstep_value(lower);
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "gl_FragCoord"))
        return frag_coord_value(lower);
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
    if (take(lower, T_PLUS)) {
        value = unary(lower);
        if (value.width == 0u)
            return value;
        if (value.matrix || value.is_bool) {
            fail(lower, "unary arithmetic does not accept bool or matrix values");
            return invalid_value();
        }
        return value;
    }
    if (!take(lower, T_MINUS))
        return primary(lower);

    value = unary(lower);
    if (value.width == 0u || value.matrix || value.is_bool) {
        fail(lower, "unary arithmetic does not accept bool or matrix values");
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
    if (left->is_bool || right->is_bool) {
        fail(lower, "boolean arithmetic is not supported");
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

static int local_decl(Lower* lower, uint8_t width, int is_i32, int is_bool,
                      uint8_t matrix_dimension)
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
    symbol = add_symbol(lower, &name, 0, width, matrix_dimension);
    if (symbol == NULL)
        return 0;
    symbol->is_i32 = (uint8_t)is_i32;
    symbol->is_bool = (uint8_t)is_bool;
    next(lower);
    if (take(lower, T_ASSIGN)) {
        Value value = expression(lower);
        if (value.matrix != matrix_dimension || value.width != width ||
            value.is_i32 != (uint8_t)is_i32 || value.is_bool != (uint8_t)is_bool) {
            fail(lower, "local initializer component count mismatch");
            return 0;
        }
        memcpy(symbol->regs, value.regs,
               (size_t)(matrix_dimension ? (uint32_t)matrix_dimension * matrix_dimension
                                         : width) * sizeof(value.regs[0]));
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
    if (symbol->varying) {
        if (lower->shader_type != RINGL_VERTEX_SHADER ||
            symbol->output == RINGL_RSH1_UNUSED) {
            fail(lower, "fragment varying is read-only");
            return 0;
        }
        if (symbol->matrix != value.matrix || symbol->width != value.width ||
            symbol->is_i32 != value.is_i32 || symbol->is_bool != value.is_bool) {
            fail(lower, "varying assignment width mismatch");
            return 0;
        }
        if (!store_output(lower, &value, symbol->output))
            return 0;
        memcpy(symbol->regs, value.regs,
               (size_t)value.width * sizeof(value.regs[0]));
        symbol->initialized = 1u;
        return 1;
    }
    if (symbol->attribute || symbol->uniform) {
        fail(lower, "attribute or uniform is read-only");
        return 0;
    }
    if (symbol->matrix != value.matrix || symbol->width != value.width ||
        symbol->is_i32 != value.is_i32 || symbol->is_bool != value.is_bool) {
        fail(lower, "assignment width mismatch");
        return 0;
    }
    memcpy(symbol->regs, value.regs,
           (size_t)(value.matrix ? (uint32_t)value.matrix * value.matrix
                                 : value.width) * sizeof(value.regs[0]));
    symbol->initialized = 1u;
    return 1;
}

static uint16_t comparison_opcode(Tok operator, int is_i32)
{
    switch (operator) {
    case T_EQ:
        return is_i32 ? RINGL_RSH1_OP_CMP_EQ_I32 : RINGL_RSH1_OP_CMP_EQ_F32;
    case T_NE:
        return is_i32 ? RINGL_RSH1_OP_CMP_NE_I32 : RINGL_RSH1_OP_CMP_NE_F32;
    case T_LT:
        return is_i32 ? RINGL_RSH1_OP_CMP_LT_I32 : RINGL_RSH1_OP_CMP_LT_F32;
    case T_LE:
        return is_i32 ? RINGL_RSH1_OP_CMP_LE_I32 : RINGL_RSH1_OP_CMP_LE_F32;
    case T_GT:
        return is_i32 ? RINGL_RSH1_OP_CMP_GT_I32 : RINGL_RSH1_OP_CMP_GT_F32;
    case T_GE:
        return is_i32 ? RINGL_RSH1_OP_CMP_GE_I32 : RINGL_RSH1_OP_CMP_GE_F32;
    default:
        return 0u;
    }
}

static int is_comparison_operator(Tok operator)
{
    return operator == T_EQ || operator == T_NE || operator == T_LT ||
           operator == T_LE || operator == T_GT || operator == T_GE;
}

static int parenthesized_condition(Lower* lower)
{
    Lower probe = *lower;
    uint32_t depth = 0u;
    int condition_operator = 0;

    while (probe.token.kind != T_EOF) {
        if (probe.token.kind == T_LPAREN) {
            depth++;
        } else if (probe.token.kind == T_RPAREN) {
            if (depth == 0u)
                return 0;
            depth--;
            if (depth == 0u)
                return condition_operator;
        } else if (depth != 0u &&
                   (is_comparison_operator(probe.token.kind) ||
                    probe.token.kind == T_NOT || probe.token.kind == T_AND ||
                    probe.token.kind == T_XOR || probe.token.kind == T_OR)) {
            condition_operator = 1;
        }
        next(&probe);
    }
    return 0;
}

static Value boolean_comparison_value(Lower* lower, const Value* left,
                                      const Value* right, Tok operator)
{
    Value result = invalid_value();
    uint16_t destination;
    uint16_t opcode;

    if (left->matrix || right->matrix || left->width != 1u ||
        right->width != 1u || left->is_i32 != right->is_i32 ||
        left->is_bool != right->is_bool ||
        (left->is_bool && operator != T_EQ && operator != T_NE)) {
        fail(lower, "if condition requires matching scalar operands");
        return result;
    }
    opcode = comparison_opcode(operator, left->is_i32);
    destination = new_reg(lower);
    if (opcode == 0u || destination == RINGL_RSH1_UNUSED ||
        !emit(lower, opcode, destination, left->regs[0], right->regs[0], 0u)) {
        return result;
    }
    result.regs[0] = destination;
    result.width = 1u;
    result.is_i32 = 1u;
    result.is_bool = 1u;
    return result;
}

/* The bounded profile permits no calls, assignments, or other observable
 * side effects inside a condition. Evaluating both scalar Boolean operands in
 * RSH1 is therefore equivalent to GLSL short-circuit evaluation while keeping
 * the executable backend-independent. */
static Value boolean_binary_value(Lower* lower, const Value* left,
                                  const Value* right, Tok operator)
{
    Value result = invalid_value();
    uint16_t destination;

    if (left->matrix || right->matrix || left->width != 1u ||
        right->width != 1u || !left->is_i32 || !right->is_i32 ||
        !left->is_bool || !right->is_bool) {
        fail(lower, "logical operators require scalar Boolean operands");
        return result;
    }
    destination = new_reg(lower);
    if (destination == RINGL_RSH1_UNUSED)
        return result;
    if (operator == T_AND) {
        if (!emit(lower, RINGL_RSH1_OP_MUL_I32, destination, left->regs[0],
                  right->regs[0], 0u)) {
            return result;
        }
    } else {
        uint16_t sum = new_reg(lower);
        uint16_t expected = new_reg(lower);

        if (sum == RINGL_RSH1_UNUSED || expected == RINGL_RSH1_UNUSED ||
            !emit(lower, RINGL_RSH1_OP_ADD_I32, sum, left->regs[0],
                  right->regs[0], 0u) ||
            !emit(lower, RINGL_RSH1_OP_CONST_I32, expected,
                  RINGL_RSH1_UNUSED, RINGL_RSH1_UNUSED,
                  operator == T_XOR ? 1u : 0u) ||
            !emit(lower, operator == T_XOR ? RINGL_RSH1_OP_CMP_EQ_I32
                                            : RINGL_RSH1_OP_CMP_NE_I32,
                  destination, sum, expected, 0u)) {
            return result;
        }
    }
    result.regs[0] = destination;
    result.width = 1u;
    result.is_i32 = 1u;
    result.is_bool = 1u;
    return result;
}

static Value conditional_or_value(Lower* lower);

static Value conditional_primary_value(Lower* lower)
{
    Value left;

    if (take(lower, T_NOT)) {
        left = conditional_primary_value(lower);
        if (left.width != 1u || left.matrix || !left.is_bool) {
            fail(lower, "'!' requires a scalar Boolean operand");
            return invalid_value();
        }
        return boolean_invert_value(lower, &left);
    }
    if (lower->token.kind == T_LPAREN && parenthesized_condition(lower)) {
        next(lower);
        left = conditional_or_value(lower);
        if (left.width == 0u ||
            !need(lower, T_RPAREN, "expected ')' after Boolean condition")) {
            return invalid_value();
        }
        return left;
    }
    left = expression(lower);
    if (left.width == 0u)
        return invalid_value();
    if (is_comparison_operator(lower->token.kind)) {
        Tok operator = lower->token.kind;
        Value right;

        next(lower);
        right = expression(lower);
        if (right.width == 0u)
            return invalid_value();
        return boolean_comparison_value(lower, &left, &right, operator);
    }
    if (left.matrix || left.width != 1u || !left.is_bool) {
        fail(lower, "if condition requires a bool or scalar comparison");
        return invalid_value();
    }
    return left;
}

static Value conditional_and_value(Lower* lower)
{
    Value left = conditional_primary_value(lower);

    while (left.width != 0u && take(lower, T_AND)) {
        Value right = conditional_primary_value(lower);

        if (right.width == 0u)
            return invalid_value();
        left = boolean_binary_value(lower, &left, &right, T_AND);
    }
    return left;
}

static Value conditional_xor_value(Lower* lower)
{
    Value left = conditional_and_value(lower);

    while (left.width != 0u && take(lower, T_XOR)) {
        Value right = conditional_and_value(lower);

        if (right.width == 0u)
            return invalid_value();
        left = boolean_binary_value(lower, &left, &right, T_XOR);
    }
    return left;
}

static Value conditional_or_value(Lower* lower)
{
    Value left = conditional_xor_value(lower);

    while (left.width != 0u && take(lower, T_OR)) {
        Value right = conditional_xor_value(lower);

        if (right.width == 0u)
            return invalid_value();
        left = boolean_binary_value(lower, &left, &right, T_OR);
    }
    return left;
}

/* A scalar RSH1 branch can only guarantee stage output on both paths when
 * each path stores the complete fixed RGBA/clip vector. A fragment may instead
 * discard on exactly one branch: DISCARD terminates execution before output
 * publication. Keep this deliberately narrower than general GLSL statements:
 * no local mutation, nested branch, or partial output can reach a later RETURN
 * with an uninitialized component. */
static int conditional_output_assignment(Lower* lower)
{
    uint32_t first_instruction = lower->ins_count;
    uint32_t component;

    if (lower->token.kind != T_IDENT ||
        (lower->shader_type == RINGL_VERTEX_SHADER
             ? !text_is(&lower->token, "gl_Position")
             : !text_is(&lower->token, "gl_FragColor"))) {
        fail(lower, "if branches must assign the stage output");
        return 0;
    }
    if (!assignment(lower))
        return 0;
    if (lower->ins_count < first_instruction + 4u) {
        fail(lower, "if branch must write all four output components");
        return 0;
    }
    for (component = 0u; component < 4u; ++component) {
        const RinGLRsh1InstructionV1* instruction =
            &lower->ins[lower->ins_count - 4u + component];

        if (instruction->opcode != RINGL_RSH1_OP_STORE_OUTPUT_F32 ||
            instruction->immediate != component) {
            fail(lower, "if branch must write all four output components");
            return 0;
        }
    }
    return 1;
}

static int discard_statement(Lower* lower);

static int conditional_branch(Lower* lower, int* discard_out)
{
    if (discard_out == NULL)
        return 0;
    *discard_out = 0;
    if (lower->token.kind == T_IDENT && text_is(&lower->token, "discard")) {
        if (lower->shader_type != RINGL_FRAGMENT_SHADER) {
            fail(lower, "discard is only available in fragment shaders");
            return 0;
        }
        if (!discard_statement(lower))
            return 0;
        *discard_out = 1;
        return 1;
    }
    return conditional_output_assignment(lower);
}

static int conditional_output(Lower* lower)
{
    Value condition;
    uint16_t zero;
    uint16_t false_result;
    uint32_t jump_to_else;
    uint32_t jump_to_end;
    int if_discards;
    int else_discards;

    next(lower);
    if (!need(lower, T_LPAREN, "expected '(' after if"))
        return 0;
    condition = conditional_or_value(lower);
    if (condition.width == 0u || condition.matrix || condition.width != 1u ||
        !condition.is_bool) {
        return 0;
    }
    if (!need(lower, T_RPAREN, "expected ')' after if condition"))
        return 0;
    zero = new_reg(lower);
    false_result = new_reg(lower);
    if (zero == RINGL_RSH1_UNUSED || false_result == RINGL_RSH1_UNUSED ||
        !emit(lower, RINGL_RSH1_OP_CONST_I32, zero, RINGL_RSH1_UNUSED,
              RINGL_RSH1_UNUSED, 0u) ||
        !emit(lower, RINGL_RSH1_OP_CMP_EQ_I32, false_result,
              condition.regs[0], zero, 0u)) {
        return 0;
    }
    if (!need(lower, T_LBRACE, "expected '{' after if condition"))
        return 0;
    jump_to_else = lower->ins_count;
    if (!emit(lower, RINGL_RSH1_OP_JUMP_IF, RINGL_RSH1_UNUSED,
              false_result, RINGL_RSH1_UNUSED, 0u) ||
        !conditional_branch(lower, &if_discards) ||
        !need(lower, T_RBRACE, "expected '}' after if branch")) {
        return 0;
    }
    jump_to_end = lower->ins_count;
    if (!emit(lower, RINGL_RSH1_OP_JUMP, RINGL_RSH1_UNUSED,
              RINGL_RSH1_UNUSED, RINGL_RSH1_UNUSED, 0u)) {
        return 0;
    }
    lower->ins[jump_to_else].immediate = lower->ins_count;
    if (!need(lower, T_ELSE, "bounded if requires else branch") ||
        !need(lower, T_LBRACE, "expected '{' after else") ||
        !conditional_branch(lower, &else_discards) ||
        !need(lower, T_RBRACE, "expected '}' after else branch")) {
        return 0;
    }
    if (if_discards && else_discards) {
        fail(lower, "conditional discard requires an output branch");
        return 0;
    }
    lower->ins[jump_to_end].immediate = lower->ins_count;
    return 1;
}

/* DISCARD is terminal for the current fragment invocation. Conditional use is
 * admitted only by conditional_branch(), which requires a complete-output
 * opposite branch. */
static int discard_statement(Lower* lower)
{
    if (lower->shader_type != RINGL_FRAGMENT_SHADER) {
        fail(lower, "discard is only available in fragment shaders");
        return 0;
    }
    next(lower);
    if (!need(lower, T_SEMI, "expected ';' after discard"))
        return 0;
    return emit(lower, RINGL_RSH1_OP_DISCARD, RINGL_RSH1_UNUSED,
                RINGL_RSH1_UNUSED, RINGL_RSH1_UNUSED, 0u);
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
            } else if (lower->token.kind == T_BOOL) {
                uniform_type = RINGL_BOOL;
            } else if (lower->token.kind == T_BVEC2) {
                uniform_type = RINGL_BOOL_VEC2;
            } else if (lower->token.kind == T_BVEC3) {
                uniform_type = RINGL_BOOL_VEC3;
            } else if (lower->token.kind == T_BVEC4) {
                uniform_type = RINGL_BOOL_VEC4;
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
                fail(lower, "only vertex mat2-4 and scalar/vector float, int, or bool uniforms are supported");
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
                                     uniform_type == RINGL_FLOAT || uniform_type == RINGL_INT || uniform_type == RINGL_BOOL ? 1u
                                     : uniform_type == RINGL_FLOAT_VEC2 || uniform_type == RINGL_INT_VEC2 || uniform_type == RINGL_BOOL_VEC2 ? 2u
                                     : uniform_type == RINGL_FLOAT_VEC3 || uniform_type == RINGL_INT_VEC3 || uniform_type == RINGL_BOOL_VEC3 ? 3u
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
                (symbol = add_varying_symbol(lower, &name, width)) == NULL) {
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
                    lower->token.kind == T_BOOL ||
                    lower->token.kind == T_BVEC2 ||
                    lower->token.kind == T_BVEC3 ||
                    lower->token.kind == T_BVEC4 ||
                    lower->token.kind == T_IVEC2 ||
                    lower->token.kind == T_IVEC3 ||
                    lower->token.kind == T_IVEC4 ||
                    lower->token.kind == T_MAT2 ||
                    lower->token.kind == T_MAT3 ||
                    lower->token.kind == T_MAT4) {
                    uint8_t width = lower->token.kind == T_FLOAT ||
                                     lower->token.kind == T_INT || lower->token.kind == T_BOOL ? 1u
                        : lower->token.kind == T_VEC2 || lower->token.kind == T_IVEC2 ||
                          lower->token.kind == T_BVEC2 ||
                          lower->token.kind == T_MAT2 ? 2u
                        : lower->token.kind == T_VEC3 || lower->token.kind == T_IVEC3 ||
                          lower->token.kind == T_BVEC3 ||
                          lower->token.kind == T_MAT3 ? 3u : 4u;
                    int is_i32 = lower->token.kind == T_INT ||
                                 lower->token.kind == T_BOOL ||
                                 lower->token.kind == T_BVEC2 ||
                                 lower->token.kind == T_BVEC3 ||
                                 lower->token.kind == T_BVEC4 ||
                                 lower->token.kind == T_IVEC2 ||
                                 lower->token.kind == T_IVEC3 ||
                                 lower->token.kind == T_IVEC4;
                    uint8_t matrix_dimension = lower->token.kind == T_MAT2 ? 2u
                        : lower->token.kind == T_MAT3 ? 3u
                        : lower->token.kind == T_MAT4 ? 4u : 0u;
                    if (!local_decl(lower, width, is_i32,
                                    lower->token.kind == T_BOOL ||
                                    lower->token.kind == T_BVEC2 ||
                                    lower->token.kind == T_BVEC3 ||
                                    lower->token.kind == T_BVEC4,
                                    matrix_dimension))
                        return 0;
                } else if (lower->token.kind == T_IF) {
                    if (!conditional_output(lower))
                        return 0;
                } else if (lower->token.kind == T_IDENT &&
                           text_is(&lower->token, "discard")) {
                    if (!discard_statement(lower))
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
    if (lower->shader_type == RINGL_VERTEX_SHADER) {
        for (uint32_t index = 0u; index < lower->symbol_count; ++index) {
            const Symbol* symbol = &lower->symbols[index];
            if (symbol->varying && !symbol->initialized) {
                fail(lower, "vertex varying must be assigned before publication");
                return 0;
            }
        }
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

/* A fragment shader consisting of `discard;` has no color assignment, yet
 * still needs the ordinary four-component output ABI so that it can be linked
 * into a WebGL draw pipeline.  Materialize zero stores after DISCARD: the
 * executor terminates at DISCARD, while the canonical unreachable tail keeps
 * the RSH1 validator and pipeline interface explicit.  Do not grant this
 * special ABI to an empty/non-discard shader. */
static int append_discard_output_defaults(Lower* lower)
{
    uint16_t zero;
    uint32_t index;
    int saw_discard = 0;

    if (lower->shader_type != RINGL_FRAGMENT_SHADER ||
        lower->output_count != 0u || lower->uses_draw_buffers != 0u) {
        return 1;
    }
    for (index = 0u; index < lower->ins_count; ++index) {
        if (lower->ins[index].opcode == RINGL_RSH1_OP_DISCARD) {
            saw_discard = 1;
            break;
        }
    }
    if (!saw_discard)
        return 1;
    zero = new_reg(lower);
    if (zero == RINGL_RSH1_UNUSED ||
        !emit(lower, RINGL_RSH1_OP_CONST_F32, zero, RINGL_RSH1_UNUSED,
              RINGL_RSH1_UNUSED, 0u)) {
        return 0;
    }
    for (index = 0u; index < 4u; ++index) {
        if (!emit(lower, RINGL_RSH1_OP_STORE_OUTPUT_F32,
                  RINGL_RSH1_UNUSED, zero, RINGL_RSH1_UNUSED, index)) {
            return 0;
        }
    }
    lower->output_count = 4u;
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
    lower.next_varying_output = 4u;
    lower.uniforms = uniforms;
    lower.uniform_count = uniform_count;
    lower.result = result;
    if (!parse_all(&lower))
        return 1;
    if (!append_draw_buffer_defaults(&lower))
        return 1;
    if (!append_discard_output_defaults(&lower))
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
