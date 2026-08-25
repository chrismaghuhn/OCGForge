# M4.3.5 Reserve-Backed Canonical Serialization A/B

**Status:** M4.3.5 REJECTED — NO MATERIAL BENEFIT

M4.3.5 was a single isolated output-buffer experiment. The reserve-backed implementation was rejected after exact equivalence and clean Release A/B measurement; the production optimization was reverted.

## Frozen identity and workload

- M4.3.4 freeze: `76c1a028f1c99a8ed46b63da3c3b93b64cf3a0e8`.
- M4.3.5 starting HEAD: `db7e5af2c97d9b6eccd697b903e9ba6fcea70a30`.
- Matchup: Swordsoul Tenyi ML v1 vs Salamangreat ML v1; master seed `20260815`.
- Games/workers/max steps: `16` / `1` / `2200`.
- FULL observations, throughput mode, trace persistence off, ordinary Release `-O3 -DNDEBUG`.
- No M4.3.4 shape instrumentation was used for timing.
- Measured A/B worker hashes: control `eb54e71502c3da04c273f3c2bc5c02723d9d563926438f33020d47078eb104dc`, experiment `55214361dee1e8df48629156fb7c7bad7b0aa5693c0fa225500eaabdb3513038`.
- Post-reversion ordinary Release worker hash: `7375e79b550f2c36c9ce41c4c4cef821417abaaa8b51bbff0b04d1c59678715f`.

## Implementation and reversion

The experiment generalized the internal writer to `std::ostream`, added a private string-backed stream buffer, and reserved a deterministic structure-only capacity hint. Field ordering, formatting, escaping, sorting, canonical bytes, SHA-256 input, privacy, and event history were unchanged.

The measured result did not meet the materiality rule, so the reserve-backed production path and its telemetry were reverted. The focused fixture, comparison harness, raw A/B artifacts, and this report remain as characterization evidence.

## Audit harness integrity

- Raw trace-hash mismatches fail the conformance comparison.
- The A/B runner closes reserve calls/bytes against lifecycle serialization counters and rejects nonmaterial timing.
- Finalization recomputes materiality, re-hashes all six worker sidecars, verifies every lifecycle ID and per-lifecycle call, and checks control/experiment build identity equality.
- Finalization validates the equivalence artifacts, all control/experiment gates, starting-HEAD reversion, and Release build policy.

## Equivalence

| Gate | Result | Evidence |
|---|---|---|
| Focused serialization/privacy fixtures | **PASS** | `C:\yogiohML-m4-3-1\artifacts\m4\m4-3-5\final\fixture-comparison-hardening2.json` |
| 16-game conformance, trace steps and per-observation hashes | **PASS** | `C:\yogiohML-m4-3-1\artifacts\m4\m4-3-5\final\conformance-comparison-hardening2.json`; 9,908 hashes |
| Canonical bytes and observation hashes | **PASS** | 1,345,246,987 bytes in each of six repetitions; 9,908 hashes equal in conformance |
| Operation/error counters | **PASS** | Identical across A1/B1/A2/B2/A3/B3; all integrity counters zero |
| Privacy and paired-world behavior | **PASS** | Focused fixtures, CTest and conformance |
| Sidecar lifecycle and build identity closure | **PASS** | Six sidecars re-hashed; lifecycle IDs/calls and compiler/rules/deck identities revalidated |

## Raw alternating Release measurements

| Run | Variant | Worker-local us | Games/s | Outer observation us | Serializer us | Hash us |
|---|---|---:|---:|---:|---:|---:|
| A1 | control | 28,241,987 | 0.566532376 | 26,113,803 | 17,873,983 | 4,306,241 |
| B1 | experiment | 28,172,224 | 0.567935283 | 26,033,133 | 17,813,125 | 4,318,152 |
| A2 | control | 28,179,174 | 0.567795209 | 26,048,688 | 17,875,558 | 4,322,236 |
| B2 | experiment | 28,129,048 | 0.568807021 | 26,000,217 | 17,786,542 | 4,315,749 |
| A3 | control | 28,048,881 | 0.570432738 | 25,938,759 | 17,813,253 | 4,308,296 |
| B3 | experiment | 28,105,675 | 0.569280048 | 26,010,608 | 17,800,239 | 4,326,344 |

## Median and range

- Control worker-local: `28,179,174` us (range `28,048,881`–`28,241,987`).
- Experiment worker-local: `28,129,048` us (range `28,105,675`–`28,172,224`).
- Control serializer: `17,873,983` us (range `17,813,253`–`17,875,558`).
- Experiment serializer: `17,800,239` us (range `17,786,542`–`17,813,125`).
- Worker-local median change: **0.177883%**.
- Serializer median change: **0.412577%**.
- Paired improvements: worker `2/3`; serializer `3/3`.
- Materiality rule: serializer median >= 5%, worker median >= 3%, and >= 2/3 paired improvements for both. **FAIL**.

## Reserve telemetry

The experiment requested a large deterministic hint but produced no growth events. Across the 9,908-observation sample, the requested capacity was `2,887,091,200` bytes versus `1,345,246,987` output bytes; unused final capacity was `1,541,913,569` bytes. This is diagnostic evidence, not a protocol limit or a reason to change canonical content.

## Regression gates

- `candidate_observation_consistency`: **PASS** 2/2
- `candidate_observation_consistency_control`: **PASS** 2/2
- `canonical_fixed_deck_regression`: **PASS** 16/16 games
- `determinism`: **PASS** partitions=[0, 1]
- `full_ctest`: **PASS** 92/92
- `full_ctest_control`: **PASS** 92/92
- `m3_python`: **PASS** 17/17
- `m4_python`: **PASS** 123/123
- `post_reversion_release_smoke`: **PASS** 5/5
- `privacy`: **PASS** 3/3
- `privacy_control`: **PASS** 3/3
- `repository_python`: **PASS** 8/8
- `reserve_output_contract`: **PASS**

Gate freshness/provenance: The frozen regression gates ran on the M4.3.5 Release A/B builds; the hardening2 A/B rerun revalidated the current post-reversion control against the preserved experiment binary; post_reversion_release_smoke ran against the ordinary Release binary.
Interactive command output hashes: NOT_CAPTURED; machine-readable comparison, fixed-deck, determinism, and sidecar artifacts retain their own hashes where applicable.

## Decision

**M4.3.5 REJECTED — NO MATERIAL BENEFIT.** The median result was experiment faster by 0.177883% for worker-local runtime and experiment faster by 0.412577% for serializer runtime. Those changes are far below the materiality thresholds; no future worker-matrix extrapolation is made.

Next recommendation: Do not carry the reserve-backed streambuf forward. If another serializer experiment is authorized, isolate stream construction with a tighter measured capacity model; do not infer a speedup or begin M5.

No event-history reduction, `to_chars` rewrite, JSON-escape rewrite, hash change, cache, incremental observation, ocgcore change, or M5 work was performed.
