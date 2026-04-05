#ifndef YALEX_NFA_H
#define YALEX_NFA_H

#include "yalex_types.h"

int nfa_add_state(NFA *nfa);
void nfa_add_edge(NFA *nfa, int from, int to, int set_id);
int nfa_add_charset(NFA *nfa, const CharSet *set);
Frag build_nfa_from_ast(NFA *nfa, AST *n);
void free_nfa(NFA *nfa);

#endif
