# OCGForge M4.2 — Observation-Path Performance Audit

Status: **M4.2 PERFORMANCE AUDIT PASS**

No optimization was implemented and M5 was not started.

## Workload

- Matchup: Swordsoul Tenyi ML v1 vs Salamangreat ML v1
- Master seed: 20260815; games: 16; max steps: 2200
- Workers: 1; mode: throughput; instrumentation: false; trace persistence: false

## Observation timing — FULL sample

| bucket | total us | calls | mean us/call | fraction of outer observation |
|---|---:|---:|---:|---:|
| observation_query_field | 102766 | 9908 | 10 | 0.017665% |
| observation_query_location | 36324156 | 118896 | 305 | 6.243835% |
| observation_query_individual | 0 | 0 | 0 | 0.000000% |
| observation_query_decode | 57692581 | 128804 | 447 | 9.916898% |
| observation_zone_projection | 40269 | 508088 | 0 | 0.006922% |
| observation_entity_projection | 2387708 | 526004 | 4 | 0.410428% |
| observation_relationship_projection | 279 | 9908 | 0 | 0.000048% |
| observation_visibility_privacy | 3269 | 1476104 | 0 | 0.000562% |
| observation_candidate_consistency | 8562 | 33247 | 0 | 0.001472% |
| observation_canonical_serialization | 178413003 | 19800 | 9010 | 30.667783% |
| observation_hash | 278926935 | 19800 | 14087 | 47.945332% |
| observation_other | 27860825 | 9908 | 2811 | 4.789055% |

Outer observation: 581760353 us across 9908 observations.

## Query call-site classification

| call site | query | calls/observation | measured calls | same-state duplicate? | safe reuse candidate |
|---|---|---:|---:|---|---|
| observation_builder.cpp: query_field | OCG_DuelQueryField | 1 | 9908 | False | Only after semantic equivalence is proved for the field snapshot |
| canonical_simulation.cpp: public_state_hash terminal/decision | OCG_DuelQueryField | 1 | 9908 | True | Only with a hash-equivalence proof including perspective and state lifetime |
| observation_builder.cpp: query_location loops | OCG_DuelQueryLocation | 12 | 118896 | False | No elimination candidate; preserve all zone queries |
| observation_builder.cpp: query_card | OCG_DuelQuery | 0 | 0 | False | No change proposed; sidecar tracks any future nonzero path |

## Entity/privacy audit

| zone | projected | identity known | redacted |
|---|---:|---:|---:|
| UNKNOWN | 0 | 0 | 0 |
| MAIN_DECK | 0 | 0 | 0 |
| HAND | 64656 | 64656 | 0 |
| MONSTER_ZONE | 0 | 0 | 0 |
| SPELL_TRAP_ZONE | 0 | 0 | 0 |
| GRAVEYARD | 312552 | 312552 | 0 |
| BANISHED | 0 | 0 | 0 |
| EXTRA_DECK | 148620 | 148620 | 0 |
| FIELD_ZONE | 0 | 0 | 0 |
| PENDULUM_RELEVANT_STATE | 176 | 176 | 0 |
| OVERLAY | 0 | 0 | 0 |

Static printed metadata lookups: 526004; current-property projections: 526004; relationship objects: 0; explicit allocation/copy events: 7371722.
The audit counted 526004 repeated static_card_data lookups during entity projection; immutable printed metadata is therefore reconstructed in the measured observation path. No representation change was implemented.

## Setup, coordinator, and off cross-check

Script loads: 1232; script-reader requests: 1200.
Setup timing: core_host_setup 346992 us; fixture_script_load 0 us; script_load 3709945 us.
Coordinator worker-compute wait: 609385066 us; CPU domains total: 10000000 us; wait is not counted as CPU.
Off sample: DIAGNOSTIC ONLY — NOT TRAINING THROUGHPUT; worker-local runtime FULL 616501509 us, off 29051031 us.

## Candidate audit

Candidate maximum: 1344 (baseline evidence: docs/m4/m4_baseline.json:evidence.rows_by_worker.1.operation_counters.candidate_max). Baseline protocol_candidate fraction: 0.189337%; audit sample legacy timing field: 0.000000% (not independently instrumented by the native audit sidecar). The baseline gate remains authoritative; no candidate optimization is proposed.

| rank | candidate | measured fraction | semantic risk | complexity | affected bucket |
|---:|---|---:|---|---|---|
| 1 | observation_hash_cost_audit | 47.945332% | high | medium | observation_hash |
| 2 | canonical_serialization_copy_reduction | 30.667783% | medium | medium | observation_canonical_serialization |
| 3 | entity_projection_static_metadata_reuse | 0.410428% | high | high | observation_entity_projection |
| 4 | query_field_reuse_for_public_state_hash | 0.017665% | high | medium | observation_query_field |
| 5 | visibility_privacy_projection_reduction | 0.000562% | high | high | observation_visibility_privacy |
| 6 | relationship_projection_reuse | 0.000048% | high | medium | observation_relationship_projection |

## First M4.3 experiment

observation_hash_cost_audit — Highest measured observation-path fraction: 47.945332% in observation_hash. This is an experiment target only; no speedup is estimated.
Required equivalence test: Canonical serialized bytes and observation_hash equality for every deterministic sample row, including perspective/privacy fixtures
