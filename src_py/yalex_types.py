from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum, auto


class ASTType(Enum):
    EMPTY = auto()
    CHARSET = auto()
    CONCAT = auto()
    ALT = auto()
    STAR = auto()
    PLUS = auto()
    QMARK = auto()
    DIFF = auto()
    EOF = auto()


@dataclass
class CharSet:
    bits: list[int] = field(default_factory=lambda: [0] * 256)


@dataclass
class AST:
    type: ASTType
    left: "AST | None" = None
    right: "AST | None" = None
    set: CharSet = field(default_factory=CharSet)


@dataclass
class LetDef:
    name: str
    regex: str
    ast: AST | None = None
    resolving: bool = False


@dataclass
class RuleDef:
    regex: str
    action: str
    token_name: str | None = None
    skip: bool = False
    is_eof: bool = False
    ast: AST | None = None


@dataclass
class YalSpec:
    lets: list[LetDef] = field(default_factory=list)
    rules: list[RuleDef] = field(default_factory=list)
    header: str | None = None
    trailer: str | None = None
    entrypoint: str | None = None
    entry_args: str | None = None


@dataclass
class NFAEdge:
    to: int
    set_id: int


@dataclass
class NFAState:
    edges: list[NFAEdge] = field(default_factory=list)
    accept_token: int = -1


@dataclass
class NFA:
    states: list[NFAState] = field(default_factory=list)
    sets: list[CharSet] = field(default_factory=list)


@dataclass
class Frag:
    start: int
    end: int


@dataclass
class DFA:
    subsets: list[tuple[int, ...]] = field(default_factory=list)
    trans: list[list[int]] = field(default_factory=list)
    accept: list[int] = field(default_factory=list)

