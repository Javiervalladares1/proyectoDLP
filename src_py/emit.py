from __future__ import annotations

import os
import re
import subprocess

from .charset import charset_count
from .util import fatal, trim_copy
from .yalex_types import AST, ASTType, DFA, RuleDef, YalSpec


def infer_token_name(action: str, idx: int) -> tuple[str, bool]:
    p = action.strip()
    if not p:
        return "SKIP", True
    if p.startswith('"'):
        try:
            m = re.fullmatch(r'"((?:\\.|[^"])*)"\s*', p, re.S)
            if m:
                label = bytes(m.group(1), "latin-1").decode("unicode_escape")
                return label, False
        except Exception:
            pass
    m = re.search(r"\breturn\b\s+([A-Za-z_][A-Za-z_0-9]*)", action, flags=re.I)
    if m:
        name = m.group(1)
        if name.lower() == "lexbuf":
            return "SKIP", True
        return name, False
    return f"TOKEN_{idx+1}", ("skip" in action.lower())


def is_eof_regex(regex: str) -> bool:
    return trim_copy(regex) == "eof"


def _dot_escape(s: str) -> str:
    out = []
    for ch in s:
        c = ord(ch) & 0xFF
        if ch in ['"', "\\"]:
            out.append("\\" + ch)
        elif ch == "\n":
            out.append("\\n")
        elif c < 32 or c > 126:
            out.append(f"\\x{c:02X}")
        else:
            out.append(ch)
    return "".join(out)


def _charset_label(bits: list[int]) -> str:
    shown = []
    total = sum(1 for b in bits if b)
    for c in range(256):
        if bits[c]:
            if len(shown) >= 8:
                break
            if 32 <= c <= 126 and chr(c) not in ['"', "\\"]:
                shown.append(f"'{chr(c)}'")
            else:
                shown.append(f"0x{c:02X}")
    if total > len(shown):
        shown.append("...")
    return f"set({','.join(shown)})"


def emit_dot_file(path: str, active_rules: list[RuleDef], eof_rule: RuleDef | None) -> None:
    lines = ["digraph RegexTree {", "  rankdir=TB;", '  node [shape=box, fontname="Helvetica"];', '  n0 [label="TOKENS"];']
    next_id = 1

    def emit_ast(n: AST | None) -> int:
        nonlocal next_id
        node_id = next_id
        next_id += 1
        if n is None:
            lines.append(f'  n{node_id} [label="<null>"];')
            return node_id
        labels = {
            ASTType.EMPTY: "epsilon",
            ASTType.EOF: "eof",
            ASTType.CONCAT: "concat",
            ASTType.ALT: "|",
            ASTType.STAR: "*",
            ASTType.PLUS: "+",
            ASTType.QMARK: "?",
            ASTType.DIFF: "#",
        }
        if n.type == ASTType.CHARSET:
            label = _charset_label(n.set.bits)
        else:
            label = labels[n.type]
        lines.append(f'  n{node_id} [label="{_dot_escape(label)}"];')
        if n.left:
            l = emit_ast(n.left)
            lines.append(f"  n{node_id} -> n{l};")
        if n.right:
            r = emit_ast(n.right)
            lines.append(f"  n{node_id} -> n{r};")
        return node_id

    for rule in active_rules:
        tok = next_id
        next_id += 1
        lines.append(f'  n{tok} [label="TOKEN {_dot_escape(rule.token_name or "")}"];')
        lines.append(f"  n0 -> n{tok};")
        root = emit_ast(rule.ast)
        lines.append(f"  n{tok} -> n{root};")
    if eof_rule:
        tok = next_id
        lines.append(f'  n{tok} [label="TOKEN {_dot_escape(eof_rule.token_name or "")} (eof)"];')
        lines.append(f"  n0 -> n{tok};")
    lines.append("}")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def emit_generated_lexer(path: str, dfa: DFA, active_rules: list[RuleDef], eof_rule: RuleDef | None, spec: YalSpec) -> None:
    token_names = [r.token_name or "" for r in active_rules]
    token_skip = [1 if r.skip else 0 for r in active_rules]
    py = f"""#!/usr/bin/env python3
import sys

TOKEN_NAMES = {token_names!r}
TOKEN_SKIP = {token_skip!r}
DFA_ACCEPT = {dfa.accept!r}
DFA_TRANS = {dfa.trans!r}
EOF_TOKEN = {None if eof_rule is None else eof_rule.token_name!r}
EOF_SKIP = {True if eof_rule is None else bool(eof_rule.skip)!r}

def read_all(path):
    with open(path, "rb") as f:
        return f.read()

def print_escaped(bs):
    out = ['"']
    for b in bs:
        if b == 92:
            out.append('\\\\\\\\')
        elif b == 34:
            out.append('\\\\\\"')
        elif b == 10:
            out.append('\\\\n')
        elif b == 13:
            out.append('\\\\r')
        elif b == 9:
            out.append('\\\\t')
        elif b < 32 or b > 126:
            out.append(f'\\\\x{{b:02X}}')
        else:
            out.append(chr(b))
    out.append('"')
    return ''.join(out)

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {{sys.argv[0]}} <input.txt>", file=sys.stderr)
        return 1
    try:
        data = read_all(sys.argv[1])
    except OSError:
        print(f"cannot read input file: {{sys.argv[1]}}", file=sys.stderr)
        return 1
    pos = 0
    line = 1
    col = 1
    n = len(data)
    while pos < n:
        state = 0
        last_tok = -1
        i = pos
        scan_line, scan_col = line, col
        last_pos = pos
        last_line, last_col = line, col
        while i < n:
            ch = data[i]
            nxt = DFA_TRANS[state][ch]
            if nxt < 0:
                break
            state = nxt
            i += 1
            if ch == 10:
                scan_line += 1
                scan_col = 1
            else:
                scan_col += 1
            if DFA_ACCEPT[state] >= 0:
                last_tok = DFA_ACCEPT[state]
                last_pos = i
                last_line = scan_line
                last_col = scan_col
        if last_tok >= 0:
            if not TOKEN_SKIP[last_tok]:
                print(f"TOKEN {{TOKEN_NAMES[last_tok]}} {{print_escaped(data[pos:last_pos])}} (line {{line}}, col {{col}})")
            pos = last_pos
            line = last_line
            col = last_col
        else:
            print(f"LEXICAL_ERROR line {{line}} col {{col}}: {{print_escaped(data[pos:pos+1])}}")
            if data[pos] == 10:
                line += 1
                col = 1
            else:
                col += 1
            pos += 1
    if EOF_TOKEN is not None and not EOF_SKIP:
        print(f'TOKEN {{EOF_TOKEN}} "<EOF>" (line {{line}}, col {{col}})')
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
"""
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(py)


def maybe_generate_tree_png(dot_path: str) -> None:
    png = dot_path[:-4] + ".png" if dot_path.endswith(".dot") else dot_path + ".png"
    try:
        subprocess.run(["dot", "-Tpng", dot_path, "-o", png], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print(f"Generated regex tree png: {png}")
    except Exception:
        print("warning: could not generate png from dot (Graphviz 'dot' unavailable or failed)")

