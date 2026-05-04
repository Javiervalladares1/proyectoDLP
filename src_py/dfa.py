from __future__ import annotations

from .yalex_types import DFA, NFA


def _epsilon_closure(nfa: NFA, subset: set[int]) -> set[int]:
    out = set(subset)
    stack = list(subset)
    while stack:
        s = stack.pop()
        for edge in nfa.states[s].edges:
            if edge.set_id == -1 and edge.to not in out:
                out.add(edge.to)
                stack.append(edge.to)
    return out


def _move_on_char(nfa: NFA, subset: set[int], c: int) -> set[int]:
    out: set[int] = set()
    for s in subset:
        for edge in nfa.states[s].edges:
            if edge.set_id >= 0 and nfa.sets[edge.set_id].bits[c]:
                out.add(edge.to)
    if out:
        out = _epsilon_closure(nfa, out)
    return out


def _choose_accept_token(nfa: NFA, subset: set[int]) -> int:
    best = -1
    for i in subset:
        tok = nfa.states[i].accept_token
        if tok >= 0 and (best < 0 or tok < best):
            best = tok
    return best


def build_dfa(nfa: NFA, nfa_start: int) -> DFA:
    dfa = DFA()
    start = _epsilon_closure(nfa, {nfa_start})
    subsets: dict[tuple[int, ...], int] = {}

    def add_state(subset: set[int]) -> int:
        key = tuple(sorted(subset))
        idx = len(dfa.subsets)
        subsets[key] = idx
        dfa.subsets.append(key)
        dfa.trans.append([-1] * 256)
        dfa.accept.append(_choose_accept_token(nfa, subset))
        return idx

    add_state(start)
    i = 0
    while i < len(dfa.subsets):
        curr = set(dfa.subsets[i])
        for c in range(256):
            nxt = _move_on_char(nfa, curr, c)
            if not nxt:
                continue
            key = tuple(sorted(nxt))
            j = subsets.get(key)
            if j is None:
                j = add_state(nxt)
            dfa.trans[i][c] = j
        i += 1
    return dfa

