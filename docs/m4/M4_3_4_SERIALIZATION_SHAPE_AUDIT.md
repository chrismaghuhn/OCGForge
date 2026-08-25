# M4.3.4 — Canonical Serialization Shape, Copy, and Growth Audit

**Status:** `M4.3.4 SERIALIZATION CHARACTERIZATION PASS`  

This is a Release-build characterization only. No optimization, format change, event-history change, static-context removal, hash change, ocgcore change, or M5 work was implemented.

## Workload and identity

- Matchup: `Swordsoul Tenyi ML v1 vs Salamangreat ML v1`; master seed `20260815`; games `16`; one worker; max steps `2200`.
- Observation mode: `full`; throughput mode; trace persistence off.
- Compiler/build: `Clang-19.1.7` / `Release`; worker SHA-256 `97634b6ba007393d0c3cd4199f29c926920c497a7f37ad40fc7488cfb83469a7`.
- Measured-run binding: source HEAD `cf16e9b23b909f37c3921f41afefeb3692c8c2a8`; measured worker SHA-256 `97634b6ba007393d0c3cd4199f29c926920c497a7f37ad40fc7488cfb83469a7`; matches reported worker `True`.
- Observations: `9,908`; canonical-without-hash bytes: `1,345,246,987`.

## Lifecycle and byte integrity

The shape records contain `9,908` unique job-scoped lifecycle IDs, with `0` duplicate IDs. Each lifecycle has one serialization and one SHA-256 call; same-mutation-epoch duplicate calls are `0`.
`serialize_without_hash` calls/bytes: `9,908` / `1,345,246,987`; SHA-256 calls: `9,908`; `canonical_serialize` calls in THROUGHPUT: `0`.
Canonical section byte sum exact: `True`. Semantic trace/observation equivalence against the frozen M4.3.3 Release traces: `PASS`.
Focused shape/no-shape fixture equivalence (six visible, hidden, paired-world, perspective, and terminal fixtures): `PASS`; timing from that check is diagnostic overhead evidence only.

## Non-overlapping serialization phases

| Phase | Calls | Total µs | Mean µs/call | % of serialize time |
|---|---:|---:|---:|---:|
| `preparation_copy` | 9,908 | 59,822 | 6.037747 | 0.267876% |
| `sorting` | 9,908 | 71,209 | 7.187021 | 0.318865% |
| `rendering` | 9,908 | 22,061,367 | 2,226.621619 | 98.788072% |
| `escaping` | 6,651,912 | 54,807 | 0.008239 | 0.245419% |
| `final_extraction` | 9,908 | 84,810 | 8.559750 | 0.379769% |

Total serializer time: `22,332,015` µs; phase sum: `22,332,015` µs; exact non-overlap reconciliation: `True`. Rendering is the largest measured residual partition, not an independently isolated timer; it includes remaining stream formatting, primitive formatting, output construction, and probe overhead after copy, sorting, escaping, and final extraction.

## Exact canonical byte composition

| Top-level section | Bytes | % of canonical bytes |
|---|---:|---:|
| `schema_basic_header` | 1,120,587 | 0.083300% |
| `globals` | 1,828,064 | 0.135891% |
| `zones` | 24,777,032 | 1.841820% |
| `entities` | 367,534,943 | 27.321001% |
| `relationships` | 188,252 | 0.013994% |
| `chain` | 343,984 | 0.025570% |
| `visible_events` | 940,315,868 | 69.899125% |
| `decision_context` | 3,069,765 | 0.228193% |
| `match_context` | 6,068,492 | 0.451106% |

Section total: `1,345,246,987` bytes; exact match: `True`. Section spans include their emitted key/value delimiters under the documented contiguous-span policy.

## Observation-size distribution

| Statistic | Canonical bytes |
|---|---:|
| minimum | 21,124 |
| mean | 135,773.817824 |
| median | 135,488 |
| p95 | 239,608 |
| p99 | 249,174 |
| maximum | 254,096 |

Correlations with decision index, engine step, entity count, visible-event count, and chain length are in the JSON report. They are descriptive and do not establish causation.

## Visible-event growth

Cumulative history observed: `True`. Workload first observation: `11` events / `2,999` bytes; median: `340` / `94,609`; p95: `634` / `177,017`; final: `658` / `183,721`; maximum: `675` / `188,363`.
Serialized event instances: `3,362,505`; unique identities observed through the per-job diagnostic proxy: `21,292`; repetition-factor lower bound: `157.923398`. The serializer therefore re-emits historical event identities in later observations; event semantics were not changed.
The canonical simulation creates one ObservationSession per perspective and does not call `clear()`; `ingest()` assigns monotonic event indices. The uniqueness count is a per-job diagnostic proxy, not a durable/canonical event ID. Tuple collisions can undercount unique events, so the reported repetition factor is a lower bound; the `clear()` reset caveat is retained in the JSON evidence.

## Entity serialization

Entities serialized: `526,004`; entity bytes: `366,880,135`; mean bytes/entity: `697.485447`. Printed property bytes: `124,698,896`; current property bytes: `118,180,853`; combined printed/current fraction of entity bytes: `66.201390`%.
Counters: `0` instances / `2,104,016` bytes. Link markers: `385,352` instances / `5,618,880` bytes.

## Static match-context repetition

Own deck bytes: `3,997,720`; opponent deck bytes: `455,768`; other immutable match-context bytes: `1,436,660`. Total: `5,890,148` bytes / `0.437849`% of canonical output.

## Copy, sorting, and formatting

Copy time: `59,822` µs (0.267876%); sorting time: `71,209` µs (0.318865%). Copy/sort phase totals reconcile: `True`.
`json_escape` calls: `6,651,912`; input bytes: `75,663,795`; escaped output bytes: `88,967,619`; numeric values: `23,762,126`; booleans: `1,825,712`; nulls: `40,138,903`. Allocator-growth counts: `NOT_MEASURED` (no global allocation hook was added).

Per-copy-kind and per-sort-kind calls, elements, approximate copied bytes, and timing are recorded in the JSON report. Approximate copy bytes describe copied object/vector storage; canonical emitted bytes are measured separately.

## Runtime and instrumentation check

Shape workload worker-local runtime: `34,558,929` µs; throughput: `0.462977` games/s.
Against the same-head Release control without shape instrumentation, the shape worker changed canonical-serialization timing by `-15.311843`% and worker-local runtime by `-17.365555`%. This is a measured workload-level instrumentation signal, not a speedup claim and not an isolated overhead benchmark.

## Candidate classification and recommendation

Observed classifications: `RENDERING_DOMINANT`.

| Rank | Candidate | Measured runtime fraction | Emitted-byte fraction | Complexity |
|---:|---|---:|---:|---|
| 1 | `rendering_output_stream` | 98.788072% | 100.000000% | HIGH |
| 2 | `copy_and_sort_preparation` | 0.586741% | NOT_DIRECTLY_MEASURED | MEDIUM |
| 3 | `json_escaping` | 0.245419% | NOT_DIRECTLY_MEASURED | MEDIUM |
| 4 | `visible_event_history_growth` | NOT_DIRECTLY_MEASURED | 69.899125% | HIGH |
| 5 | `entity_property_representation` | NOT_DIRECTLY_MEASURED | 27.321001% | MEDIUM |
| 6 | `static_match_context_repetition` | NOT_DIRECTLY_MEASURED | 0.451106% | MEDIUM |

**First future experiment:** `rendering_output_stream` — Isolated A/B output-construction experiment targeting the rendering residual, with a reserve-backed builder evaluated only against exact byte/hash/privacy equivalence.

The candidate details in the JSON report include measured evidence, suspected redundant work, semantic/privacy risk, required equivalence tests, and affected buckets. No speedup estimate is made because this audit does not measure a candidate implementation.

## Gates

| Gate | Status | Verification evidence |
|---|---|---|
| `semantic_trace_equivalence` | `PASS` | derived from frozen-reference trace, observation-hash, gameplay-hash, and step equality |
| `focused_shape_equivalence` | `PASS` | derived from six-fixture shape/no-shape byte/hash test marker |
| `measured_run_binary_identity` | `PASS` | derived from run-time worker SHA-256 and source identity binding |
| `canonical_section_byte_integrity` | `PASS` | derived from exact emitted section-span sum |
| `residual_timing_integrity` | `PASS` | derived from explicit per-record residual-underflow flags and zero aggregate underflows |
| `shape_record_completeness` | `PASS` | derived from sidecar completeness and shape/lifecycle key join |
| `ctests` | `PASS` | exit=0; command=`ctest --test-dir build/m4-3-4-shape --output-on-failure`; stdout_sha256=`664796a1a27fdf8c3d62ee50e9d6da6ad185402d3b72a9da73612c10fd055fda`; stderr_sha256=`e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `repository_python_tests` | `PASS` | exit=0; command=`C:\Users\chris\AppData\Local\Python\pythoncore-3.14-64\python.exe -m unittest discover -s tests/python -v`; stdout_sha256=`e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`; stderr_sha256=`487db73442a4563479f1226f08555420823807f0c3dd8b4208574ee715770741` |
| `m3_python_tests` | `PASS` | exit=0; command=`C:\Users\chris\AppData\Local\Python\pythoncore-3.14-64\python.exe -m unittest discover -s tests/m3 -v`; stdout_sha256=`e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`; stderr_sha256=`23e934ddf57946a760b9bc3a0fe6105da5d7204f2f484852d8c4855c7141e1db` |
| `m4_python_tests` | `PASS` | exit=0; command=`C:\Users\chris\AppData\Local\Python\pythoncore-3.14-64\python.exe -m unittest discover -s tests/m4 -v`; stdout_sha256=`e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`; stderr_sha256=`5d41a9c0954eee1d4be60ae69f9fe2a21c65993c5fe08d94b9cf35cbde8a6616` |
| `privacy_tests` | `PASS` | exit=0; command=`ctest --test-dir build/m4-3-4-shape --output-on-failure -R ^(privacy_projection_test|continuation_privacy_test|m3_real_deck_privacy_test)$`; stdout_sha256=`67597fedd7b2505d7cd25d16db663107dd21ee6a9548044d0272ef4cd806abef`; stderr_sha256=`e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `candidate_observation_consistency` | `PASS` | exit=0; command=`ctest --test-dir build/m4-3-4-shape --output-on-failure -R ^(observation_builder_test|m4_worker_integration_test)$`; stdout_sha256=`69d06fd1132db1dabb31a161cb186a595d95945a5f44befb25912c7c995b8890`; stderr_sha256=`e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `worker_count_semantic_gate` | `PASS` | exit=0; command=`C:\Users\chris\AppData\Local\Python\pythoncore-3.14-64\python.exe -m unittest tests.m4.test_worker_integration.NativeWorkerIntegrationTests.test_worker_counts_preserve_semantics_and_trace_hashes -v`; stdout_sha256=`e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`; stderr_sha256=`83673a2041b88471c4f346e6e69b99c5f77bd019fa07fc0767b190fe90552490` |
| `gate_evidence_complete` | `PASS` | derived from non-empty evidence for every externally declared PASS gate |

The audit stops here. No optimization is authorized by this characterization task.
