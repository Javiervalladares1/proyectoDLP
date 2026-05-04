from __future__ import annotations

import argparse

from .dfa import build_dfa
from .emit import emit_dot_file, emit_generated_lexer, infer_token_name, is_eof_regex, maybe_generate_tree_png
from .nfa import build_nfa_from_ast, nfa_add_edge, nfa_add_state
from .regex_parse import parse_regex_with_lets
from .util import YalexError, fatal, read_file
from .yal_spec import parse_spec, yal_strip_comments
from .yalex_types import NFA


def run(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="yalexgen", add_help=False)
    parser.add_argument("spec")
    parser.add_argument("-o", dest="out_lexer", default="lexer_generated.py")
    parser.add_argument("--dot", dest="out_dot", default="regex_tree.dot")
    parser.add_argument("--no-png", action="store_true")
    parser.add_argument("-h", "--help", action="help")
    args = parser.parse_args(argv)

    raw = read_file(args.spec)
    clean = yal_strip_comments(raw)
    spec = parse_spec(clean)
    if not spec.rules:
        fatal("rule section has no alternatives")

    active_rules = []
    eof_rule = None
    for i, rule in enumerate(spec.rules):
        name, skip = infer_token_name(rule.action, i)
        rule.token_name = name
        rule.skip = skip
        rule.is_eof = is_eof_regex(rule.regex)
        if rule.is_eof:
            if eof_rule is None:
                eof_rule = rule
            continue
        rule.ast = parse_regex_with_lets(rule.regex, spec.lets)
        active_rules.append(rule)
    if not active_rules:
        fatal("no non-eof token rules found")

    emit_dot_file(args.out_dot, active_rules, eof_rule)
    if not args.no_png:
        maybe_generate_tree_png(args.out_dot)

    nfa = NFA()
    nfa_start = nfa_add_state(nfa)
    for i, rule in enumerate(active_rules):
        frag = build_nfa_from_ast(nfa, rule.ast)
        nfa_add_edge(nfa, nfa_start, frag.start, -1)
        if nfa.states[frag.end].accept_token < 0 or i < nfa.states[frag.end].accept_token:
            nfa.states[frag.end].accept_token = i
    dfa = build_dfa(nfa, nfa_start)
    emit_generated_lexer(args.out_lexer, dfa, active_rules, eof_rule, spec)
    print(f"Generated lexer source: {args.out_lexer}")
    print(f"Generated regex tree dot: {args.out_dot}")
    print(f"DFA states: {len(dfa.subsets)}")
    print(f"Token rules: {len(active_rules)}")
    return 0


def main() -> int:
    try:
        return run()
    except YalexError as exc:
        print(f"error: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

