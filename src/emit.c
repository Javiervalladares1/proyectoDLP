#include "emit.h"

#include "ast.h"
#include "charset.h"
#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

typedef struct {
    FILE *f;
    int next_id;
} DotCtx;

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

static int is_ident_start_char(int c) {
    return isalpha((unsigned char)c) || c == '_';
}

static int is_ident_char(int c) {
    return isalnum((unsigned char)c) || c == '_';
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

char *infer_token_name(const char *action, int idx, int *skip_out) {
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

int is_eof_regex(const char *regex) {
    char *t = trim_copy(regex, strlen(regex));
    int r = strcmp(t, "eof") == 0;
    free(t);
    return r;
}

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

void emit_dot_file(const char *path, RuleDef **active_rules, int active_count, RuleDef *eof_rule) {
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

void emit_generated_lexer(const char *path,
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

void maybe_generate_tree_png(const char *dot_path) {
    const char *dot_cmd = "dot";
    char *png = derive_png_path(dot_path);
    size_t cmd_len = strlen(dot_path) + strlen(png) + 80;
    char *cmd = (char *)xmalloc(cmd_len);
    int rc;
#ifndef _WIN32
    if (access("/usr/bin/dot", X_OK) == 0) {
        dot_cmd = "/usr/bin/dot";
    }
#endif
    snprintf(cmd, cmd_len, "%s -Tpng \"%s\" -o \"%s\"", dot_cmd, dot_path, png);
    rc = system(cmd);
    if (rc == 0) {
        fprintf(stdout, "Generated regex tree png: %s\n", png);
    } else {
        fprintf(stderr, "warning: could not generate png from dot (Graphviz 'dot' unavailable or failed)\n");
    }
    free(cmd);
    free(png);
}
