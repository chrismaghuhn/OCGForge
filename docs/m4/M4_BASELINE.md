# OCGForge M4 Baseline

**M4 BASELINE PASS — PERFORMANCE AUDIT READY**

all independently verified acceptance gates passed

## Scaling

| workers | games | wall s | games/s | engine steps/s | decisions/s | speedup | efficiency | sim mean us | p50 us | p95 us | p99 us | peak working set | physical-memory % | result timeout s |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 64 | 66.198606 | 0.966788 | 1464.925110 | 596.976318 | 1.000000 | 1.000000 | 974927.328125 | 969901 | 1046190 | 1062960 | 14340096 | 0.041834 | 300.000000 |
| 2 | 64 | 46.689890 | 1.370746 | 2077.023545 | 846.414509 | 1.417836 | 0.708918 | 1393155.156250 | 1333419 | 1776964 | 1887688 | 27435008 | 0.080035 | 300.000000 |
| 4 | 64 | 27.558118 | 2.322365 | 3518.963151 | 1434.023931 | 2.402145 | 0.600536 | 1635080.453125 | 1593761 | 2166025 | 2235572 | 53211136 | 0.155230 | 300.000000 |
| 8 | 64 | 12.462934 | 5.135227 | 7781.153423 | 3170.922724 | 5.311639 | 0.663955 | 1431505.875000 | 1430797 | 1483667 | 1507590 | 103424000 | 0.301714 | 300.000000 |
| 16 | 64 | 8.726749 | 7.333773 | 11112.500199 | 4528.490507 | 7.585712 | 0.474107 | 1907009.140625 | 1856302 | 2560437 | 2679427 | 198811648 | 0.579983 | 300.000000 |
| 32 | 64 | 9.270774 | 6.903415 | 10460.399644 | 4262.750923 | 7.140570 | 0.223143 | 3571790.671875 | 3219980 | 6924872 | 7673885 | 367009792 | 1.070659 | 300.000000 |
| 64 | 64 | 11.502609 | 5.563955 | 8430.782964 | 3435.655337 | 5.755095 | 0.089923 | 8108166.609375 | 8287704 | 10972472 | 11118732 | 688861184 | 2.009580 | 300.000000 |

## Optional worker rows

| workers | status | reason |
|---:|---|---|
| 64 | MEASURED |  |
| 128 | NOT_RUN | NOT_RUN - no final measurement; 64 workers is the maximum semantically validated concurrency for this M4 final corpus |

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
| core_process | 10.362272 |
| protocol_candidate | 1.431007 |
| continuation | 0.000000 |
| observation | 83.621286 |
| trace_hash | 0.941424 |
| serialization | 0.000000 |
| other | 3.644010 |
| trace_persistence | 0.000000 |
| dispatch_to_receipt | 100.000000 |
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
- dispatch/result latency audit: measured dispatch_to_receipt timing bucket
- script-load audit: measured script_loads counter
- candidate-set audit: measured candidate_total counter

No candidate is implemented by this baseline handoff.
