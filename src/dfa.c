#include "dfa.h"

#include "util.h"

#include <stdlib.h>
#include <string.h>

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

DFA build_dfa(const NFA *nfa, int nfa_start) {
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

void free_dfa(DFA *dfa) {
    int i;
    for (i = 0; i < dfa->count; i++) {
        free(dfa->subsets[i]);
        free(dfa->trans[i]);
    }
    free(dfa->subsets);
    free(dfa->trans);
    free(dfa->accept);
}
