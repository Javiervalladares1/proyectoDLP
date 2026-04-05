#include "yal_spec.h"

#include "ast.h"
#include "util.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

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

LetDef *find_let(LetVec *lets, const char *name) {
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

char *yal_strip_comments(const char *src) {
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

void parse_spec(const char *src, YalSpec *spec) {
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

void free_spec(YalSpec *spec) {
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
