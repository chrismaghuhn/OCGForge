# OCGForge M3 Fixed Matchup

The locked M3 matchup is `ocgforge.matchup.swordsoul_salamangreat.v1` under `TCG_ADVANCED_2026_05_18`, with no side deck.

| Deck | Main / Extra | SHA-256 | Unique cards |
| --- | ---: | --- | ---: |
| `ocgforge.swordsoul_tenyi.ml_v1` | 40 / 15 | `8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7` | 26 |
| `ocgforge.salamangreat.ml_v1` | 40 / 15 | `6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188` | 28 |

The combined compatibility audit contains 110 ordered slots and 50 unique passcodes. The exact ordered passcode arrays and all slot metadata are machine-readable in [`card_compatibility.json`](card_compatibility.json) and [`ocgforge.matchup.swordsoul_salamangreat.v1.json`](../../fixtures/decks/ocgforge.matchup.swordsoul_salamangreat.v1.json).

The pinned component commits remain unchanged. The gameplay-relevant rules
identity changes because the canonical duel mode is now part of the lock:

- bundle `ff8721aae1a17da6a72079e65ae75a05012c0c367b6f249651c1de713c1fbf91`
- ocgcore `9a0c558c2d686542f7914a6d529fd7aa57746aed`
- CardScripts `f337c87018ca723c1aded5143e616bb649555273`
- BabelCDB `89ad6837b0766a52984d8c715a7d5d4f8447946b`
- public API `11.0`

No deck substitution, side-deck content, upstream component update, or
ocgcore patch is part of M3.2.

The canonical locked fixture now uses `DUEL_MODE_MR5 = 0x2E800` through the
lock-backed format mapping. Fixtures, full games, deterministic replay, and
semantic replay use the same flags; no test-only MR5 override remains. The
previous flags-0 evidence is retained as explicitly non-canonical historical
evidence. See [`RULE_MODE_AUDIT.md`](RULE_MODE_AUDIT.md) for the before/after
identity and source evidence. Link-zone-dependent mechanics are not classified
`NOT_APPLICABLE_FIXED_MATCHUP` merely because of the historical configuration.
