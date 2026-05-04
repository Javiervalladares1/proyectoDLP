from __future__ import annotations

from .yalex_types import CharSet


def charset_clear(s: CharSet) -> None:
    s.bits = [0] * 256


def charset_fill(s: CharSet) -> None:
    s.bits = [1] * 256


def charset_add(s: CharSet, c: int) -> None:
    s.bits[c & 0xFF] = 1


def charset_add_range(s: CharSet, a: int, b: int) -> None:
    if a > b:
        a, b = b, a
    for c in range(a, b + 1):
        s.bits[c & 0xFF] = 1


def charset_union(a: CharSet, b: CharSet) -> CharSet:
    out = CharSet()
    out.bits = [1 if (a.bits[i] or b.bits[i]) else 0 for i in range(256)]
    return out


def charset_diff(a: CharSet, b: CharSet) -> CharSet:
    out = CharSet()
    out.bits = [1 if (a.bits[i] and not b.bits[i]) else 0 for i in range(256)]
    return out


def charset_not(s: CharSet) -> None:
    s.bits = [0 if bit else 1 for bit in s.bits]


def charset_count(s: CharSet) -> int:
    return sum(1 for bit in s.bits if bit)

