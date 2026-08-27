/* SPDX-License-Identifier: MIT */
#include "glsl_parser.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ringl/ringl.h>

typedef enum TokenKind {
    TOK_EOF = 0,
    TOK_IDENT,
    TOK_NUMBER,
    TOK_VOID,
    TOK_FLOAT,
    TOK_VEC2,
    TOK_VEC3,
    TOK_VEC4,
    TOK_MAT2,
    TOK_MAT3,
    TOK_MAT4,
    TOK_INT,
    TOK_BOOL,
    TOK_TRUE,
    TOK_FALSE,
    TOK_BVEC2,
    TOK_BVEC3,
    TOK_BVEC4,
    TOK_IVEC2,
    TOK_IVEC3,
    TOK_IVEC4,
    TOK_SAMPLER2D,
    TOK_ATTRIBUTE,
    TOK_UNIFORM,
    TOK_VARYING,
    TOK_CONST,
    TOK_PRECISION,
    TOK_LOWP,
    TOK_MEDIUMP,
    TOK_HIGHP,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_SEMI,
    TOK_COMMA,
    TOK_ASSIGN,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_DOT,
    TOK_HASH,
    TOK_COLON,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_IF,
    TOK_ELSE,
    TOK_EQ,
    TOK_NE,
    TOK_LT,
    TOK_LE,
    TOK_GT,
    TOK_GE,
    TOK_NOT,
    TOK_AND,
    TOK_XOR,
    TOK_OR,
    TOK_INVALID,
} TokenKind;

typedef enum SymbolKind {
    SYMBOL_VALUE = 0,
    SYMBOL_ATTRIBUTE = 1,
    SYMBOL_SAMPLER2D = 2,
    SYMBOL_VARYING = 3,
    SYMBOL_UNIFORM_VEC4 = 4,
    SYMBOL_UNIFORM_FLOAT = 5,
    SYMBOL_UNIFORM_VEC2 = 6,
    SYMBOL_UNIFORM_VEC3 = 7,
    SYMBOL_UNIFORM_MAT2 = 8,
    SYMBOL_UNIFORM_MAT3 = 9,
    SYMBOL_UNIFORM_MAT4 = 10,
    SYMBOL_UNIFORM_INT = 11,
    SYMBOL_UNIFORM_IVEC2 = 12,
    SYMBOL_UNIFORM_IVEC3 = 13,
    SYMBOL_UNIFORM_IVEC4 = 14,
    SYMBOL_UNIFORM_BOOL = 15,
    SYMBOL_UNIFORM_BVEC2 = 16,
    SYMBOL_UNIFORM_BVEC3 = 17,
    SYMBOL_UNIFORM_BVEC4 = 18,
} SymbolKind;

typedef struct Token {
    TokenKind kind;
    const char* begin;
    size_t length;
    uint32_t line;
} Token;

typedef struct Symbol {
    char name[64];
    uint32_t kind;
    uint32_t width;
    /* A declaration remains one source symbol. Array elements are expanded
     * only for linked WebGL reflection and location state. */
    uint32_t sampler_array_length;
    /* This bounded profile accepts a const int initialized by an integer
     * literal as an array constant-index-expression. */
    uint8_t constant_i32;
    int32_t constant_i32_value;
} Symbol;

typedef struct Parser {
    const char* source;
    size_t length;
    size_t offset;
    uint32_t line;
    Token token;
    uint32_t shader_type;
    uint32_t main_seen;
    Symbol symbols[RINGL_GLSL_MAX_SYMBOLS];
    uint32_t symbol_count;
    RinGLGlslParseResult* result;
} Parser;

static void fail(Parser* parser, const char* message)
{
    if (parser->result->diagnostic[0] == '\0') {
        (void)snprintf(parser->result->diagnostic,
                       sizeof(parser->result->diagnostic),
                       "line %u: %s", parser->token.line, message);
    }
}

static int token_is_ident(const Token* token, const char* text)
{
    size_t length = strlen(text);
    return token->kind == TOK_IDENT && token->length == length &&
           memcmp(token->begin, text, length) == 0;
}

static void skip_space(Parser* parser)
{
    while (parser->offset < parser->length) {
        char c = parser->source[parser->offset];
        if (c == ' ' || c == '\t' || c == '\r') {
            parser->offset++;
            continue;
        }
        if (c == '\n') {
            parser->offset++;
            parser->line++;
            continue;
        }
        if (c == '/' && parser->offset + 1u < parser->length &&
            parser->source[parser->offset + 1u] == '/') {
            parser->offset += 2u;
            while (parser->offset < parser->length &&
                   parser->source[parser->offset] != '\n')
                parser->offset++;
            continue;
        }
        break;
    }
}

static TokenKind keyword_kind(const char* begin, size_t length)
{
    if (length == 4u && memcmp(begin, "void", 4u) == 0)
        return TOK_VOID;
    if (length == 5u && memcmp(begin, "float", 5u) == 0)
        return TOK_FLOAT;
    if (length == 4u && memcmp(begin, "vec2", 4u) == 0)
        return TOK_VEC2;
    if (length == 4u && memcmp(begin, "vec3", 4u) == 0)
        return TOK_VEC3;
    if (length == 4u && memcmp(begin, "vec4", 4u) == 0)
        return TOK_VEC4;
    if (length == 4u && memcmp(begin, "mat2", 4u) == 0)
        return TOK_MAT2;
    if (length == 4u && memcmp(begin, "mat3", 4u) == 0)
        return TOK_MAT3;
    if (length == 4u && memcmp(begin, "mat4", 4u) == 0)
        return TOK_MAT4;
    if (length == 3u && memcmp(begin, "int", 3u) == 0)
        return TOK_INT;
    if (length == 4u && memcmp(begin, "bool", 4u) == 0)
        return TOK_BOOL;
    if (length == 4u && memcmp(begin, "true", 4u) == 0)
        return TOK_TRUE;
    if (length == 5u && memcmp(begin, "false", 5u) == 0)
        return TOK_FALSE;
    if (length == 5u && memcmp(begin, "bvec2", 5u) == 0)
        return TOK_BVEC2;
    if (length == 5u && memcmp(begin, "bvec3", 5u) == 0)
        return TOK_BVEC3;
    if (length == 5u && memcmp(begin, "bvec4", 5u) == 0)
        return TOK_BVEC4;
    if (length == 5u && memcmp(begin, "ivec2", 5u) == 0)
        return TOK_IVEC2;
    if (length == 5u && memcmp(begin, "ivec3", 5u) == 0)
        return TOK_IVEC3;
    if (length == 5u && memcmp(begin, "ivec4", 5u) == 0)
        return TOK_IVEC4;
    if (length == 9u && memcmp(begin, "sampler2D", 9u) == 0)
        return TOK_SAMPLER2D;
    if (length == 9u && memcmp(begin, "attribute", 9u) == 0)
        return TOK_ATTRIBUTE;
    if (length == 7u && memcmp(begin, "uniform", 7u) == 0)
        return TOK_UNIFORM;
    if (length == 7u && memcmp(begin, "varying", 7u) == 0)
        return TOK_VARYING;
    if (length == 5u && memcmp(begin, "const", 5u) == 0)
        return TOK_CONST;
    if (length == 9u && memcmp(begin, "precision", 9u) == 0)
        return TOK_PRECISION;
    if (length == 4u && memcmp(begin, "lowp", 4u) == 0)
        return TOK_LOWP;
    if (length == 7u && memcmp(begin, "mediump", 7u) == 0)
        return TOK_MEDIUMP;
    if (length == 5u && memcmp(begin, "highp", 5u) == 0)
        return TOK_HIGHP;
    if (length == 2u && memcmp(begin, "if", 2u) == 0)
        return TOK_IF;
    if (length == 4u && memcmp(begin, "else", 4u) == 0)
        return TOK_ELSE;
    return TOK_IDENT;
}

static void next_token(Parser* parser)
{
    Token token;
    char c;

    skip_space(parser);
    memset(&token, 0, sizeof(token));
    token.line = parser->line;
    token.begin = parser->source + parser->offset;

    if (parser->offset >= parser->length) {
        token.kind = TOK_EOF;
        parser->token = token;
        return;
    }

    c = parser->source[parser->offset++];
    if (isalpha((unsigned char)c) || c == '_') {
        size_t start = parser->offset - 1u;
        while (parser->offset < parser->length) {
            c = parser->source[parser->offset];
            if (!isalnum((unsigned char)c) && c != '_')
                break;
            parser->offset++;
        }
        token.begin = parser->source + start;
        token.length = parser->offset - start;
        token.kind = keyword_kind(token.begin, token.length);
        parser->token = token;
        return;
    }

    if (isdigit((unsigned char)c) ||
        (c == '.' && parser->offset < parser->length &&
         isdigit((unsigned char)parser->source[parser->offset]))) {
        size_t start = parser->offset - 1u;
        int dot_seen = c == '.';
        int exponent_seen = 0;

        while (parser->offset < parser->length) {
            c = parser->source[parser->offset];
            if (isdigit((unsigned char)c)) {
                parser->offset++;
                continue;
            }
            if (c == '.' && !dot_seen) {
                dot_seen = 1;
                parser->offset++;
                continue;
            }
            if ((c == 'e' || c == 'E') && !exponent_seen) {
                size_t exponent = parser->offset + 1u;

                exponent_seen = 1;
                if (exponent < parser->length &&
                    (parser->source[exponent] == '+' ||
                     parser->source[exponent] == '-')) {
                    ++exponent;
                }
                if (exponent >= parser->length ||
                    !isdigit((unsigned char)parser->source[exponent])) {
                    token.kind = TOK_INVALID;
                    token.begin = parser->source + start;
                    token.length = exponent - start;
                    parser->offset = exponent;
                    parser->token = token;
                    return;
                }
                parser->offset = exponent + 1u;
                continue;
            }
            break;
        }
        token.begin = parser->source + start;
        token.length = parser->offset - start;
        token.kind = TOK_NUMBER;
        parser->token = token;
        return;
    }

    token.length = 1u;
    switch (c) {
    case '(': token.kind = TOK_LPAREN; break;
    case ')': token.kind = TOK_RPAREN; break;
    case '{': token.kind = TOK_LBRACE; break;
    case '}': token.kind = TOK_RBRACE; break;
    case ';': token.kind = TOK_SEMI; break;
    case ',': token.kind = TOK_COMMA; break;
    case '=':
        if (parser->offset < parser->length &&
            parser->source[parser->offset] == '=') {
            parser->offset++;
            token.length = 2u;
            token.kind = TOK_EQ;
        } else {
            token.kind = TOK_ASSIGN;
        }
        break;
    case '!':
        if (parser->offset < parser->length &&
            parser->source[parser->offset] == '=') {
            parser->offset++;
            token.length = 2u;
            token.kind = TOK_NE;
        } else {
            token.kind = TOK_NOT;
        }
        break;
    case '&':
        if (parser->offset < parser->length &&
            parser->source[parser->offset] == '&') {
            parser->offset++;
            token.length = 2u;
            token.kind = TOK_AND;
        } else {
            token.kind = TOK_INVALID;
        }
        break;
    case '^':
        if (parser->offset < parser->length &&
            parser->source[parser->offset] == '^') {
            parser->offset++;
            token.length = 2u;
            token.kind = TOK_XOR;
        } else {
            token.kind = TOK_INVALID;
        }
        break;
    case '|':
        if (parser->offset < parser->length &&
            parser->source[parser->offset] == '|') {
            parser->offset++;
            token.length = 2u;
            token.kind = TOK_OR;
        } else {
            token.kind = TOK_INVALID;
        }
        break;
    case '<':
        if (parser->offset < parser->length &&
            parser->source[parser->offset] == '=') {
            parser->offset++;
            token.length = 2u;
            token.kind = TOK_LE;
        } else {
            token.kind = TOK_LT;
        }
        break;
    case '>':
        if (parser->offset < parser->length &&
            parser->source[parser->offset] == '=') {
            parser->offset++;
            token.length = 2u;
            token.kind = TOK_GE;
        } else {
            token.kind = TOK_GT;
        }
        break;
    case '+': token.kind = TOK_PLUS; break;
    case '-': token.kind = TOK_MINUS; break;
    case '*': token.kind = TOK_STAR; break;
    case '/': token.kind = TOK_SLASH; break;
    case '.': token.kind = TOK_DOT; break;
    case '#': token.kind = TOK_HASH; break;
    case ':': token.kind = TOK_COLON; break;
    case '[': token.kind = TOK_LBRACKET; break;
    case ']': token.kind = TOK_RBRACKET; break;
    default: token.kind = TOK_INVALID; break;
    }
    parser->token = token;
}

static int accept(Parser* parser, TokenKind kind)
{
    if (parser->token.kind != kind)
        return 0;
    next_token(parser);
    return 1;
}

static int expect(Parser* parser, TokenKind kind, const char* message)
{
    if (!accept(parser, kind)) {
        fail(parser, message);
        return 0;
    }
    return 1;
}

static Symbol* find_symbol(Parser* parser, const Token* token)
{
    uint32_t i;
    for (i = 0; i < parser->symbol_count; ++i) {
        size_t length = strlen(parser->symbols[i].name);
        if (length == token->length &&
            memcmp(parser->symbols[i].name, token->begin, length) == 0)
            return &parser->symbols[i];
    }
    return NULL;
}

static int symbol_exists(Parser* parser, const Token* token)
{
    return find_symbol(parser, token) != NULL;
}

static int add_symbol(Parser* parser, const Token* token,
                      uint32_t kind, uint32_t width)
{
    Symbol* symbol;
    if (token->length == 0u || token->length >= sizeof(parser->symbols[0].name)) {
        fail(parser, "identifier is too long");
        return 0;
    }
    if (symbol_exists(parser, token)) {
        fail(parser, "duplicate declaration");
        return 0;
    }
    if (parser->symbol_count >= RINGL_GLSL_MAX_SYMBOLS) {
        fail(parser, "too many declarations");
        return 0;
    }
    symbol = &parser->symbols[parser->symbol_count++];
    memcpy(symbol->name, token->begin, token->length);
    symbol->name[token->length] = '\0';
    symbol->kind = kind;
    symbol->width = width;
    symbol->sampler_array_length = 1u;
    symbol->constant_i32 = 0u;
    symbol->constant_i32_value = 0;
    return 1;
}

static int token_unsigned_integer(const Token* token, uint32_t* value_out)
{
    uint32_t value = 0u;
    size_t index;

    if (token == NULL || value_out == NULL || token->kind != TOK_NUMBER ||
        token->length == 0u)
        return 0;
    for (index = 0u; index < token->length; ++index) {
        uint32_t digit;

        if (token->begin[index] < '0' || token->begin[index] > '9')
            return 0;
        digit = (uint32_t)(token->begin[index] - '0');
        if (value > (UINT32_MAX - digit) / 10u)
            return 0;
        value = value * 10u + digit;
    }
    *value_out = value;
    return 1;
}

static int constant_i32_literal(Parser* parser, int32_t* value_out)
{
    uint32_t magnitude;
    int negative = 0;

    if (parser == NULL || value_out == NULL)
        return 0;
    if (accept(parser, TOK_MINUS))
        negative = 1;
    else
        (void)accept(parser, TOK_PLUS);
    if (!token_unsigned_integer(&parser->token, &magnitude) ||
        magnitude > (negative ? UINT32_C(2147483648) : INT32_MAX)) {
        fail(parser, "const int initializer must be an in-range integer literal");
        return 0;
    }
    if (negative && magnitude == UINT32_C(2147483648))
        *value_out = INT32_MIN;
    else
        *value_out = negative ? -(int32_t)magnitude : (int32_t)magnitude;
    next_token(parser);
    return 1;
}

/* WebGL 1 requires support for constant-index-expressions in the fragment
 * profile. Keep the accepted subset explicit: an unsigned decimal literal or
 * a local/global const int whose initializer was such a literal. */
static int uniform_array_constant_index(Parser* parser, uint32_t array_length,
                                        uint32_t* index_out)
{
    Symbol* index_symbol;
    uint32_t index;

    if (parser == NULL || index_out == NULL || array_length == 0u ||
        !expect(parser, TOK_LBRACKET,
                "uniform array requires a constant integer index")) {
        return 0;
    }
    if (token_unsigned_integer(&parser->token, &index)) {
        next_token(parser);
    } else if (parser->token.kind == TOK_IDENT &&
               (index_symbol = find_symbol(parser, &parser->token)) != NULL &&
               index_symbol->constant_i32 != 0u &&
               index_symbol->constant_i32_value >= 0) {
        index = (uint32_t)index_symbol->constant_i32_value;
        next_token(parser);
    } else {
        fail(parser, "uniform array requires a non-negative constant integer index");
        return 0;
    }
    if (index >= array_length) {
        fail(parser, "uniform array index is outside the declared range");
        return 0;
    }
    if (!expect(parser, TOK_RBRACKET,
                "expected ']' after uniform array index")) {
        return 0;
    }
    *index_out = index;
    return 1;
}

static int sampler_array_element_name(char* destination, size_t destination_size,
                                      const Token* base_name,
                                      uint32_t element_index)
{
    int written;

    if (destination == NULL || destination_size == 0u || base_name == NULL ||
        base_name->length == 0u || base_name->length >= destination_size)
        return 0;
    written = snprintf(destination, destination_size, "%.*s[%u]",
                       (int)base_name->length, base_name->begin, element_index);
    return written >= 0 && (size_t)written < destination_size;
}

/* Keep every supported uniform-array declaration in the same bounded
 * representation: the source symbol retains its aggregate length, while the
 * parser publishes contiguous element names for WebGL reflection/location
 * state. `names` is the first byte of fixed RINGL_GLSL_NAME_MAX-byte slots. */
static int uniform_array_declaration(Parser* parser, const Token* name,
                                     SymbolKind kind, uint32_t width,
                                     char* names, uint32_t* count,
                                     uint32_t capacity,
                                     const char* type_name)
{
    uint32_t array_length = 1u;
    uint32_t index;
    Symbol* symbol;

    if (parser == NULL || name == NULL || names == NULL || count == NULL ||
        type_name == NULL || *count > capacity)
        return 0;
    next_token(parser);
    if (accept(parser, TOK_LBRACKET)) {
        if (!token_unsigned_integer(&parser->token, &array_length) ||
            array_length == 0u || array_length > capacity ||
            !expect(parser, TOK_NUMBER,
                    "uniform array length must be a positive integer") ||
            !expect(parser, TOK_RBRACKET,
                    "expected ']' after uniform array length")) {
            fail(parser, "uniform array length is outside the supported range");
            return 0;
        }
    }
    if (array_length > capacity - *count) {
        fail(parser, "too many uniform array elements");
        return 0;
    }
    if (!add_symbol(parser, name, kind, width))
        return 0;
    symbol = find_symbol(parser, name);
    if (symbol == NULL)
        return 0;
    symbol->sampler_array_length = array_length;
    for (index = 0u; index < array_length; ++index) {
        char* reflected_name = names + (*count + index) * RINGL_GLSL_NAME_MAX;

        if (array_length == 1u) {
            memcpy(reflected_name, name->begin, name->length);
            reflected_name[name->length] = '\0';
        } else if (!sampler_array_element_name(
                       reflected_name, RINGL_GLSL_NAME_MAX, name, index)) {
            fail(parser, "uniform array name is too long");
            return 0;
        }
    }
    *count += array_length;
    if (!expect(parser, TOK_SEMI, "expected ';' after uniform"))
        return 0;
    parser->result->declaration_count++;
    (void)type_name;
    return 1;
}

static int expression(Parser* parser);

static int constructor(Parser* parser, TokenKind kind)
{
    uint32_t component_count = 0u;
    uint32_t target_components = kind == TOK_VEC2 || kind == TOK_IVEC2 ||
                                         kind == TOK_BVEC2 ? 2u :
                                 kind == TOK_VEC3 || kind == TOK_IVEC3 ||
                                         kind == TOK_BVEC3 ? 3u : 4u;

    next_token(parser);
    if (!expect(parser, TOK_LPAREN, "expected '(' after vector constructor"))
        return 0;
    if (parser->token.kind == TOK_RPAREN) {
        fail(parser, "vector constructor requires arguments");
        return 0;
    }
    for (;;) {
        if (!expression(parser))
            return 0;
        component_count++;
        if (component_count > target_components) {
            fail(parser, "too many vector constructor arguments");
            return 0;
        }
        if (!accept(parser, TOK_COMMA))
            break;
    }
    return expect(parser, TOK_RPAREN, "expected ')' after vector constructor");
}

static int finite_number(Parser* parser)
{
    char text[64];
    char* parsed_end;
    float value;

    if (parser->token.kind == TOK_PLUS || parser->token.kind == TOK_MINUS) {
        text[0] = parser->token.kind == TOK_MINUS ? '-' : '+';
        next_token(parser);
        if (parser->token.kind != TOK_NUMBER ||
            parser->token.length + 1u >= sizeof(text)) {
            fail(parser, "expected finite numeric literal");
            return 0;
        }
        memcpy(text + 1u, parser->token.begin, parser->token.length);
        text[parser->token.length + 1u] = '\0';
    } else {
        if (parser->token.kind != TOK_NUMBER ||
            parser->token.length >= sizeof(text)) {
            fail(parser, "expected finite numeric literal");
            return 0;
        }
        memcpy(text, parser->token.begin, parser->token.length);
        text[parser->token.length] = '\0';
    }
    value = strtof(text, &parsed_end);
    if (parsed_end == text || *parsed_end != '\0' || !isfinite(value)) {
        fail(parser, "expected finite numeric literal");
        return 0;
    }
    next_token(parser);
    return 1;
}

/* texture2D needs a vec2 after every read selector.  The bounded RSH1
 * lowerer keeps the two selected components as scalar register sources, so
 * accept only selectors that preserve vec2 width here. */
static int texture2d_coordinate_swizzle(Parser* parser)
{
    while (accept(parser, TOK_DOT)) {
        Token swizzle = parser->token;
        uint8_t family = 0u;
        size_t index;

        if (swizzle.kind != TOK_IDENT || swizzle.length != 2u) {
            fail(parser, "texture2D coordinate selection must be vec2");
            return 0;
        }
        for (index = 0u; index < swizzle.length; ++index) {
            uint8_t component_family;
            uint32_t component_index;

            switch (swizzle.begin[index]) {
            case 'x': component_family = 1u; component_index = 0u; break;
            case 'y': component_family = 1u; component_index = 1u; break;
            case 'r': component_family = 2u; component_index = 0u; break;
            case 'g': component_family = 2u; component_index = 1u; break;
            case 's': component_family = 3u; component_index = 0u; break;
            case 't': component_family = 3u; component_index = 1u; break;
            default:
                fail(parser, "texture2D coordinate selection is outside vec2");
                return 0;
            }
            if ((family != 0u && family != component_family) ||
                component_index >= 2u) {
                fail(parser, "invalid texture2D coordinate selection");
                return 0;
            }
            family = component_family;
        }
        next_token(parser);
    }
    return 1;
}

/* The lowerer owns the exact component and type accounting.  The parser
 * admits a bounded matrix argument list so it remains syntactically aligned
 * with matN constructors without pretending that a vector argument is one
 * scalar component. */
static int matrix_constructor(Parser* parser)
{
    uint32_t argument_count = 0u;

    next_token(parser);
    if (!expect(parser, TOK_LPAREN, "expected '(' after matrix constructor"))
        return 0;
    if (parser->token.kind == TOK_RPAREN) {
        fail(parser, "matrix constructor requires arguments");
        return 0;
    }
    for (;;) {
        if (++argument_count > 16u || !expression(parser)) {
            if (argument_count > 16u)
                fail(parser, "too many matrix constructor arguments");
            return 0;
        }
        if (!accept(parser, TOK_COMMA))
            break;
    }
    return expect(parser, TOK_RPAREN, "expected ')' after matrix constructor");
}

static int varying_vec2_offset(Parser* parser)
{
    if (parser->token.kind != TOK_PLUS && parser->token.kind != TOK_MINUS &&
        parser->token.kind != TOK_STAR && parser->token.kind != TOK_SLASH)
        return 1;
    next_token(parser);
    if (parser->token.kind == TOK_IDENT) {
        Symbol* coordinate = find_symbol(parser, &parser->token);

        if (coordinate == NULL || coordinate->width != 2u ||
            (coordinate->kind != SYMBOL_VARYING &&
             coordinate->kind != SYMBOL_UNIFORM_VEC2)) {
            fail(parser,
                 "texture2D varying operation requires vec2 varying or uniform");
            return 0;
        }
        next_token(parser);
        if (coordinate->kind == SYMBOL_UNIFORM_VEC2) {
            return texture2d_coordinate_swizzle(parser);
        }
        if (!texture2d_coordinate_swizzle(parser))
            return 0;
        return 1;
    }
    if (!expect(parser, TOK_VEC2,
                "texture2D varying offset must use vec2")) {
        return 0;
    }
    if (!expect(parser, TOK_LPAREN,
                "expected '(' after texture2D varying offset vec2") ||
        !finite_number(parser) ||
        !expect(parser, TOK_COMMA,
                "expected ',' in texture2D varying offset vec2") ||
        !finite_number(parser) ||
        !expect(parser, TOK_RPAREN,
                "expected ')' after texture2D varying offset vec2")) {
        return 0;
    }
    return 1;
}

static int texture2d_call(Parser* parser, int explicit_lod, int projected)
{
    Token sampler_name;
    Symbol* sampler;
    uint32_t sampler_index = 0u;

    if (parser->shader_type != RINGL_FRAGMENT_SHADER) {
        fail(parser, projected ? "texture2DProj is only supported in fragment shaders"
                               : "texture2D is only supported in fragment shaders");
        return 0;
    }
    if (explicit_lod && parser->result->shader_texture_lod_enabled == 0u) {
        fail(parser, "texture2DLodEXT requires GL_EXT_shader_texture_lod");
        return 0;
    }
    if (explicit_lod)
        parser->result->uses_shader_texture_lod = 1u;
    next_token(parser);
    if (!expect(parser, TOK_LPAREN, "expected '(' after texture2D"))
        return 0;
    if (parser->token.kind != TOK_IDENT) {
        fail(parser, "texture2D requires a sampler2D uniform");
        return 0;
    }
    sampler_name = parser->token;
    sampler = find_symbol(parser, &sampler_name);
    if (sampler == NULL || sampler->kind != SYMBOL_SAMPLER2D) {
        fail(parser, "texture2D first argument must be sampler2D");
        return 0;
    }
    next_token(parser);
    if (sampler->sampler_array_length > 1u) {
        if (!uniform_array_constant_index(parser,
                                          sampler->sampler_array_length,
                                          &sampler_index)) {
            return 0;
        }
    } else if (parser->token.kind == TOK_LBRACKET) {
        fail(parser, "texture2D scalar sampler cannot be indexed");
        return 0;
    }
    if (!expect(parser, TOK_COMMA, "expected ',' after texture2D sampler"))
        return 0;

    /* The generic lowerer owns the projected-coordinate type check and emits
     * the two real RSH1 divisions before sampling. Unlike the historical
     * shape-specific texture2D parser, accept its normal expression grammar
     * here so locals, varyings, swizzles, arithmetic, and uniforms reach that
     * one executable path. */
    if (projected) {
        if (!expression(parser))
            return 0;
        return expect(parser, TOK_RPAREN,
                      "expected ')' after texture2DProj arguments");
    }

    if (parser->token.kind == TOK_VEC2) {
        if (!constructor(parser, TOK_VEC2))
            return 0;
    } else if (parser->token.kind == TOK_IDENT &&
               token_is_ident(&parser->token, "gl_PointCoord")) {
        /* This exact point-sprite coordinate form is lowered to RSH1 builtin
         * loads, not a declared varying. Do not accept swizzles or offsets
         * here until the bounded texture lowerer can execute those forms. */
        next_token(parser);
    } else if (parser->token.kind == TOK_IDENT &&
               token_is_ident(&parser->token, "gl_FragCoord")) {
        /* Keep this parser admission exactly aligned with texture_lower.c:
         * only a finite explicit normalization is useful as a WebGL texture
         * coordinate, and the lowering owns both builtin loads and divides. */
        next_token(parser);
        if (!expect(parser, TOK_DOT,
                    "texture2D gl_FragCoord coordinate requires '.xy'") ||
            parser->token.kind != TOK_IDENT || parser->token.length != 2u ||
            memcmp(parser->token.begin, "xy", 2u) != 0) {
            fail(parser,
                 "texture2D gl_FragCoord coordinate requires '.xy'");
            return 0;
        }
        next_token(parser);
        if (!expect(parser, TOK_SLASH,
                    "texture2D gl_FragCoord coordinate requires '/ vec2(...)'") ||
            !expect(parser, TOK_VEC2,
                    "texture2D gl_FragCoord coordinate requires vec2") ||
            !expect(parser, TOK_LPAREN,
                    "expected '(' after gl_FragCoord normalization") ||
            !finite_number(parser) ||
            !expect(parser, TOK_COMMA,
                    "expected ',' in gl_FragCoord normalization") ||
            !finite_number(parser) ||
            !expect(parser, TOK_RPAREN,
                    "expected ')' after gl_FragCoord normalization")) {
            return 0;
        }
    } else if (parser->token.kind == TOK_IDENT) {
        Symbol* coordinate = find_symbol(parser, &parser->token);
        if (coordinate == NULL || coordinate->width != 2u ||
            (coordinate->kind != SYMBOL_VARYING &&
             coordinate->kind != SYMBOL_VALUE &&
             coordinate->kind != SYMBOL_UNIFORM_VEC2)) {
            fail(parser, "texture2D coordinate must be vec2");
            return 0;
        }
        next_token(parser);
        if (!texture2d_coordinate_swizzle(parser))
            return 0;
        if (coordinate->kind == SYMBOL_UNIFORM_VEC2) {
            Symbol* right_coordinate;

            if (parser->token.kind != TOK_PLUS &&
                parser->token.kind != TOK_MINUS &&
                parser->token.kind != TOK_STAR &&
                parser->token.kind != TOK_SLASH) {
                fail(parser,
                     "texture2D uniform coordinate requires +, -, *, or / vec2");
                return 0;
            }
            next_token(parser);
            if (parser->token.kind != TOK_IDENT) {
                fail(parser, "texture2D uniform coordinate requires vec2");
                return 0;
            }
            right_coordinate = find_symbol(parser, &parser->token);
            if (right_coordinate == NULL || right_coordinate->width != 2u ||
                (right_coordinate->kind != SYMBOL_VARYING &&
                 right_coordinate->kind != SYMBOL_VALUE)) {
                fail(parser, "texture2D uniform coordinate requires vec2");
                return 0;
            }
            next_token(parser);
            if (!texture2d_coordinate_swizzle(parser))
                return 0;
        } else if (!varying_vec2_offset(parser)) {
            return 0;
        }
    } else {
        fail(parser, "texture2D coordinate must be vec2");
        return 0;
    }
    if (explicit_lod) {
        if (!expect(parser, TOK_COMMA,
                    "expected ',' before texture2DLodEXT level") ||
            !expression(parser)) {
            return 0;
        }
    }
    return expect(parser, TOK_RPAREN,
                  explicit_lod ? "expected ')' after texture2DLodEXT arguments"
                               : "expected ')' after texture2D arguments");
}

/* The lowering stage owns the exact scalar/vector type checks. Keep parser
 * admission in sync with its fixed builtin arity so malformed calls fail at
 * compile time rather than leaking into a later declaration or assignment. */
static int common_math_builtin_call(Parser* parser, uint32_t argument_count)
{
    uint32_t index;

    next_token(parser);
    if (!expect(parser, TOK_LPAREN, "expected '(' after math builtin"))
        return 0;
    for (index = 0u; index < argument_count; ++index) {
        if (!expression(parser))
            return 0;
        if (index + 1u < argument_count &&
            !expect(parser, TOK_COMMA, "expected ',' in math builtin")) {
            return 0;
        }
    }
    return expect(parser, TOK_RPAREN, "expected ')' after math builtin");
}

static int atan_builtin_call(Parser* parser)
{
    next_token(parser);
    if (!expect(parser, TOK_LPAREN, "expected '(' after atan"))
        return 0;
    if (!expression(parser))
        return 0;
    if (accept(parser, TOK_COMMA) && !expression(parser))
        return 0;
    return expect(parser, TOK_RPAREN, "expected ')' after atan arguments");
}

static int binary_math_builtin_call(Parser* parser, const char* name)
{
    next_token(parser);
    if (!expect(parser, TOK_LPAREN, "expected '(' after math builtin") ||
        !expression(parser) ||
        !expect(parser, TOK_COMMA, "expected ',' in math builtin") ||
        !expression(parser)) {
        return 0;
    }
    return expect(parser, TOK_RPAREN, name);
}

static int primary(Parser* parser)
{
    if (accept(parser, TOK_NUMBER) || accept(parser, TOK_TRUE) ||
        accept(parser, TOK_FALSE))
        return 1;
    if (parser->token.kind == TOK_FLOAT || parser->token.kind == TOK_INT) {
        next_token(parser);
        if (!expect(parser, TOK_LPAREN, "expected '(' after scalar conversion") ||
            !expression(parser))
            return 0;
        return expect(parser, TOK_RPAREN, "expected ')' after scalar conversion");
    }
    if (parser->token.kind == TOK_VEC2 || parser->token.kind == TOK_VEC3 ||
        parser->token.kind == TOK_VEC4 || parser->token.kind == TOK_IVEC2 ||
        parser->token.kind == TOK_IVEC3 || parser->token.kind == TOK_IVEC4 ||
        parser->token.kind == TOK_BVEC2 || parser->token.kind == TOK_BVEC3 ||
        parser->token.kind == TOK_BVEC4)
        return constructor(parser, parser->token.kind);
    if (parser->token.kind == TOK_MAT2 || parser->token.kind == TOK_MAT3 ||
        parser->token.kind == TOK_MAT4)
        return matrix_constructor(parser);
    if (parser->token.kind == TOK_IDENT) {
        Token ident = parser->token;
        Symbol* symbol = find_symbol(parser, &ident);
        uint32_t value_width = symbol ? symbol->width : 0u;
        if (token_is_ident(&ident, "texture2D"))
            return texture2d_call(parser, 0, 0);
        if (token_is_ident(&ident, "texture2DLodEXT"))
            return texture2d_call(parser, 1, 0);
        if (token_is_ident(&ident, "texture2DProj"))
            return texture2d_call(parser, 0, 1);
        if (token_is_ident(&ident, "equal") ||
            token_is_ident(&ident, "notEqual"))
            return common_math_builtin_call(parser, 2u);
        if (token_is_ident(&ident, "not") || token_is_ident(&ident, "any") ||
            token_is_ident(&ident, "all"))
            return common_math_builtin_call(parser, 1u);
        if (token_is_ident(&ident, "min") || token_is_ident(&ident, "max") ||
            token_is_ident(&ident, "dot") || token_is_ident(&ident, "mod") ||
            token_is_ident(&ident, "step") || token_is_ident(&ident, "distance") ||
            token_is_ident(&ident, "cross") || token_is_ident(&ident, "reflect"))
            return common_math_builtin_call(parser, 2u);
        if (token_is_ident(&ident, "clamp") || token_is_ident(&ident, "mix") ||
            token_is_ident(&ident, "smoothstep") ||
            token_is_ident(&ident, "faceforward") ||
            token_is_ident(&ident, "refract"))
            return common_math_builtin_call(parser, 3u);
        if (token_is_ident(&ident, "floor") || token_is_ident(&ident, "ceil") ||
            token_is_ident(&ident, "fract") || token_is_ident(&ident, "abs") ||
            token_is_ident(&ident, "sign") || token_is_ident(&ident, "sqrt") ||
            token_is_ident(&ident, "inversesqrt") ||
            token_is_ident(&ident, "length") || token_is_ident(&ident, "normalize") ||
            token_is_ident(&ident, "radians") || token_is_ident(&ident, "degrees") ||
            token_is_ident(&ident, "sin") || token_is_ident(&ident, "cos") ||
            token_is_ident(&ident, "tan") || token_is_ident(&ident, "asin") ||
            token_is_ident(&ident, "acos") || token_is_ident(&ident, "exp") ||
            token_is_ident(&ident, "log") || token_is_ident(&ident, "exp2") ||
            token_is_ident(&ident, "log2"))
            return common_math_builtin_call(parser, 1u);
        if (token_is_ident(&ident, "atan"))
            return atan_builtin_call(parser);
        if (token_is_ident(&ident, "pow"))
            return binary_math_builtin_call(parser,
                                            "expected ')' after pow arguments");
        if (token_is_ident(&ident, "matrixCompMult"))
            return binary_math_builtin_call(
                parser, "expected ')' after matrixCompMult arguments");
        if (token_is_ident(&ident, "dFdx") ||
            token_is_ident(&ident, "dFdy") ||
            token_is_ident(&ident, "fwidth")) {
            if (parser->shader_type != RINGL_FRAGMENT_SHADER) {
                fail(parser, "derivatives are only supported in fragment shaders");
                return 0;
            }
            if (parser->result->standard_derivatives_enabled == 0u) {
                fail(parser, "derivatives require GL_OES_standard_derivatives");
                return 0;
            }
            parser->result->uses_standard_derivatives = 1u;
            next_token(parser);
            if (!expect(parser, TOK_LPAREN, "expected '(' after derivative builtin") ||
                !expression(parser) ||
                !expect(parser, TOK_RPAREN, "expected ')' after derivative builtin")) {
                return 0;
            }
            return 1;
        }
        if (token_is_ident(&ident, "gl_PointSize") &&
            parser->shader_type != RINGL_VERTEX_SHADER) {
            fail(parser, "gl_PointSize is only available in vertex shaders");
            return 0;
        }
        if (token_is_ident(&ident, "gl_PointCoord")) {
            if (parser->shader_type != RINGL_FRAGMENT_SHADER) {
                fail(parser, "gl_PointCoord is only available in fragment shaders");
                return 0;
            }
            value_width = 2u;
        }
        if (token_is_ident(&ident, "gl_FragCoord")) {
            if (parser->shader_type != RINGL_FRAGMENT_SHADER) {
                fail(parser, "gl_FragCoord is only available in fragment shaders");
                return 0;
            }
            value_width = 4u;
        }
        if (!token_is_ident(&ident, "gl_Position") &&
            !token_is_ident(&ident, "gl_FragColor") &&
            !token_is_ident(&ident, "gl_PointSize") &&
            !token_is_ident(&ident, "gl_PointCoord") &&
            !token_is_ident(&ident, "gl_FragCoord") &&
            !symbol_exists(parser, &ident)) {
            fail(parser, "use of undeclared identifier");
            return 0;
        }
        next_token(parser);
        if (symbol != NULL && symbol->sampler_array_length > 1u) {
            uint32_t array_index;

            if (!uniform_array_constant_index(parser,
                                              symbol->sampler_array_length,
                                              &array_index)) {
                return 0;
            }
        } else if (parser->token.kind == TOK_LBRACKET) {
            fail(parser, "scalar value cannot be indexed");
            return 0;
        }
        while (accept(parser, TOK_DOT)) {
            Token swizzle = parser->token;
            uint8_t family = 0u;
            size_t index;

            /* Keep compile-time admission aligned with the RSH1 lowerer.
             * Mixed selector families are not legal GLSL, even though their
             * individual letters are valid in another family. */
            if (swizzle.kind != TOK_IDENT || swizzle.length == 0u ||
                swizzle.length > 4u) {
                fail(parser, "invalid vector component selection");
                return 0;
            }
            for (index = 0u; index < swizzle.length; ++index) {
                uint8_t component_family;
                uint32_t component_index;

                switch (swizzle.begin[index]) {
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
                    fail(parser, "invalid vector component selection");
                    return 0;
                }
                if (family != 0u && family != component_family) {
                    fail(parser, "mixed vector component selection");
                    return 0;
                }
                family = component_family;
                if (value_width != 0u && component_index >= value_width) {
                    fail(parser, "vector component is outside the declared width");
                    return 0;
                }
            }
            value_width = (uint32_t)swizzle.length;
            next_token(parser);
        }
        return 1;
    }
    if (accept(parser, TOK_LPAREN)) {
        if (!expression(parser))
            return 0;
        return expect(parser, TOK_RPAREN, "expected ')' after expression");
    }
    fail(parser, "expected expression");
    return 0;
}

static int unary(Parser* parser)
{
    if (accept(parser, TOK_PLUS) || accept(parser, TOK_MINUS))
        return unary(parser);
    return primary(parser);
}

static int multiplicative(Parser* parser)
{
    if (!unary(parser))
        return 0;
    while (parser->token.kind == TOK_STAR || parser->token.kind == TOK_SLASH) {
        next_token(parser);
        if (!unary(parser))
            return 0;
    }
    return 1;
}

static int expression(Parser* parser)
{
    if (!multiplicative(parser))
        return 0;
    while (parser->token.kind == TOK_PLUS || parser->token.kind == TOK_MINUS) {
        next_token(parser);
        if (!multiplicative(parser))
            return 0;
    }
    return 1;
}

/* A GLSL read selector may repeat components, but a writable selector must
 * designate each target component exactly once. Keep this parser admission in
 * lockstep with the RSH1 lowerer so a shader that compiles never becomes an
 * ambiguous set of native output stores during link. */
static int writable_lvalue_swizzle(Parser* parser, uint8_t vector_width)
{
    Token swizzle;
    uint8_t family = 0u;
    uint16_t selected = 0u;
    size_t index;

    if (parser == NULL || !accept(parser, TOK_DOT))
        return 0;
    swizzle = parser->token;
    if (swizzle.kind != TOK_IDENT || swizzle.length == 0u ||
        swizzle.length > 4u || vector_width < 2u || vector_width > 4u) {
        fail(parser, "invalid writable vector component selection");
        return 0;
    }
    for (index = 0u; index < swizzle.length; ++index) {
        uint8_t component_family;
        uint8_t component;

        switch (swizzle.begin[index]) {
        case 'x': component_family = 1u; component = 0u; break;
        case 'y': component_family = 1u; component = 1u; break;
        case 'z': component_family = 1u; component = 2u; break;
        case 'w': component_family = 1u; component = 3u; break;
        case 'r': component_family = 2u; component = 0u; break;
        case 'g': component_family = 2u; component = 1u; break;
        case 'b': component_family = 2u; component = 2u; break;
        case 'a': component_family = 2u; component = 3u; break;
        case 's': component_family = 3u; component = 0u; break;
        case 't': component_family = 3u; component = 1u; break;
        case 'p': component_family = 3u; component = 2u; break;
        case 'q': component_family = 3u; component = 3u; break;
        default:
            fail(parser, "invalid writable vector component selection");
            return 0;
        }
        if ((family != 0u && family != component_family) ||
            component >= vector_width ||
            (selected & (uint16_t)(UINT32_C(1) << component)) != 0u) {
            fail(parser, "invalid writable vector component selection");
            return 0;
        }
        family = component_family;
        selected |= (uint16_t)(UINT32_C(1) << component);
    }
    next_token(parser);
    return 1;
}

static int assignment(Parser* parser)
{
    Token target = parser->token;
    Symbol* symbol = NULL;
    uint8_t writable_vector_width = 0u;
    int frag_data = 0;
    int frag_depth = 0;
    if (target.kind != TOK_IDENT) {
        fail(parser, "expected assignment target");
        return 0;
    }
    if (token_is_ident(&target, "gl_Position")) {
        if (parser->shader_type != RINGL_VERTEX_SHADER) {
            fail(parser, "gl_Position is only writable in vertex shaders");
            return 0;
        }
        writable_vector_width = 4u;
    } else if (token_is_ident(&target, "gl_PointSize")) {
        if (parser->shader_type != RINGL_VERTEX_SHADER) {
            fail(parser, "gl_PointSize is only writable in vertex shaders");
            return 0;
        }
    } else if (token_is_ident(&target, "gl_FragColor")) {
        if (parser->shader_type != RINGL_FRAGMENT_SHADER) {
            fail(parser, "gl_FragColor is only writable in fragment shaders");
            return 0;
        }
        writable_vector_width = 4u;
    } else if (token_is_ident(&target, "gl_FragDepthEXT")) {
        if (parser->shader_type != RINGL_FRAGMENT_SHADER ||
            parser->result->frag_depth_enabled == 0u) {
            fail(parser, "gl_FragDepthEXT requires GL_EXT_frag_depth");
            return 0;
        }
        frag_depth = 1;
    } else if (token_is_ident(&target, "gl_FragData")) {
        if (parser->shader_type != RINGL_FRAGMENT_SHADER ||
            parser->result->draw_buffers_enabled == 0u) {
            fail(parser, "gl_FragData requires GL_EXT_draw_buffers");
            return 0;
        }
        frag_data = 1;
    } else {
        symbol = find_symbol(parser, &target);
        if (symbol == NULL) {
            fail(parser, "assignment to undeclared identifier");
            return 0;
        }
        if (symbol->kind == SYMBOL_SAMPLER2D ||
            symbol->kind == SYMBOL_UNIFORM_FLOAT ||
            symbol->kind == SYMBOL_UNIFORM_INT ||
            symbol->kind == SYMBOL_UNIFORM_BOOL ||
            symbol->kind == SYMBOL_UNIFORM_VEC2 ||
            symbol->kind == SYMBOL_UNIFORM_IVEC2 ||
            symbol->kind == SYMBOL_UNIFORM_VEC3 ||
            symbol->kind == SYMBOL_UNIFORM_IVEC3 ||
            symbol->kind == SYMBOL_UNIFORM_VEC4 ||
            symbol->kind == SYMBOL_UNIFORM_IVEC4 ||
            symbol->kind == SYMBOL_UNIFORM_MAT2 ||
            symbol->kind == SYMBOL_UNIFORM_MAT3 ||
            symbol->kind == SYMBOL_UNIFORM_MAT4) {
            fail(parser, "uniforms are read-only");
            return 0;
        }
        if (symbol->constant_i32 != 0u) {
            fail(parser, "const int is read-only");
            return 0;
        }
        if (symbol->kind == SYMBOL_VARYING &&
            parser->shader_type != RINGL_VERTEX_SHADER) {
            fail(parser, "varyings are read-only in fragment shaders");
            return 0;
        }
    }
    next_token(parser);
    if (symbol != NULL)
        writable_vector_width = symbol->width;
    if (writable_vector_width != 0u && parser->token.kind == TOK_DOT &&
        !writable_lvalue_swizzle(parser, writable_vector_width)) {
        return 0;
    }
    if (frag_data) {
        if (!expect(parser, TOK_LBRACKET,
                    "expected '[' after gl_FragData") ||
            parser->token.kind != TOK_NUMBER || parser->token.length != 1u ||
            parser->token.begin[0] < '0' ||
            parser->token.begin[0] >=
                (char)('0' + RINGL_MAX_COLOR_ATTACHMENTS)) {
            fail(parser, "gl_FragData index is outside the supported range");
            return 0;
        }
        next_token(parser);
        if (!expect(parser, TOK_RBRACKET,
                    "expected ']' after gl_FragData index")) {
            return 0;
        }
        parser->result->uses_webgl_draw_buffers = 1u;
        if (parser->token.kind == TOK_DOT &&
            !writable_lvalue_swizzle(parser, 4u)) {
            return 0;
        }
    }
    if (!expect(parser, TOK_ASSIGN, "expected '='"))
        return 0;
    if (!expression(parser))
        return 0;
    if (!expect(parser, TOK_SEMI, "expected ';' after assignment"))
        return 0;
    if (frag_depth)
        parser->result->uses_webgl_frag_depth = 1u;
    parser->result->statement_count++;
    return 1;
}

/* The lowerer makes the type and full-output checks. Keep this admission
 * grammar deliberately aligned with its executable control-flow slice: scalar
 * comparisons and scalar Boolean `!`, `&&`, `^^`, or `||`, one complete stage
 * output assignment per branch, or a fragment discard on exactly one branch,
 * and a mandatory else. General statements and nested branches remain
 * unsupported. */
static int conditional_output_assignment(Parser* parser)
{
    if (parser->token.kind != TOK_IDENT ||
        (parser->shader_type == RINGL_VERTEX_SHADER
             ? !token_is_ident(&parser->token, "gl_Position")
             : !token_is_ident(&parser->token, "gl_FragColor"))) {
        fail(parser, "if branches must assign the stage output");
        return 0;
    }
    return assignment(parser);
}

/* Conditional discard is intentionally narrower than a general statement
 * block. The opposite branch must still write the complete fragment output,
 * so no path can reach RETURN with a partially initialized color vector. */
static int conditional_branch(Parser* parser, int* discard_out)
{
    if (discard_out == NULL)
        return 0;
    *discard_out = 0;
    if (parser->token.kind == TOK_IDENT &&
        token_is_ident(&parser->token, "discard")) {
        if (parser->shader_type != RINGL_FRAGMENT_SHADER) {
            fail(parser, "discard is only available in fragment shaders");
            return 0;
        }
        next_token(parser);
        if (!expect(parser, TOK_SEMI, "expected ';' after discard"))
            return 0;
        *discard_out = 1;
        return 1;
    }
    return conditional_output_assignment(parser);
}

static int is_comparison_operator(TokenKind operator)
{
    return operator == TOK_EQ || operator == TOK_NE || operator == TOK_LT ||
           operator == TOK_LE || operator == TOK_GT || operator == TOK_GE;
}

static int parenthesized_condition(Parser* parser)
{
    Parser probe = *parser;
    uint32_t depth = 0u;
    int condition_operator = 0;

    while (probe.token.kind != TOK_EOF) {
        if (probe.token.kind == TOK_LPAREN) {
            depth++;
        } else if (probe.token.kind == TOK_RPAREN) {
            if (depth == 0u)
                return 0;
            depth--;
            if (depth == 0u)
                return condition_operator;
        } else if (depth != 0u &&
                   (is_comparison_operator(probe.token.kind) ||
                    probe.token.kind == TOK_NOT || probe.token.kind == TOK_AND ||
                    probe.token.kind == TOK_XOR || probe.token.kind == TOK_OR)) {
            condition_operator = 1;
        }
        next_token(&probe);
    }
    return 0;
}

static int conditional_or_expression(Parser* parser);

static int conditional_primary_expression(Parser* parser)
{
    if (accept(parser, TOK_NOT))
        return conditional_primary_expression(parser);
    if (parser->token.kind == TOK_LPAREN && parenthesized_condition(parser)) {
        next_token(parser);
        if (!conditional_or_expression(parser))
            return 0;
        return expect(parser, TOK_RPAREN, "expected ')' after Boolean condition");
    }
    if (!expression(parser))
        return 0;
    if (is_comparison_operator(parser->token.kind)) {
        next_token(parser);
        return expression(parser);
    }
    return 1;
}

static int conditional_and_expression(Parser* parser)
{
    if (!conditional_primary_expression(parser))
        return 0;
    while (accept(parser, TOK_AND)) {
        if (!conditional_primary_expression(parser))
            return 0;
    }
    return 1;
}

static int conditional_xor_expression(Parser* parser)
{
    if (!conditional_and_expression(parser))
        return 0;
    while (accept(parser, TOK_XOR)) {
        if (!conditional_and_expression(parser))
            return 0;
    }
    return 1;
}

static int conditional_or_expression(Parser* parser)
{
    if (!conditional_xor_expression(parser))
        return 0;
    while (accept(parser, TOK_OR)) {
        if (!conditional_xor_expression(parser))
            return 0;
    }
    return 1;
}

static int conditional_statement(Parser* parser)
{
    int if_discards;
    int else_discards;

    next_token(parser);
    if (!expect(parser, TOK_LPAREN, "expected '(' after if") ||
        !conditional_or_expression(parser)) {
        return 0;
    }
    if (!expect(parser, TOK_RPAREN, "expected ')' after if condition") ||
        !expect(parser, TOK_LBRACE, "expected '{' after if condition") ||
        !conditional_branch(parser, &if_discards) ||
        !expect(parser, TOK_RBRACE, "expected '}' after if branch") ||
        !expect(parser, TOK_ELSE, "bounded if requires else branch") ||
        !expect(parser, TOK_LBRACE, "expected '{' after else") ||
        !conditional_branch(parser, &else_discards) ||
        !expect(parser, TOK_RBRACE, "expected '}' after else branch")) {
        return 0;
    }
    if (if_discards && else_discards) {
        fail(parser, "conditional discard requires an output branch");
        return 0;
    }
    return 1;
}

static int discard_statement(Parser* parser)
{
    if (parser->shader_type != RINGL_FRAGMENT_SHADER) {
        fail(parser, "discard is only available in fragment shaders");
        return 0;
    }
    next_token(parser);
    return expect(parser, TOK_SEMI, "expected ';' after discard");
}

static int local_declaration(Parser* parser)
{
    Token name;
    uint32_t width;

    if (parser->token.kind == TOK_FLOAT)
        width = 1u;
    else if (parser->token.kind == TOK_INT || parser->token.kind == TOK_BOOL)
        width = 1u;
    else if (parser->token.kind == TOK_VEC2)
        width = 2u;
    else if (parser->token.kind == TOK_IVEC2 || parser->token.kind == TOK_BVEC2)
        width = 2u;
    else if (parser->token.kind == TOK_VEC3)
        width = 3u;
    else if (parser->token.kind == TOK_IVEC3 || parser->token.kind == TOK_BVEC3)
        width = 3u;
    else if (parser->token.kind == TOK_VEC4)
        width = 4u;
    else if (parser->token.kind == TOK_IVEC4 || parser->token.kind == TOK_BVEC4)
        width = 4u;
    else if (parser->token.kind == TOK_MAT2)
        width = 2u;
    else if (parser->token.kind == TOK_MAT3)
        width = 3u;
    else if (parser->token.kind == TOK_MAT4)
        width = 4u;
    else {
        fail(parser, "expected scalar, vector, or matrix type in local declaration");
        return 0;
    }

    next_token(parser);
    if (parser->token.kind != TOK_IDENT) {
        fail(parser, "expected identifier after local type");
        return 0;
    }
    name = parser->token;
    if (!add_symbol(parser, &name, SYMBOL_VALUE, width))
        return 0;
    next_token(parser);
    if (accept(parser, TOK_ASSIGN) && !expression(parser))
        return 0;
    if (!expect(parser, TOK_SEMI, "expected ';' after declaration"))
        return 0;
    parser->result->declaration_count++;
    return 1;
}

static int constant_int_declaration(Parser* parser)
{
    Token name;
    Symbol* symbol;
    int32_t value;

    next_token(parser);
    if (!expect(parser, TOK_INT, "only const int is supported"))
        return 0;
    if (parser->token.kind != TOK_IDENT) {
        fail(parser, "expected identifier after const int");
        return 0;
    }
    name = parser->token;
    next_token(parser);
    if (!expect(parser, TOK_ASSIGN,
                "const int requires an integer literal initializer") ||
        !constant_i32_literal(parser, &value) ||
        !expect(parser, TOK_SEMI, "expected ';' after const int")) {
        return 0;
    }
    if (!add_symbol(parser, &name, SYMBOL_VALUE, 1u))
        return 0;
    symbol = find_symbol(parser, &name);
    if (symbol == NULL) {
        fail(parser, "failed to record const int");
        return 0;
    }
    symbol->constant_i32 = 1u;
    symbol->constant_i32_value = value;
    parser->result->declaration_count++;
    return 1;
}

static int main_function(Parser* parser)
{
    next_token(parser);
    if (!token_is_ident(&parser->token, "main")) {
        fail(parser, "only void main() is supported");
        return 0;
    }
    if (parser->main_seen) {
        fail(parser, "duplicate main function");
        return 0;
    }
    parser->main_seen = 1u;
    next_token(parser);
    if (!expect(parser, TOK_LPAREN, "expected '(' after main") ||
        !expect(parser, TOK_RPAREN, "expected ')' after main") ||
        !expect(parser, TOK_LBRACE, "expected '{' for main body"))
        return 0;

    while (parser->token.kind != TOK_RBRACE && parser->token.kind != TOK_EOF) {
        if (parser->token.kind == TOK_CONST) {
            if (!constant_int_declaration(parser))
                return 0;
        } else if (parser->token.kind == TOK_FLOAT ||
            parser->token.kind == TOK_INT ||
            parser->token.kind == TOK_BOOL ||
            parser->token.kind == TOK_VEC2 ||
            parser->token.kind == TOK_IVEC2 ||
             parser->token.kind == TOK_BVEC2 ||
             parser->token.kind == TOK_VEC3 ||
             parser->token.kind == TOK_IVEC3 ||
             parser->token.kind == TOK_BVEC3 ||
             parser->token.kind == TOK_VEC4 ||
             parser->token.kind == TOK_IVEC4 ||
             parser->token.kind == TOK_BVEC4 ||
             parser->token.kind == TOK_MAT2 ||
             parser->token.kind == TOK_MAT3 ||
             parser->token.kind == TOK_MAT4) {
            if (!local_declaration(parser))
                return 0;
        } else if (parser->token.kind == TOK_IF) {
            if (!conditional_statement(parser))
                return 0;
        } else if (parser->token.kind == TOK_IDENT &&
                   token_is_ident(&parser->token, "discard")) {
            if (!discard_statement(parser))
                return 0;
        } else if (!assignment(parser)) {
            return 0;
        }
    }
    return expect(parser, TOK_RBRACE, "expected '}' after main body");
}

static int attribute_declaration(Parser* parser)
{
    Token name;
    uint32_t width;
    if (parser->shader_type != RINGL_VERTEX_SHADER) {
        fail(parser, "attribute declarations require a vertex shader");
        return 0;
    }
    next_token(parser);
    if (parser->token.kind == TOK_FLOAT)
        width = 1u;
    else if (parser->token.kind == TOK_VEC2)
        width = 2u;
    else if (parser->token.kind == TOK_VEC3)
        width = 3u;
    else if (parser->token.kind == TOK_VEC4)
        width = 4u;
    else {
        fail(parser,
             "only 'attribute float', 'attribute vec2', 'attribute vec3', and 'attribute vec4' are supported");
        return 0;
    }
    next_token(parser);
    if (parser->token.kind != TOK_IDENT) {
        fail(parser, "expected attribute identifier");
        return 0;
    }
    name = parser->token;
    if (!add_symbol(parser, &name, SYMBOL_ATTRIBUTE, width))
        return 0;
    if (parser->result->attribute_count >= RINGL_GLSL_MAX_ATTRIBUTES) {
        fail(parser, "too many attributes");
        return 0;
    }
    memcpy(parser->result->attribute_names[parser->result->attribute_count],
           name.begin, name.length);
    parser->result->attribute_names[parser->result->attribute_count][name.length] = '\0';
    parser->result->attribute_widths[parser->result->attribute_count] = width;
    next_token(parser);
    if (!expect(parser, TOK_SEMI, "expected ';' after attribute"))
        return 0;
    parser->result->attribute_count++;
    parser->result->declaration_count++;
    return 1;
}

static int uniform_declaration(Parser* parser)
{
    Token name;
    uint32_t index;

    next_token(parser);
    if (parser->token.kind != TOK_SAMPLER2D &&
        parser->token.kind != TOK_FLOAT && parser->token.kind != TOK_INT &&
        parser->token.kind != TOK_BOOL &&
        parser->token.kind != TOK_VEC2 && parser->token.kind != TOK_IVEC2 &&
        parser->token.kind != TOK_BVEC2 &&
        parser->token.kind != TOK_VEC3 && parser->token.kind != TOK_IVEC3 &&
        parser->token.kind != TOK_BVEC3 &&
        parser->token.kind != TOK_VEC4 && parser->token.kind != TOK_IVEC4 &&
        parser->token.kind != TOK_BVEC4 &&
        parser->token.kind != TOK_MAT2 && parser->token.kind != TOK_MAT3 &&
        parser->token.kind != TOK_MAT4) {
        fail(parser, "only uniform sampler2D, float/int/bool, vec/ivec/bvec2-4, and mat2-4 are supported");
        return 0;
    }
    {
        TokenKind type = parser->token.kind;

    next_token(parser);
    if (parser->token.kind != TOK_IDENT) {
        fail(parser, "expected uniform identifier");
        return 0;
    }
    name = parser->token;
    if (type == TOK_SAMPLER2D)
        return uniform_array_declaration(parser, &name, SYMBOL_SAMPLER2D, 0u,
                                         (char*)parser->result->sampler_uniform_names,
                                         &parser->result->sampler_uniform_count,
                                         RINGL_GLSL_MAX_SAMPLER_UNIFORMS, "sampler2D");
    if (type == TOK_FLOAT)
        return uniform_array_declaration(parser, &name, SYMBOL_UNIFORM_FLOAT, 1u,
                                         (char*)parser->result->float_uniform_names,
                                         &parser->result->float_uniform_count,
                                         RINGL_GLSL_MAX_FLOAT_UNIFORMS, "float");
    if (type == TOK_INT)
        return uniform_array_declaration(parser, &name, SYMBOL_UNIFORM_INT, 1u,
                                         (char*)parser->result->int_uniform_names,
                                         &parser->result->int_uniform_count,
                                         RINGL_GLSL_MAX_INT_UNIFORMS, "int");
    if (type == TOK_BOOL)
        return uniform_array_declaration(parser, &name, SYMBOL_UNIFORM_BOOL, 1u,
                                         (char*)parser->result->bool_uniform_names,
                                         &parser->result->bool_uniform_count,
                                         RINGL_GLSL_MAX_BOOL_UNIFORMS, "bool");
    if (type == TOK_VEC2 || type == TOK_IVEC2 || type == TOK_BVEC2)
        return uniform_array_declaration(parser, &name,
                                         type == TOK_VEC2 ? SYMBOL_UNIFORM_VEC2 : type == TOK_IVEC2 ? SYMBOL_UNIFORM_IVEC2 : SYMBOL_UNIFORM_BVEC2,
                                         2u,
                                         type == TOK_VEC2 ? (char*)parser->result->vec2_uniform_names : type == TOK_IVEC2 ? (char*)parser->result->ivec2_uniform_names : (char*)parser->result->bvec2_uniform_names,
                                         type == TOK_VEC2 ? &parser->result->vec2_uniform_count : type == TOK_IVEC2 ? &parser->result->ivec2_uniform_count : &parser->result->bvec2_uniform_count,
                                         type == TOK_VEC2 ? RINGL_GLSL_MAX_VEC2_UNIFORMS : type == TOK_IVEC2 ? RINGL_GLSL_MAX_IVEC2_UNIFORMS : RINGL_GLSL_MAX_BVEC2_UNIFORMS,
                                         "vec2");
    if (type == TOK_VEC3 || type == TOK_IVEC3 || type == TOK_BVEC3)
        return uniform_array_declaration(parser, &name,
                                         type == TOK_VEC3 ? SYMBOL_UNIFORM_VEC3 : type == TOK_IVEC3 ? SYMBOL_UNIFORM_IVEC3 : SYMBOL_UNIFORM_BVEC3,
                                         3u,
                                         type == TOK_VEC3 ? (char*)parser->result->vec3_uniform_names : type == TOK_IVEC3 ? (char*)parser->result->ivec3_uniform_names : (char*)parser->result->bvec3_uniform_names,
                                         type == TOK_VEC3 ? &parser->result->vec3_uniform_count : type == TOK_IVEC3 ? &parser->result->ivec3_uniform_count : &parser->result->bvec3_uniform_count,
                                         type == TOK_VEC3 ? RINGL_GLSL_MAX_VEC3_UNIFORMS : type == TOK_IVEC3 ? RINGL_GLSL_MAX_IVEC3_UNIFORMS : RINGL_GLSL_MAX_BVEC3_UNIFORMS,
                                         "vec3");
    if (type == TOK_VEC4 || type == TOK_IVEC4 || type == TOK_BVEC4)
        return uniform_array_declaration(parser, &name,
                                         type == TOK_VEC4 ? SYMBOL_UNIFORM_VEC4 : type == TOK_IVEC4 ? SYMBOL_UNIFORM_IVEC4 : SYMBOL_UNIFORM_BVEC4,
                                         4u,
                                         type == TOK_VEC4 ? (char*)parser->result->vec4_uniform_names : type == TOK_IVEC4 ? (char*)parser->result->ivec4_uniform_names : (char*)parser->result->bvec4_uniform_names,
                                         type == TOK_VEC4 ? &parser->result->vec4_uniform_count : type == TOK_IVEC4 ? &parser->result->ivec4_uniform_count : &parser->result->bvec4_uniform_count,
                                         type == TOK_VEC4 ? RINGL_GLSL_MAX_VEC4_UNIFORMS : type == TOK_IVEC4 ? RINGL_GLSL_MAX_IVEC4_UNIFORMS : RINGL_GLSL_MAX_BVEC4_UNIFORMS,
                                         "vec4");
    if (type == TOK_MAT2 || type == TOK_MAT3 || type == TOK_MAT4)
        return uniform_array_declaration(parser, &name,
                                         type == TOK_MAT2 ? SYMBOL_UNIFORM_MAT2 : type == TOK_MAT3 ? SYMBOL_UNIFORM_MAT3 : SYMBOL_UNIFORM_MAT4,
                                         type == TOK_MAT2 ? 4u : type == TOK_MAT3 ? 9u : 16u,
                                         type == TOK_MAT2 ? (char*)parser->result->mat2_uniform_names : type == TOK_MAT3 ? (char*)parser->result->mat3_uniform_names : (char*)parser->result->mat4_uniform_names,
                                         type == TOK_MAT2 ? &parser->result->mat2_uniform_count : type == TOK_MAT3 ? &parser->result->mat3_uniform_count : &parser->result->mat4_uniform_count,
                                         type == TOK_MAT2 ? RINGL_GLSL_MAX_MAT2_UNIFORMS : type == TOK_MAT3 ? RINGL_GLSL_MAX_MAT3_UNIFORMS : RINGL_GLSL_MAX_MAT4_UNIFORMS,
                                         "matrix");
    if (type == TOK_SAMPLER2D) {
        uint32_t sampler_array_length = 1u;

        next_token(parser);
        if (accept(parser, TOK_LBRACKET)) {
            if (!token_unsigned_integer(&parser->token, &sampler_array_length) ||
                sampler_array_length == 0u ||
                sampler_array_length > RINGL_GLSL_MAX_SAMPLER_UNIFORMS ||
                !expect(parser, TOK_NUMBER,
                        "sampler array length must be a positive integer") ||
                !expect(parser, TOK_RBRACKET,
                        "expected ']' after sampler array length")) {
                fail(parser, "sampler array length is outside the supported range");
                return 0;
            }
        }
        if (sampler_array_length > RINGL_GLSL_MAX_SAMPLER_UNIFORMS -
                                       parser->result->sampler_uniform_count) {
            fail(parser, "too many sampler uniforms");
            return 0;
        }
        if (!add_symbol(parser, &name, SYMBOL_SAMPLER2D, 0u))
            return 0;
        find_symbol(parser, &name)->sampler_array_length = sampler_array_length;
        for (index = 0u; index < sampler_array_length; ++index) {
            char* reflected_name =
                parser->result->sampler_uniform_names[
                    parser->result->sampler_uniform_count++];

            if (sampler_array_length == 1u) {
                memcpy(reflected_name, name.begin, name.length);
                reflected_name[name.length] = '\0';
            } else if (!sampler_array_element_name(
                           reflected_name, RINGL_GLSL_NAME_MAX, &name, index)) {
                fail(parser, "sampler array name is too long");
                return 0;
            }
        }
        if (!expect(parser, TOK_SEMI, "expected ';' after sampler uniform"))
            return 0;
        parser->result->declaration_count++;
        return 1;
    }
    /* Scalar float arrays share the executable numeric-uniform path. Source
     * indexing is validated against this declaration, while reflection keeps
     * one contiguous WebGL location for each element. */
    if (type == TOK_FLOAT) {
        uint32_t float_array_length = 1u;
        Symbol* symbol;

        next_token(parser);
        if (accept(parser, TOK_LBRACKET)) {
            if (!token_unsigned_integer(&parser->token, &float_array_length) ||
                float_array_length == 0u ||
                float_array_length > RINGL_GLSL_MAX_FLOAT_UNIFORMS ||
                !expect(parser, TOK_NUMBER,
                        "float array length must be a positive integer") ||
                !expect(parser, TOK_RBRACKET,
                        "expected ']' after float array length")) {
                fail(parser, "float array length is outside the supported range");
                return 0;
            }
        }
        if (float_array_length > RINGL_GLSL_MAX_FLOAT_UNIFORMS -
                                     parser->result->float_uniform_count) {
            fail(parser, "too many float uniforms");
            return 0;
        }
        if (!add_symbol(parser, &name, SYMBOL_UNIFORM_FLOAT, 1u))
            return 0;
        symbol = find_symbol(parser, &name);
        if (symbol == NULL)
            return 0;
        symbol->sampler_array_length = float_array_length;
        for (index = 0u; index < float_array_length; ++index) {
            char* reflected_name = parser->result->float_uniform_names[
                parser->result->float_uniform_count++];

            if (float_array_length == 1u) {
                memcpy(reflected_name, name.begin, name.length);
                reflected_name[name.length] = '\0';
            } else if (!sampler_array_element_name(
                           reflected_name, RINGL_GLSL_NAME_MAX, &name, index)) {
                fail(parser, "float array name is too long");
                return 0;
            }
        }
        if (!expect(parser, TOK_SEMI, "expected ';' after float uniform"))
            return 0;
        parser->result->declaration_count++;
        return 1;
    }
    if (!add_symbol(parser, &name,
                    type == TOK_SAMPLER2D ? SYMBOL_SAMPLER2D
                    : type == TOK_FLOAT ? SYMBOL_UNIFORM_FLOAT
                    : type == TOK_INT ? SYMBOL_UNIFORM_INT
                    : type == TOK_BOOL ? SYMBOL_UNIFORM_BOOL
                    : type == TOK_VEC2 ? SYMBOL_UNIFORM_VEC2
                    : type == TOK_IVEC2 ? SYMBOL_UNIFORM_IVEC2
                    : type == TOK_BVEC2 ? SYMBOL_UNIFORM_BVEC2
                    : type == TOK_VEC3 ? SYMBOL_UNIFORM_VEC3
                    : type == TOK_IVEC3 ? SYMBOL_UNIFORM_IVEC3
                    : type == TOK_BVEC3 ? SYMBOL_UNIFORM_BVEC3
                    : type == TOK_VEC4 ? SYMBOL_UNIFORM_VEC4
                    : type == TOK_IVEC4 ? SYMBOL_UNIFORM_IVEC4
                    : type == TOK_BVEC4 ? SYMBOL_UNIFORM_BVEC4
                    : type == TOK_MAT2 ? SYMBOL_UNIFORM_MAT2
                    : type == TOK_MAT3 ? SYMBOL_UNIFORM_MAT3
                                        : SYMBOL_UNIFORM_MAT4,
                    type == TOK_SAMPLER2D ? 0u
                    : type == TOK_FLOAT || type == TOK_INT || type == TOK_BOOL ? 1u
                    : type == TOK_VEC2 || type == TOK_IVEC2 || type == TOK_BVEC2 ? 2u
                    : type == TOK_VEC3 || type == TOK_IVEC3 || type == TOK_BVEC3 ? 3u
                    : type == TOK_VEC4 || type == TOK_IVEC4 || type == TOK_BVEC4 ? 4u
                    : type == TOK_MAT2 ? 4u : type == TOK_MAT3 ? 9u : 16u))
        return 0;
    if (type == TOK_SAMPLER2D) {
        if (parser->result->sampler_uniform_count >=
            RINGL_GLSL_MAX_SAMPLER_UNIFORMS) {
            fail(parser, "too many sampler uniforms");
            return 0;
        }
        index = parser->result->sampler_uniform_count++;
        memcpy(parser->result->sampler_uniform_names[index], name.begin,
               name.length);
        parser->result->sampler_uniform_names[index][name.length] = '\0';
    } else if (type == TOK_INT) {
        if (parser->result->int_uniform_count >= RINGL_GLSL_MAX_INT_UNIFORMS) {
            fail(parser, "too many int uniforms");
            return 0;
        }
        index = parser->result->int_uniform_count++;
        memcpy(parser->result->int_uniform_names[index], name.begin, name.length);
        parser->result->int_uniform_names[index][name.length] = '\0';
    } else if (type == TOK_BOOL) {
        if (parser->result->bool_uniform_count >= RINGL_GLSL_MAX_BOOL_UNIFORMS) {
            fail(parser, "too many bool uniforms");
            return 0;
        }
        index = parser->result->bool_uniform_count++;
        memcpy(parser->result->bool_uniform_names[index], name.begin, name.length);
        parser->result->bool_uniform_names[index][name.length] = '\0';
    } else if (type == TOK_BVEC2) {
        if (parser->result->bvec2_uniform_count >= RINGL_GLSL_MAX_BVEC2_UNIFORMS) {
            fail(parser, "too many bvec2 uniforms");
            return 0;
        }
        index = parser->result->bvec2_uniform_count++;
        memcpy(parser->result->bvec2_uniform_names[index], name.begin, name.length);
        parser->result->bvec2_uniform_names[index][name.length] = '\0';
    } else if (type == TOK_BVEC3) {
        if (parser->result->bvec3_uniform_count >= RINGL_GLSL_MAX_BVEC3_UNIFORMS) {
            fail(parser, "too many bvec3 uniforms");
            return 0;
        }
        index = parser->result->bvec3_uniform_count++;
        memcpy(parser->result->bvec3_uniform_names[index], name.begin, name.length);
        parser->result->bvec3_uniform_names[index][name.length] = '\0';
    } else if (type == TOK_BVEC4) {
        if (parser->result->bvec4_uniform_count >= RINGL_GLSL_MAX_BVEC4_UNIFORMS) {
            fail(parser, "too many bvec4 uniforms");
            return 0;
        }
        index = parser->result->bvec4_uniform_count++;
        memcpy(parser->result->bvec4_uniform_names[index], name.begin, name.length);
        parser->result->bvec4_uniform_names[index][name.length] = '\0';
    } else if (type == TOK_VEC2) {
        if (parser->result->vec2_uniform_count >=
            RINGL_GLSL_MAX_VEC2_UNIFORMS) {
            fail(parser, "too many vec2 uniforms");
            return 0;
        }
        index = parser->result->vec2_uniform_count++;
        memcpy(parser->result->vec2_uniform_names[index], name.begin,
               name.length);
        parser->result->vec2_uniform_names[index][name.length] = '\0';
    } else if (type == TOK_IVEC2) {
        if (parser->result->ivec2_uniform_count >= RINGL_GLSL_MAX_IVEC2_UNIFORMS) {
            fail(parser, "too many ivec2 uniforms");
            return 0;
        }
        index = parser->result->ivec2_uniform_count++;
        memcpy(parser->result->ivec2_uniform_names[index], name.begin, name.length);
        parser->result->ivec2_uniform_names[index][name.length] = '\0';
    } else if (type == TOK_VEC3) {
        if (parser->result->vec3_uniform_count >=
            RINGL_GLSL_MAX_VEC3_UNIFORMS) {
            fail(parser, "too many vec3 uniforms");
            return 0;
        }
        index = parser->result->vec3_uniform_count++;
        memcpy(parser->result->vec3_uniform_names[index], name.begin,
               name.length);
        parser->result->vec3_uniform_names[index][name.length] = '\0';
    } else if (type == TOK_IVEC3) {
        if (parser->result->ivec3_uniform_count >= RINGL_GLSL_MAX_IVEC3_UNIFORMS) {
            fail(parser, "too many ivec3 uniforms");
            return 0;
        }
        index = parser->result->ivec3_uniform_count++;
        memcpy(parser->result->ivec3_uniform_names[index], name.begin, name.length);
        parser->result->ivec3_uniform_names[index][name.length] = '\0';
    } else if (type == TOK_VEC4) {
        if (parser->result->vec4_uniform_count >=
            RINGL_GLSL_MAX_VEC4_UNIFORMS) {
            fail(parser, "too many vec4 uniforms");
            return 0;
        }
        index = parser->result->vec4_uniform_count++;
        memcpy(parser->result->vec4_uniform_names[index], name.begin,
               name.length);
        parser->result->vec4_uniform_names[index][name.length] = '\0';
    } else if (type == TOK_IVEC4) {
        if (parser->result->ivec4_uniform_count >= RINGL_GLSL_MAX_IVEC4_UNIFORMS) {
            fail(parser, "too many ivec4 uniforms");
            return 0;
        }
        index = parser->result->ivec4_uniform_count++;
        memcpy(parser->result->ivec4_uniform_names[index], name.begin, name.length);
        parser->result->ivec4_uniform_names[index][name.length] = '\0';
    } else if (type == TOK_MAT2) {
        if (parser->result->mat2_uniform_count >=
            RINGL_GLSL_MAX_MAT2_UNIFORMS) {
            fail(parser, "too many mat2 uniforms");
            return 0;
        }
        index = parser->result->mat2_uniform_count++;
        memcpy(parser->result->mat2_uniform_names[index], name.begin,
               name.length);
        parser->result->mat2_uniform_names[index][name.length] = '\0';
    } else if (type == TOK_MAT3) {
        if (parser->result->mat3_uniform_count >=
            RINGL_GLSL_MAX_MAT3_UNIFORMS) {
            fail(parser, "too many mat3 uniforms");
            return 0;
        }
        index = parser->result->mat3_uniform_count++;
        memcpy(parser->result->mat3_uniform_names[index], name.begin,
               name.length);
        parser->result->mat3_uniform_names[index][name.length] = '\0';
    } else {
        if (parser->result->mat4_uniform_count >=
            RINGL_GLSL_MAX_MAT4_UNIFORMS) {
            fail(parser, "too many mat4 uniforms");
            return 0;
        }
        index = parser->result->mat4_uniform_count++;
        memcpy(parser->result->mat4_uniform_names[index], name.begin,
               name.length);
        parser->result->mat4_uniform_names[index][name.length] = '\0';
    }
    next_token(parser);
    if (!expect(parser, TOK_SEMI, "expected ';' after uniform"))
        return 0;
    parser->result->declaration_count++;
    return 1;
    }
}

static int varying_declaration(Parser* parser)
{
    Token name;
    uint32_t index;
    uint32_t width;

    next_token(parser);
    if (parser->token.kind == TOK_FLOAT) {
        width = 1u;
    } else if (parser->token.kind == TOK_VEC2) {
        width = 2u;
    } else if (parser->token.kind == TOK_VEC3) {
        width = 3u;
    } else if (parser->token.kind == TOK_VEC4) {
        width = 4u;
    } else {
        fail(parser,
             "only 'varying float', 'varying vec2', 'varying vec3', or "
             "'varying vec4' is supported");
        return 0;
    }
    next_token(parser);
    if (parser->token.kind != TOK_IDENT) {
        fail(parser, "expected varying identifier");
        return 0;
    }
    name = parser->token;
    if (!add_symbol(parser, &name, SYMBOL_VARYING, width))
        return 0;
    if (parser->result->varying_count >= RINGL_GLSL_MAX_GENERIC_VARYINGS) {
        fail(parser, "too many varyings");
        return 0;
    }
    index = parser->result->varying_count++;
    memcpy(parser->result->varying_names[index], name.begin, name.length);
    parser->result->varying_names[index][name.length] = '\0';
    parser->result->varying_widths[index] = width;
    next_token(parser);
    if (!expect(parser, TOK_SEMI, "expected ';' after varying"))
        return 0;
    parser->result->declaration_count++;
    return 1;
}

/* RSH1 executes this profile in binary32. A GLSL ES default precision
 * declaration therefore selects an admissible source-language precision but
 * does not add a second, hidden execution mode. Keep it syntactically and
 * semantically explicit: accepting arbitrary identifiers here would make an
 * unsupported storage type appear to compile. */
static int precision_declaration(Parser* parser)
{
    next_token(parser);
    if (parser->token.kind != TOK_LOWP && parser->token.kind != TOK_MEDIUMP &&
        parser->token.kind != TOK_HIGHP) {
        fail(parser, "precision declaration requires lowp, mediump, or highp");
        return 0;
    }
    next_token(parser);
    if (parser->token.kind != TOK_FLOAT && parser->token.kind != TOK_INT &&
        parser->token.kind != TOK_SAMPLER2D) {
        fail(parser, "precision declaration type is not supported");
        return 0;
    }
    next_token(parser);
    if (!expect(parser, TOK_SEMI, "expected ';' after precision declaration"))
        return 0;
    parser->result->declaration_count++;
    return 1;
}

/* This bounded parser admits exactly the extension directive whose execution
 * path RinGL implements.  It deliberately does not treat arbitrary
 * preprocessor text as a comment: silently ignoring an unsupported directive
 * could make a WebGL shader appear to have a capability it does not have. */
static int extension_directive(Parser* parser)
{
    if (parser->result->declaration_count != 0u || parser->main_seen != 0u) {
        fail(parser, "#extension must precede declarations");
        return 0;
    }
    next_token(parser);
    if (!token_is_ident(&parser->token, "extension")) {
        fail(parser, "only #extension is supported");
        return 0;
    }
    next_token(parser);
    if (!token_is_ident(&parser->token, "GL_OES_standard_derivatives") &&
        !token_is_ident(&parser->token, "GL_EXT_shader_texture_lod") &&
        !token_is_ident(&parser->token, "GL_EXT_frag_depth") &&
        !token_is_ident(&parser->token, "GL_EXT_draw_buffers")) {
        fail(parser, "unsupported GLSL extension");
        return 0;
    }
    {
        int standard_derivatives = token_is_ident(
            &parser->token, "GL_OES_standard_derivatives");
        int shader_texture_lod = token_is_ident(
            &parser->token, "GL_EXT_shader_texture_lod");
        int frag_depth = token_is_ident(&parser->token, "GL_EXT_frag_depth");
    next_token(parser);
    if (!expect(parser, TOK_COLON, "expected ':' in #extension directive"))
        return 0;
    if (!token_is_ident(&parser->token, "enable") &&
        !token_is_ident(&parser->token, "require")) {
        fail(parser, "extension must be enabled or required");
        return 0;
    }
    if (parser->shader_type != RINGL_FRAGMENT_SHADER) {
        fail(parser, "extension requires a fragment shader");
        return 0;
    }
        if (standard_derivatives)
            parser->result->standard_derivatives_enabled = 1u;
        else if (shader_texture_lod)
            parser->result->shader_texture_lod_enabled = 1u;
        else if (frag_depth)
            parser->result->frag_depth_enabled = 1u;
        else
            parser->result->draw_buffers_enabled = 1u;
    }
    next_token(parser);
    return 1;
}

int ringl_glsl_parse(uint32_t shader_type,
                     const char* source,
                     size_t source_length,
                     RinGLGlslParseResult* result)
{
    Parser parser;

    if (source == NULL || result == NULL)
        return -1;
    memset(result, 0, sizeof(*result));
    memset(&parser, 0, sizeof(parser));
    parser.source = source;
    parser.length = source_length;
    parser.line = 1u;
    parser.shader_type = shader_type;
    parser.result = result;
    next_token(&parser);

    while (parser.token.kind != TOK_EOF && result->diagnostic[0] == '\0') {
        if (parser.token.kind == TOK_HASH) {
            if (!extension_directive(&parser))
                break;
        } else if (parser.token.kind == TOK_ATTRIBUTE) {
            if (!attribute_declaration(&parser))
                break;
        } else if (parser.token.kind == TOK_UNIFORM) {
            if (!uniform_declaration(&parser))
                break;
        } else if (parser.token.kind == TOK_VARYING) {
            if (!varying_declaration(&parser))
                break;
        } else if (parser.token.kind == TOK_PRECISION) {
            if (!precision_declaration(&parser))
                break;
        } else if (parser.token.kind == TOK_CONST) {
            if (!constant_int_declaration(&parser))
                break;
        } else if (parser.token.kind == TOK_VOID) {
            if (!main_function(&parser))
                break;
        } else {
            fail(&parser, "unsupported top-level declaration");
            break;
        }
    }

    if (result->diagnostic[0] == '\0' && !parser.main_seen)
        fail(&parser, "missing void main()");
    if (result->diagnostic[0] == '\0') {
        result->ok = 1u;
        return 0;
    }
    return 1;
}
