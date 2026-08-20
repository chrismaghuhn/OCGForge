# M4 Observation Serialization Lifecycle Audit Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Characterize, by diagnostic observation lifecycle ID, every hash-input serialization and canonical serialization in the M4 observation path, and only implement the smallest per-observation reuse if duplicate materialization is proven.

**Architecture:** Keep the existing M4.2 performance sidecar and contract stable. Add audit-only lifecycle records to `PerformanceAuditCollector`, with a mutation epoch so repeated serialization in one unchanged finalized state is distinguishable from the intentional pre-/post-context lifecycle. Emit the records through a separate diagnostic sidecar. Any reuse change must be opt-in to the existing observation lifetime, preserve the current canonical bytes and SHA-256 contract, and be gated by direct byte/hash/privacy tests before a benchmark.

**Tech Stack:** C++20/CMake/Ninja, native OCGForge observation/worker code, SHA-256 canonical JSON serializer, Windows PowerShell, Python `unittest` M4 audit harness.

---

### Task 1: Establish the red-capable lifecycle characterization seam

**Files:**
- Modify: `tests/observation/observation_builder_test.cpp`
- Test: `build/windows-zig/observation_builder_test.exe` and the audit build equivalent

- [ ] **Step 1: Add an audit-only lifecycle assertion first.**

Under `YGO_M4_PERFORMANCE_AUDIT`, create a `PerformanceAuditCollector`, wrap one real observation in `observation_scope()`, call the existing audited `observation_hash()` and `canonical_serialize()` APIs in controlled order, and assert that a lifecycle record exposes a nonzero lifecycle ID plus separate counts/byte totals for `serialize_without_hash`, SHA-256, and `canonical_serialize`.

- [ ] **Step 2: Run the focused audit test before implementing the telemetry.**

Run:

```powershell
cmake --build C:\yogiohML-m4-3-1\build\m4-audit --target observation_builder_test --parallel 4
```

Expected result: RED because the lifecycle record/API does not exist yet. This proves the test exercises the requested seam rather than existing aggregate timing.

- [ ] **Step 3: Keep the characterization assertions contract-focused.**

The test must assert exact final bytes and hash equality for the existing serializer, but must not lock to an intermediate hash. It must also assert that a second serialization in the same explicit lifecycle and mutation epoch is reported as same-state duplicate materialization, while a serialization after `record_observation_mutation()` is not classified as a duplicate.

### Task 2: Add audit-only lifecycle IDs and serialization telemetry

**Files:**
- Modify: `include/ygo/observation/performance_audit.hpp`
- Modify: `src/observation/serialization.cpp`
- Modify: `src/observation/decision_integration.cpp`
- Modify: `include/ygo/observation/serialization.hpp`

- [ ] **Step 1: Add per-lifecycle records without changing normal builds.**

Add an audit-only record containing `lifecycle_id`, current mutation epoch, `serialize_without_hash` call count and bytes, SHA-256 call count, `canonical_serialize` call count and bytes, and same-epoch duplicate serialization count. Assign monotonically increasing IDs from `ObservationScope`; restore the previous active lifecycle on scope destruction.

- [ ] **Step 2: Record serializer events at the actual boundaries.**

Record the output size immediately after `serialize_without_hash()` returns, record SHA-256 invocations in both audited `observation_hash()` and audited `canonical_serialize()`, and record final canonical output size for `canonical_serialize()`. Do not count the existing aggregate timing bucket as a proxy for these call types.

- [ ] **Step 3: Record mutation epochs only at known observation mutation boundaries.**

In the audit build, `attach_decision_context()` increments the active lifecycle mutation epoch before its context/reference mutation and final hash. No gameplay, privacy, or observation fields change because of this counter.

- [ ] **Step 4: Add a separate diagnostic sidecar serializer.**

Emit `M4_SERIALIZATION_LIFECYCLE` with a versioned schema, job ID, lifecycle records, aggregate totals, and a `canonical_serialize_consumed` boolean derived from actual audited calls. Leave `M4_PERFORMANCE_AUDIT` and its strict M4.2 parser unchanged.

- [ ] **Step 5: Run the focused audit test to GREEN.**

Run the same audit target and executable from Task 1. Expected result: PASS with a nonzero lifecycle ID, exact byte totals, and correct duplicate-vs-mutation classification.

### Task 3: Characterize THROUGHPUT and direct canonical serialization separately

**Files:**
- Create: `tools/m4/serialization_lifecycle_audit.py` if a reusable parser/runner is needed
- Create: `docs/m4/M4_3_2_OBSERVATION_SERIALIZATION_LIFECYCLE.md`
- Create: `docs/m4/m4_3_2_observation_serialization_lifecycle.json`

- [ ] **Step 1: Run a representative one-worker FULL/THROUGHPUT audit.**

Use the locked Swordsoul Tenyi ML v1 vs Salamangreat ML v1 workload, master seed `20260815`, 16 games, max steps `2200`, one worker, no trace persistence, and the instrumented audit worker. Parse the separate lifecycle sidecar by job ID and lifecycle ID.

- [ ] **Step 2: Report the requested counts separately.**

Report totals and per-lifecycle distributions for:

```text
serialize_without_hash calls
serialize_without_hash bytes produced
SHA-256 calls
canonical_serialize calls
canonical_serialize bytes produced
canonical bytes consumed in THROUGHPUT
same finalized observation serialized more than once without mutation
```

Also report terminal versus decision lifecycles and the exact lifecycle IDs for any duplicate classification.

- [ ] **Step 3: Run a direct consumer characterization.**

Use the real observation fixture to call `observation_hash()` and `canonical_serialize()` in one unchanged lifecycle, then compare the lifecycle record with the THROUGHPUT records. This distinguishes “the API can duplicate materialization when both consumers are called” from “THROUGHPUT actually consumes canonical bytes.”

- [ ] **Step 4: Decide the optimization gate from evidence.**

Duplicate materialization is proven only when the same lifecycle and mutation epoch contain more than one `serialize_without_hash()` materialization whose bytes are equal, or when both consumers demonstrably request the same unchanged observation bytes. If THROUGHPUT has zero `canonical_serialize()` calls and one hash-input serialization per lifecycle, stop the optimization path and record that no throughput duplicate was proven.

### Task 4: Conditional smallest reuse experiment

**Files:**
- Modify only the serializer/observation lifetime files identified by Task 3
- Modify: `tests/observation/observation_builder_test.cpp` or a focused serialization test
- Modify: `docs/m4/M4_3_2_OBSERVATION_SERIALIZATION_LIFECYCLE.md`

- [ ] **Step 1: Design the smallest lifetime-local buffer only if Task 3 proves duplication.**

Use one immutable `canonical_without_hash` byte buffer owned by the single `PlayerObservation` lifetime or an explicit finalization result. Do not cache across observations, change `PlayerObservation` field semantics, stream SHA-256, add a serializer, change JSON format, or alter privacy.

- [ ] **Step 2: Add failing byte/hash/privacy equivalence tests before production code.**

Cover visible decision, continuation, both perspectives, hidden-information, paired-world privacy, terminal observations, and the exact SHA-256-of-current-canonical-bytes invariant. Watch the new reuse assertion fail before adding reuse code.

- [ ] **Step 3: Implement only the minimal reuse seam.**

Reuse the immutable bytes for the two consumers within one finalized observation lifetime, invalidate them on any semantic mutation, and retain Immediate/Deferred behavior and the existing default contract.

- [ ] **Step 4: Run all equivalence and semantic gates before benchmarking.**

Run the focused native tests, all CTests, repository Python tests, M3 Python tests, M4 tests, privacy/candidate tests, M3 determinism, and the canonical 16-game integrity workload. Reject the experiment on any byte, hash, privacy, gameplay, trace, terminal, action, rules, patchset, deck, or lifecycle divergence.

- [ ] **Step 5: Benchmark only after the gates pass.**

If no duplicate is proven, do not benchmark an optimization. If reuse is proven and equivalence passes, use the exact M4.2 workload and report calls, bytes, timing, integrity counters, and measured speedup without an unsupported target.

### Task 5: Review and final verification

**Files:**
- Verify: all changed files and both M4.3.2 evidence artifacts

- [ ] **Step 1: Request an independent read-only spec/code review.**

Review lifecycle identity, mutation-epoch classification, THROUGHPUT versus direct-consumer separation, strict scope, and no forbidden optimization.

- [ ] **Step 2: Run `git diff --check` and verify no debug instrumentation remains outside the audit-only path.**

- [ ] **Step 3: Stop after the lifecycle audit and conditional experiment.**

Do not start M5, alter ocgcore/CardScripts/rules/decks, create a PR, or add unrelated optimization work.
