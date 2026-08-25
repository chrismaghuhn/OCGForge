# M4.3.5 Reserve-Backed Canonical Serialization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Measure one Release A/B replacement of the top-level canonical serializer buffer with a private reserve-backed output buffer without changing canonical bytes or semantics.

**Architecture:** Freeze M4.3.4 first. Keep the current serializer expressions, sort operations, escaping, and SHA-256 contract unchanged; generalize only internal writer parameters and select a local output wrapper by build definition. Add a future-prefixed performance sidecar object for reserve telemetry, compare control/experiment fixture and 16-game outputs before timing, then retain or remove only the experimental buffer according to measured evidence.

**Tech Stack:** C++17, CMake/Ninja, Zig Windows toolchain, `std::ostream`/private `std::streambuf`, native CTest, repository Python M4 benchmark/audit tools.

---

### Task 1: Record the frozen checkpoint and design artifacts

**Files:**
- Create: `docs/superpowers/specs/2026-08-25-m4-3-5-reserve-backed-serialization-design.md`
- Create: `docs/superpowers/plans/2026-08-25-m4-3-5-reserve-backed-serialization.md`
- Reference: `docs/m4/M4_3_4_SERIALIZATION_SHAPE_AUDIT.md`

- [x] **Step 1: Freeze M4.3.4 before experiment code**

  Starting state was checked with `git rev-parse HEAD`, `git status --short`,
  `git diff --check`, and `git diff --stat`. The completed M4.3.4 changes were
  committed separately as `76c1a028f1c99a8ed46b63da3c3b93b64cf3a0e8` with a
  clean worktree.

- [x] **Step 2: Write the design and implementation plan**

  The design fixes the private stream-buffer boundary, future-prefixed
  telemetry, exact-equivalence order, and Release A/B decision rule before
  implementation. The M4.3.4 shape-enabled worker is explicitly excluded from
  performance timing.

- [ ] **Step 3: Self-review the plan**

  Check that every user constraint maps to a task: no event-history/schema/hash
  change; no `to_chars`, JSON-escape rewrite, caching, exact-size prepass,
  ocgcore/M5 work; focused equivalence before timing; clean control/experiment
  Release builds; raw alternating repetitions; fresh gates; and a report that
  says ACCEPTED or REJECTED.

### Task 2: Add focused cross-variant equivalence and output-contract tests

**Files:**
- Create: `tests/observation/m4_3_5_serialization_fixture_test.cpp`
- Modify: `CMakeLists.txt`
- Create: `tools/m4/compare_reserve_serialization.py`

- [ ] **Step 1: Write the fixture test before production changes**

  Add a deterministic executable that constructs `PlayerObservation` values
  containing escaped schema/locator/decision strings, visible events, sorted
  zones/entities/relationships, chain targets, counters, link markers, both
  printed/current properties, hidden/redacted entities, terminal fields, and a
  relationship with `RelationshipKind::XyzMaterial`. For every named fixture,
  print only stable lengths and SHA-256 digests of
  `canonical_serialize_without_hash`, the observation hash, and
  `canonical_serialize`; assert that the observation hash equals SHA-256 of the
  exact no-hash bytes. Register it as `m4_3_5_serialization_fixture_test`.

- [ ] **Step 2: Run the new test in the frozen control build**

  Configure/build an ordinary Release control tree with
  `YGO_M4_PERFORMANCE_AUDIT` and no shape or reserve definition, then run the
  new CTest executable. Expected result: it builds and passes, establishing
  the control digest format before the experimental implementation exists.

- [ ] **Step 3: Add the comparison runner and make its first assertion fail**

  Add a Python runner that executes the fixture executable and the existing
  `observation_builder_test` from control and experiment paths, captures
  stdout/stderr/exit codes, and compares stable stdout byte-for-byte. It must
  reject missing executables, nonzero exits, output differences, and a missing
  SHA-256 contract line. Before the reserve binary exists, run it against a
  deliberately absent experimental path and confirm the runner fails for the
  expected missing-binary reason; keep that RED evidence in the run artifact,
  not in production source.

- [ ] **Step 4: Add the exact workload semantic-comparison path**

  In the same runner, use the existing deterministic job generator and
  `PersistentWorkerPool`/protocol helpers to run both variants in conformance
  mode for seed `20260815`, 16 games, one worker, max steps `2200`, FULL
  observations, and trace persistence on for semantic evidence. Compare job
  identity, terminal result, winner/reason, steps, decisions/actions, gameplay
  hash, final observation hashes, trace hash, and error counters. Re-run the
  fixed-deck privacy and candidate/observation CTest gates against both
  binaries; any mismatch exits before timing.

### Task 3: Implement the private reserve-backed output path

**Files:**
- Modify: `src/observation/serialization.cpp`
- Modify: `include/ygo/observation/performance_audit.hpp`
- Modify: `tools/ygo_m4_worker/json_protocol.cpp`
- Modify: `tools/ygo_m4_worker/json_protocol.hpp` only if the existing protocol declaration requires it

- [ ] **Step 1: Add the failing output-buffer behavior test**

  Extend the fixture executable with a reserve-metrics assertion that the
  experimental build emits one output-buffer record per serialized fixture,
  records a positive requested capacity and final size, and reports a
  nonnegative growth/unused-capacity relation. The control build must emit the
  same record with `mode=ostringstream`; before implementation the experimental
  build either lacks the record or fails the assertion, proving the test is
  checking the new behavior.

- [ ] **Step 2: Generalize only internal writer signatures**

  Change `output_offset`, optional writers, array writers, and serialization
  callbacks from `std::ostringstream&` to `std::ostream&`. Leave
  `json_escape_impl` as its existing temporary `std::ostringstream`; do not
  alter primitive formatting expressions or sort/copy order. Confirm that
  `tellp()` remains valid by making the experimental stream buffer report its
  logical output position.

- [ ] **Step 3: Add the control/experimental output wrapper**

  Implement a private `SerializationOutput` with this behavior:

  ```cpp
  class SerializationOutput {
  public:
      explicit SerializationOutput(const PlayerObservation& observation,
                                   PerformanceAuditCollector* audit);
      std::ostream& stream() noexcept;
      std::string finish();
  };
  ```

  The default branch owns `std::ostringstream`. The
  `YGO_M4_RESERVE_BACKED_SERIALIZATION` branch owns a `std::string` reserved
  from an inexpensive structure-only hint and a private `std::streambuf` whose
  `xsputn` appends byte ranges and whose `overflow` appends one byte. Implement
  `seekoff`/`seekpos` for the current output position and `sync` as a no-op.
  `finish()` returns the exact accumulated string after flushing the ostream.
  There is no global flag, previous-observation state, exact-size prepass, or
  public buffer type.

- [ ] **Step 4: Record reserve telemetry without changing the old contract**

  Add a performance-audit snapshot structure with `mode`, `calls`,
  `requested_capacity`, `final_bytes`, `growth_events`, and
  `unused_capacity`. Record one completed output per lifecycle and expose it
  from `serialize_performance_audit()` under the future-prefixed JSON key
  `future_m4_3_5_reserve_output`, which the existing sidecar parser accepts as
  forward-compatible. Control mode is `ostringstream` with zero reserve
  requests; experiment mode is `reserve_backed`. Do not count
  `json_escape_impl` temporaries as top-level output-buffer growth.

- [ ] **Step 5: Route only `serialize_without_hash()` through the wrapper**

  Replace the local top-level `std::ostringstream out` and final `out.str()`
  with the wrapper's `std::ostream&` and `finish()`. Keep every existing field,
  delimiter, sort, escape, and SHA-256 call unchanged. Preserve the M4.3.4
  shape probes and output offsets when that diagnostic macro is explicitly
  enabled, but do not use that binary for timing.

- [ ] **Step 6: Build and run the focused tests**

  Build both ordinary Release variants and run the fixture, existing
  observation-builder, privacy, decision, continuation, and XYZ tests. The
  comparison runner must now pass exact stdout/digest equivalence and the
  SHA-256 contract before any throughput benchmark is started.

### Task 4: Build ordinary Release A/B binaries and prove workload equivalence

**Files:**
- Modify: `tools/m4/compare_reserve_serialization.py`
- Create: `artifacts/m4/m4-3-5/` (ignored generated evidence only)

- [ ] **Step 1: Configure the two clean Release trees**

  Use the existing `release-windows-zig` toolchain identity and ordinary
  Release configuration (`-O3 -DNDEBUG`) in separate binary directories:

  ```powershell
  cmake --preset release-windows-zig -B build/m4-3-5-control -DCMAKE_CXX_FLAGS=-DYGO_M4_PERFORMANCE_AUDIT
  cmake --preset release-windows-zig -B build/m4-3-5-reserve -DCMAKE_CXX_FLAGS="-DYGO_M4_PERFORMANCE_AUDIT -DYGO_M4_RESERVE_BACKED_SERIALIZATION"
  cmake --build build/m4-3-5-control --config Release
  cmake --build build/m4-3-5-reserve --config Release
  ```

  Record compiler path/version, CMake generator/cache, relevant compile flags,
  worker paths, binary SHA-256, and the frozen M4.3.4 starting commit. Fail if
  either tree contains `YGO_M4_SERIALIZATION_SHAPE_AUDIT`, LTO, PGO, or
  architecture-specific flags.

- [ ] **Step 2: Run exact control-vs-experiment conformance**

  Run the runner with both `ygo_m4_worker.exe` paths and compare all 16 jobs
  before allowing a throughput run. Require identical operation counters,
  error counters, rules bundle/patchset/deck identity, gameplay and trace
  hashes, final observation hashes, terminal results, winners/reasons, steps,
  decisions/actions, and privacy/candidate gates.

- [ ] **Step 3: Capture sidecar structural baselines**

  Parse each FULL throughput sidecar with the existing strict parser and read
  the future reserve object. Require exactly 9,908 observations, identical
  serializer calls and canonical bytes between A and B, and reserve telemetry
  whose final-byte total matches the serializer byte total. Keep sidecar logs,
  stdout/stderr byte counts, and SHA-256s in the ignored artifact directory.

### Task 5: Run alternating clean Release A/B timing

**Files:**
- Modify: `tools/m4/compare_reserve_serialization.py`
- Create: `docs/m4/M4_3_5_RESERVE_BACKED_SERIALIZATION.md`
- Create: `docs/m4/m4_3_5_reserve_backed_serialization.json`

- [ ] **Step 1: Run the prescribed workload in alternating order**

  Execute `A1, B1, A2, B2, A3, B3`, each with seed `20260815`, 16 games,
  max steps `2200`, one worker, FULL observations, throughput mode, and trace
  persistence off. Use the same worker pool and timeout configuration for all
  repetitions. Do not invoke the M4.3.4 shape worker.

- [ ] **Step 2: Preserve every raw repetition**

  Store each run's worker-local simulation time, games/s, outer observation
  time, canonical serialization time, hash time, observation/canonical byte
  counts, all operation/error counters, reserve requested/final/growth/unused
  values, worker/binary identity, and raw stdout/stderr file metadata. Do not
  drop a run because it is inconvenient or noisy.

- [ ] **Step 3: Calculate medians, ranges, and the decision rule**

  Report all six raw values, A/B medians, min/max ranges, paired deltas, and
  measured speedups. Classify the experiment as material only when the median
  serializer time improves by at least 5%, worker-local time improves by at
  least 3%, and at least two of three pairs improve in both metrics. This is a
  measurement rule for this experiment, not a speedup extrapolation.

- [ ] **Step 4: Generate the required M4.3.5 report**

  Write the Markdown and JSON artifacts with starting/final heads, worktree
  status, build identities/flags/hashes, equivalence gates, exact raw tables,
  structural metrics, reserve-policy distribution, instrumentation status,
  acceptance decision, and explicit scope boundary. If the rule fails while
  equivalence passes, remove only the reserve production branch and preserve
  the evidence as `M4.3.5 REJECTED — NO MATERIAL BENEFIT`.

### Task 6: Run fresh regression gates and review the final diff

**Files:**
- Modify only if a review finding requires it: all M4.3.5 source/report files

- [ ] **Step 1: Run the full required verification**

  Run `git diff --check`, full CTest, repository Python tests, M3 Python tests,
  M4 Python tests, privacy, candidate/observation consistency, canonical
  fixed-deck regression, and deterministic worker semantic gates against the
  accepted/rejected final production tree. Record each command exit code and
  stdout/stderr artifact hash. Re-check rules bundle, patchset, deck hashes,
  and the absence of shape instrumentation from timing binaries.

- [ ] **Step 2: Dispatch spec-compliance review**

  Give a fresh read-only subagent the complete M4.3.5 requirements, final diff,
  report JSON, and gate evidence. It must check every forbidden-scope item,
  exact-equivalence evidence, clean-build identity, raw A/B measurements, and
  ACCEPTED/REJECTED decision. Let it finish and fix/re-review every finding.

- [ ] **Step 3: Dispatch code-quality review after spec review passes**

  Give a second fresh read-only subagent the final source diff and tests. It
  must inspect stream-buffer lifetime/seek behavior, exception/error state,
  reserve accounting, macro isolation, formatting preservation, and test
  maintainability. Let it finish and fix/re-review every finding.

- [ ] **Step 4: Final verification and stop**

  Re-run the exact affected build/tests after any review fix, verify the final
  worktree and commit identities, update the plan, and report only the measured
  M4.3.5 status. Do not begin visible-event-history, `to_chars`, hash, ocgcore,
  or M5 work.
