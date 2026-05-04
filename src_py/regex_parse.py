from __future__ import annotations

from dataclasses import dataclass

from .ast import ast_charset, ast_clone, ast_empty, ast_new, make_diff_ast
from .charset import charset_add, charset_add_range, charset_clear, charset_fill, charset_not
from .util import fatal, trim_copy
from .yal_spec import find_let
from .yalex_types import AST, ASTType, CharSet, LetDef


@dataclass
class RegexParser:
    s: str
    pos: int
    lets: list[LetDef]


def _skip_ws(p: RegexParser) -> None:
    while p.pos < len(p.s) and p.s[p.pos].isspace():
        p.pos += 1


def _peek(p: RegexParser) -> str:
    return p.s[p.pos] if p.pos < len(p.s) else ""


def _get(p: RegexParser) -> str:
    c = _peek(p)
    if c:
        p.pos += 1
    return c


def _match(p: RegexParser, c: str) -> bool:
    if _peek(p) == c:
        p.pos += 1
        return True
    return False


def _parse_escape_char(p: RegexParser) -> str:
    c = _get(p)
    if not c:
        fatal("unfinished escape sequence")
    return {"n": "\n", "t": "\t", "r": "\r", "\\": "\\", "'": "'", '"': '"', "0": "\0"}.get(c, c)


def _parse_char_literal(p: RegexParser) -> str:
    if not _match(p, "'"):
        fatal("expected character literal")
    ch = _parse_escape_char(p) if _match(p, "\\") else _get(p)
    if not ch:
        fatal("unterminated character literal")
    if not _match(p, "'"):
        fatal("unterminated character literal")
    return ch


def _parse_set_char(p: RegexParser) -> str:
    if _peek(p) == "'":
        return _parse_char_literal(p)
    if _match(p, "\\"):
        return _parse_escape_char(p)
    c = _get(p)
    if not c:
        fatal("unterminated set")
    return c


def _resolve_let_ast(lets: list[LetDef], name: str) -> AST:
    d = find_let(lets, name)
    if d is None:
        fatal(f"undefined regex identifier: {name}")
    if d.resolving:
        fatal(f"recursive let definition detected for: {name}")
    if d.ast is None:
        d.resolving = True
        rp = RegexParser(d.regex, 0, lets)
        d.ast = _parse_regex_expr(rp)
        _skip_ws(rp)
        if _peek(rp):
            fatal(f"cannot parse full let regex '{d.name}': trailing: {rp.s[rp.pos:rp.pos+30]}")
        d.resolving = False
    out = ast_clone(d.ast)
    assert out is not None
    return out


def _is_ident_start(c: str) -> bool:
    return bool(c) and (c.isalpha() or c == "_")


def _is_ident(c: str) -> bool:
    return bool(c) and (c.isalnum() or c == "_")


def _parse_primary(p: RegexParser) -> AST | None:
    _skip_ws(p)
    c = _peek(p)
    if not c:
        return None
    if c == "(":
        _get(p)
        e = _parse_regex_expr(p)
        _skip_ws(p)
        if not _match(p, ")"):
            fatal("missing ')' in regex")
        return e
    if c == "'":
        cs = CharSet()
        charset_clear(cs)
        charset_add(cs, ord(_parse_char_literal(p)) & 0xFF)
        return ast_charset(cs)
    if c == '"':
        _get(p)
        res: AST | None = None
        while _peek(p) and _peek(p) != '"':
            ch = _parse_escape_char(p) if _match(p, "\\") else _get(p)
            cs = CharSet()
            charset_clear(cs)
            charset_add(cs, ord(ch) & 0xFF)
            n = ast_charset(cs)
            res = n if res is None else ast_new(ASTType.CONCAT, res, n)
        if not _match(p, '"'):
            fatal("unterminated string literal")
        return res or ast_empty()
    if c == "[":
        _get(p)
        cs = CharSet()
        charset_clear(cs)
        negate = _match(p, "^")
        while _peek(p) and _peek(p) != "]":
            if _peek(p).isspace():
                _get(p)
                continue
            first = _parse_set_char(p)
            if _peek(p) == "-" and (p.pos + 1) < len(p.s) and p.s[p.pos + 1] != "]":
                _get(p)
                while _peek(p).isspace():
                    _get(p)
                second = _parse_set_char(p)
                charset_add_range(cs, ord(first) & 0xFF, ord(second) & 0xFF)
            else:
                charset_add(cs, ord(first) & 0xFF)
        if not _match(p, "]"):
            fatal("unterminated set")
        if negate:
            charset_not(cs)
        return ast_charset(cs)
    if c == "_":
        _get(p)
        cs = CharSet()
        charset_fill(cs)
        return ast_charset(cs)
    if _is_ident_start(c):
        start = p.pos
        while _is_ident(_peek(p)):
            _get(p)
        ident = trim_copy(p.s[start:p.pos])
        if ident == "eof":
            return ast_new(ASTType.EOF, None, None)
        return _resolve_let_ast(p.lets, ident)
    fatal(f"unexpected regex token near: {p.s[p.pos:p.pos+30]}")
    return None


def _parse_diff_atom(p: RegexParser) -> AST | None:
    left = _parse_primary(p)
    if left is None:
        return None
    while True:
        _skip_ws(p)
        if not _match(p, "#"):
            break
        right = _parse_primary(p)
        if right is None:
            fatal("missing rhs of '#'")
        left = make_diff_ast(left, right)
    return left


def _parse_postfix(p: RegexParser) -> AST | None:
    n = _parse_diff_atom(p)
    if n is None:
        return None
    while True:
        _skip_ws(p)
        if _match(p, "*"):
            n = ast_new(ASTType.STAR, n, None)
        elif _match(p, "+"):
            n = ast_new(ASTType.PLUS, n, None)
        elif _match(p, "?"):
            n = ast_new(ASTType.QMARK, n, None)
        else:
            break
    return n


def _next_starts_primary(p: RegexParser) -> bool:
    i = p.pos
    while i < len(p.s) and p.s[i].isspace():
        i += 1
    c = p.s[i] if i < len(p.s) else ""
    if not c or c in ")|*+?#":
        return False
    return c in "(\"'[_" or _is_ident_start(c)


def _parse_concat(p: RegexParser) -> AST | None:
    left = _parse_postfix(p)
    if left is None:
        return None
    while _next_starts_primary(p):
        right = _parse_postfix(p)
        if right is None:
            break
        left = ast_new(ASTType.CONCAT, left, right)
    return left


def _parse_regex_expr(p: RegexParser) -> AST:
    left = _parse_concat(p) or ast_empty()
    while True:
        _skip_ws(p)
        if not _match(p, "|"):
            break
        right = _parse_concat(p) or ast_empty()
        left = ast_new(ASTType.ALT, left, right)
    return left


def parse_regex_with_lets(text: str, lets: list[LetDef]) -> AST:
    p = RegexParser(text, 0, lets)
    ast = _parse_regex_expr(p)
    _skip_ws(p)
    if _peek(p):
        fatal(f"trailing regex text: {p.s[p.pos:p.pos+30]}")
    return ast

