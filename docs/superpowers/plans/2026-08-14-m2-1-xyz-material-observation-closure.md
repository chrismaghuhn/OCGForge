# M2.1 Xyz Material Observation Closure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Determine and document whether the pinned public OCG API can identify individual Xyz overlay materials, implementing perspective-safe material entities only if the public contract proves that capability.

**Architecture:** Keep the existing M2 `PlayerObservation` and privacy boundary unchanged. First establish capability from the pinned public declarations and real engine query results; use a narrow pinned-core fixture as the authority. If the public API exposes only aggregate overlay information, retain the existing redacted material entity/relationship and record the exact API limitation rather than adding private-core access or synthetic identity.

**Tech Stack:** C++20, CMake/Ninja, pinned OCG API 11.0, CardScripts/BabelCDB fixture data, CTest, Python unittest, Windows-only CI metadata.

---

### Task 1: Preserve and record the M2 baseline

**Files:**
- Inspect only: `C:/Users/chris/Documents/yogiohML` worktree and existing M2 tests.
- Record in the final report: branch, HEAD, worktree status, rules bundle, CTest/Python results.

- [x] Run `git status`, `git branch --show-current`, `git rev-parse HEAD`, `git diff --stat`, `git diff`, and `git diff --check`.
- [x] Run `cmake --preset dev-windows-zig`.
- [x] Run `cmake --build --preset dev-windows-zig --parallel`.
- [x] Run `ctest --preset dev-windows-zig --output-on-failure` and confirm 33/33.
- [x] Run `python -m unittest discover -s tests/python -v` and confirm 3/3.

### Task 2: Inspect the pinned public query surface

**Files:**
- Inspect: `third_party/rules_bundle.lock.json`.
- Inspect: pinned `ocgapi.h`, `ocgapi_types.h`, query constants, and query implementation sources under `.cache/rules_bundle`.
- Add evidence only to: `docs/observation/M2_1_XYZ_API_INVESTIGATION.md`.

- [ ] Record the exact declarations and structures for `OCG_DuelQueryCount`, `OCG_DuelQuery`, `OCG_DuelQueryLocation`, and `OCG_DuelQueryField`.
- [ ] Record every public query flag related to overlay/material state and the public encoding of controller, location, sequence, position, and any overlay subsequence.
- [ ] Inspect the pinned implementation path for overlay lookup and determine whether `LOCATION_OVERLAY` plus a sequence/subsequence can return a material record.
- [ ] Reject any conclusion based on internal `card*` pointers or private headers.

### Task 3: Run a real pinned-core overlay capability probe

**Files:**
- Test/modify only if needed: `tests/observation/m2_1_xyz_api_test.cpp`, `CMakeLists.txt`.
- Reuse: `fixtures/m2_setup.lua`, `src/core/core_host.cpp`, and the existing public query decoder.
- Document results in: `docs/observation/M2_1_XYZ_API_INVESTIGATION.md` and `docs/observation/M2_FIXTURES.md`.

- [ ] Define a failing test or probe assertion for the strongest public-query claim: a real Xyz card with at least two attached materials must produce a per-material public record with distinct overlay sequence values if the API supports it.
- [ ] Run the probe against the pinned fixture before changing production code and capture raw query lengths/record boundaries without persisting hidden raw state into observations.
- [ ] Compare aggregate overlay count, `OCG_DuelQuery` records, `OCG_DuelQueryLocation` records, and `OCG_DuelQueryField` entries.
- [ ] Run the same query for perspective 0 and perspective 1 and record whether identity and metadata are legitimately visible.

### Task 4: Classify the public capability

**Files:**
- Modify: `docs/observation/M2_1_XYZ_API_INVESTIGATION.md`.
- Modify: `docs/contracts/player-observation-v1.md`, `docs/observation/M2_FIXTURES.md`, and coverage inventory files only when the classification changes the documented contract.

- [ ] Classify the evidence as Outcome A (individual materials queryable), Outcome B (aggregate-only), or Outcome C (queryable but visibility ambiguous).
- [ ] For Outcome B/C, preserve the existing redacted overlay entities and `XyzMaterial` relationships, state the missing public capability or unresolved visibility rule, and mark only the corresponding identity criterion as API-limited.
- [ ] For Outcome A, define the observation-local overlay locator and deterministic `overlay_sequence` ordering before adding projection code.

### Task 5A: Implement and test individual materials when Outcome A is proven

**Files:**
- Modify: `src/observation/observation_builder.cpp` and related observation headers only if the public query probe proves the needed records.
- Test: `tests/observation/m2_1_xyz_api_test.cpp`, `tests/observation/decision_observation_test.cpp`.
- Document: `docs/contracts/player-observation-v1.md`, `docs/observation/M2_FIXTURES.md`.

- [ ] Add material entities from real public query records, carrying only proven identity/state fields and an observation-local overlay locator.
- [ ] Emit `XyzMaterial` relationships from material entity to parent Xyz entity, ordered by ascending public overlay sequence.
- [ ] Add perspective tests for own and opponent Xyz materials, including metadata redaction if the engine proves only partial visibility.
- [ ] Add paired visible-world tests proving a visible material identity changes canonical bytes/hash and hidden omniscient changes do not.
- [ ] Add candidate consistency coverage for any material locator exposed by a real decision request; fail closed when the candidate is not observable.

### Task 5B: Close Outcome B/C as a documented API limitation

**Files:**
- Add/modify: `docs/observation/M2_1_XYZ_API_INVESTIGATION.md`.
- Modify: `docs/contracts/player-observation-v1.md`, `docs/observation/M2_FIXTURES.md`, `docs/observation/OBSERVATION_FIELD_COVERAGE.md`, and `docs/observation/observation_field_coverage.json`.
- Test: retain or extend `tests/observation/mechanics_projection_test.cpp` only to assert conservative behavior.

- [ ] State the exact public calls and flags that were tested, the aggregate result they return, and the missing per-material lookup result.
- [ ] Assert that no private core pointer, internal engine object ID, raw overlay code, or fabricated material identity enters `PlayerObservation`.
- [ ] Keep the correct material count and redacted `XyzMaterial` edge, and explicitly label material identity as API-limited rather than an OCGForge implementation failure.
- [ ] Mark detach as `DEFERRED` if the pinned public fixture cannot expose a deterministic detach transition without unrelated mechanics work.

### Task 6: Verify all M2.1 invariants and report

**Files:**
- Verify: all existing M0/M1/M2 source, tests, docs, and lock files.
- Final report: this task response plus `docs/observation/M2_1_XYZ_API_INVESTIGATION.md`.

- [ ] Run focused API/material tests, privacy tests, paired-world tests, hidden deck/Extra/face-down tests, candidate consistency, and observation determinism.
- [ ] Run the full build, CTest, Python suite, rules-bundle verification, lock diff, and `git diff --check`.
- [ ] Confirm no Linux changes, no rules-bundle change, no CardScripts/core patch, no commit, no push, no tag, and no PR.
- [ ] Report `CI NOT RUN — no push/PR requested`.
- [ ] Recommend `M2 FINAL PASS` only for proven Outcome A; recommend `M2 PASS WITH DOCUMENTED API LIMITATION` for proven Outcome B; retain `M2 FINAL ACCEPTANCE PENDING` only for an unresolved safe public path or implementation failure.

## Self-review checklist

- [ ] Public API declarations, query flags, implementation semantics, and real fixture output all agree.
- [ ] No conclusion depends on private `card*` pointers or private headers.
- [ ] Any material identity exposed by observation is backed by a real public query record.
- [ ] Any unproven visibility remains redacted and explicitly documented.
- [ ] The final acceptance matrix distinguishes API limitation from OCGForge implementation gap.
