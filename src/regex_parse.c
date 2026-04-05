#include "regex_parse.h"

#include "ast.h"
#include "charset.h"
#include "util.h"
#include "yal_spec.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static void rp_skip_ws(RegexParser *p) {
    while (p->s[p->pos] && isspace((unsigned char)p->s[p->pos])) {
        p->pos++;
    }
}

static int rp_peek(RegexParser *p) {
    return (unsigned char)p->s[p->pos];
}

static int rp_get(RegexParser *p) {
    int c = (unsigned char)p->s[p->pos];
    if (c) {
        p->pos++;
    }
    return c;
}

static int rp_match(RegexParser *p, int c) {
    if (rp_peek(p) == c) {
        p->pos++;
        return 1;
    }
    return 0;
}

static unsigned char parse_escape_char(RegexParser *p) {
    int c = rp_get(p);
    if (!c) {
        fatal("unfinished escape sequence");
    }
    switch (c) {
    case 'n':
        return '\n';
    case 't':
        return '\t';
    case 'r':
        return '\r';
    case '\\':
        return '\\';
    case '\'':
        return '\'';
    case '"':
        return '"';
    case '0':
        return '\0';
    default:
        return (unsigned char)c;
    }
}

static unsigned char parse_char_literal(RegexParser *p) {
    unsigned char ch;
    if (!rp_match(p, '\'')) {
        fatal("expected character literal");
    }
    if (rp_match(p, '\\')) {
        ch = parse_escape_char(p);
    } else {
        int c = rp_get(p);
        if (!c) {
            fatal("unterminated character literal");
        }
        ch = (unsigned char)c;
    }
    if (!rp_match(p, '\'')) {
        fatal("unterminated character literal");
    }
    return ch;
}

static unsigned char parse_set_char(RegexParser *p) {
    if (rp_peek(p) == '\'') {
        return parse_char_literal(p);
    }
    if (rp_match(p, '\\')) {
        return parse_escape_char(p);
    }
    if (!rp_peek(p)) {
        fatal("unterminated set");
    }
    return (unsigned char)rp_get(p);
}

static AST *parse_regex_expr(RegexParser *p);

static AST *resolve_let_ast(LetVec *lets, const char *name) {
    LetDef *d = find_let(lets, name);
    RegexParser rp;
    AST *ast;
    if (!d) {
        fatal("undefined regex identifier: %s", name);
    }
    if (d->resolving) {
        fatal("recursive let definition detected for: %s", name);
    }
    if (!d->ast) {
        d->resolving = 1;
        rp.s = d->regex;
        rp.pos = 0;
        rp.lets = lets;
        d->ast = parse_regex_expr(&rp);
        rp_skip_ws(&rp);
        if (rp_peek(&rp) != 0) {
            fatal("cannot parse full let regex '%s': trailing: %.30s", d->name, rp.s + rp.pos);
        }
        d->resolving = 0;
    }
    ast = ast_clone(d->ast);
    return ast;
}

static int is_ident_start_char(int c) {
    return isalpha((unsigned char)c) || c == '_';
}

static int is_ident_char(int c) {
    return isalnum((unsigned char)c) || c == '_';
}

static AST *parse_regex_primary(RegexParser *p) {
    int c;
    rp_skip_ws(p);
    c = rp_peek(p);
    if (c == 0) {
        return NULL;
    }
    if (c == '(') {
        AST *e;
        rp_get(p);
        e = parse_regex_expr(p);
        rp_skip_ws(p);
        if (!rp_match(p, ')')) {
            fatal("missing ')' in regex");
        }
        return e;
    }
    if (c == '\'') {
        CharSet set;
        unsigned char ch = parse_char_literal(p);
        charset_clear(&set);
        charset_add(&set, ch);
        return ast_charset(&set);
    }
    if (c == '"') {
        AST *res = NULL;
        rp_get(p);
        while (rp_peek(p) && rp_peek(p) != '"') {
            CharSet set;
            unsigned char ch;
            AST *n;
            if (rp_match(p, '\\')) {
                ch = parse_escape_char(p);
            } else {
                ch = (unsigned char)rp_get(p);
            }
            charset_clear(&set);
            charset_add(&set, ch);
            n = ast_charset(&set);
            if (!res) {
                res = n;
            } else {
                res = ast_new(AST_CONCAT, res, n);
            }
        }
        if (!rp_match(p, '"')) {
            fatal("unterminated string literal");
        }
        if (!res) {
            return ast_empty();
        }
        return res;
    }
    if (c == '[') {
        CharSet set;
        int negate = 0;
        rp_get(p);
        charset_clear(&set);
        if (rp_match(p, '^')) {
            negate = 1;
        }
        while (rp_peek(p) && rp_peek(p) != ']') {
            unsigned char first;
            if (isspace((unsigned char)rp_peek(p))) {
                rp_get(p);
                continue;
            }
            first = parse_set_char(p);
            if (rp_peek(p) == '-' && p->s[p->pos + 1] && p->s[p->pos + 1] != ']') {
                unsigned char second;
                rp_get(p); /* '-' */
                while (isspace((unsigned char)rp_peek(p))) {
                    rp_get(p);
                }
                second = parse_set_char(p);
                charset_add_range(&set, first, second);
            } else {
                charset_add(&set, first);
            }
        }
        if (!rp_match(p, ']')) {
            fatal("unterminated set");
        }
        if (negate) {
            charset_not(&set);
        }
        return ast_charset(&set);
    }
    if (c == '_') {
        CharSet set;
        rp_get(p);
        charset_fill(&set);
        return ast_charset(&set);
    }
    if (is_ident_start_char(c)) {
        size_t start = p->pos;
        size_t end;
        char *id;
        while (is_ident_char((unsigned char)p->s[p->pos])) {
            p->pos++;
        }
        end = p->pos;
        id = trim_copy(p->s + start, end - start);
        if (strcmp(id, "eof") == 0) {
            free(id);
            return ast_new(AST_EOF, NULL, NULL);
        }
        {
            AST *d = resolve_let_ast(p->lets, id);
            free(id);
            return d;
        }
    }
    fatal("unexpected regex token near: %.30s", p->s + p->pos);
    return NULL;
}

static AST *parse_regex_diff_atom(RegexParser *p) {
    AST *left = parse_regex_primary(p);
    if (!left) {
        return NULL;
    }
    while (1) {
        rp_skip_ws(p);
        if (!rp_match(p, '#')) {
            break;
        }
        {
            AST *right = parse_regex_primary(p);
            if (!right) {
                fatal("missing rhs of '#'");
            }
            left = make_diff_ast(left, right);
        }
    }
    return left;
}

static AST *parse_regex_postfix(RegexParser *p) {
    AST *n = parse_regex_diff_atom(p);
    if (!n) {
        return NULL;
    }
    while (1) {
        rp_skip_ws(p);
        if (rp_match(p, '*')) {
            n = ast_new(AST_STAR, n, NULL);
        } else if (rp_match(p, '+')) {
            n = ast_new(AST_PLUS, n, NULL);
        } else if (rp_match(p, '?')) {
            n = ast_new(AST_QMARK, n, NULL);
        } else {
            break;
        }
    }
    return n;
}

static int regex_next_starts_primary(RegexParser *p) {
    size_t i = p->pos;
    int c;
    while (p->s[i] && isspace((unsigned char)p->s[i])) {
        i++;
    }
    c = (unsigned char)p->s[i];
    if (!c) {
        return 0;
    }
    if (c == ')' || c == '|') {
        return 0;
    }
    if (c == '*' || c == '+' || c == '?' || c == '#') {
        return 0;
    }
    return c == '(' || c == '\'' || c == '"' || c == '[' || c == '_' || is_ident_start_char(c);
}

static AST *parse_regex_concat(RegexParser *p) {
    AST *left = parse_regex_postfix(p);
    if (!left) {
        return NULL;
    }
    while (regex_next_starts_primary(p)) {
        AST *right = parse_regex_postfix(p);
        if (!right) {
            break;
        }
        left = ast_new(AST_CONCAT, left, right);
    }
    return left;
}

static AST *parse_regex_expr(RegexParser *p) {
    AST *left = parse_regex_concat(p);
    if (!left) {
        return ast_empty();
    }
    while (1) {
        rp_skip_ws(p);
        if (!rp_match(p, '|')) {
            break;
        }
        {
            AST *right = parse_regex_concat(p);
            if (!right) {
                right = ast_empty();
            }
            left = ast_new(AST_ALT, left, right);
        }
    }
    return left;
}

AST *parse_regex_with_lets(const char *text, LetVec *lets) {
    RegexParser p;
    AST *ast;
    p.s = text;
    p.pos = 0;
    p.lets = lets;
    ast = parse_regex_expr(&p);
    rp_skip_ws(&p);
    if (rp_peek(&p) != 0) {
        fatal("trailing regex text: %.30s", p.s + p.pos);
    }
    return ast;
}
