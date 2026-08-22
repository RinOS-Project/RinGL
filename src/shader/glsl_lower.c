/* SPDX-License-Identifier: MIT */
#include "glsl_lower.h"
#include "rsh1_abi.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ringl/ringl.h>

typedef enum Tok {
    T_EOF = 0, T_IDENT, T_NUMBER, T_VOID, T_FLOAT, T_ATTRIBUTE,
    T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE, T_SEMI, T_ASSIGN,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_BAD
} Tok;

typedef struct Token {
    Tok kind;
    const char* begin;
    size_t length;
} Token;

typedef struct Symbol {
    char name[64];
    uint16_t reg;
    uint16_t input;
    uint8_t attribute;
    uint8_t initialized;
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
    RinGLRsh1InstructionV1 ins[RINGL_RSH1_MAX_INSTRUCTIONS];
    uint32_t ins_count;
    RinGLGlslLowerResult* result;
} Lower;

static void fail(Lower* l, const char* message)
{
    if (l->result->diagnostic[0] == '\0')
        (void)snprintf(l->result->diagnostic, sizeof(l->result->diagnostic), "%s", message);
}

static int text_is(const Token* t, const char* s)
{
    size_t n = strlen(s);
    return t->kind == T_IDENT && t->length == n && memcmp(t->begin, s, n) == 0;
}

static void skip(Lower* l)
{
    for (;;) {
        while (l->offset < l->length && isspace((unsigned char)l->source[l->offset])) l->offset++;
        if (l->offset + 1u < l->length && l->source[l->offset] == '/' && l->source[l->offset + 1u] == '/') {
            l->offset += 2u;
            while (l->offset < l->length && l->source[l->offset] != '\n') l->offset++;
            continue;
        }
        break;
    }
}

static Tok keyword(const char* p, size_t n)
{
    if (n == 4u && memcmp(p, "void", 4u) == 0) return T_VOID;
    if (n == 5u && memcmp(p, "float", 5u) == 0) return T_FLOAT;
    if (n == 9u && memcmp(p, "attribute", 9u) == 0) return T_ATTRIBUTE;
    return T_IDENT;
}

static void next(Lower* l)
{
    Token t = {0};
    char c;
    skip(l);
    if (l->offset >= l->length) { l->token.kind = T_EOF; return; }
    t.begin = l->source + l->offset;
    c = l->source[l->offset++];
    if (isalpha((unsigned char)c) || c == '_') {
        size_t start = l->offset - 1u;
        while (l->offset < l->length) {
            c = l->source[l->offset];
            if (!isalnum((unsigned char)c) && c != '_') break;
            l->offset++;
        }
        t.begin = l->source + start;
        t.length = l->offset - start;
        t.kind = keyword(t.begin, t.length);
        l->token = t;
        return;
    }
    if (isdigit((unsigned char)c) || c == '.') {
        size_t start = l->offset - 1u;
        while (l->offset < l->length) {
            c = l->source[l->offset];
            if (!isdigit((unsigned char)c) && c != '.') break;
            l->offset++;
        }
        t.begin = l->source + start;
        t.length = l->offset - start;
        t.kind = T_NUMBER;
        l->token = t;
        return;
    }
    t.length = 1u;
    switch (c) {
    case '(': t.kind = T_LPAREN; break; case ')': t.kind = T_RPAREN; break;
    case '{': t.kind = T_LBRACE; break; case '}': t.kind = T_RBRACE; break;
    case ';': t.kind = T_SEMI; break; case '=': t.kind = T_ASSIGN; break;
    case '+': t.kind = T_PLUS; break; case '-': t.kind = T_MINUS; break;
    case '*': t.kind = T_STAR; break; case '/': t.kind = T_SLASH; break;
    default: t.kind = T_BAD; break;
    }
    l->token = t;
}

static int take(Lower* l, Tok k)
{
    if (l->token.kind != k) return 0;
    next(l);
    return 1;
}

static int need(Lower* l, Tok k, const char* message)
{
    if (!take(l, k)) { fail(l, message); return 0; }
    return 1;
}

static Symbol* find_symbol(Lower* l, const Token* t)
{
    uint32_t i;
    for (i = 0; i < l->symbol_count; ++i) {
        size_t n = strlen(l->symbols[i].name);
        if (n == t->length && memcmp(l->symbols[i].name, t->begin, n) == 0) return &l->symbols[i];
    }
    return NULL;
}

static Symbol* add_symbol(Lower* l, const Token* t, int attribute)
{
    Symbol* s;
    if (l->symbol_count >= 64u || t->length == 0u || t->length >= 64u) { fail(l, "symbol limit exceeded"); return NULL; }
    s = &l->symbols[l->symbol_count++];
    memset(s, 0, sizeof(*s));
    memcpy(s->name, t->begin, t->length);
    s->name[t->length] = '\0';
    s->attribute = (uint8_t)attribute;
    s->input = attribute ? l->next_input++ : RINGL_RSH1_UNUSED;
    s->reg = RINGL_RSH1_UNUSED;
    s->initialized = (uint8_t)attribute;
    return s;
}

static uint16_t new_reg(Lower* l)
{
    if (l->next_reg >= RINGL_RSH1_MAX_REGISTERS) { fail(l, "RSH1 register limit exceeded"); return RINGL_RSH1_UNUSED; }
    return l->next_reg++;
}

static int emit(Lower* l, uint16_t opcode, uint16_t dst, uint16_t a, uint16_t b, uint32_t imm)
{
    RinGLRsh1InstructionV1* in;
    if (l->ins_count >= RINGL_RSH1_MAX_INSTRUCTIONS) { fail(l, "RSH1 instruction limit exceeded"); return 0; }
    in = &l->ins[l->ins_count++];
    memset(in, 0, sizeof(*in));
    in->opcode = opcode;
    in->destination = dst;
    in->source0 = a;
    in->source1 = b;
    in->resource = RINGL_RSH1_UNUSED;
    in->immediate = imm;
    return 1;
}

static uint16_t expression(Lower* l);

static uint16_t primary(Lower* l)
{
    uint16_t r;
    if (l->token.kind == T_NUMBER) {
        char tmp[64];
        char* end = NULL;
        float value;
        uint32_t bits;
        if (l->token.length >= sizeof(tmp)) { fail(l, "numeric literal too long"); return RINGL_RSH1_UNUSED; }
        memcpy(tmp, l->token.begin, l->token.length); tmp[l->token.length] = '\0';
        value = strtof(tmp, &end);
        if (end == tmp || *end != '\0') { fail(l, "invalid numeric literal"); return RINGL_RSH1_UNUSED; }
        memcpy(&bits, &value, sizeof(bits));
        next(l);
        r = new_reg(l);
        if (r == RINGL_RSH1_UNUSED || !emit(l, RINGL_RSH1_OP_CONST_F32, r, RINGL_RSH1_UNUSED, RINGL_RSH1_UNUSED, bits)) return RINGL_RSH1_UNUSED;
        return r;
    }
    if (l->token.kind == T_IDENT) {
        Token name = l->token;
        Symbol* s = find_symbol(l, &name);
        if (s == NULL || !s->initialized) { fail(l, "use of unavailable scalar value"); return RINGL_RSH1_UNUSED; }
        next(l);
        if (s->attribute) {
            r = new_reg(l);
            if (r == RINGL_RSH1_UNUSED || !emit(l, RINGL_RSH1_OP_LOAD_INPUT_F32, r, RINGL_RSH1_UNUSED, RINGL_RSH1_UNUSED, s->input)) return RINGL_RSH1_UNUSED;
            return r;
        }
        return s->reg;
    }
    if (take(l, T_LPAREN)) {
        r = expression(l);
        if (r == RINGL_RSH1_UNUSED || !need(l, T_RPAREN, "expected ')'")) return RINGL_RSH1_UNUSED;
        return r;
    }
    fail(l, "expected scalar expression");
    return RINGL_RSH1_UNUSED;
}

static uint16_t unary(Lower* l)
{
    if (take(l, T_PLUS)) return unary(l);
    if (take(l, T_MINUS)) {
        uint16_t src = unary(l), zero, dst;
        uint32_t bits = 0u;
        if (src == RINGL_RSH1_UNUSED) return src;
        zero = new_reg(l); dst = new_reg(l);
        if (zero == RINGL_RSH1_UNUSED || dst == RINGL_RSH1_UNUSED) return RINGL_RSH1_UNUSED;
        if (!emit(l, RINGL_RSH1_OP_CONST_F32, zero, RINGL_RSH1_UNUSED, RINGL_RSH1_UNUSED, bits) ||
            !emit(l, RINGL_RSH1_OP_SUB_F32, dst, zero, src, 0u)) return RINGL_RSH1_UNUSED;
        return dst;
    }
    return primary(l);
}

static uint16_t mul(Lower* l)
{
    uint16_t left = unary(l);
    while (left != RINGL_RSH1_UNUSED && (l->token.kind == T_STAR || l->token.kind == T_SLASH)) {
        Tok op = l->token.kind;
        uint16_t right, dst;
        next(l); right = unary(l); dst = new_reg(l);
        if (right == RINGL_RSH1_UNUSED || dst == RINGL_RSH1_UNUSED) return RINGL_RSH1_UNUSED;
        if (!emit(l, op == T_STAR ? RINGL_RSH1_OP_MUL_F32 : RINGL_RSH1_OP_DIV_F32, dst, left, right, 0u)) return RINGL_RSH1_UNUSED;
        left = dst;
    }
    return left;
}

static uint16_t expression(Lower* l)
{
    uint16_t left = mul(l);
    while (left != RINGL_RSH1_UNUSED && (l->token.kind == T_PLUS || l->token.kind == T_MINUS)) {
        Tok op = l->token.kind;
        uint16_t right, dst;
        next(l); right = mul(l); dst = new_reg(l);
        if (right == RINGL_RSH1_UNUSED || dst == RINGL_RSH1_UNUSED) return RINGL_RSH1_UNUSED;
        if (!emit(l, op == T_PLUS ? RINGL_RSH1_OP_ADD_F32 : RINGL_RSH1_OP_SUB_F32, dst, left, right, 0u)) return RINGL_RSH1_UNUSED;
        left = dst;
    }
    return left;
}

static int local_decl(Lower* l)
{
    Token name; Symbol* s;
    next(l);
    if (l->token.kind != T_IDENT) { fail(l, "expected local name"); return 0; }
    name = l->token; s = find_symbol(l, &name);
    if (s != NULL) { fail(l, "duplicate local"); return 0; }
    s = add_symbol(l, &name, 0); if (s == NULL) return 0;
    next(l);
    if (take(l, T_ASSIGN)) {
        s->reg = expression(l);
        if (s->reg == RINGL_RSH1_UNUSED) return 0;
        s->initialized = 1u;
    }
    return need(l, T_SEMI, "expected ';' after local");
}

static int assignment(Lower* l)
{
    Token target = l->token;
    Symbol* s = NULL;
    uint16_t value;
    int output = 0;
    if (target.kind != T_IDENT) { fail(l, "expected assignment"); return 0; }
    if (text_is(&target, "gl_Position")) output = l->shader_type == RINGL_VERTEX_SHADER;
    else if (text_is(&target, "gl_FragColor")) output = l->shader_type == RINGL_FRAGMENT_SHADER;
    else s = find_symbol(l, &target);
    if (!output && s == NULL) { fail(l, "unknown assignment target"); return 0; }
    next(l); if (!need(l, T_ASSIGN, "expected '='")) return 0;
    value = expression(l); if (value == RINGL_RSH1_UNUSED) return 0;
    if (!need(l, T_SEMI, "expected ';' after assignment")) return 0;
    if (output) return emit(l, RINGL_RSH1_OP_STORE_OUTPUT_F32, RINGL_RSH1_UNUSED, value, RINGL_RSH1_UNUSED, 0u);
    if (s->attribute) { fail(l, "attribute is read-only"); return 0; }
    s->reg = value; s->initialized = 1u;
    return 1;
}

static int parse_all(Lower* l)
{
    int main_seen = 0;
    next(l);
    while (l->token.kind != T_EOF) {
        if (l->token.kind == T_ATTRIBUTE) {
            Token name;
            if (l->shader_type != RINGL_VERTEX_SHADER) { fail(l, "fragment attribute"); return 0; }
            next(l); if (!need(l, T_FLOAT, "expected float")) return 0;
            if (l->token.kind != T_IDENT) { fail(l, "expected attribute name"); return 0; }
            name = l->token;
            if (find_symbol(l, &name) != NULL || add_symbol(l, &name, 1) == NULL) return 0;
            next(l); if (!need(l, T_SEMI, "expected ';' after attribute")) return 0;
            continue;
        }
        if (l->token.kind == T_VOID) {
            next(l);
            if (!text_is(&l->token, "main") || main_seen) { fail(l, "expected unique main"); return 0; }
            main_seen = 1; next(l);
            if (!need(l, T_LPAREN, "expected '('") || !need(l, T_RPAREN, "expected ')'" ) || !need(l, T_LBRACE, "expected '{'")) return 0;
            while (l->token.kind != T_RBRACE && l->token.kind != T_EOF) {
                if (l->token.kind == T_FLOAT) { if (!local_decl(l)) return 0; }
                else if (!assignment(l)) return 0;
            }
            if (!need(l, T_RBRACE, "expected '}'")) return 0;
            continue;
        }
        fail(l, "unsupported lowering syntax"); return 0;
    }
    if (!main_seen) { fail(l, "missing main"); return 0; }
    return emit(l, RINGL_RSH1_OP_RETURN, RINGL_RSH1_UNUSED, RINGL_RSH1_UNUSED, RINGL_RSH1_UNUSED, 0u);
}

int ringl_glsl_lower_rsh1(uint32_t shader_type, const char* source, size_t source_length, RinGLGlslLowerResult* result)
{
    Lower l;
    RinGLRsh1HeaderV1 h;
    size_t total;
    if (source == NULL || result == NULL) return -1;
    memset(result, 0, sizeof(*result)); memset(&l, 0, sizeof(l));
    l.source = source; l.length = source_length; l.shader_type = shader_type; l.result = result;
    if (!parse_all(&l)) return 1;
    if (l.next_reg == 0u) l.next_reg = 1u;
    memset(&h, 0, sizeof(h));
    h.magic = RINGL_RSH1_MAGIC; h.version = RINGL_RSH1_VERSION; h.header_size = sizeof(h);
    h.stage = shader_type == RINGL_VERTEX_SHADER ? RINGL_RSH1_STAGE_VERTEX : RINGL_RSH1_STAGE_FRAGMENT;
    h.instruction_count = l.ins_count; h.register_count = l.next_reg;
    h.input_count = l.next_input; h.output_count = 1u; h.entry_instruction = 0u;
    total = sizeof(h) + (size_t)l.ins_count * sizeof(l.ins[0]);
    h.total_size = (uint32_t)total;
    memcpy(result->bytes, &h, sizeof(h)); memcpy(result->bytes + sizeof(h), l.ins, (size_t)l.ins_count * sizeof(l.ins[0]));
    result->ok = 1u; result->instruction_count = l.ins_count; result->register_count = l.next_reg;
    result->input_count = l.next_input; result->output_count = 1u; result->byte_size = (uint32_t)total;
    return 0;
}
