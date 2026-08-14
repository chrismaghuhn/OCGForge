# OCGForge M1.1 acceptance matrix

The `M1.1 status` column uses the exact machine-readable classifications from
[`decision_coverage.json`](decision_coverage.json). `Gate` is the acceptance
result for this milestone, not a second coverage classification.

| Message | ID | M1.1 status | Gate | Evidence / reason |
| --- | ---: | --- | --- | --- |
| `MSG_REQUEST_DECK` | 8 | `OUT_OF_SCOPE_M1` | NOT APPLICABLE | No pinned writer; deliberately out of scope |
| `MSG_SELECT_BATTLECMD` | 10 | `SUPPORTED_ENGINE_VERIFIED` | PASS | `m0.controlled_duel` / `determinism_test` |
| `MSG_SELECT_IDLECMD` | 11 | `SUPPORTED_ENGINE_VERIFIED` | PASS | `m0.controlled_duel` / `determinism_test` |
| `MSG_SELECT_EFFECTYN` | 12 | `SUPPORTED_ENGINE_VERIFIED` | PASS | `m1.1.select_option` / `m1_engine_select_option`; real yes branch accepted |
| `MSG_SELECT_YESNO` | 13 | `SUPPORTED_PROTOCOL_VERIFIED` | PENDING | Parser and yes/no tests; no stable focused fixture |
| `MSG_SELECT_OPTION` | 14 | `SUPPORTED_ENGINE_VERIFIED` | PASS | `m1.1.select_option` / `m1_engine_select_option`; two real ordinals |
| `MSG_SELECT_CARD` | 15 | `SUPPORTED_ENGINE_VERIFIED` | PASS | `m1.1.select_card_multi` / `m1_engine_select_card_multi`; two-card real continuation |
| `MSG_SELECT_CHAIN` | 16 | `SUPPORTED_ENGINE_VERIFIED` | PASS | `m0.controlled_duel` / `determinism_test` |
| `MSG_SELECT_PLACE` | 18 | `SUPPORTED_ENGINE_VERIFIED` | PASS | `m0.controlled_duel.atomic_place` / `determinism_test`; count-one engine path |
| `MSG_SELECT_POSITION` | 19 | `SUPPORTED_PROTOCOL_VERIFIED` | PENDING | Position parser/tests; no stable focused fixture |
| `MSG_SELECT_TRIBUTE` | 20 | `SUPPORTED_ENGINE_VERIFIED` | PASS | `m1.tribute.vip_whale` / `m1_engine_fixture_test` |
| `MSG_SORT_CHAIN` | 21 | `SUPPORTED_PROTOCOL_VERIFIED` | PENDING | Permutation oracle; stable fixture would require unrelated chain-order mechanics |
| `MSG_SELECT_COUNTER` | 22 | `SUPPORTED_ENGINE_VERIFIED` | PASS | `m1.1.select_counter` / `m1_engine_select_counter`; final allocation `0,1` |
| `MSG_SELECT_SUM` | 23 | `SUPPORTED_ENGINE_VERIFIED` | PASS | `m1.1.select_sum` / `m1_engine_select_sum`; real target/mode/contribution domain |
| `MSG_SELECT_DISFIELD` | 24 | `SUPPORTED_ENGINE_VERIFIED` | PASS | `m1.1.select_disfield` / `m1_engine_select_disfield`; two typed distinct zones |
| `MSG_SORT_CARD` | 25 | `SUPPORTED_ENGINE_VERIFIED` | PASS | `m1.1.sort_card` / `m1_engine_sort_card`; order `0,1,2` |
| `MSG_SELECT_UNSELECT_CARD` | 26 | `SUPPORTED_PROTOCOL_VERIFIED` | PENDING | Iterative toggle tests; no stable focused fixture |
| `MSG_ROCK_PAPER_SCISSORS` | 132 | `OUT_OF_SCOPE_M1` | NOT APPLICABLE | Pre-game choice outside M1.1 |
| `MSG_ANNOUNCE_RACE` | 140 | `SUPPORTED_PROTOCOL_VERIFIED` | PENDING | Mask oracle; no stable focused fixture |
| `MSG_ANNOUNCE_ATTRIB` | 141 | `SUPPORTED_PROTOCOL_VERIFIED` | PENDING | Mask oracle; no stable focused fixture |
| `MSG_ANNOUNCE_CARD` | 142 | `UNSUPPORTED_FAIL_CLOSED` | NOT APPLICABLE | Complete legal domain is not proven; no heuristic subset |
| `MSG_ANNOUNCE_NUMBER` | 143 | `SUPPORTED_PROTOCOL_VERIFIED` | PENDING | Number parser/oracle; no stable focused fixture |

## M1.1 gates

- Existing M0 lifecycle, determinism, privacy, and rules-bundle gates remain mandatory.
- Existing M1 oracle, fail-closed, stale-action, continuation, and trace tests remain mandatory.
- The real multi-step continuation gate is satisfied by `m1.1.select_card_multi`: two intermediate `engine_advanced=false` records, unchanged engine state, exactly one final response submission, and subsequent engine output.
- The four additional complex-family fixtures (`MSG_SELECT_SUM`, `MSG_SELECT_COUNTER`, `MSG_SORT_CARD`, and `MSG_SELECT_DISFIELD`) satisfy the same retry-free final-response contract.
- Same-seed semantic gameplay hashes are compared across independent processes. Trace hashes remain artifact/toolchain provenance and are not used as the cross-process determinism requirement.
- The rules bundle lock, upstream core, CardScripts, and BabelCDB remain unchanged. No commit, push, tag, or pull request is part of this task.
