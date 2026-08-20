# OCGForge M4.3.1 — Eliminate Premature Decision-Observation Finalization

Status: **M4.3.1 ACCEPTED**

This report covers one narrow optimization experiment only. No ocgcore,
CardScripts, rules bundle, deck identity, privacy model, canonical format,
hash algorithm, worker architecture, candidate generation, trace semantics,
or M5 work was changed.

## Scope and implementation

`ObservationBuildConfig` now has an explicit
`ObservationFinalization::{Immediate,Deferred}` setting. `Immediate` remains
the default, so normal and standalone callers still receive a finalized
`PlayerObservation` with a valid `observation_hash`.

Only the interactive decision branch of `run_canonical_simulation()` opts into
`Deferred`:

```text
build_player_observation(..., Deferred)
    -> attach_decision_context(...)
    -> exactly one final observation_hash()
```

Deferred observations are explicitly distinguishable while unfinished: their
stored `observation_hash` is empty until the decision context is attached and
the final hash is computed. Terminal observations retain the Immediate
default. `attach_decision_context()` and the finalization algorithm were not
changed.

## Characterization before the production change

The regression characterization was added before the production change and
proves the obsolete lifecycle without asserting the obsolete intermediate
hash:

```text
build_player_observation()       -> intermediate hash
attach_decision_context()        -> context mutation
                                  -> final hash
```

Representative visible decision fixture:

- intermediate hash:
  `b618b963f8ea78da8ae46e87885beb629b7c4c52e0fe168a793a06800424c04a`
- final hash:
  `b493835150402de4673c30d971596a0f53fb7446b573e875ce87b553a3247fc7`
- final canonical byte length: `6852`
- SHA-256 of final canonical bytes:
  `1ef3d2351370c2a9a21d2be4ce0d5e629710a399d3c5f86268b8b0640472a734`

The test captures and prints the complete final canonical byte string at
runtime. The final hash is checked against `SHA-256(canonical_serialize())`;
the intermediate hash is used only to demonstrate that context mutation
invalidated it.

## Equivalence coverage

The eager and deferred paths are compared after decision context attachment.
For every fixture, both `canonical_serialize(observation)` and the stored
`observation_hash` are byte-/value-identical, and the stored hash equals the
SHA-256 digest of the current canonical bytes.

Covered fixtures include:

- ordinary visible decision with a real candidate reference;
- both player perspectives;
- continuation decision;
- hidden-information perspective;
- paired hidden-world privacy fixture for both perspectives;
- terminal observation unchanged;
- candidate/observation consistency;
- empty deferred hash before attachment, preventing silent misuse.

## Benchmark workload

The comparison uses one fresh representative repetition per side because a
single 16-game sample takes roughly ten minutes. Both sides use the exact
M4.2 workload and the same Debug toolchain identity:

- Swordsoul Tenyi ML v1 vs Salamangreat ML v1;
- master seed `20260815`;
- 16 games, max steps `2200`;
- one worker, FULL mode, no trace persistence;
- before worker:
  `C:\yogiohML\build\m4-audit\ygo_m4_worker.exe`;
- after worker:
  `C:\yogiohML-m4-3-1\build\m4-audit\ygo_m4_worker.exe`;
- both audit reports: `M4.2 PERFORMANCE AUDIT PASS`;
- canonical workload and identity matched; the observation-off run remained
  diagnostic-only.

## Before / after measurements

| metric | before | after | result |
|---|---:|---:|---:|
| worker-local simulation time | 653,314,944 us | 411,611,012 us | -36.9965% |
| worker games/s | 0.0244905 | 0.0388717 | 1.5872x |
| games/s change | — | — | +58.7214% |
| outer observation time | 616,989,008 us | 374,814,096 us | -39.2511% |
| outer observation fraction | 94.439751% | 91.060269% | -3.379482 pp |
| canonical serialization calls | 19,800 | 9,908 | exactly one/observation |
| canonical serialization time | 188,866,369 us | 94,686,307 us | -49.8660% |
| observation hash calls | 19,800 | 9,908 | exactly one/observation |
| observation hash time | 297,308,375 us | 147,586,934 us | -50.3590% |
| query-decode calls | 128,804 | 128,804 | unchanged |
| query-decode time | 60,646,795 us | 61,785,651 us | measurement variation |
| `OCG_DuelQueryField` calls | 19,816 | 19,816 | unchanged |
| `OCG_DuelQueryLocation` calls | 118,896 | 118,896 | unchanged |
| projected entities | 526,004 | 526,004 | unchanged |
| script loads | 1,232 | 1,232 | unchanged |

The call-count reduction is the expected structural result: the intermediate
decision finalization disappeared, while terminal observations and final
decision hashes remain covered by one hash per observation. The measured
runtime improvement is reported, not promised or extrapolated.

The audit's primary operation counters, including observations, candidates,
query counts, entities, scripts, semantic hashes, and trace bytes, are
identical before and after. `allocation_copy_events` changed from `7,371,722`
to `3,970,235`; this is the performance-side copy counter for the removed
second serialization pass, not a semantic state or integrity counter.

## Integrity and semantic gates

- Full CTest: **90/90 passed**, including the worker integration gate.
- Repository Python tests: **8/8 passed**.
- M3 Python unit suite: **17/17 passed**.
- M4 Python suite: **121/121 passed**, with 3 declared skips.
- M3 canonical fixed-deck run: **16/16 complete**, both start-player
  partitions present.
- M3 determinism: independent-process, semantic replay, and CRLF replay
  checks passed for both start players.
- Before/after M3 trace hashes are identical:
  - start player 0:
    `afb6ee362c5cabf850c5ec4c7098a12981ce0d550be7658a28309090343457a8`;
  - start player 1:
    `7a252b56a5f4b22b9e9a58f17be1414137614fc5f7e8ff95e84ff4069f214a36`.
- The 16-game FULL benchmark integrity gate passed, all games terminated,
  and the complete before/after gameplay-hash lists are identical.
- Privacy partitions and paired-world equivalence passed.
- Rules bundle, patchset, deck hashes, action counts, terminal results, and
  worker lifecycle gates are unchanged.

## 118/119 manifest clarification

The historical test-count concern is not an M4.3.1 regression. The relevant
checkout currently discovers 121 M4 tests. During the baseline investigation,
the acceptance-manifest assertion at
`tests/m4/test_benchmark_integrity.py:195` was also reproduced on the parent
`ee314e5^` checkout, with the same acceptance-pending condition. That proves
the earlier failure was pre-existing rather than introduced by the deferred
finalization change. The final M4 run on this branch is green (121 passed,
3 skipped) after the fresh audit evidence was present.

## Next measured observation bucket

After the experiment, `observation_hash` remains the largest measured bucket
at `147,586,934 us` (`39.38%` of outer observation time), followed by
`observation_canonical_serialization` at `25.26%` and query decode at
`16.48%`. No further hash, serialization, query, metadata, cache, or
incremental-state optimization is implemented here.

## Review and recommendation

An independent read-only review found no blocker or major issue. It confirmed
the Immediate default, the Decision-only Deferred opt-in, unchanged terminal
behavior, privacy/equivalence coverage, and the narrow four-file scope. Its
only minor note was that the builder equivalence tests do not themselves form
a separate full `run_canonical_simulation()` trace-output fixture; the full
CTest/worker integration and the exact before/after canonical 16-game
integrity run provide that end-to-end evidence.

**Recommendation for the next experiment:** stop M4.3.1 here. If another M4
experiment is authorized, first characterize the remaining `observation_hash`
bucket and its canonical-byte boundaries; do not optimize it broadly based
on this report alone. M5 is not started.
