#include "nfa.h"

#include "ast.h"
#include "charset.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

int nfa_add_state(NFA *nfa) {
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

void nfa_add_edge(NFA *nfa, int from, int to, int set_id) {
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

int nfa_add_charset(NFA *nfa, const CharSet *set) {
    if (nfa->set_count == nfa->set_cap) {
        nfa->set_cap = nfa->set_cap ? nfa->set_cap * 2 : 32;
        nfa->sets = (CharSet *)xrealloc(nfa->sets, (size_t)nfa->set_cap * sizeof(CharSet));
    }
    nfa->sets[nfa->set_count] = *set;
    return nfa->set_count++;
}

Frag build_nfa_from_ast(NFA *nfa, AST *n) {
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

void free_nfa(NFA *nfa) {
    int i;
    for (i = 0; i < nfa->state_count; i++) {
        free(nfa->states[i].edges);
    }
    free(nfa->states);
    free(nfa->sets);
}
