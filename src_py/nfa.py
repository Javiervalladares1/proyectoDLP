from __future__ import annotations

from .ast import ast_eval_charset
from .util import fatal
from .yalex_types import AST, ASTType, CharSet, Frag, NFA, NFAEdge, NFAState


def nfa_add_state(nfa: NFA) -> int:
    nfa.states.append(NFAState())
    return len(nfa.states) - 1


def nfa_add_edge(nfa: NFA, from_state: int, to_state: int, set_id: int) -> None:
    nfa.states[from_state].edges.append(NFAEdge(to=to_state, set_id=set_id))


def nfa_add_charset(nfa: NFA, set_obj: CharSet) -> int:
    nfa.sets.append(CharSet(bits=set_obj.bits.copy()))
    return len(nfa.sets) - 1


def build_nfa_from_ast(nfa: NFA, n: AST | None) -> Frag:
    if n is None or n.type in (ASTType.EMPTY, ASTType.EOF):
        s = nfa_add_state(nfa)
        e = nfa_add_state(nfa)
        nfa_add_edge(nfa, s, e, -1)
        return Frag(s, e)
    if n.type == ASTType.CHARSET:
        set_id = nfa_add_charset(nfa, n.set)
        s = nfa_add_state(nfa)
        e = nfa_add_state(nfa)
        nfa_add_edge(nfa, s, e, set_id)
        return Frag(s, e)
    if n.type == ASTType.CONCAT:
        a = build_nfa_from_ast(nfa, n.left)
        b = build_nfa_from_ast(nfa, n.right)
        nfa_add_edge(nfa, a.end, b.start, -1)
        return Frag(a.start, b.end)
    if n.type == ASTType.ALT:
        a = build_nfa_from_ast(nfa, n.left)
        b = build_nfa_from_ast(nfa, n.right)
        s = nfa_add_state(nfa)
        e = nfa_add_state(nfa)
        nfa_add_edge(nfa, s, a.start, -1)
        nfa_add_edge(nfa, s, b.start, -1)
        nfa_add_edge(nfa, a.end, e, -1)
        nfa_add_edge(nfa, b.end, e, -1)
        return Frag(s, e)
    if n.type == ASTType.STAR:
        a = build_nfa_from_ast(nfa, n.left)
        s = nfa_add_state(nfa)
        e = nfa_add_state(nfa)
        nfa_add_edge(nfa, s, a.start, -1)
        nfa_add_edge(nfa, s, e, -1)
        nfa_add_edge(nfa, a.end, a.start, -1)
        nfa_add_edge(nfa, a.end, e, -1)
        return Frag(s, e)
    if n.type == ASTType.PLUS:
        a = build_nfa_from_ast(nfa, n.left)
        s = nfa_add_state(nfa)
        e = nfa_add_state(nfa)
        nfa_add_edge(nfa, s, a.start, -1)
        nfa_add_edge(nfa, a.end, a.start, -1)
        nfa_add_edge(nfa, a.end, e, -1)
        return Frag(s, e)
    if n.type == ASTType.QMARK:
        a = build_nfa_from_ast(nfa, n.left)
        s = nfa_add_state(nfa)
        e = nfa_add_state(nfa)
        nfa_add_edge(nfa, s, a.start, -1)
        nfa_add_edge(nfa, s, e, -1)
        nfa_add_edge(nfa, a.end, e, -1)
        return Frag(s, e)
    if n.type == ASTType.DIFF:
        d = ast_eval_charset(n)
        if d is None:
            fatal("operator '#' is supported only for character sets")
        set_id = nfa_add_charset(nfa, d)
        s = nfa_add_state(nfa)
        e = nfa_add_state(nfa)
        nfa_add_edge(nfa, s, e, set_id)
        return Frag(s, e)
    fatal("internal AST type")
    raise AssertionError("unreachable")

