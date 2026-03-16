#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned char bits[256];
} CharSet;

typedef enum {
    AST_EMPTY,
    AST_CHARSET,
    AST_CONCAT,
    AST_ALT,
    AST_STAR,
    AST_PLUS,
    AST_QMARK,
    AST_DIFF,
    AST_EOF
} ASTType;

typedef struct AST AST;
struct AST {
    ASTType type;
    AST *left;
    AST *right;
    CharSet set;
};

typedef struct {
    char *name;
    char *regex;
    AST *ast;
    int resolving;
} LetDef;

typedef struct {
    char *regex;
    char *action;
    char *token_name;
    int skip;
    int is_eof;
    AST *ast;
} RuleDef;

typedef struct {
    LetDef *data;
    int count;
    int cap;
} LetVec;

typedef struct {
    RuleDef *data;
    int count;
    int cap;
} RuleVec;

typedef struct {
    LetVec lets;
    RuleVec rules;
    char *header;
    char *trailer;
    char *entrypoint;
    char *entry_args;
} YalSpec;

typedef struct {
    int to;
    int set_id; /* -1 => epsilon */
} NFAEdge;

typedef struct {
    NFAEdge *edges;
    int edge_count;
    int edge_cap;
    int accept_token; /* -1 => non-accepting */
} NFAState;

typedef struct {
    NFAState *states;
    int state_count;
    int state_cap;
    CharSet *sets;
    int set_count;
    int set_cap;
} NFA;

typedef struct {
    int start;
    int end;
} Frag;

typedef struct {
    uint64_t **subsets; /* one bitset per DFA state */
    int **trans;        /* [state][byte] */
    int *accept;        /* token index per DFA state */
    int count;
    int cap;
    int words;
} DFA;

typedef struct {
    const char *s;
    size_t pos;
    LetVec *lets;
} RegexParser;

static void fatal(const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "error: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}

static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) {
        fatal("out of memory");
    }
    return p;
}

static void *xcalloc(size_t n, size_t sz) {
    void *p = calloc(n, sz);
    if (!p) {
        fatal("out of memory");
    }
    return p;
}

static void *xrealloc(void *p, size_t n) {
    void *r = realloc(p, n);
    if (!r) {
        fatal("out of memory");
    }
    return r;
}

static char *xstrdup(const char *s) {
    size_t n = strlen(s);
    char *r = (char *)xmalloc(n + 1);
    memcpy(r, s, n + 1);
    return r;
}

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    size_t n;
    char *buf;
    if (!f) {
        fatal("cannot open input file: %s", path);
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        fatal("cannot seek input file");
    }
    n = (size_t)ftell(f);
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        fatal("cannot seek input file");
    }
    buf = (char *)xmalloc(n + 1);
    if (n > 0 && fread(buf, 1, n, f) != n) {
        fclose(f);
        free(buf);
        fatal("cannot read input file");
    }
    fclose(f);
    buf[n] = '\0';
    if (out_len) {
        *out_len = n;
    }
    return buf;
}

static char *trim_copy(const char *s, size_t n) {
    size_t i = 0;
    size_t j = n;
    char *r;
    while (i < n && isspace((unsigned char)s[i])) {
        i++;
    }
    while (j > i && isspace((unsigned char)s[j - 1])) {
        j--;
    }
    r = (char *)xmalloc(j - i + 1);
    memcpy(r, s + i, j - i);
    r[j - i] = '\0';
    return r;
}

static void append_text_block(char **dst, const char *text) {
    size_t a, b;
    char *r;
    if (!text || !*text) {
        return;
    }
    if (!*dst) {
        *dst = xstrdup(text);
        return;
    }
    a = strlen(*dst);
    b = strlen(text);
    r = (char *)xmalloc(a + b + 2);
    memcpy(r, *dst, a);
    if (a > 0 && r[a - 1] != '\n') {
        r[a++] = '\n';
    }
    memcpy(r + a, text, b);
    r[a + b] = '\0';
    free(*dst);
    *dst = r;
}

static void lets_push(LetVec *v, LetDef item) {
    if (v->count == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 8;
        v->data = (LetDef *)xrealloc(v->data, (size_t)v->cap * sizeof(LetDef));
    }
    v->data[v->count++] = item;
}

static void rules_push(RuleVec *v, RuleDef item) {
    if (v->count == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 8;
        v->data = (RuleDef *)xrealloc(v->data, (size_t)v->cap * sizeof(RuleDef));
    }
    v->data[v->count++] = item;
}

static void charset_clear(CharSet *s) {
    memset(s->bits, 0, sizeof(s->bits));
}

static void charset_fill(CharSet *s) {
    memset(s->bits, 1, sizeof(s->bits));
}

static void charset_add(CharSet *s, unsigned char c) {
    s->bits[c] = 1;
}

static void charset_add_range(CharSet *s, unsigned char a, unsigned char b) {
    unsigned int i;
    if (a > b) {
        unsigned char t = a;
        a = b;
        b = t;
    }
    for (i = a; i <= b; i++) {
        s->bits[i] = 1;
    }
}

static void charset_union(CharSet *out, const CharSet *a, const CharSet *b) {
    int i;
    for (i = 0; i < 256; i++) {
        out->bits[i] = (unsigned char)(a->bits[i] || b->bits[i]);
    }
}

static void charset_diff(CharSet *out, const CharSet *a, const CharSet *b) {
    int i;
    for (i = 0; i < 256; i++) {
        out->bits[i] = (unsigned char)(a->bits[i] && !b->bits[i]);
    }
}

static void charset_not(CharSet *s) {
    int i;
    for (i = 0; i < 256; i++) {
        s->bits[i] = (unsigned char)!s->bits[i];
    }
}

static int charset_count(const CharSet *s) {
    int i;
    int n = 0;
    for (i = 0; i < 256; i++) {
        if (s->bits[i]) {
            n++;
        }
    }
    return n;
}

static AST *ast_new(ASTType t, AST *l, AST *r) {
    AST *n = (AST *)xcalloc(1, sizeof(AST));
    n->type = t;
    n->left = l;
    n->right = r;
    return n;
}

static AST *ast_empty(void) {
    return ast_new(AST_EMPTY, NULL, NULL);
}

static AST *ast_charset(const CharSet *s) {
    AST *n = ast_new(AST_CHARSET, NULL, NULL);
    n->set = *s;
    return n;
}

static AST *ast_clone(const AST *n) {
    AST *c;
    if (!n) {
        return NULL;
    }
    c = ast_new(n->type, NULL, NULL);
    c->set = n->set;
    c->left = ast_clone(n->left);
    c->right = ast_clone(n->right);
    return c;
}

static void ast_free(AST *n) {
    if (!n) {
        return;
    }
    ast_free(n->left);
    ast_free(n->right);
    free(n);
}

static LetDef *find_let(LetVec *lets, const char *name) {
    int i;
    for (i = 0; i < lets->count; i++) {
        if (strcmp(lets->data[i].name, name) == 0) {
            return &lets->data[i];
        }
    }
    return NULL;
}

static int is_ident_start_char(int c) {
    return isalpha((unsigned char)c) || c == '_';
}

static int is_ident_char(int c) {
    return isalnum((unsigned char)c) || c == '_';
}

static void skip_ws(const char **p) {
    while (**p && isspace((unsigned char)**p)) {
        (*p)++;
    }
}

static int starts_kw(const char *p, const char *kw) {
    size_t n = strlen(kw);
    if (strncmp(p, kw, n) != 0) {
        return 0;
    }
    if (is_ident_char((unsigned char)p[n])) {
        return 0;
    }
    return 1;
}

static char *parse_identifier(const char **p) {
    const char *s = *p;
    if (!is_ident_start_char((unsigned char)*s)) {
        fatal("expected identifier near: %.30s", s);
    }
    s++;
    while (is_ident_char((unsigned char)*s)) {
        s++;
    }
    {
        char *id = trim_copy(*p, (size_t)(s - *p));
        *p = s;
        return id;
    }
}

static char *capture_balanced(const char **p, char open, char close) {
    const char *s = *p;
    const char *it;
    int depth = 0;
    int in_sq = 0;
    int in_dq = 0;
    int esc = 0;
    if (*s != open) {
        fatal("expected '%c'", open);
    }
    it = s;
    while (*it) {
        char c = *it;
        if (in_sq) {
            if (!esc && c == '\\') {
                esc = 1;
            } else if (!esc && c == '\'') {
                in_sq = 0;
            } else {
                esc = 0;
            }
        } else if (in_dq) {
            if (!esc && c == '\\') {
                esc = 1;
            } else if (!esc && c == '"') {
                in_dq = 0;
            } else {
                esc = 0;
            }
        } else {
            if (c == '\'') {
                in_sq = 1;
                esc = 0;
            } else if (c == '"') {
                in_dq = 1;
                esc = 0;
            } else if (c == open) {
                depth++;
            } else if (c == close) {
                depth--;
                if (depth == 0) {
                    char *r = (char *)xmalloc((size_t)(it - s));
                    memcpy(r, s + 1, (size_t)(it - s - 1));
                    r[it - s - 1] = '\0';
                    *p = it + 1;
                    return r;
                }
            }
        }
        it++;
    }
    fatal("unclosed block starting with '%c'", open);
    return NULL;
}

static char *strip_comments(const char *src) {
    size_t n = strlen(src);
    char *out = (char *)xmalloc(n + 1);
    size_t oi = 0;
    size_t i = 0;
    int depth = 0;
    int in_sq = 0;
    int in_dq = 0;
    int esc = 0;
    while (i < n) {
        if (depth == 0 && !in_sq && !in_dq && src[i] == '(' && i + 1 < n && src[i + 1] == '*') {
            depth = 1;
            i += 2;
            continue;
        }
        if (depth > 0) {
            if (src[i] == '(' && i + 1 < n && src[i + 1] == '*') {
                depth++;
                i += 2;
                continue;
            }
            if (src[i] == '*' && i + 1 < n && src[i + 1] == ')') {
                depth--;
                i += 2;
                continue;
            }
            if (src[i] == '\n' || src[i] == '\r') {
                out[oi++] = src[i];
            }
            i++;
            continue;
        }
        if (in_sq) {
            out[oi++] = src[i];
            if (!esc && src[i] == '\\') {
                esc = 1;
            } else if (!esc && src[i] == '\'') {
                in_sq = 0;
            } else {
                esc = 0;
            }
            i++;
            continue;
        }
        if (in_dq) {
            out[oi++] = src[i];
            if (!esc && src[i] == '\\') {
                esc = 1;
            } else if (!esc && src[i] == '"') {
                in_dq = 0;
            } else {
                esc = 0;
            }
            i++;
            continue;
        }
        if (src[i] == '\'') {
            in_sq = 1;
            esc = 0;
            out[oi++] = src[i++];
            continue;
        }
        if (src[i] == '"') {
            in_dq = 1;
            esc = 0;
            out[oi++] = src[i++];
            continue;
        }
        out[oi++] = src[i++];
    }
    out[oi] = '\0';
    return out;
}

static char *capture_regex_before_action(const char **p) {
    const char *s = *p;
    const char *it = s;
    int paren = 0;
    int bracket = 0;
    int in_sq = 0;
    int in_dq = 0;
    int esc = 0;
    while (*it) {
        char c = *it;
        if (in_sq) {
            if (!esc && c == '\\') {
                esc = 1;
            } else if (!esc && c == '\'') {
                in_sq = 0;
            } else {
                esc = 0;
            }
        } else if (in_dq) {
            if (!esc && c == '\\') {
                esc = 1;
            } else if (!esc && c == '"') {
                in_dq = 0;
            } else {
                esc = 0;
            }
        } else {
            if (c == '\'') {
                in_sq = 1;
                esc = 0;
            } else if (c == '"') {
                in_dq = 1;
                esc = 0;
            } else if (c == '[') {
                bracket++;
            } else if (c == ']') {
                if (bracket > 0) {
                    bracket--;
                }
            } else if (c == '(') {
                paren++;
            } else if (c == ')') {
                if (paren > 0) {
                    paren--;
                }
            } else if (c == '{' && paren == 0 && bracket == 0) {
                break;
            }
        }
        it++;
    }
    if (*it != '{') {
        fatal("missing action block '{...}' near: %.40s", s);
    }
    *p = it;
    return trim_copy(s, (size_t)(it - s));
}

static void parse_spec(const char *src, YalSpec *spec) {
    const char *p = src;
    memset(spec, 0, sizeof(*spec));

    skip_ws(&p);
    if (*p == '{') {
        spec->header = capture_balanced(&p, '{', '}');
    }

    while (1) {
        skip_ws(&p);
        if (starts_kw(p, "rule")) {
            break;
        }
        if (starts_kw(p, "let")) {
            const char *line_start;
            const char *it;
            LetDef d;
            char *name;
            char *regex;
            p += 3;
            skip_ws(&p);
            name = parse_identifier(&p);
            skip_ws(&p);
            if (*p != '=') {
                free(name);
                fatal("expected '=' in let definition near: %.40s", p);
            }
            p++;
            line_start = p;
            it = p;
            while (*it && *it != '\n' && *it != '\r') {
                it++;
            }
            regex = trim_copy(line_start, (size_t)(it - line_start));
            d.name = name;
            d.regex = regex;
            d.ast = NULL;
            d.resolving = 0;
            lets_push(&spec->lets, d);
            p = it;
            while (*p == '\n' || *p == '\r') {
                p++;
            }
            continue;
        }
        if (*p) {
            const char *line_start = p;
            const char *it = p;
            char *line;
            while (*it && *it != '\n' && *it != '\r') {
                it++;
            }
            line = trim_copy(line_start, (size_t)(it - line_start));
            if (*line) {
                append_text_block(&spec->header, line);
            }
            free(line);
            p = it;
            while (*p == '\n' || *p == '\r') {
                p++;
            }
            continue;
        }
        break;
    }

    skip_ws(&p);
    if (!starts_kw(p, "rule")) {
        fatal("missing 'rule' section");
    }
    p += 4;
    skip_ws(&p);
    if (is_ident_start_char((unsigned char)*p)) {
        spec->entrypoint = parse_identifier(&p);
    }
    {
        const char *arg_start;
        const char *arg_end;
        skip_ws(&p);
        arg_start = p;
        while (*p && *p != '=') {
            p++;
        }
        arg_end = p;
        spec->entry_args = trim_copy(arg_start, (size_t)(arg_end - arg_start));
        if (spec->entry_args && spec->entry_args[0] == '\0') {
            free(spec->entry_args);
            spec->entry_args = NULL;
        }
    }
    while (*p && *p != '=') {
        p++;
    }
    if (*p != '=') {
        fatal("missing '=' after rule declaration");
    }
    p++;

    while (1) {
        RuleDef r;
        char *regex;
        char *action;
        skip_ws(&p);
        if (*p == '\0') {
            break;
        }
        if (*p == '{') {
            char *trailer = capture_balanced(&p, '{', '}');
            append_text_block(&spec->trailer, trailer);
            free(trailer);
            skip_ws(&p);
            if (*p == '\0') {
                break;
            }
            continue;
        }
        if (*p == '|') {
            p++;
            skip_ws(&p);
        }
        if (*p == '\0') {
            break;
        }
        regex = capture_regex_before_action(&p);
        skip_ws(&p);
        if (*p != '{') {
            free(regex);
            fatal("expected action block near: %.40s", p);
        }
        action = capture_balanced(&p, '{', '}');
        memset(&r, 0, sizeof(r));
        r.regex = regex;
        r.action = action;
        r.token_name = NULL;
        r.skip = 0;
        r.is_eof = 0;
        r.ast = NULL;
        rules_push(&spec->rules, r);
        skip_ws(&p);
    }
}

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

static int ast_eval_charset(const AST *n, CharSet *out) {
    CharSet a, b;
    if (!n) {
        return 0;
    }
    switch (n->type) {
    case AST_CHARSET:
        *out = n->set;
        return 1;
    case AST_ALT:
        if (!ast_eval_charset(n->left, &a) || !ast_eval_charset(n->right, &b)) {
            return 0;
        }
        charset_union(out, &a, &b);
        return 1;
    case AST_DIFF:
        if (!ast_eval_charset(n->left, &a) || !ast_eval_charset(n->right, &b)) {
            return 0;
        }
        charset_diff(out, &a, &b);
        return 1;
    default:
        return 0;
    }
}

static AST *make_diff_ast(AST *left, AST *right) {
    CharSet a, b, d;
    if (ast_eval_charset(left, &a) && ast_eval_charset(right, &b)) {
        AST *n;
        charset_diff(&d, &a, &b);
        ast_free(left);
        ast_free(right);
        n = ast_charset(&d);
        return n;
    }
    return ast_new(AST_DIFF, left, right);
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

static AST *parse_regex_with_lets(const char *text, LetVec *lets) {
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

static int nfa_add_state(NFA *nfa) {
    NFAState s;
    if (nfa->state_count == nfa->state_cap) {
        nfa->state_cap = nfa->state_cap ? nfa->state_cap * 2 : 32;
        nfa->states = (NFAState *)xrealloc(nfa->states, (size_t)nfa->state_cap * sizeof(NFAState));
    }
    memset(&s, 0, sizeof(s));
    s.accept_token = -1;
    nfa->states[nfa->state_count] = s;
    return nfa->state_count++;
}

static void nfa_add_edge(NFA *nfa, int from, int to, int set_id) {
    NFAState *s = &nfa->states[from];
    NFAEdge e;
    if (s->edge_count == s->edge_cap) {
        s->edge_cap = s->edge_cap ? s->edge_cap * 2 : 4;
        s->edges = (NFAEdge *)xrealloc(s->edges, (size_t)s->edge_cap * sizeof(NFAEdge));
    }
    e.to = to;
    e.set_id = set_id;
    s->edges[s->edge_count++] = e;
}

static int nfa_add_charset(NFA *nfa, const CharSet *set) {
    if (nfa->set_count == nfa->set_cap) {
        nfa->set_cap = nfa->set_cap ? nfa->set_cap * 2 : 32;
        nfa->sets = (CharSet *)xrealloc(nfa->sets, (size_t)nfa->set_cap * sizeof(CharSet));
    }
    nfa->sets[nfa->set_count] = *set;
    return nfa->set_count++;
}

static Frag build_nfa_from_ast(NFA *nfa, AST *n) {
    Frag f;
    if (!n) {
        f.start = nfa_add_state(nfa);
        f.end = nfa_add_state(nfa);
        nfa_add_edge(nfa, f.start, f.end, -1);
        return f;
    }
    switch (n->type) {
    case AST_EMPTY:
    case AST_EOF:
        f.start = nfa_add_state(nfa);
        f.end = nfa_add_state(nfa);
        nfa_add_edge(nfa, f.start, f.end, -1);
        return f;
    case AST_CHARSET: {
        int set_id = nfa_add_charset(nfa, &n->set);
        f.start = nfa_add_state(nfa);
        f.end = nfa_add_state(nfa);
        nfa_add_edge(nfa, f.start, f.end, set_id);
        return f;
    }
    case AST_CONCAT: {
        Frag a = build_nfa_from_ast(nfa, n->left);
        Frag b = build_nfa_from_ast(nfa, n->right);
        nfa_add_edge(nfa, a.end, b.start, -1);
        f.start = a.start;
        f.end = b.end;
        return f;
    }
    case AST_ALT: {
        Frag a = build_nfa_from_ast(nfa, n->left);
        Frag b = build_nfa_from_ast(nfa, n->right);
        f.start = nfa_add_state(nfa);
        f.end = nfa_add_state(nfa);
        nfa_add_edge(nfa, f.start, a.start, -1);
        nfa_add_edge(nfa, f.start, b.start, -1);
        nfa_add_edge(nfa, a.end, f.end, -1);
        nfa_add_edge(nfa, b.end, f.end, -1);
        return f;
    }
    case AST_STAR: {
        Frag a = build_nfa_from_ast(nfa, n->left);
        f.start = nfa_add_state(nfa);
        f.end = nfa_add_state(nfa);
        nfa_add_edge(nfa, f.start, a.start, -1);
        nfa_add_edge(nfa, f.start, f.end, -1);
        nfa_add_edge(nfa, a.end, a.start, -1);
        nfa_add_edge(nfa, a.end, f.end, -1);
        return f;
    }
    case AST_PLUS: {
        Frag a = build_nfa_from_ast(nfa, n->left);
        f.start = nfa_add_state(nfa);
        f.end = nfa_add_state(nfa);
        nfa_add_edge(nfa, f.start, a.start, -1);
        nfa_add_edge(nfa, a.end, a.start, -1);
        nfa_add_edge(nfa, a.end, f.end, -1);
        return f;
    }
    case AST_QMARK: {
        Frag a = build_nfa_from_ast(nfa, n->left);
        f.start = nfa_add_state(nfa);
        f.end = nfa_add_state(nfa);
        nfa_add_edge(nfa, f.start, a.start, -1);
        nfa_add_edge(nfa, f.start, f.end, -1);
        nfa_add_edge(nfa, a.end, f.end, -1);
        return f;
    }
    case AST_DIFF: {
        CharSet d;
        int set_id;
        if (!ast_eval_charset(n, &d)) {
            fatal("operator '#' is supported only for character sets");
        }
        set_id = nfa_add_charset(nfa, &d);
        f.start = nfa_add_state(nfa);
        f.end = nfa_add_state(nfa);
        nfa_add_edge(nfa, f.start, f.end, set_id);
        return f;
    }
    }
    fatal("internal AST type");
    return f;
}

static void bitset_zero(uint64_t *bits, int words) {
    memset(bits, 0, (size_t)words * sizeof(uint64_t));
}

static void bitset_set(uint64_t *bits, int i) {
    bits[i >> 6] |= (uint64_t)1u << (i & 63);
}

static int bitset_get(const uint64_t *bits, int i) {
    return (int)((bits[i >> 6] >> (i & 63)) & 1u);
}

static int bitset_empty(const uint64_t *bits, int words) {
    int i;
    for (i = 0; i < words; i++) {
        if (bits[i]) {
            return 0;
        }
    }
    return 1;
}

static void epsilon_closure(const NFA *nfa, uint64_t *bits, int words) {
    int *stack = (int *)xmalloc((size_t)nfa->state_count * sizeof(int));
    int top = 0;
    int i;
    (void)words;
    for (i = 0; i < nfa->state_count; i++) {
        if (bitset_get(bits, i)) {
            stack[top++] = i;
        }
    }
    while (top > 0) {
        int s = stack[--top];
        int e;
        for (e = 0; e < nfa->states[s].edge_count; e++) {
            NFAEdge ed = nfa->states[s].edges[e];
            if (ed.set_id == -1 && !bitset_get(bits, ed.to)) {
                bitset_set(bits, ed.to);
                stack[top++] = ed.to;
            }
        }
    }
    free(stack);
}

static void move_on_char(const NFA *nfa, const uint64_t *from, unsigned char c, uint64_t *out, int words) {
    int i, e;
    bitset_zero(out, words);
    for (i = 0; i < nfa->state_count; i++) {
        if (!bitset_get(from, i)) {
            continue;
        }
        for (e = 0; e < nfa->states[i].edge_count; e++) {
            NFAEdge ed = nfa->states[i].edges[e];
            if (ed.set_id >= 0 && nfa->sets[ed.set_id].bits[c]) {
                bitset_set(out, ed.to);
            }
        }
    }
    if (!bitset_empty(out, words)) {
        epsilon_closure(nfa, out, words);
    }
}

static int choose_accept_token(const NFA *nfa, const uint64_t *subset) {
    int best = -1;
    int i;
    for (i = 0; i < nfa->state_count; i++) {
        int tok;
        if (!bitset_get(subset, i)) {
            continue;
        }
        tok = nfa->states[i].accept_token;
        if (tok >= 0 && (best < 0 || tok < best)) {
            best = tok;
        }
    }
    return best;
}

static int dfa_find_subset(const DFA *dfa, const uint64_t *bits) {
    int i;
    for (i = 0; i < dfa->count; i++) {
        if (memcmp(dfa->subsets[i], bits, (size_t)dfa->words * sizeof(uint64_t)) == 0) {
            return i;
        }
    }
    return -1;
}

static int dfa_add_state(DFA *dfa, const uint64_t *bits, int accept_token) {
    int i;
    if (dfa->count == dfa->cap) {
        int old = dfa->cap;
        dfa->cap = dfa->cap ? dfa->cap * 2 : 16;
        dfa->subsets = (uint64_t **)xrealloc(dfa->subsets, (size_t)dfa->cap * sizeof(uint64_t *));
        dfa->trans = (int **)xrealloc(dfa->trans, (size_t)dfa->cap * sizeof(int *));
        dfa->accept = (int *)xrealloc(dfa->accept, (size_t)dfa->cap * sizeof(int));
        for (i = old; i < dfa->cap; i++) {
            dfa->subsets[i] = NULL;
            dfa->trans[i] = NULL;
        }
    }
    dfa->subsets[dfa->count] = (uint64_t *)xmalloc((size_t)dfa->words * sizeof(uint64_t));
    memcpy(dfa->subsets[dfa->count], bits, (size_t)dfa->words * sizeof(uint64_t));
    dfa->trans[dfa->count] = (int *)xmalloc(256 * sizeof(int));
    for (i = 0; i < 256; i++) {
        dfa->trans[dfa->count][i] = -1;
    }
    dfa->accept[dfa->count] = accept_token;
    return dfa->count++;
}

static DFA build_dfa(const NFA *nfa, int nfa_start) {
    DFA dfa;
    uint64_t *start_bits;
    uint64_t *tmp;
    int i;
    memset(&dfa, 0, sizeof(dfa));
    dfa.words = (nfa->state_count + 63) / 64;
    start_bits = (uint64_t *)xcalloc((size_t)dfa.words, sizeof(uint64_t));
    tmp = (uint64_t *)xcalloc((size_t)dfa.words, sizeof(uint64_t));
    bitset_set(start_bits, nfa_start);
    epsilon_closure(nfa, start_bits, dfa.words);
    dfa_add_state(&dfa, start_bits, choose_accept_token(nfa, start_bits));
    for (i = 0; i < dfa.count; i++) {
        int c;
        for (c = 0; c < 256; c++) {
            int j;
            move_on_char(nfa, dfa.subsets[i], (unsigned char)c, tmp, dfa.words);
            if (bitset_empty(tmp, dfa.words)) {
                dfa.trans[i][c] = -1;
                continue;
            }
            j = dfa_find_subset(&dfa, tmp);
            if (j < 0) {
                j = dfa_add_state(&dfa, tmp, choose_accept_token(nfa, tmp));
            }
            dfa.trans[i][c] = j;
        }
    }
    free(start_bits);
    free(tmp);
    return dfa;
}

static char *to_lower_copy(const char *s) {
    size_t n = strlen(s);
    size_t i;
    char *r = (char *)xmalloc(n + 1);
    for (i = 0; i < n; i++) {
        r[i] = (char)tolower((unsigned char)s[i]);
    }
    r[n] = '\0';
    return r;
}

static int ci_match_word(const char *p, const char *w) {
    size_t i;
    for (i = 0; w[i]; i++) {
        if (!p[i]) {
            return 0;
        }
        if (tolower((unsigned char)p[i]) != tolower((unsigned char)w[i])) {
            return 0;
        }
    }
    if (is_ident_char((unsigned char)p[i])) {
        return 0;
    }
    return 1;
}

static char *infer_token_name(const char *action, int idx, int *skip_out) {
    const char *p = action;
    *skip_out = 0;
    while (*p) {
        if (ci_match_word(p, "return")) {
            p += 6;
            while (*p && isspace((unsigned char)*p)) {
                p++;
            }
            if (ci_match_word(p, "lexbuf")) {
                *skip_out = 1;
                return xstrdup("SKIP");
            }
            if (is_ident_start_char((unsigned char)*p)) {
                const char *s = p;
                while (is_ident_char((unsigned char)*p)) {
                    p++;
                }
                return trim_copy(s, (size_t)(p - s));
            }
        }
        p++;
    }
    {
        char name[64];
        char *lower = to_lower_copy(action);
        if (strstr(lower, "skip")) {
            *skip_out = 1;
        }
        free(lower);
        snprintf(name, sizeof(name), "TOKEN_%d", idx + 1);
        return xstrdup(name);
    }
}

static int is_eof_regex(const char *regex) {
    char *t = trim_copy(regex, strlen(regex));
    int r = strcmp(t, "eof") == 0;
    free(t);
    return r;
}

typedef struct {
    FILE *f;
    int next_id;
} DotCtx;

static void dot_escape(FILE *f, const char *s) {
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (c == '"' || c == '\\') {
            fputc('\\', f);
            fputc((int)c, f);
        } else if (c == '\n') {
            fputs("\\n", f);
        } else if (c < 32 || c > 126) {
            fprintf(f, "\\x%02X", c);
        } else {
            fputc((int)c, f);
        }
    }
}

static void charset_label(const CharSet *set, char *buf, size_t n) {
    int shown = 0;
    int total = charset_count(set);
    size_t pos = 0;
    int c;
    pos += (size_t)snprintf(buf + pos, n - pos, "set(");
    for (c = 0; c < 256 && shown < 8; c++) {
        if (set->bits[c]) {
            if (shown > 0) {
                pos += (size_t)snprintf(buf + pos, n - pos, ",");
            }
            if (isprint(c) && c != '"' && c != '\\') {
                pos += (size_t)snprintf(buf + pos, n - pos, "'%c'", c);
            } else {
                pos += (size_t)snprintf(buf + pos, n - pos, "0x%02X", c);
            }
            shown++;
        }
    }
    if (total > shown) {
        pos += (size_t)snprintf(buf + pos, n - pos, ",...");
    }
    (void)snprintf(buf + pos, n - pos, ")");
}

static int emit_ast_dot(DotCtx *ctx, AST *n) {
    int id = ctx->next_id++;
    if (!n) {
        fprintf(ctx->f, "  n%d [label=\"<null>\"];\n", id);
        return id;
    }
    switch (n->type) {
    case AST_EMPTY:
        fprintf(ctx->f, "  n%d [label=\"epsilon\"];\n", id);
        break;
    case AST_EOF:
        fprintf(ctx->f, "  n%d [label=\"eof\"];\n", id);
        break;
    case AST_CHARSET: {
        char label[256];
        charset_label(&n->set, label, sizeof(label));
        fprintf(ctx->f, "  n%d [label=\"", id);
        dot_escape(ctx->f, label);
        fprintf(ctx->f, "\"];\n");
        break;
    }
    case AST_CONCAT:
        fprintf(ctx->f, "  n%d [label=\"concat\"];\n", id);
        break;
    case AST_ALT:
        fprintf(ctx->f, "  n%d [label=\"|\"];\n", id);
        break;
    case AST_STAR:
        fprintf(ctx->f, "  n%d [label=\"*\"];\n", id);
        break;
    case AST_PLUS:
        fprintf(ctx->f, "  n%d [label=\"+\"];\n", id);
        break;
    case AST_QMARK:
        fprintf(ctx->f, "  n%d [label=\"?\"];\n", id);
        break;
    case AST_DIFF:
        fprintf(ctx->f, "  n%d [label=\"#\"];\n", id);
        break;
    }
    if (n->left) {
        int l = emit_ast_dot(ctx, n->left);
        fprintf(ctx->f, "  n%d -> n%d;\n", id, l);
    }
    if (n->right) {
        int r = emit_ast_dot(ctx, n->right);
        fprintf(ctx->f, "  n%d -> n%d;\n", id, r);
    }
    return id;
}

static void emit_dot_file(const char *path, RuleDef **active_rules, int active_count, RuleDef *eof_rule) {
    FILE *f = fopen(path, "w");
    DotCtx ctx;
    int i;
    if (!f) {
        fatal("cannot open dot output file: %s", path);
    }
    ctx.f = f;
    ctx.next_id = 1;
    fprintf(f, "digraph RegexTree {\n");
    fprintf(f, "  rankdir=TB;\n");
    fprintf(f, "  node [shape=box, fontname=\"Helvetica\"];\n");
    fprintf(f, "  n0 [label=\"TOKENS\"];\n");
    for (i = 0; i < active_count; i++) {
        int tok_node = ctx.next_id++;
        int root;
        fprintf(f, "  n%d [label=\"TOKEN ", tok_node);
        dot_escape(f, active_rules[i]->token_name);
        fprintf(f, "\"];\n");
        fprintf(f, "  n0 -> n%d;\n", tok_node);
        root = emit_ast_dot(&ctx, active_rules[i]->ast);
        fprintf(f, "  n%d -> n%d;\n", tok_node, root);
    }
    if (eof_rule) {
        int tok_node = ctx.next_id++;
        fprintf(f, "  n%d [label=\"TOKEN ", tok_node);
        dot_escape(f, eof_rule->token_name);
        fprintf(f, " (eof)\"];\n");
        fprintf(f, "  n0 -> n%d;\n", tok_node);
    }
    fprintf(f, "}\n");
    fclose(f);
}

static void emit_c_string(FILE *f, const char *s) {
    fputc('"', f);
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        switch (c) {
        case '\\':
            fputs("\\\\", f);
            break;
        case '"':
            fputs("\\\"", f);
            break;
        case '\n':
            fputs("\\n", f);
            break;
        case '\r':
            fputs("\\r", f);
            break;
        case '\t':
            fputs("\\t", f);
            break;
        default:
            if (c < 32 || c > 126) {
                fprintf(f, "\\x%02X", c);
            } else {
                fputc((int)c, f);
            }
            break;
        }
    }
    fputc('"', f);
}

static void emit_generated_lexer(const char *path,
                                 const DFA *dfa,
                                 RuleDef **active_rules,
                                 int active_count,
                                 RuleDef *eof_rule,
                                 const YalSpec *spec) {
    FILE *f = fopen(path, "w");
    int i, j;
    if (!f) {
        fatal("cannot open lexer output file: %s", path);
    }

    fprintf(f, "#include <stdio.h>\n");
    fprintf(f, "#include <stdlib.h>\n");
    fprintf(f, "#include <string.h>\n");
    fprintf(f, "\n");
    if ((spec->entrypoint && *spec->entrypoint) || (spec->entry_args && *spec->entry_args)) {
        fprintf(f, "/* YALex entrypoint: %s%s%s */\n",
                (spec->entrypoint && *spec->entrypoint) ? spec->entrypoint : "entrypoint",
                (spec->entry_args && *spec->entry_args) ? " " : "",
                (spec->entry_args && *spec->entry_args) ? spec->entry_args : "");
    }
    if (spec->header && *spec->header) {
        fprintf(f, "/* ===== BEGIN YALex header ===== */\n");
        fprintf(f, "%s\n", spec->header);
        fprintf(f, "/* ===== END YALex header ===== */\n\n");
    }
    fprintf(f, "#define DFA_STATES %d\n", dfa->count);
    fprintf(f, "#define TOKEN_COUNT %d\n", active_count);
    fprintf(f, "\n");

    fprintf(f, "static const char *TOKEN_NAMES[TOKEN_COUNT] = {\n");
    for (i = 0; i < active_count; i++) {
        fprintf(f, "    ");
        emit_c_string(f, active_rules[i]->token_name);
        fprintf(f, "%s\n", (i + 1 < active_count) ? "," : "");
    }
    fprintf(f, "};\n\n");

    fprintf(f, "static const int TOKEN_SKIP[TOKEN_COUNT] = {\n");
    for (i = 0; i < active_count; i++) {
        fprintf(f, "    %d%s\n", active_rules[i]->skip ? 1 : 0, (i + 1 < active_count) ? "," : "");
    }
    fprintf(f, "};\n\n");

    fprintf(f, "static const int DFA_ACCEPT[DFA_STATES] = {\n");
    for (i = 0; i < dfa->count; i++) {
        fprintf(f, "    %d%s\n", dfa->accept[i], (i + 1 < dfa->count) ? "," : "");
    }
    fprintf(f, "};\n\n");

    fprintf(f, "static const int DFA_TRANS[DFA_STATES][256] = {\n");
    for (i = 0; i < dfa->count; i++) {
        fprintf(f, "    {");
        for (j = 0; j < 256; j++) {
            if (j > 0) {
                fprintf(f, ",");
            }
            fprintf(f, "%d", dfa->trans[i][j]);
        }
        fprintf(f, "}%s\n", (i + 1 < dfa->count) ? "," : "");
    }
    fprintf(f, "};\n\n");

    fprintf(f, "static char *read_all(const char *path, size_t *len_out) {\n");
    fprintf(f, "    FILE *fp = fopen(path, \"rb\");\n");
    fprintf(f, "    long n;\n");
    fprintf(f, "    char *buf;\n");
    fprintf(f, "    if (!fp) return NULL;\n");
    fprintf(f, "    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }\n");
    fprintf(f, "    n = ftell(fp);\n");
    fprintf(f, "    if (n < 0) { fclose(fp); return NULL; }\n");
    fprintf(f, "    if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return NULL; }\n");
    fprintf(f, "    buf = (char *)malloc((size_t)n + 1);\n");
    fprintf(f, "    if (!buf) { fclose(fp); return NULL; }\n");
    fprintf(f, "    if (n > 0 && fread(buf, 1, (size_t)n, fp) != (size_t)n) { free(buf); fclose(fp); return NULL; }\n");
    fprintf(f, "    fclose(fp);\n");
    fprintf(f, "    buf[n] = '\\0';\n");
    fprintf(f, "    if (len_out) *len_out = (size_t)n;\n");
    fprintf(f, "    return buf;\n");
    fprintf(f, "}\n\n");

    fprintf(f, "static void print_escaped(const char *s, size_t n) {\n");
    fprintf(f, "    size_t i;\n");
    fprintf(f, "    putchar('\"');\n");
    fprintf(f, "    for (i = 0; i < n; i++) {\n");
    fprintf(f, "        unsigned char c = (unsigned char)s[i];\n");
    fprintf(f, "        switch (c) {\n");
    fprintf(f, "        case '\\\\': fputs(\"\\\\\\\\\", stdout); break;\n");
    fprintf(f, "        case '\"': fputs(\"\\\\\\\"\", stdout); break;\n");
    fprintf(f, "        case '\\n': fputs(\"\\\\n\", stdout); break;\n");
    fprintf(f, "        case '\\r': fputs(\"\\\\r\", stdout); break;\n");
    fprintf(f, "        case '\\t': fputs(\"\\\\t\", stdout); break;\n");
    fprintf(f, "        default:\n");
    fprintf(f, "            if (c < 32 || c > 126) printf(\"\\\\x%%02X\", c);\n");
    fprintf(f, "            else putchar((int)c);\n");
    fprintf(f, "        }\n");
    fprintf(f, "    }\n");
    fprintf(f, "    putchar('\"');\n");
    fprintf(f, "}\n\n");

    fprintf(f, "int main(int argc, char **argv) {\n");
    fprintf(f, "    char *input;\n");
    fprintf(f, "    size_t len = 0;\n");
    fprintf(f, "    size_t pos = 0;\n");
    fprintf(f, "    size_t line = 1, col = 1;\n");
    fprintf(f, "    if (argc < 2) {\n");
    fprintf(f, "        fprintf(stderr, \"Usage: %%s <input.txt>\\n\", argv[0]);\n");
    fprintf(f, "        return 1;\n");
    fprintf(f, "    }\n");
    fprintf(f, "    input = read_all(argv[1], &len);\n");
    fprintf(f, "    if (!input) {\n");
    fprintf(f, "        fprintf(stderr, \"cannot read input file: %%s\\n\", argv[1]);\n");
    fprintf(f, "        return 1;\n");
    fprintf(f, "    }\n");
    fprintf(f, "    while (pos < len) {\n");
    fprintf(f, "        int state = 0;\n");
    fprintf(f, "        int last_tok = -1;\n");
    fprintf(f, "        size_t i = pos;\n");
    fprintf(f, "        size_t scan_line = line, scan_col = col;\n");
    fprintf(f, "        size_t last_pos = pos;\n");
    fprintf(f, "        size_t last_line = line, last_col = col;\n");
    fprintf(f, "        while (i < len) {\n");
    fprintf(f, "            unsigned char ch = (unsigned char)input[i];\n");
    fprintf(f, "            int next = DFA_TRANS[state][ch];\n");
    fprintf(f, "            if (next < 0) break;\n");
    fprintf(f, "            state = next;\n");
    fprintf(f, "            i++;\n");
    fprintf(f, "            if (ch == '\\n') { scan_line++; scan_col = 1; }\n");
    fprintf(f, "            else { scan_col++; }\n");
    fprintf(f, "            if (DFA_ACCEPT[state] >= 0) {\n");
    fprintf(f, "                last_tok = DFA_ACCEPT[state];\n");
    fprintf(f, "                last_pos = i;\n");
    fprintf(f, "                last_line = scan_line;\n");
    fprintf(f, "                last_col = scan_col;\n");
    fprintf(f, "            }\n");
    fprintf(f, "        }\n");
    fprintf(f, "        if (last_tok >= 0) {\n");
    fprintf(f, "            if (!TOKEN_SKIP[last_tok]) {\n");
    fprintf(f, "                printf(\"TOKEN %%s \", TOKEN_NAMES[last_tok]);\n");
    fprintf(f, "                print_escaped(input + pos, last_pos - pos);\n");
    fprintf(f, "                printf(\" (line %%zu, col %%zu)\\n\", line, col);\n");
    fprintf(f, "            }\n");
    fprintf(f, "            pos = last_pos;\n");
    fprintf(f, "            line = last_line;\n");
    fprintf(f, "            col = last_col;\n");
    fprintf(f, "        } else {\n");
    fprintf(f, "            printf(\"LEXICAL_ERROR line %%zu col %%zu: \", line, col);\n");
    fprintf(f, "            print_escaped(input + pos, 1);\n");
    fprintf(f, "            printf(\"\\n\");\n");
    fprintf(f, "            if ((unsigned char)input[pos] == '\\n') {\n");
    fprintf(f, "                line++;\n");
    fprintf(f, "                col = 1;\n");
    fprintf(f, "            } else {\n");
    fprintf(f, "                col++;\n");
    fprintf(f, "            }\n");
    fprintf(f, "            pos++;\n");
    fprintf(f, "        }\n");
    fprintf(f, "    }\n");
    if (eof_rule && !eof_rule->skip) {
        fprintf(f, "    printf(\"TOKEN %%s \\\"<EOF>\\\" (line %%zu, col %%zu)\\n\", ");
        emit_c_string(f, eof_rule->token_name);
        fprintf(f, ", line, col);\n");
    }
    fprintf(f, "    free(input);\n");
    fprintf(f, "    return 0;\n");
    fprintf(f, "}\n");
    if (spec->trailer && *spec->trailer) {
        fprintf(f, "\n/* ===== BEGIN YALex trailer ===== */\n");
        fprintf(f, "%s\n", spec->trailer);
        fprintf(f, "/* ===== END YALex trailer ===== */\n");
    }

    fclose(f);
}

static char *derive_png_path(const char *dot_path) {
    size_t n = strlen(dot_path);
    const char *ext = strrchr(dot_path, '.');
    char *png;
    if (ext && strcmp(ext, ".dot") == 0) {
        size_t base = (size_t)(ext - dot_path);
        png = (char *)xmalloc(base + 5);
        memcpy(png, dot_path, base);
        memcpy(png + base, ".png", 5);
        return png;
    }
    png = (char *)xmalloc(n + 5);
    memcpy(png, dot_path, n);
    memcpy(png + n, ".png", 5);
    return png;
}

static void maybe_generate_tree_png(const char *dot_path) {
    char *png = derive_png_path(dot_path);
    size_t cmd_len = strlen(dot_path) + strlen(png) + 64;
    char *cmd = (char *)xmalloc(cmd_len);
    int rc;
    snprintf(cmd, cmd_len, "dot -Tpng \"%s\" -o \"%s\"", dot_path, png);
    rc = system(cmd);
    if (rc == 0) {
        fprintf(stdout, "Generated regex tree png: %s\n", png);
    } else {
        fprintf(stderr, "warning: could not generate png from dot (Graphviz 'dot' unavailable or failed)\n");
    }
    free(cmd);
    free(png);
}

static void free_spec(YalSpec *spec) {
    int i;
    free(spec->header);
    free(spec->trailer);
    free(spec->entrypoint);
    free(spec->entry_args);
    for (i = 0; i < spec->lets.count; i++) {
        free(spec->lets.data[i].name);
        free(spec->lets.data[i].regex);
        ast_free(spec->lets.data[i].ast);
    }
    free(spec->lets.data);
    for (i = 0; i < spec->rules.count; i++) {
        free(spec->rules.data[i].regex);
        free(spec->rules.data[i].action);
        free(spec->rules.data[i].token_name);
        ast_free(spec->rules.data[i].ast);
    }
    free(spec->rules.data);
}

static void free_nfa(NFA *nfa) {
    int i;
    for (i = 0; i < nfa->state_count; i++) {
        free(nfa->states[i].edges);
    }
    free(nfa->states);
    free(nfa->sets);
}

static void free_dfa(DFA *dfa) {
    int i;
    for (i = 0; i < dfa->count; i++) {
        free(dfa->subsets[i]);
        free(dfa->trans[i]);
    }
    free(dfa->subsets);
    free(dfa->trans);
    free(dfa->accept);
}

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s <spec.yal> [-o lexer_generated.c] [--dot regex_tree.dot] [--no-png]\n",
            prog);
}

int main(int argc, char **argv) {
    const char *input_path;
    const char *out_lexer = "lexer_generated.c";
    const char *out_dot = "regex_tree.dot";
    char *raw;
    char *clean;
    YalSpec spec;
    RuleDef **active_rules;
    int active_count = 0;
    int generate_png = 1;
    RuleDef *eof_rule = NULL;
    NFA nfa;
    DFA dfa;
    int nfa_start;
    int i;

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }
    input_path = argv[1];
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 1;
            }
            out_lexer = argv[++i];
        } else if (strcmp(argv[i], "--dot") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 1;
            }
            out_dot = argv[++i];
        } else if (strcmp(argv[i], "--no-png") == 0) {
            generate_png = 0;
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    raw = read_file(input_path, NULL);
    clean = strip_comments(raw);
    free(raw);

    parse_spec(clean, &spec);
    free(clean);

    if (spec.rules.count == 0) {
        free_spec(&spec);
        fatal("rule section has no alternatives");
    }

    for (i = 0; i < spec.rules.count; i++) {
        int skip = 0;
        spec.rules.data[i].token_name = infer_token_name(spec.rules.data[i].action, i, &skip);
        spec.rules.data[i].skip = skip;
        spec.rules.data[i].is_eof = is_eof_regex(spec.rules.data[i].regex);
        if (spec.rules.data[i].is_eof) {
            if (!eof_rule) {
                eof_rule = &spec.rules.data[i];
            }
            continue;
        }
        spec.rules.data[i].ast = parse_regex_with_lets(spec.rules.data[i].regex, &spec.lets);
        active_count++;
    }

    if (active_count == 0) {
        free_spec(&spec);
        fatal("no non-eof token rules found");
    }

    active_rules = (RuleDef **)xmalloc((size_t)active_count * sizeof(RuleDef *));
    {
        int k = 0;
        for (i = 0; i < spec.rules.count; i++) {
            if (!spec.rules.data[i].is_eof) {
                active_rules[k++] = &spec.rules.data[i];
            }
        }
    }

    emit_dot_file(out_dot, active_rules, active_count, eof_rule);
    if (generate_png) {
        maybe_generate_tree_png(out_dot);
    }

    memset(&nfa, 0, sizeof(nfa));
    nfa_start = nfa_add_state(&nfa);
    for (i = 0; i < active_count; i++) {
        Frag f = build_nfa_from_ast(&nfa, active_rules[i]->ast);
        nfa_add_edge(&nfa, nfa_start, f.start, -1);
        if (nfa.states[f.end].accept_token < 0 || i < nfa.states[f.end].accept_token) {
            nfa.states[f.end].accept_token = i;
        }
    }

    dfa = build_dfa(&nfa, nfa_start);
    emit_generated_lexer(out_lexer, &dfa, active_rules, active_count, eof_rule, &spec);

    fprintf(stdout, "Generated lexer source: %s\n", out_lexer);
    fprintf(stdout, "Generated regex tree dot: %s\n", out_dot);
    fprintf(stdout, "DFA states: %d\n", dfa.count);
    fprintf(stdout, "Token rules: %d\n", active_count);

    free(active_rules);
    free_dfa(&dfa);
    free_nfa(&nfa);
    free_spec(&spec);
    return 0;
}
