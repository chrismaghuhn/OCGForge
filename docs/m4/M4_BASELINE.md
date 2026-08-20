# OCGForge M4 Baseline

**M4 BASELINE PASS — PERFORMANCE AUDIT READY**

all independently verified acceptance gates passed

## Scaling

| workers | games | wall s | games/s | engine steps/s | decisions/s | speedup | efficiency | sim mean us | p50 us | p95 us | p99 us | peak working set | physical-memory % | result timeout s |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 64 | 2299.501353 | 0.027832 | 42.172621 | 17.185900 | 1.000000 | 1.000000 | 35870808.484375 | 35302045 | 42816128 | 46252056 | 19742720 | 0.057594 | 120.000000 |
| 2 | 64 | 1614.055837 | 0.039652 | 60.082184 | 24.484283 | 1.424673 | 0.712336 | 50373951.718750 | 50268280 | 52156549 | 54816396 | 37584896 | 0.109645 | 120.000000 |
| 4 | 64 | 873.496244 | 0.073269 | 111.020512 | 45.242324 | 2.632526 | 0.658131 | 54358792.375000 | 54321911 | 59179521 | 61238466 | 71700480 | 0.209168 | 120.000000 |
| 8 | 64 | 433.148363 | 0.147755 | 223.886336 | 91.236637 | 5.308808 | 0.663601 | 53867049.718750 | 53867432 | 54943598 | 55570216 | 141246464 | 0.412051 | 120.000000 |
| 16 | 64 | 266.621764 | 0.240040 | 363.721245 | 148.221208 | 8.624582 | 0.539036 | 65273640.812500 | 65302555 | 68506236 | 69919230 | 279064576 | 0.814101 | 120.000000 |
| 32 | 64 | 272.795726 | 0.234608 | 355.489441 | 144.866639 | 8.429389 | 0.263418 | 116939068.375000 | 105635458 | 204366384 | 236910562 | 502218752 | 1.465097 | 300.000000 |
| 64 | 64 | 272.719186 | 0.234674 | 355.589210 | 144.907297 | 8.431755 | 0.131746 | 187804613.734375 | 205705649 | 268876855 | 272598239 | 916389888 | 2.673338 | 600.000000 |

## Optional worker rows

| workers | status | reason |
|---:|---|---|
| 64 | MEASURED |  |
| 128 | NOT_RUN | NOT_RUN - measured 64-worker throughput did not improve over 32 workers; no measured usefulness for another oversubscribed row |

## Integrity and semantic gate

- Semantic gate: **PASS** across workers 1, 2, 4, 8, 16, 32, 64.
- Every accepted row has 64/64 terminal games, zero integrity counters, sorted unique job IDs, empty worker stderr, and the canonical handshake/environment.
- Result timeout is an operational guard, not a simulation-policy input; the measured 32/64-worker rows use documented longer guards because the 120-second control run timed out.
- Semantic comparisons cover: job_id, terminal, winner, win_reason, engine_steps, interactive_decisions, semantic_action_count, gameplay_hash, errors.

## Error counters

| workers | retries | unsupported | automatic | truncated | core errors | worker errors | handshake | malformed protocol | failed games | worker crashes | worker restarts |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 2 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 4 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 8 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 16 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 32 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 64 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |

## Timing percentages — one-worker reference

| bucket | measured percent |
|---|---:|
| core_process | 4.114108 |
| protocol_candidate | 0.189337 |
| continuation | 0.000000 |
| observation | 94.539848 |
| trace_hash | 0.394048 |
| serialization | 0.000000 |
| other | 0.762660 |
| trace_persistence | 0.000000 |
| coordinator_ipc | 100.000000 |
| coordinator_other | 0.000000 |

## Operation counters — one-worker reference

| counter | value |
|---|---:|
| candidate_max | 1344 |
| candidate_sets | 39519 |
| candidate_total | 187025 |
| entities_projected | 2100038 |
| observations | 39583 |
| ocg_duel_process | 96976 |
| ocg_duel_query | 0 |
| ocg_duel_query_count | 0 |
| ocg_duel_query_field | 79166 |
| ocg_duel_query_location | 474996 |
| script_loads | 4928 |
| script_reader_requests | 4800 |
| semantic_hashes | 64 |
| trace_bytes_serialized | 0 |

## PERFORMANCE AUDIT CANDIDATES

- observation-path audit: measured observation timing bucket
- coordinator/IPC audit: measured coordinator_ipc timing bucket
- script-load audit: measured script_loads counter
- candidate-set audit: measured candidate_total counter

No candidate is implemented by this baseline handoff.
