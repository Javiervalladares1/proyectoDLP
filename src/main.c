#include "dfa.h"
#include "emit.h"
#include "nfa.h"
#include "regex_parse.h"
#include "util.h"
#include "yal_spec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    clean = yal_strip_comments(raw);
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
