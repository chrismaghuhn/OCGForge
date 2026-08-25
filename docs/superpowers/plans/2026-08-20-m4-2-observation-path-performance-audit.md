# M4.2 Observation-Path Performance Audit Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add measurement-only instrumentation that explains the M4 observation-path cost, corrects coordinator timing interpretation, and produces the requested audit artifacts without changing gameplay, privacy, observation semantics, worker architecture, or throughput behavior.

> Execution status: COMPLETE. The canonical 16-game FULL/off audit, report review, Default CTest, and audit-build CTest all passed. The pre-existing M4 acceptance manifest remains unchanged and its historical run-identity drift is reported separately.

**Architecture:** Native instrumentation records non-overlapping observation sub-buckets and operation counters inside the existing `PlayerObservation` construction boundary, but only in an opt-in `YGO_M4_PERFORMANCE_AUDIT` build. The default M4 worker and JSONL contract remain byte/schema compatible with the accepted M4 baseline; the audit worker emits a structured stderr sidecar consumed by new M4.2 code. The audit coordinator records CPU-only coordinator sub-buckets separately from wall-clock worker-compute wait, and a report layer validates and renders the controlled audit evidence.

**Tech Stack:** C++17, CMake/Ninja, pinned native OCG core, Python 3 standard library, `unittest`, JSON Schema, existing persistent M4 worker protocol.

---

### Task 1: Define the isolated M4.2 telemetry contract and failing tests

**Files:**
- Create: `tools/m4/performance_audit_contract.py`
- Create: `tests/m4/test_performance_audit_contract.py`

- [ ] **Step 1: Write contract tests for the new audit fields**

Add tests that require the exact observation bucket names, coordinator bucket names, counter names, nonnegative integer values, and the rule that nested observation timings sum to no more than the outer observation timing. Add a test that a result without audit telemetry remains valid for the existing throughput path, while an audit report must reject missing telemetry.

- [ ] **Step 2: Run the focused contract tests and confirm the red state**

Run:

```powershell
python -B -m unittest tests.m4.test_performance_audit_contract -v
```

Expected: the new field and audit-validation tests fail because the M4.2 fields do not yet exist.

- [ ] **Step 3: Add the exact telemetry shape without changing semantics**

Use these stable names in the audit sidecar/report contract:

```text
observation_timing_us:
  observation_query_field
  observation_query_location
  observation_query_individual
  observation_query_decode
  observation_zone_projection
  observation_entity_projection
  observation_relationship_projection
  observation_visibility_privacy
  observation_candidate_consistency
  observation_canonical_serialization
  observation_hash
  observation_other

observation_counters:
  observations
  query_field_calls
  query_location_calls
  query_individual_calls
  entities_projected
  identity_known_entities
  redacted_entities
  static_card_data_lookups
  current_property_projections
  relationship_objects
  allocation_copy_events
  script_loads

coordinator_timing_us:
  worker_compute_wait
  pipe_read_write_cpu
  json_encode_decode_cpu
  dispatch_queue_overhead
  other
```

Keep `timing_us.observation` and `simulation_elapsed_us` as the existing primary worker timing domain. The audit contract is separate from `tools/m4/worker_protocol_contract.py` and `docs/m4/m4_benchmark_schema.json`; the accepted M4 baseline files must remain byte-identical. Missing telemetry is compatible only for ordinary non-audit inputs; `validate_audit_report` must require it.

- [ ] **Step 4: Run the focused contract tests and confirm they pass**

Run the same `unittest` command. Expected: all contract tests pass.

- [ ] **Step 5: Commit the contract slice**

```powershell
git add tools/m4/performance_audit_contract.py tests/m4/test_performance_audit_contract.py
git commit -m "feat: M4.2 define observation audit telemetry"
```

### Task 2: Instrument the opt-in native observation path with nested timing and counters

**Files:**
- Create: `include/ygo/observation/performance_audit.hpp`
- Modify: `include/ygo/simulation/simulation_contract.hpp` under `#ifdef YGO_M4_PERFORMANCE_AUDIT` only
- Modify: `include/ygo/observation/observation_builder.hpp`
- Modify: `include/ygo/observation/decision_integration.hpp`
- Modify: `include/ygo/observation/serialization.hpp`
- Modify: `src/observation/observation_builder.cpp`
- Modify: `src/observation/zone_projection.cpp`
- Modify: `src/observation/card_projection.cpp`
- Modify: `src/observation/decision_integration.cpp`
- Modify: `src/observation/serialization.cpp`
- Modify: `src/simulation/canonical_simulation.cpp`
- Modify: `tools/ygo_m4_worker/json_protocol.cpp`
- Modify: `tools/ygo_m4_worker/main.cpp`
- Modify: `tests/m4/test_performance_audit_contract.py`
- Modify: `tests/observation/observation_builder_test.cpp`

- [ ] **Step 1: Add a scoped native audit collector**

Create a value-owned collector passed through `ObservationBuildConfig` only for instrumentation. Each scope records elapsed steady-clock microseconds into one exclusive bucket and increments counters at the authoritative call site. The collector must be inert when absent, and nested scopes must subtract child time from the parent `observation_other` rather than double-counting.

- [ ] **Step 2: Instrument query, decode, projection, privacy, candidate, serialization, and hash boundaries**

Record `CoreHost::query_field`, `CoreHost::query_location`, and `CoreHost::query` call counts and elapsed time; record query decoding separately from native API calls. Record zone traversal, entity/card projection, relationship construction, visibility/privacy decisions, candidate consistency, canonical observation serialization, and observation hash separately. Count immutable printed-card lookups, current-property projections, known/redacted entities, and relationship objects without changing any returned values.

- [ ] **Step 3: Keep script-load measurement in setup scope**

Measure setup/script loading in a separate worker-level setup bucket using the existing `CoreHostMetrics.script_loads` and `script_reader_requests`. Do not cache, deduplicate, preload, or otherwise alter the ScriptStore behavior.

- [ ] **Step 4: Export native telemetry in an audit-only stderr sidecar**

Serialize one strict `M4_PERFORMANCE_AUDIT` JSON line per completed audit job to stderr from `tools/ygo_m4_worker/main.cpp`. Do not add audit keys to stdout JSONL, the default build, the existing worker protocol, or the M4 benchmark schema. Preserve result semantics, canonical hashes, and privacy projection exactly.

- [ ] **Step 5: Run native observation and M4 focused tests**

Run:

```powershell
cmake --build --preset dev-windows-zig --parallel
ctest --preset dev-windows-zig --output-on-failure -R "observation|m4"
python -B -m unittest tests.m4.test_performance_audit_contract tests.m4.test_benchmark_integrity -v
```

Expected: the default build and selected tests pass; the default worker remains baseline-compatible. Configure a separate audit build with `YGO_M4_PERFORMANCE_AUDIT` before collecting audit samples; no optimization or semantic test is removed.

### Task 3: Correct coordinator metric interpretation without charging blocking waits to IPC CPU

**Files:**
- Create: `tools/m4/performance_audit.py`
- Modify: `tests/m4/test_performance_audit_contract.py`
- Modify: `tests/m4/test_worker_integration.py`

- [ ] **Step 1: Add deterministic coordinator timing tests**

Use a fake worker and injected clock/process-time seams to prove that time blocked in `_next_event()` is classified as `worker_compute_wait`, while JSON encode/decode, pipe write/read, and queue dispatch work are classified into their CPU buckets. Assert that `coordinator_ipc` is no longer presented as a CPU bottleneck.

- [ ] **Step 2: Instrument coordinator CPU and wait domains**

Measure wall-clock worker wait separately from process CPU in the audit-only coordinator wrapper. The coordinator CPU total is the sum of `pipe_read_write_cpu`, `json_encode_decode_cpu`, `dispatch_queue_overhead`, and `other`; blocking queue/pipe waits never enter that sum. Preserve the existing `tools/m4/benchmark.py` receipt-time domain unchanged.

- [ ] **Step 3: Validate and aggregate the new domains**

Reject negative, missing, or inconsistent coordinator audit fields in audit mode. Render CPU percentages using only the CPU-domain sum and report `worker_compute_wait` as a separate wall-time observation.

- [ ] **Step 4: Run coordinator-focused tests**

```powershell
python -B -m unittest tests.m4.test_performance_audit_contract tests.m4.test_worker_integration tests.m4.test_benchmark_integrity -v
```

Expected: all tests pass and no result-count, lifecycle, or failure-isolation contract regresses.

### Task 4: Build the controlled M4.2 audit report and machine-readable artifact

**Files:**
- Modify: `tools/m4/performance_audit.py`
- Create: `docs/m4/m4_performance_audit_schema.json`
- Create: `docs/m4/m4_performance_audit.json`
- Create: `docs/m4/M4_PERFORMANCE_AUDIT.md`
- Modify: `tests/m4/test_performance_audit_contract.py`

- [ ] **Step 1: Add report-builder tests before report code**

Test that the audit builder accepts only complete full-observation and observation-off diagnostic reports with matching canonical identities, validates all semantic/error gates, preserves counts, and refuses to label diagnostic observation-off games as training throughput.

- [ ] **Step 2: Implement report aggregation**

Read one deterministic one-worker full-observation sample and its observation-off diagnostic cross-check. Aggregate total time, call counts, mean time/call, zone/entity/visibility breakdowns, script setup timing, coordinator CPU/wait domains, and query call-site classifications. Compute optimization candidates in the required order: measured runtime fraction, semantic risk, implementation complexity. Do not calculate speedups unless an observed fraction or Amdahl bound supports it.

- [ ] **Step 3: Write the exact output documents**

The JSON artifact must include canonical identities, workload/sample metadata, all bucket totals and means, query call-site table, entity/zone audit, observation-off comparison, candidate ranking, and explicit `optimization_implemented: false`. The Markdown document must state the measured evidence, unknowns, semantic/privacy constraints, and the first M4.3 experiment recommendation.

- [ ] **Step 4: Validate report mutation and privacy gates**

Add tests that mutate a bucket, query count, identity, privacy classification, or observation-off label and confirm the builder returns `M4.2 PERFORMANCE AUDIT PENDING` rather than accepting the artifact.

### Task 5: Run the representative canonical workload and generate evidence

**Files:**
- Generate: `artifacts/m4/audit/full-observation.json`
- Generate: `artifacts/m4/audit/observation-off-diagnostic.json`
- Generate: `docs/m4/m4_performance_audit.json`
- Generate: `docs/m4/M4_PERFORMANCE_AUDIT.md`

- [ ] **Step 1: Build and run the full-observation sample**

Use the existing canonical worker executable, rules bundle, locked decks, MR5 identity, one worker, balanced seat/start assignments, a deterministic 16-game sample, `--instrument`, and `observation_mode=full`. Record the exact command and hashes in the artifact.

- [ ] **Step 2: Run the observation-off diagnostic cross-check**

Repeat the same jobs and identity with `observation_mode=off_diagnostic`. Treat the run as diagnostic only; require semantic, terminal, identity, and error gates, but never use its games/second as primary throughput.

- [ ] **Step 3: Generate and validate both output documents**

Run the report builder, validate JSON syntax/schema, verify all canonical fingerprints and query/counter relationships, and confirm `optimization_implemented` is false.

### Task 6: Final regression, independent review, and stop

**Files:**
- Modify only if review requires a measurement-only correction: files listed in Tasks 1–4.

- [ ] **Step 1: Run the full applicable verification set**

```powershell
python -B -m unittest discover -s tests/m4 -v
cmake --build --preset dev-windows-zig --parallel
ctest --preset dev-windows-zig --output-on-failure
git diff --check
```

Also run the existing canonical M3 semantic gate if the audit report consumes its fingerprints. Expected: all existing gates remain green and no worker/process remains active after the commands finish.

- [ ] **Step 2: Dispatch a spec-compliance reviewer**

Review every M4.2 requirement against the diff and the two output artifacts. The reviewer must explicitly confirm that no optimization, cache, batching, thread, core/CardScripts, privacy, semantic, candidate, trace-hash, worker-architecture, or M5 change was introduced.

- [ ] **Step 3: Dispatch a code-quality reviewer after spec approval**

Review timing-domain correctness, double-counting, overflow, deterministic serialization, fail-closed validation, and test quality. Fix and re-review every blocking or important finding.

- [ ] **Step 4: Stop after the audit**

Do not implement the recommended optimization, do not begin M4.3, do not begin M5, and do not create or update a PR unless separately requested.
