from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class LockedCard:
    name: str
    count: int


@dataclass(frozen=True)
class LockedDeck:
    deck_id: str
    main: tuple[LockedCard, ...]
    extra: tuple[LockedCard, ...]

    @staticmethod
    def expand(cards: tuple[LockedCard, ...]) -> tuple[str, ...]:
        return tuple(card.name for card in cards for _ in range(card.count))


SWORDSoul = LockedDeck(
    deck_id="ocgforge.swordsoul_tenyi.ml_v1",
    main=(
        LockedCard("Swordsoul of Mo Ye", 3),
        LockedCard("Swordsoul Strategist Longyuan", 3),
        LockedCard("Swordsoul of Taia", 3),
        LockedCard("Incredible Ecclesia, the Virtuous", 3),
        LockedCard("Tenyi Spirit - Ashuna", 3),
        LockedCard("Tenyi Spirit - Vishuda", 3),
        LockedCard("Tenyi Spirit - Adhara", 3),
        LockedCard("Tenyi Spirit - Shthana", 1),
        LockedCard("Ash Blossom & Joyous Spring", 3),
        LockedCard("Effect Veiler", 3),
        LockedCard("Swordsoul Emergence", 3),
        LockedCard("Heavenly Dragon Circle", 3),
        LockedCard("Swordsoul Sacred Summit", 1),
        LockedCard("Called by the Grave", 1),
        LockedCard("Infinite Impermanence", 3),
        LockedCard("Swordsoul Blackout", 1),
    ),
    extra=(
        LockedCard("Monk of the Tenyi", 3),
        LockedCard("Shaman of the Tenyi", 1),
        LockedCard("Swordsoul Grandmaster - Chixiao", 2),
        LockedCard("Baxia, Brightness of the Yang Zing", 2),
        LockedCard("Draco Berserker of the Tenyi", 2),
        LockedCard("Yazi, Evil of the Yang Zing", 1),
        LockedCard("Adamancipator Risen - Dragite", 1),
        LockedCard("Chaofeng, Phantom of the Yang Zing", 1),
        LockedCard("Swordsoul Sinister Sovereign - Qixing Longyuan", 1),
        LockedCard("Swordsoul Supreme Sovereign - Chengying", 1),
    ),
)


SALAMANGREAT = LockedDeck(
    deck_id="ocgforge.salamangreat.ml_v1",
    main=(
        LockedCard("Salamangreat of Fire", 3),
        LockedCard("Salamangreat Gazelle", 3),
        LockedCard("Salamangreat Spinny", 2),
        LockedCard("Salamangreat Foxy", 2),
        LockedCard("Salamangreat Jack Jaguar", 2),
        LockedCard("Salamangreat Weasel", 1),
        LockedCard("Salamangreat Falco", 1),
        LockedCard("Code of Soul", 1),
        LockedCard("Ash Blossom & Joyous Spring", 3),
        LockedCard("Ghost Belle & Haunted Mansion", 3),
        LockedCard("Effect Veiler", 3),
        LockedCard("Salamangreat Circle", 3),
        LockedCard("Cynet Mining", 3),
        LockedCard("Will of the Salamangreat", 2),
        LockedCard("Salamangreat Sanctuary", 1),
        LockedCard("Salamangreat Charge", 1),
        LockedCard("Called by the Grave", 1),
        LockedCard("Infinite Impermanence", 3),
        LockedCard("Salamangreat Roar", 1),
        LockedCard("Salamangreat Rage", 1),
    ),
    extra=(
        LockedCard("Salamangreat Balelynx", 3),
        LockedCard("Salamangreat Sunlight Wolf", 3),
        LockedCard("Salamangreat Raging Phoenix", 2),
        LockedCard("Salamangreat Pyro Phoenix", 2),
        LockedCard("Salamangreat Heatleo", 1),
        LockedCard("Salamangreat Miragestallio", 2),
        LockedCard("Promethean Princess, Bestower of Flames", 1),
        LockedCard("Hiita the Fire Charmer, Ablaze", 1),
    ),
)


LOCKED_DECKS = {"deck_a": SWORDSoul, "deck_b": SALAMANGREAT}
