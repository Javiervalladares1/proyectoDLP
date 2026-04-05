#ifndef YALEX_EMIT_H
#define YALEX_EMIT_H

#include "yalex_types.h"

#include <stddef.h>

char *infer_token_name(const char *action, int idx, int *skip_out);
int is_eof_regex(const char *regex);
void emit_dot_file(const char *path, RuleDef **active_rules, int active_count, RuleDef *eof_rule);
void emit_generated_lexer(const char *path,
                          const DFA *dfa,
                          RuleDef **active_rules,
                          int active_count,
                          RuleDef *eof_rule,
                          const YalSpec *spec);
void maybe_generate_tree_png(const char *dot_path);

#endif
