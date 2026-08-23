/* SPDX-License-Identifier: MIT */
#include "glsl_lower.h"
#include "rsh1_abi.h"

#include <ctype.h>
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
    T_MAT4,
    T_ATTRIBUTE,
    T_UNIFORM,
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
    if (length == 4u && memcmp(begin, "mat4", 4u) == 0)
        return T_MAT4;
    if (length == 9u && memcmp(begin, "attribute", 9u) == 0)
        return T_ATTRIBUTE;
    if (length == 7u && memcmp(begin, "uniform", 7u) == 0)
        return T_UNIFORM;
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
    if (isdigit((unsigned char)c) || c == '.') {
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
                          int attribute, uint8_t width)
{
    Symbol* symbol;
    uint32_t index;

    if (lower->symbol_count >= 64u || token->length == 0u ||
        token->length >= 64u || width == 0u ||
        (width > 4u && width != 16u)) {
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
    symbol->matrix = width == 16u;
    symbol->input = attribute ? lower->next_input : RINGL_RSH1_UNUSED;
    if (attribute)
        lower->next_input = (uint16_t)(lower->next_input + width);
    for (index = 0u; index < (symbol->matrix ? 16u : 4u); ++index)
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
    float zero_values[16] = { 0.0f };
    const float* values = zero_values;
    uint32_t index;

    if (lower == NULL || symbol == NULL || name == NULL)
        return 0;
    uniform = find_uniform_value(lower, name);
    if (uniform != NULL && uniform->type != type) {
        fail(lower, "uniform reflection type mismatch");
        return 0;
    }
    if (uniform != NULL)
        values = uniform->values;
    symbol->uniform = 1u;
    for (index = 0u; index < (symbol->matrix ? 16u : symbol->width); ++index) {
        uint16_t reg = new_reg(lower);
        uint32_t bits;

        memcpy(&bits, &values[index], sizeof(bits));
        if (reg == RINGL_RSH1_UNUSED ||
            !emit(lower, RINGL_RSH1_OP_CONST_F32, reg,
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
    float number;
    uint32_t bits;
    uint16_t reg;

    if (lower->token.length >= sizeof(temp)) {
        fail(lower, "numeric literal too long");
        return value;
    }
    memcpy(temp, lower->token.begin, lower->token.length);
    temp[lower->token.length] = '\0';
    number = strtof(temp, &end);
    if (end == temp || *end != '\0') {
        fail(lower, "invalid numeric literal");
        return value;
    }
    memcpy(&bits, &number, sizeof(bits));
    next(lower);
    reg = new_reg(lower);
    if (reg == RINGL_RSH1_UNUSED ||
        !emit(lower, RINGL_RSH1_OP_CONST_F32, reg, RINGL_RSH1_UNUSED,
              RINGL_RSH1_UNUSED, bits)) {
        return value;
    }
    value.regs[0] = reg;
    value.width = 1u;
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
        for (index = 0u; index < (symbol->matrix ? 16u : symbol->width); ++index)
            value.regs[index] = symbol->regs[index];
    }
    return value;
}

static Value constructor_value(Lower* lower, uint8_t target_width)
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
    return result;
}

static Value primary(Lower* lower)
{
    Value value;
    if (lower->token.kind == T_NUMBER)
        return number_value(lower);
    if (lower->token.kind == T_VEC2)
        return constructor_value(lower, 2u);
    if (lower->token.kind == T_VEC3)
        return constructor_value(lower, 3u);
    if (lower->token.kind == T_VEC4)
        return constructor_value(lower, 4u);
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
            !emit(lower, RINGL_RSH1_OP_CONST_F32, zero, RINGL_RSH1_UNUSED,
                  RINGL_RSH1_UNUSED, 0u) ||
            !emit(lower, RINGL_RSH1_OP_SUB_F32, result, zero, value.regs[index],
                  0u)) {
            return invalid_value();
        }
        value.regs[index] = result;
    }
    return value;
}

static Value matrix_times_vec4(Lower* lower, const Value* matrix,
                               const Value* vector)
{
    Value result = invalid_value();
    uint32_t row;

    if (matrix == NULL || vector == NULL || !matrix->matrix ||
        vector->matrix || vector->width != 4u)
        return result;
    for (row = 0u; row < 4u; ++row) {
        uint16_t products[4];
        uint16_t left_sum;
        uint16_t right_sum;
        uint16_t output;
        uint32_t column;

        for (column = 0u; column < 4u; ++column) {
            products[column] = new_reg(lower);
            if (products[column] == RINGL_RSH1_UNUSED ||
                !emit(lower, RINGL_RSH1_OP_MUL_F32, products[column],
                      matrix->regs[column * 4u + row], vector->regs[column],
                      0u)) {
                return invalid_value();
            }
        }
        left_sum = new_reg(lower);
        right_sum = new_reg(lower);
        output = new_reg(lower);
        if (left_sum == RINGL_RSH1_UNUSED || right_sum == RINGL_RSH1_UNUSED ||
            output == RINGL_RSH1_UNUSED ||
            !emit(lower, RINGL_RSH1_OP_ADD_F32, left_sum, products[0],
                  products[1], 0u) ||
            !emit(lower, RINGL_RSH1_OP_ADD_F32, right_sum, products[2],
                  products[3], 0u) ||
            !emit(lower, RINGL_RSH1_OP_ADD_F32, output, left_sum, right_sum,
                  0u)) {
            return invalid_value();
        }
        result.regs[row] = output;
    }
    result.width = 4u;
    return result;
}

/* GLSL scalar/vector arithmetic is represented as scalar RSH1 instructions.
 * Keep the expansion at this frontend boundary: the RSH1 ABI has no hidden
 * vector operation and the generated module must remain independently
 * executable by every RinGPU backend. Addition/subtraction require matching
 * vector widths, while multiplication/division additionally permit the GLES
 * scalar broadcast form. */
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
            left = matrix_times_vec4(lower, &left, &right);
            if (left.width == 0u)
                fail(lower, "mat4 multiplication requires a vec4 right operand");
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
            0);
    }
    return left;
}

static int local_decl(Lower* lower, uint8_t width)
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
    symbol = add_symbol(lower, &name, 0, width);
    if (symbol == NULL)
        return 0;
    next(lower);
    if (take(lower, T_ASSIGN)) {
        Value value = expression(lower);
        if (value.matrix || value.width != width) {
            fail(lower, "local initializer component count mismatch");
            return 0;
        }
        memcpy(symbol->regs, value.regs,
               (size_t)width * sizeof(value.regs[0]));
        symbol->initialized = 1u;
    }
    return need(lower, T_SEMI, "expected ';' after local");
}

static int store_output(Lower* lower, const Value* value)
{
    uint32_t index;
    for (index = 0u; index < value->width; ++index) {
        if (!emit(lower, RINGL_RSH1_OP_STORE_OUTPUT_F32,
                  RINGL_RSH1_UNUSED, value->regs[index],
                  RINGL_RSH1_UNUSED, index)) {
            return 0;
        }
    }
    if (lower->output_count < value->width)
        lower->output_count = value->width;
    return 1;
}

static int assignment(Lower* lower)
{
    Token target = lower->token;
    Symbol* symbol = NULL;
    Value value;
    int output = 0;

    if (target.kind != T_IDENT) {
        fail(lower, "expected assignment");
        return 0;
    }
    if (text_is(&target, "gl_Position"))
        output = lower->shader_type == RINGL_VERTEX_SHADER;
    else if (text_is(&target, "gl_FragColor"))
        output = lower->shader_type == RINGL_FRAGMENT_SHADER;
    else
        symbol = find_symbol(lower, &target);
    if (!output && symbol == NULL) {
        fail(lower, "unknown assignment target");
        return 0;
    }
    next(lower);
    if (!need(lower, T_ASSIGN, "expected '='"))
        return 0;
    value = expression(lower);
    if (value.width == 0u)
        return 0;
    if (!need(lower, T_SEMI, "expected ';' after assignment"))
        return 0;
    if (output) {
        if (value.matrix || (value.width != 1u && value.width != 4u)) {
            fail(lower, "shader output must be scalar or vec4");
            return 0;
        }
        return store_output(lower, &value);
    }
    if (symbol->attribute || symbol->uniform) {
        fail(lower, "attribute or uniform is read-only");
        return 0;
    }
    if (symbol->width != value.width) {
        fail(lower, "assignment width mismatch");
        return 0;
    }
    memcpy(symbol->regs, value.regs,
           (size_t)value.width * sizeof(value.regs[0]));
    symbol->initialized = 1u;
    return 1;
}

static int parse_all(Lower* lower)
{
    int main_seen = 0;
    next(lower);
    while (lower->token.kind != T_EOF) {
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
                add_symbol(lower, &name, 1, width) == NULL) {
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
            } else if (lower->token.kind == T_VEC2) {
                uniform_type = RINGL_FLOAT_VEC2;
            } else if (lower->token.kind == T_VEC3) {
                uniform_type = RINGL_FLOAT_VEC3;
            } else if (lower->token.kind == T_VEC4) {
                uniform_type = RINGL_FLOAT_VEC4;
            } else if (lower->token.kind == T_MAT4 &&
                       lower->shader_type == RINGL_VERTEX_SHADER) {
                uniform_type = RINGL_FLOAT_MAT4;
            } else {
                fail(lower, "only vertex uniform mat4 and uniform float/vec2/vec3/vec4 lowering are supported");
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
                                     uniform_type == RINGL_FLOAT ? 1u
                                     : uniform_type == RINGL_FLOAT_VEC2 ? 2u
                                     : uniform_type == RINGL_FLOAT_VEC3 ? 3u
                                     : uniform_type == RINGL_FLOAT_VEC4 ? 4u : 16u)) == NULL ||
                !initialize_uniform(lower, symbol, &name, uniform_type)) {
                return 0;
            }
            next(lower);
            if (!need(lower, T_SEMI, "expected ';' after uniform"))
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
                    lower->token.kind == T_VEC4) {
                    uint8_t width = lower->token.kind == T_FLOAT ? 1u
                        : lower->token.kind == T_VEC2 ? 2u
                        : lower->token.kind == T_VEC3 ? 3u : 4u;
                    if (!local_decl(lower, width))
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
 * occupies outputs 0..3 and four interpolants occupy outputs 4..7.  GLSL ES
 * shaders without varyings still need to provide the latter slots so that a
 * constant fragment shader can execute through the same pipeline.  Preserve
 * scalar lowering as-is; it is used by the standalone IR API rather than the
 * graphics pipeline.
 */
static int append_raster_defaults(Lower* lower)
{
    uint16_t zero;
    uint16_t one;
    float one_value = 1.0f;
    uint32_t one_bits;

    if (lower->shader_type != RINGL_VERTEX_SHADER ||
        lower->output_count != 4u) {
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
        !emit(lower, RINGL_RSH1_OP_STORE_OUTPUT_F32, RINGL_RSH1_UNUSED,
              zero, RINGL_RSH1_UNUSED, 4u) ||
        !emit(lower, RINGL_RSH1_OP_STORE_OUTPUT_F32, RINGL_RSH1_UNUSED,
              zero, RINGL_RSH1_UNUSED, 5u) ||
        !emit(lower, RINGL_RSH1_OP_STORE_OUTPUT_F32, RINGL_RSH1_UNUSED,
              zero, RINGL_RSH1_UNUSED, 6u) ||
        !emit(lower, RINGL_RSH1_OP_STORE_OUTPUT_F32, RINGL_RSH1_UNUSED,
              one, RINGL_RSH1_UNUSED, 7u)) {
        return 0;
    }
    lower->output_count = 8u;
    return 1;
}

/* Emit type-bearing loads for the fixed interpolant ABI.  They are unused by
 * constant fragment shaders, but RinGPU validates every declared input when
 * it builds a native graphics pipeline. */
static int append_fragment_interpolant_inputs(Lower* lower)
{
    uint32_t index;

    if (lower->shader_type != RINGL_FRAGMENT_SHADER ||
        lower->output_count != 4u || lower->next_input != 0u) {
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
