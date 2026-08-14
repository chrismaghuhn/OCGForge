# OCGForge M1.1 decision coverage

The machine-readable authority is
[`decision_coverage.json`](decision_coverage.json). It is pinned to ocgcore
`9a0c558c2d686542f7914a6d529fd7aa57746aed` and the unchanged rules bundle
`6fbbd212ae4be2df36170dcbfcdf5c46aaaa0e3091cf815c2d0261fd01640ea4`.

## Status definitions

`SUPPORTED_PROTOCOL_VERIFIED` means:

> The parser, legal candidate generation, continuation semantics, response encoder, and isolated/property/oracle tests are verified, but no actual pinned-core fixture has yet proven the full decision path.

`SUPPORTED_ENGINE_VERIFIED` means:

> A pinned real ocgcore fixture has emitted the relevant interactive decision, OCGForge exposed it correctly, a valid semantic response was selected and encoded, ocgcore accepted it, and execution continued without retry/protocol failure.

`UNSUPPORTED_FAIL_CLOSED` means the decision family is recognized as
unsupported and fails closed without guessing or silently selecting an action.

> The decision family is recognized as unsupported and fails closed without guessing or silently selecting an action.

`OUT_OF_SCOPE_M1` means the family is intentionally outside the M1/M1.1 scope
and is not counted as supported coverage.

> The decision family is intentionally outside the M1/M1.1 scope and is not counted as supported coverage.

## Final inventory

| Message | ID | M0 status | M1.1 status | Engine evidence |
| --- | ---: | --- | --- | --- |
| `MSG_REQUEST_DECK` | 8 | `UNSUPPORTED_FAIL_CLOSED` | `OUT_OF_SCOPE_M1` | None; no pinned writer |
| `MSG_SELECT_BATTLECMD` | 10 | `SUPPORTED_PROTOCOL_VERIFIED` | `SUPPORTED_ENGINE_VERIFIED` | `m0.controlled_duel` / `determinism_test` |
| `MSG_SELECT_IDLECMD` | 11 | `SUPPORTED_PROTOCOL_VERIFIED` | `SUPPORTED_ENGINE_VERIFIED` | `m0.controlled_duel` / `determinism_test` |
| `MSG_SELECT_EFFECTYN` | 12 | `SUPPORTED_PROTOCOL_VERIFIED` | `SUPPORTED_ENGINE_VERIFIED` | `m1.1.select_option` / `m1_engine_select_option`; real yes branch accepted |
| `MSG_SELECT_YESNO` | 13 | `SUPPORTED_PROTOCOL_VERIFIED` | `SUPPORTED_PROTOCOL_VERIFIED` | No stable focused fixture obtained |
| `MSG_SELECT_OPTION` | 14 | `UNSUPPORTED_FAIL_CLOSED` | `SUPPORTED_ENGINE_VERIFIED` | `m1.1.select_option` / `m1_engine_select_option` |
| `MSG_SELECT_CARD` | 15 | `SUPPORTED_PROTOCOL_VERIFIED` | `SUPPORTED_ENGINE_VERIFIED` | `m1.1.select_card_multi` / `m1_engine_select_card_multi` |
| `MSG_SELECT_CHAIN` | 16 | `SUPPORTED_PROTOCOL_VERIFIED` | `SUPPORTED_ENGINE_VERIFIED` | `m0.controlled_duel` / `determinism_test` |
| `MSG_SELECT_PLACE` | 18 | `SUPPORTED_PROTOCOL_VERIFIED` | `SUPPORTED_ENGINE_VERIFIED` | `m0.controlled_duel.atomic_place` / `determinism_test`; count-one path |
| `MSG_SELECT_POSITION` | 19 | `SUPPORTED_PROTOCOL_VERIFIED` | `SUPPORTED_PROTOCOL_VERIFIED` | No stable focused fixture obtained |
| `MSG_SELECT_TRIBUTE` | 20 | `UNSUPPORTED_FAIL_CLOSED` | `SUPPORTED_ENGINE_VERIFIED` | `m1.tribute.vip_whale` / `m1_engine_fixture_test` |
| `MSG_SORT_CHAIN` | 21 | `UNSUPPORTED_FAIL_CLOSED` | `SUPPORTED_PROTOCOL_VERIFIED` | No stable focused fixture obtained |
| `MSG_SELECT_COUNTER` | 22 | `UNSUPPORTED_FAIL_CLOSED` | `SUPPORTED_ENGINE_VERIFIED` | `m1.1.select_counter` / `m1_engine_select_counter` |
| `MSG_SELECT_SUM` | 23 | `UNSUPPORTED_FAIL_CLOSED` | `SUPPORTED_ENGINE_VERIFIED` | `m1.1.select_sum` / `m1_engine_select_sum` |
| `MSG_SELECT_DISFIELD` | 24 | `UNSUPPORTED_FAIL_CLOSED` | `SUPPORTED_ENGINE_VERIFIED` | `m1.1.select_disfield` / `m1_engine_select_disfield` |
| `MSG_SORT_CARD` | 25 | `UNSUPPORTED_FAIL_CLOSED` | `SUPPORTED_ENGINE_VERIFIED` | `m1.1.sort_card` / `m1_engine_sort_card` |
| `MSG_SELECT_UNSELECT_CARD` | 26 | `UNSUPPORTED_FAIL_CLOSED` | `SUPPORTED_PROTOCOL_VERIFIED` | No stable focused fixture obtained |
| `MSG_ROCK_PAPER_SCISSORS` | 132 | `UNSUPPORTED_FAIL_CLOSED` | `OUT_OF_SCOPE_M1` | Pre-game choice outside M1.1 |
| `MSG_ANNOUNCE_RACE` | 140 | `UNSUPPORTED_FAIL_CLOSED` | `SUPPORTED_PROTOCOL_VERIFIED` | No stable focused fixture obtained |
| `MSG_ANNOUNCE_ATTRIB` | 141 | `UNSUPPORTED_FAIL_CLOSED` | `SUPPORTED_PROTOCOL_VERIFIED` | No stable focused fixture obtained |
| `MSG_ANNOUNCE_CARD` | 142 | `UNSUPPORTED_FAIL_CLOSED` | `UNSUPPORTED_FAIL_CLOSED` | Complete legal domain remains unproven |
| `MSG_ANNOUNCE_NUMBER` | 143 | `UNSUPPORTED_FAIL_CLOSED` | `SUPPORTED_PROTOCOL_VERIFIED` | No stable focused fixture obtained |

The initial M1 inventory was `1 / 18 / 1 / 2` for engine-verified,
protocol-verified, fail-closed, and out-of-scope families respectively. The
M1.1 inventory is `12 / 7 / 1 / 2`. The increase is from actual pinned-core
fixtures, not from changing the protocol definition.

## Engine-conformance boundary

The six new focused fixtures use the unchanged pinned ocgcore, CardScripts,
and BabelCDB bundle. They load the existing player deck files, add only the
scenario cards through the public `OCG_DuelNewCard` setup API, and stop after a
valid final response has produced a subsequent engine message. Every fixture
uses `ygo.engine_trace.v2`, rejects `MSG_RETRY`, checks the complete candidate
domain, and asserts exactly one response submission for its target decision.

`m1.1.select_card_multi` is the mandatory real continuation: the engine emits
two candidates with `min=1` and `max=2`; OCGForge records pick, pick, and finish
locally; the first two records have `engine_advanced=false` and no final
response hash; and one final response is accepted by the engine. The public
state hash remains
`01f7405ff7984e6d95b91410d72f8ecd08843a69ba181988e0104c7e106ea83b`
throughout the local continuation.

The sum, counter, sort-card, and disfield fixtures exercise the same
immobility and one-final-response contract. `MSG_SELECT_PLACE` remains engine
verified through the existing M0 count-one fixture; no claim is made that a
multi-zone place fixture was obtained. `MSG_SORT_CHAIN` remains protocol-only:
source inspection shows the stable path depends on multiple simultaneous
chain-order mechanics, and no small deterministic CardScripts fixture was
available without pulling unrelated combat/mechanics scope into M1.1.

The lower-risk pending families remain protocol-only because no deterministic
focused pinned-core fixture was obtained within the narrow milestone:
`MSG_SELECT_YESNO`,
`MSG_SELECT_POSITION`, `MSG_ANNOUNCE_NUMBER`, `MSG_ANNOUNCE_ATTRIB`,
`MSG_ANNOUNCE_RACE`, and `MSG_SELECT_UNSELECT_CARD`. Their parser, candidate,
oracle, privacy, determinism, and fail-closed tests remain mandatory.

`MSG_ANNOUNCE_CARD` remains fail-closed because the pinned message supplies
predicate opcodes rather than a complete legal card list. No guessed subset or
name-search heuristic was added. `MSG_REQUEST_DECK` and
`MSG_ROCK_PAPER_SCISSORS` remain outside M1.1.

No legal-action cap, automatic player decision, Linux path, modified upstream
core, or modified CardScripts was introduced.
