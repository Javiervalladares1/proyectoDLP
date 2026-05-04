from __future__ import annotations

from .util import append_text_block, fatal, trim_copy
from .yalex_types import LetDef, RuleDef, YalSpec


def find_let(lets: list[LetDef], name: str) -> LetDef | None:
    for item in lets:
        if item.name == name:
            return item
    return None


def _is_ident_start(c: str) -> bool:
    return c.isalpha() or c == "_"


def _is_ident(c: str) -> bool:
    return c.isalnum() or c == "_"


def _starts_kw(src: str, pos: int, kw: str) -> bool:
    if not src.startswith(kw, pos):
        return False
    end = pos + len(kw)
    return end >= len(src) or not _is_ident(src[end])


def _skip_ws(src: str, pos: int) -> int:
    while pos < len(src) and src[pos].isspace():
        pos += 1
    return pos


def _parse_identifier(src: str, pos: int) -> tuple[str, int]:
    if pos >= len(src) or not _is_ident_start(src[pos]):
        fatal(f"expected identifier near: {src[pos:pos+30]}")
    start = pos
    pos += 1
    while pos < len(src) and _is_ident(src[pos]):
        pos += 1
    return trim_copy(src[start:pos]), pos


def _capture_balanced(src: str, pos: int, open_ch: str, close_ch: str) -> tuple[str, int]:
    if pos >= len(src) or src[pos] != open_ch:
        fatal(f"expected '{open_ch}'")
    i = pos
    depth = 0
    in_sq = False
    in_dq = False
    esc = False
    while i < len(src):
        c = src[i]
        if in_sq:
            if not esc and c == "\\":
                esc = True
            elif not esc and c == "'":
                in_sq = False
            else:
                esc = False
        elif in_dq:
            if not esc and c == "\\":
                esc = True
            elif not esc and c == '"':
                in_dq = False
            else:
                esc = False
        else:
            if c == "'":
                in_sq = True
                esc = False
            elif c == '"':
                in_dq = True
                esc = False
            elif c == open_ch:
                depth += 1
            elif c == close_ch:
                depth -= 1
                if depth == 0:
                    return src[pos + 1 : i], i + 1
        i += 1
    fatal(f"unclosed block starting with '{open_ch}'")
    raise AssertionError("unreachable")


def yal_strip_comments(src: str) -> str:
    out: list[str] = []
    i = 0
    depth = 0
    in_sq = False
    in_dq = False
    esc = False
    while i < len(src):
        if depth == 0 and not in_sq and not in_dq and src.startswith("(*", i):
            depth = 1
            i += 2
            continue
        if depth > 0:
            if src.startswith("(*", i):
                depth += 1
                i += 2
                continue
            if src.startswith("*)", i):
                depth -= 1
                i += 2
                continue
            if src[i] in "\r\n":
                out.append(src[i])
            i += 1
            continue
        c = src[i]
        if in_sq:
            out.append(c)
            if not esc and c == "\\":
                esc = True
            elif not esc and c == "'":
                in_sq = False
            else:
                esc = False
            i += 1
            continue
        if in_dq:
            out.append(c)
            if not esc and c == "\\":
                esc = True
            elif not esc and c == '"':
                in_dq = False
            else:
                esc = False
            i += 1
            continue
        if c == "'":
            in_sq = True
            esc = False
        elif c == '"':
            in_dq = True
            esc = False
        out.append(c)
        i += 1
    return "".join(out)


def _capture_regex_before_action(src: str, pos: int) -> tuple[str, int]:
    i = pos
    paren = 0
    bracket = 0
    in_sq = False
    in_dq = False
    esc = False
    while i < len(src):
        c = src[i]
        if in_sq:
            if not esc and c == "\\":
                esc = True
            elif not esc and c == "'":
                in_sq = False
            else:
                esc = False
        elif in_dq:
            if not esc and c == "\\":
                esc = True
            elif not esc and c == '"':
                in_dq = False
            else:
                esc = False
        else:
            if c == "'":
                in_sq = True
                esc = False
            elif c == '"':
                in_dq = True
                esc = False
            elif c == "[":
                bracket += 1
            elif c == "]" and bracket > 0:
                bracket -= 1
            elif c == "(":
                paren += 1
            elif c == ")" and paren > 0:
                paren -= 1
            elif c == "{" and paren == 0 and bracket == 0:
                return trim_copy(src[pos:i]), i
        i += 1
    fatal(f"missing action block '{{...}}' near: {src[pos:pos+40]}")
    raise AssertionError("unreachable")


def parse_spec(src: str) -> YalSpec:
    spec = YalSpec()
    p = _skip_ws(src, 0)
    if p < len(src) and src[p] == "{":
        spec.header, p = _capture_balanced(src, p, "{", "}")
    while True:
        p = _skip_ws(src, p)
        if _starts_kw(src, p, "rule"):
            break
        if _starts_kw(src, p, "let"):
            p += 3
            p = _skip_ws(src, p)
            name, p = _parse_identifier(src, p)
            p = _skip_ws(src, p)
            if p >= len(src) or src[p] != "=":
                fatal(f"expected '=' in let definition near: {src[p:p+40]}")
            p += 1
            line_start = p
            while p < len(src) and src[p] not in "\r\n":
                p += 1
            regex = trim_copy(src[line_start:p])
            spec.lets.append(LetDef(name=name, regex=regex))
            while p < len(src) and src[p] in "\r\n":
                p += 1
            continue
        if p < len(src):
            line_start = p
            while p < len(src) and src[p] not in "\r\n":
                p += 1
            line = trim_copy(src[line_start:p])
            if line:
                spec.header = append_text_block(spec.header, line)
            while p < len(src) and src[p] in "\r\n":
                p += 1
            continue
        break
    p = _skip_ws(src, p)
    if not _starts_kw(src, p, "rule"):
        fatal("missing 'rule' section")
    p += 4
    p = _skip_ws(src, p)
    if p < len(src) and _is_ident_start(src[p]):
        spec.entrypoint, p = _parse_identifier(src, p)
    p = _skip_ws(src, p)
    arg_start = p
    while p < len(src) and src[p] != "=":
        p += 1
    spec.entry_args = trim_copy(src[arg_start:p]) or None
    if p >= len(src) or src[p] != "=":
        fatal("missing '=' after rule declaration")
    p += 1
    while True:
        p = _skip_ws(src, p)
        if p >= len(src):
            break
        if src[p] == "{":
            trailer, p = _capture_balanced(src, p, "{", "}")
            spec.trailer = append_text_block(spec.trailer, trailer)
            continue
        if src[p] == "|":
            p += 1
            p = _skip_ws(src, p)
        if p >= len(src):
            break
        regex, p = _capture_regex_before_action(src, p)
        p = _skip_ws(src, p)
        if p >= len(src) or src[p] != "{":
            fatal(f"expected action block near: {src[p:p+40]}")
        action, p = _capture_balanced(src, p, "{", "}")
        spec.rules.append(RuleDef(regex=regex, action=action))
    return spec

