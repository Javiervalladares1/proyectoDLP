from __future__ import annotations

from .charset import charset_diff, charset_union
from .yalex_types import AST, ASTType, CharSet


def ast_new(t: ASTType, l: AST | None, r: AST | None) -> AST:
    return AST(type=t, left=l, right=r)


def ast_empty() -> AST:
    return ast_new(ASTType.EMPTY, None, None)


def ast_charset(s: CharSet) -> AST:
    return AST(type=ASTType.CHARSET, set=CharSet(bits=s.bits.copy()))


def ast_clone(n: AST | None) -> AST | None:
    if n is None:
        return None
    c = AST(type=n.type, set=CharSet(bits=n.set.bits.copy()))
    c.left = ast_clone(n.left)
    c.right = ast_clone(n.right)
    return c


def ast_eval_charset(n: AST | None) -> CharSet | None:
    if n is None:
        return None
    if n.type == ASTType.CHARSET:
        return CharSet(bits=n.set.bits.copy())
    if n.type == ASTType.ALT:
        a = ast_eval_charset(n.left)
        b = ast_eval_charset(n.right)
        if a is None or b is None:
            return None
        return charset_union(a, b)
    if n.type == ASTType.DIFF:
        a = ast_eval_charset(n.left)
        b = ast_eval_charset(n.right)
        if a is None or b is None:
            return None
        return charset_diff(a, b)
    return None


def make_diff_ast(left: AST, right: AST) -> AST:
    a = ast_eval_charset(left)
    b = ast_eval_charset(right)
    if a is not None and b is not None:
        return ast_charset(charset_diff(a, b))
    return ast_new(ASTType.DIFF, left, right)

