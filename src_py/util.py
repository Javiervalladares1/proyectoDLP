from __future__ import annotations


class YalexError(RuntimeError):
    pass


def fatal(message: str) -> None:
    raise YalexError(message)


def read_file(path: str) -> str:
    try:
        with open(path, "r", encoding="latin-1", newline="") as f:
            return f.read()
    except OSError as exc:
        fatal(f"cannot open input file: {path} ({exc})")
    raise AssertionError("unreachable")


def trim_copy(text: str) -> str:
    return text.strip()


def append_text_block(dst: str | None, text: str | None) -> str | None:
    if not text:
        return dst
    if not dst:
        return text
    if dst and not dst.endswith("\n"):
        return f"{dst}\n{text}"
    return f"{dst}{text}"

