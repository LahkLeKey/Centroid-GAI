"""Keep complete public declaration docs canonical in Doxygen's parsing input.

Only the third character of a duplicate definition's Doxygen block marker changes.
The original files, code, comment text, offsets and line endings remain untouched.
Private/test definitions remain canonical when they fully document private prototypes.
Unmatched or incompletely documented functions retain their original documentation.
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path


_TOKENS = re.compile(
    r"(?P<doc>/\*[*!](?!<).*?\*/)"
    r"|(?P<comment>/\*.*?\*/|//[^\r\n]*)"
    r'|(?P<string>"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\')'
    r"|(?P<identifier>[A-Za-z_]\w*)|(?P<symbol>[^\s])",
    re.DOTALL,
)
_PARAM = re.compile(r"[@\\]param(?:\s*\[[^\]]+\])?\s+(\w+)\s+([^\r\n@\\]+)")


@dataclass(frozen=True)
class Function:
    """One simple named C declaration or definition following a Doxygen block."""

    name: str
    parameters: tuple[str, ...]
    returns_value: bool
    definition: bool
    comment: re.Match[str]


def parameter_names(tokens: list[str]) -> tuple[str, ...] | None:
    """Read named ordinary/array/callback parameters; unsupported forms stay intact."""
    if tokens == ["void"]:
        return ()
    groups: list[list[str]] = [[]]
    depth = 0
    for token in tokens:
        if token == "," and depth == 0:
            groups.append([])
            continue
        groups[-1].append(token)
        depth += token in ("(", "[")
        depth -= token in (")", "]")
    names = []
    for group in groups:
        if group == [".", ".", "."]:
            continue
        callback = re.search(r"\( \* (\w+) \)", " ".join(group))
        if callback:
            names.append(callback[1])
            continue
        ordinary = group[:group.index("[")] if "[" in group else group
        if len(ordinary) < 2 or not re.fullmatch(r"[A-Za-z_]\w*", ordinary[-1]):
            return None
        names.append(ordinary[-1])
    return tuple(names)


def functions(text: str) -> list[Function]:
    """Recognize directly documented, nonstatic function signatures, not C lookalikes."""
    tokens = [token for token in _TOKENS.finditer(text) if token.lastgroup != "comment"]
    result = []
    for index, comment in enumerate(tokens):
        if comment.lastgroup != "doc":
            continue
        cursor, prefix = index + 1, []
        while cursor < len(tokens) and (
            tokens[cursor].lastgroup == "identifier" or tokens[cursor][0] == "*"
        ):
            prefix.append(tokens[cursor][0])
            cursor += 1
        if (len(prefix) < 2 or any(word in prefix for word in ("static", "typedef"))
                or cursor >= len(tokens) or tokens[cursor][0] != "("):
            continue
        cursor += 1
        depth, parameters = 1, []
        while cursor < len(tokens) and depth:
            value = tokens[cursor][0]
            depth += value == "("
            depth -= value == ")"
            if depth:
                parameters.append(value)
            cursor += 1
        names = parameter_names(parameters)
        if depth or names is None or cursor >= len(tokens) or tokens[cursor][0] not in (";", "{"):
            continue
        result.append(Function(prefix[-1], names, prefix[-2] != "void",
                               tokens[cursor][0] == "{", comment))
    return result


def complete_documentation(function: Function) -> bool:
    """Require a description, every named parameter, and a nonvoid return contract."""
    body = re.sub(r"(?m)^\s*\* ?", "", function.comment[0][3:-2]).strip()
    if re.search(r"[@\\](?:file|defgroup|addtogroup|name)\b", body):
        return False
    descriptions = _PARAM.findall(body)
    names = [name for name, description in descriptions if description.strip()]
    has_description = bool(re.search(r"[@\\]brief\s+\S", body)) or (
        bool(body) and not body.startswith(("@", "\\")))
    has_return = not function.returns_value or bool(re.search(r"[@\\]return\s+\S", body))
    return (has_description and has_return and len(names) == len(function.parameters)
            and set(names) == set(function.parameters))


def public_declarations(include: Path) -> set[tuple[str, tuple[str, ...]]]:
    """Index only fully documented prototypes from the public API headers."""
    declarations = set()
    for path in sorted(include.glob("*.h")):
        for function in functions(path.read_bytes().decode("utf-8")):
            if not function.definition and complete_documentation(function):
                declarations.add((function.name, function.parameters))
    return declarations


def filter_source(text: str, declarations: set[tuple[str, tuple[str, ...]]]) -> str:
    """Demote duplicate public definition blocks while preserving all offsets and lines."""
    replacements = [function.comment.start() + 2 for function in functions(text)
                    if function.definition and (function.name, function.parameters) in declarations]
    for position in reversed(replacements):
        text = text[:position] + " " + text[position + 1:]
    return text


def documented_definitions(root: Path) -> set[tuple[str, tuple[str, ...]]]:
    """Index complete private/test implementation contracts in Doxygen's C input trees."""
    definitions = set()
    for directory in ("src", "tests", "persistence/api/native"):
        for path in sorted((root / directory).rglob("*.c")):
            for function in functions(path.read_bytes().decode("utf-8")):
                if function.definition and complete_documentation(function):
                    definitions.add((function.name, function.parameters))
    return definitions


def filter_private_header(text: str, definitions: set[tuple[str, tuple[str, ...]]]) -> str:
    """Prefer complete private definition docs without hiding unmatched declaration docs."""
    replacements = [function.comment.start() + 2 for function in functions(text)
                    if not function.definition and (function.name, function.parameters) in definitions]
    for position in reversed(replacements):
        text = text[:position] + " " + text[position + 1:]
    return text


def main() -> None:
    """Emit only parsing input; Doxygen appends the input filename to this command."""
    path = Path(sys.argv[1])
    source = path.read_bytes()
    root = Path(__file__).resolve().parents[2]
    if path.suffix == ".c":
        source = filter_source(source.decode("utf-8"), public_declarations(root / "include")).encode("utf-8")
    elif path.suffix == ".h" and not path.resolve().is_relative_to(root / "include"):
        source = filter_private_header(source.decode("utf-8"), documented_definitions(root)).encode("utf-8")
    sys.stdout.buffer.write(source)


if __name__ == "__main__":
    main()
