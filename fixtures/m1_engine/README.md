# M1.1 pinned-engine fixtures

Each directory contains one bounded scenario README and is executed by
`m1_engine_conformance_test` on Windows. The scenarios use the unchanged
ocgcore/CardScripts/BabelCDB rules bundle and add setup cards only through the
public `OCG_DuelNewCard` API after `OCG_StartDuel` and before the first process.

The `cards.deck` file is a data-preparation input, not a player deck. The
player deck hashes recorded in each scenario manifest remain the committed
`fixtures/decks/player_a.deck` and `fixtures/decks/player_b.deck` files.

Stable fixture commands are registered as:

- `select_option`
- `select_card_multi`
- `select_sum`
- `select_counter`
- `sort_card`
- `select_disfield`

Generated `ygo.engine_trace.v2` artifacts are written under
`artifacts/m1-engine/` and are intentionally not tracked.
