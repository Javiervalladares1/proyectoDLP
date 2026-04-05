#ifndef YALEX_DFA_H
#define YALEX_DFA_H

#include "yalex_types.h"

DFA build_dfa(const NFA *nfa, int nfa_start);
void free_dfa(DFA *dfa);

#endif
