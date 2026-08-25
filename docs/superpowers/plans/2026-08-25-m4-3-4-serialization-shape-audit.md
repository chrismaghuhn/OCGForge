# M4.3.4 Serialization Shape, Copy, and Growth Audit

## Goal

Characterize the Release `serialize_without_hash()` path for the exact M4.3.3
workload without changing canonical bytes, hashes, privacy, observation
semantics, or production behavior. The work ends with the requested Markdown
and JSON audit artifacts and does not implement an optimization.

## Frozen checkpoint

- Start from commit `cf16e9b` (`perf: characterize optimized M4 build`).
- Verify `git diff --check`, `git status --short`, and the checkpoint diff stat
  before adding M4.3.4 files.
- Keep the M4.3.3 Release worker/reference artifacts immutable and write new
  outputs under a separate `artifacts/m4/m4-3-4/` directory.

## Task 1: Define an opt-in shape-audit data path

Files:

- `include/ygo/observation/performance_audit.hpp`
- `src/observation/serialization.cpp`
- `tools/ygo_m4_worker/json_protocol.hpp`
- `tools/ygo_m4_worker/json_protocol.cpp`
- `tools/ygo_m4_worker/main.cpp`
- `include/ygo/simulation/simulation_contract.hpp`

Add `YGO_M4_SERIALIZATION_SHAPE_AUDIT` as a build-only diagnostic define,
always alongside the existing performance-audit define. Extend the existing
per-worker audit snapshot with a shape snapshot that contains aggregate
serialization phases and one compact record per finalized observation.

The record must retain at least:

- lifecycle/observation identity, canonical byte count, and all top-level
  section byte spans;
- decision index, engine step index, entity count, visible-event count, and
  chain length;
- visible-event bytes/count and entity/property/counter/link-marker counts and
  bytes;
- copy/sort counters and copy-byte estimates needed to reconcile the phase
  totals.

The sidecar uses a new prefix and schema, for example
`M4_SERIALIZATION_SHAPE`, so the existing M4 performance-audit parser and
contract remain unchanged. Sidecar serialization is diagnostic only and is
emitted after the existing sidecars.

## Task 2: Instrument serializer phases without changing output

File:

- `src/observation/serialization.cpp`

Pass an explicit shape collector through serializer helpers under the new
define. Do not use a global allocation hook, global mutable audit flag, or
any production serializer redesign.

Measure the following non-overlapping accounting:

- preparation copies, including top-level vectors, nested targets/markers/
  counters/references, and match-context decks;
- sorting, separately for each required vector family;
- JSON escaping calls, input bytes, output bytes, and elapsed time;
- final `ostringstream::str()` extraction elapsed time;
- rendering as the residual of total serializer time minus the measured copy,
  sort, escape, and extraction intervals. Clamp/report any clock-resolution
  residual explicitly rather than double-counting nested intervals.

Use stream positions around the existing writes to measure exact emitted
section spans. Assign each delimiter/key span to one documented section so
the nine section totals reproduce the complete canonical-without-hash length.
Do not build separate section strings or alter field order.

Instrument primitive formatting counts where practical (numeric, boolean, and
null writes) without changing the expressions that write their bytes.
Record nested entity/property/counter/link-marker spans and match-context
deck spans from the same output stream.

Record a diagnostic visible-event identity using the existing event index,
engine-step index, and kind fields. Use it only for per-job unique-event and
repetition counts; never include it in canonical output or observation hashes.

## Task 3: Add focused semantic and overhead checks

Files:

- `tests/observation/observation_builder_test.cpp` or a dedicated observation
  serialization audit test target
- `tools/m4/compare_serialization_shape.py`

Add a focused check that shape instrumentation produces byte-identical
canonical-without-hash output and identical SHA-256 output for representative
visible, hidden, paired-world, and terminal fixtures. Add a bounded timing
comparison of the same fixture through the existing serializer overload with
and without shape collection; report it as instrumentation overhead evidence,
not as a performance result.

Create a runner that executes the exact Release conformance workload, compares
all per-job results and persisted traces against the M4.3.3 Release reference,
and records observation-hash/trace/gameplay equivalence before the throughput
audit. It must fail closed on missing sidecars, mismatched job identities,
changed counts, changed hashes, or semantic/privacy divergence.

## Task 4: Build and run the controlled Release audit

Use a separate build directory, e.g. `build/m4-3-4-release`, configured from
`release-windows-zig` with the ordinary Release flags plus only the diagnostic
defines required by M4.3.3 and M4.3.4. Do not add LTO, PGO, architecture
tuning, SIMD, fast-math, or serializer/hash changes.

Run:

- exact 16-game Release conformance semantic comparison;
- exact one-worker 16-game FULL/THROUGHPUT workload, seed `20260815`, max
  steps `2200`, trace persistence off;
- focused fixture overhead check;
- required existing CTest, Python M3/M4, privacy, and candidate/observation
  consistency gates.

Parse the shape sidecar into an aggregate JSON artifact containing exact
totals, means, percentages, distributions, correlations, event repetition,
entity composition, static match-context repetition, copy/sort timing, and
instrumentation overhead evidence. Verify section-byte sums and total bytes
for every record before aggregating.

## Task 5: Produce the M4.3.4 report

Files:

- `tools/m4/report_serialization_shape.py`
- `docs/m4/M4_3_4_SERIALIZATION_SHAPE_AUDIT.md`
- `docs/m4/m4_3_4_serialization_shape_audit.json`

The report must include:

- exact workload/build/reference identity;
- semantic and privacy gate results;
- phase tables with calls, total microseconds, means, and percentages;
- byte composition whose sum reconciles to all canonical bytes;
- size distribution and correlations without claiming causation;
- visible-event cumulative growth and repetition factor;
- entity printed/current/counter/link-marker composition;
- static match-context byte repetition;
- copy/sort boundaries and `ostringstream`/escape characterization;
- measured instrumentation overhead and any `NOT_MEASURED` fields;
- candidate ranking by Release time, bytes, semantic/privacy risk, and
  implementation complexity;
- one best first future experiment, explicitly deferred beyond M4.3.4.

Use the requested classifications only when supported by measured Release
evidence. Do not estimate speedup without a measured fraction/Amdahl bound.

## Task 6: Independent review and final verification

Keep the read-only review subagent active until it reports. After integration,
request a final read-only review of the M4.3.4 diff and artifacts. Run
`git diff --check`, inspect the final status and diff stat, rerun the artifact
schema/contract checks, and confirm no optimization or M5 files were changed.
Close completed review subagents only after their final messages are received.

## Stop condition

Stop after the audit artifacts and evidence are complete. The final response
must state `M4.3.4 SERIALIZATION CHARACTERIZATION PASS` or the exact failure
status and identify the single best first future optimization experiment.
