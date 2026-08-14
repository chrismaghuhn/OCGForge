from __future__ import annotations

from dataclasses import dataclass
import sqlite3
from pathlib import Path

from .scripts import ScriptResolution, resolve_card_script


TYPE_MONSTER = 0x1
TYPE_SPELL = 0x2
TYPE_TRAP = 0x4
TYPE_NORMAL = 0x10
TYPE_EFFECT = 0x20
TYPE_FUSION = 0x40
TYPE_RITUAL = 0x80
TYPE_TUNER = 0x1000
TYPE_SYNCHRO = 0x2000
TYPE_XYZ = 0x800000
TYPE_PENDULUM = 0x1000000
TYPE_LINK = 0x4000000
EXTRA_TYPES = TYPE_FUSION | TYPE_SYNCHRO | TYPE_XYZ | TYPE_LINK


@dataclass(frozen=True)
class CardRow:
    passcode: int
    name: str
    alias: int
    setcode: int
    type: int
    atk: int
    defense: int
    raw_level: int
    race: int
    attribute: int

    @property
    def level_or_rank(self) -> int:
        return self.raw_level & 0xFF

    @property
    def is_extra_deck_capable(self) -> bool:
        return bool(self.type & EXTRA_TYPES)

    @property
    def is_monster_effect(self) -> bool:
        return bool(self.type & TYPE_EFFECT)

    @property
    def script_required(self) -> bool:
        return bool(self.type & (TYPE_EFFECT | TYPE_SPELL | TYPE_TRAP | EXTRA_TYPES))

    @property
    def decoded_metadata(self) -> dict[str, object]:
        metadata: dict[str, object] = {
            "type": self.type,
            "atk": self.atk,
            "def": None if self.type & TYPE_LINK else self.defense,
            "level": None if self.type & TYPE_XYZ else self.level_or_rank,
            "rank": self.level_or_rank if self.type & TYPE_XYZ else None,
            "link_rating": self.level_or_rank if self.type & TYPE_LINK else None,
            "link_markers": self.defense if self.type & TYPE_LINK else None,
            "race": self.race,
            "attribute": self.attribute,
            "pendulum_left_scale": (self.raw_level >> 16) & 0xFF if self.type & TYPE_PENDULUM else None,
            "pendulum_right_scale": (self.raw_level >> 24) & 0xFF if self.type & TYPE_PENDULUM else None,
        }
        return metadata

    def as_dict(self) -> dict[str, object]:
        return {
            "passcode": self.passcode,
            "name": self.name,
            "alias": self.alias,
            "setcode": self.setcode,
            "type": self.type,
            "metadata": self.decoded_metadata,
        }


class PinnedCatalog:
    def __init__(self, database: str | Path, scripts: str | Path):
        self.database = Path(database)
        self.scripts = Path(scripts)

    def _rows(self, query: str, parameters: tuple[object, ...]) -> list[CardRow]:
        connection = sqlite3.connect(self.database)
        try:
            values = connection.execute(query, parameters).fetchall()
        finally:
            connection.close()
        return [CardRow(*row) for row in values]

    def by_code(self, passcode: int) -> CardRow | None:
        rows = self._rows(
            "select d.id, t.name, d.alias, d.setcode, d.type, d.atk, d.def, d.level, d.race, d.attribute "
            "from datas d join texts t on t.id=d.id where d.id=?",
            (passcode,),
        )
        return rows[0] if rows else None

    def by_name(self, name: str) -> tuple[CardRow, ...]:
        return tuple(
            self._rows(
                "select d.id, t.name, d.alias, d.setcode, d.type, d.atk, d.def, d.level, d.race, d.attribute "
                "from datas d join texts t on t.id=d.id where t.name=? order by d.id",
                (name,),
            )
        )

    def resolve_name(self, name: str) -> tuple[CardRow, tuple[int, ...]]:
        rows = self.by_name(name)
        if not rows:
            raise KeyError(f"card name is absent from pinned BabelCDB: {name}")
        scripted = [row for row in rows if not row.script_required or resolve_card_script(row.passcode, self.scripts).load_result == "PASS"]
        selected = min(scripted or list(rows), key=lambda row: row.passcode)
        alternatives = tuple(row.passcode for row in rows if row.passcode != selected.passcode)
        return selected, alternatives

    def script(self, row: CardRow) -> ScriptResolution:
        return resolve_card_script(row.passcode, self.scripts)
