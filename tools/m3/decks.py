from __future__ import annotations

from dataclasses import dataclass
import hashlib
from pathlib import Path


class DeckFormatError(ValueError):
    """Raised when a YDK source does not match the locked fixture grammar."""


@dataclass(frozen=True)
class ParsedDeck:
    main: tuple[int, ...]
    extra: tuple[int, ...]
    side: tuple[int, ...]
    sha256: str


def parse_ydk(
    source: bytes,
    *,
    require_main: int | None = None,
    require_extra: int | None = None,
    allow_side: bool = False,
) -> ParsedDeck:
    if not isinstance(source, bytes):
        raise TypeError("YDK source must be bytes")

    sections: dict[str, list[int]] = {"main": [], "extra": [], "side": []}
    section: str | None = None
    for line_number, raw_line in enumerate(source.decode("utf-8-sig").splitlines(), start=1):
        line = raw_line.strip()
        if not line:
            continue
        if line == "#main":
            section = "main"
            continue
        if line == "#extra":
            section = "extra"
            continue
        if line == "!side":
            section = "side"
            continue
        if line.startswith("#"):
            continue
        if section is None:
            raise DeckFormatError(f"card passcode before a section on line {line_number}")
        try:
            code = int(line, 10)
        except ValueError as error:
            raise DeckFormatError(f"invalid card passcode on line {line_number}: {line}") from error
        if code <= 0 or code > 0xFFFFFFFF:
            raise DeckFormatError(f"card passcode outside uint32 range on line {line_number}: {line}")
        sections[section].append(code)

    if require_main is not None and len(sections["main"]) != require_main:
        raise DeckFormatError(
            f"expected {require_main} Main Deck cards, got {len(sections['main'])}"
        )
    if require_extra is not None and len(sections["extra"]) != require_extra:
        raise DeckFormatError(
            f"expected {require_extra} Extra Deck cards, got {len(sections['extra'])}"
        )
    if not allow_side and sections["side"]:
        raise DeckFormatError("side deck must be empty for this fixture")

    return ParsedDeck(
        main=tuple(sections["main"]),
        extra=tuple(sections["extra"]),
        side=tuple(sections["side"]),
        sha256=hashlib.sha256(source).hexdigest(),
    )


def parse_ydk_file(
    path: str | Path,
    *,
    require_main: int | None = None,
    require_extra: int | None = None,
    allow_side: bool = False,
) -> ParsedDeck:
    source_path = Path(path)
    return parse_ydk(
        source_path.read_bytes(),
        require_main=require_main,
        require_extra=require_extra,
        allow_side=allow_side,
    )
