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
    TOK_SAMPLER2D,
    TOK_ATTRIBUTE,
    TOK_UNIFORM,
    TOK_VARYING,
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
    TOK_INVALID,
} TokenKind;

typedef enum SymbolKind {
    SYMBOL_VALUE = 0,
    SYMBOL_ATTRIBUTE = 1,
    SYMBOL_SAMPLER2D = 2,
    SYMBOL_VARYING = 3,
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
    if (length == 9u && memcmp(begin, "sampler2D", 9u) == 0)
        return TOK_SAMPLER2D;
    if (length == 9u && memcmp(begin, "attribute", 9u) == 0)
        return TOK_ATTRIBUTE;
    if (length == 7u && memcmp(begin, "uniform", 7u) == 0)
        return TOK_UNIFORM;
    if (length == 7u && memcmp(begin, "varying", 7u) == 0)
        return TOK_VARYING;
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

    if (isdigit((unsigned char)c) || c == '.') {
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
    case '=': token.kind = TOK_ASSIGN; break;
    case '+': token.kind = TOK_PLUS; break;
    case '-': token.kind = TOK_MINUS; break;
    case '*': token.kind = TOK_STAR; break;
    case '/': token.kind = TOK_SLASH; break;
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
    return 1;
}

static int expression(Parser* parser);

static int constructor(Parser* parser, TokenKind kind)
{
    uint32_t component_count = 0u;
    uint32_t target_components = kind == TOK_VEC2 ? 2u :
                                 kind == TOK_VEC3 ? 3u : 4u;

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

static int varying_vec2_offset(Parser* parser)
{
    if (parser->token.kind != TOK_PLUS && parser->token.kind != TOK_MINUS)
        return 1;
    next_token(parser);
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

static int texture2d_call(Parser* parser)
{
    Token sampler_name;
    Symbol* sampler;

    if (parser->shader_type != RINGL_FRAGMENT_SHADER) {
        fail(parser, "texture2D is only supported in fragment shaders");
        return 0;
    }
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
    if (!expect(parser, TOK_COMMA, "expected ',' after texture2D sampler"))
        return 0;

    if (parser->token.kind == TOK_VEC2) {
        if (!constructor(parser, TOK_VEC2))
            return 0;
    } else if (parser->token.kind == TOK_IDENT) {
        Symbol* coordinate = find_symbol(parser, &parser->token);
        if (coordinate == NULL || coordinate->width != 2u ||
            (coordinate->kind != SYMBOL_VARYING &&
             coordinate->kind != SYMBOL_VALUE)) {
            fail(parser, "texture2D coordinate must be vec2");
            return 0;
        }
        next_token(parser);
        if (!varying_vec2_offset(parser))
            return 0;
    } else {
        fail(parser, "texture2D coordinate must be vec2");
        return 0;
    }
    return expect(parser, TOK_RPAREN, "expected ')' after texture2D arguments");
}

static int primary(Parser* parser)
{
    if (accept(parser, TOK_NUMBER))
        return 1;
    if (parser->token.kind == TOK_VEC2 || parser->token.kind == TOK_VEC3 ||
        parser->token.kind == TOK_VEC4)
        return constructor(parser, parser->token.kind);
    if (parser->token.kind == TOK_IDENT) {
        Token ident = parser->token;
        if (token_is_ident(&ident, "texture2D"))
            return texture2d_call(parser);
        if (!token_is_ident(&ident, "gl_Position") &&
            !token_is_ident(&ident, "gl_FragColor") &&
            !symbol_exists(parser, &ident)) {
            fail(parser, "use of undeclared identifier");
            return 0;
        }
        next_token(parser);
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

static int assignment(Parser* parser)
{
    Token target = parser->token;
    Symbol* symbol;
    if (target.kind != TOK_IDENT) {
        fail(parser, "expected assignment target");
        return 0;
    }
    if (token_is_ident(&target, "gl_Position")) {
        if (parser->shader_type != RINGL_VERTEX_SHADER) {
            fail(parser, "gl_Position is only writable in vertex shaders");
            return 0;
        }
    } else if (token_is_ident(&target, "gl_FragColor")) {
        if (parser->shader_type != RINGL_FRAGMENT_SHADER) {
            fail(parser, "gl_FragColor is only writable in fragment shaders");
            return 0;
        }
    } else {
        symbol = find_symbol(parser, &target);
        if (symbol == NULL) {
            fail(parser, "assignment to undeclared identifier");
            return 0;
        }
        if (symbol->kind == SYMBOL_SAMPLER2D) {
            fail(parser, "sampler uniforms are read-only");
            return 0;
        }
        if (symbol->kind == SYMBOL_VARYING &&
            parser->shader_type != RINGL_VERTEX_SHADER) {
            fail(parser, "varyings are read-only in fragment shaders");
            return 0;
        }
    }
    next_token(parser);
    if (!expect(parser, TOK_ASSIGN, "expected '='"))
        return 0;
    if (!expression(parser))
        return 0;
    if (!expect(parser, TOK_SEMI, "expected ';' after assignment"))
        return 0;
    parser->result->statement_count++;
    return 1;
}

static int local_declaration(Parser* parser)
{
    Token name;
    uint32_t width;

    if (parser->token.kind == TOK_FLOAT)
        width = 1u;
    else if (parser->token.kind == TOK_VEC2)
        width = 2u;
    else if (parser->token.kind == TOK_VEC3)
        width = 3u;
    else if (parser->token.kind == TOK_VEC4)
        width = 4u;
    else {
        fail(parser, "expected scalar or vector type in local declaration");
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
        if (parser->token.kind == TOK_FLOAT ||
            parser->token.kind == TOK_VEC2 ||
            parser->token.kind == TOK_VEC3 ||
            parser->token.kind == TOK_VEC4) {
            if (!local_declaration(parser))
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
    if (parser->token.kind != TOK_SAMPLER2D) {
        fail(parser, "only 'uniform sampler2D' is supported");
        return 0;
    }
    next_token(parser);
    if (parser->token.kind != TOK_IDENT) {
        fail(parser, "expected uniform identifier");
        return 0;
    }
    name = parser->token;
    if (!add_symbol(parser, &name, SYMBOL_SAMPLER2D, 0u))
        return 0;
    if (parser->result->sampler_uniform_count >= RINGL_GLSL_MAX_SAMPLER_UNIFORMS) {
        fail(parser, "too many sampler uniforms");
        return 0;
    }
    index = parser->result->sampler_uniform_count++;
    memcpy(parser->result->sampler_uniform_names[index], name.begin, name.length);
    parser->result->sampler_uniform_names[index][name.length] = '\0';
    next_token(parser);
    if (!expect(parser, TOK_SEMI, "expected ';' after uniform"))
        return 0;
    parser->result->declaration_count++;
    return 1;
}

static int varying_declaration(Parser* parser)
{
    Token name;
    uint32_t index;
    uint32_t width;

    next_token(parser);
    if (parser->token.kind == TOK_VEC2) {
        width = 2u;
    } else if (parser->token.kind == TOK_VEC3) {
        width = 3u;
    } else if (parser->token.kind == TOK_VEC4) {
        width = 4u;
    } else {
        fail(parser, "only 'varying vec2', 'varying vec3', or 'varying vec4' is supported");
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
    if (parser->result->varying_count >= RINGL_GLSL_MAX_VARYINGS) {
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
        if (parser.token.kind == TOK_ATTRIBUTE) {
            if (!attribute_declaration(&parser))
                break;
        } else if (parser.token.kind == TOK_UNIFORM) {
            if (!uniform_declaration(&parser))
                break;
        } else if (parser.token.kind == TOK_VARYING) {
            if (!varying_declaration(&parser))
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
