# Phase 6 Task 3 Framework-Neutral BC Scorer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a framework-neutral Phase-6 reference seam that scores every supplied encoded candidate and resolves one deterministic existing public action key without implementing a neural network or selecting a training backend.

**Architecture:** `ygo::phase6` will accept only an already validated `EncodedModelInputV1`. A state encoder produces an opaque reference representation, a candidate encoder receives each encoded candidate in source order without its routing key, and a candidate scoring function produces one reference execution score per row. A separate selection boundary validates exact cardinality and finite scores, then applies the frozen higher-score/bytewise-key tie rule against the input's unchanged routing sidecar.

**Tech Stack:** Existing C++17 standard-library types and accepted `ygo::model` values. No PyTorch, JAX, NumPy, ML dependency, optimizer, checkpoint, or training code.

---

## Task 1: Define the callback seam and failure result types

**Files:**
- Create: `include/ygo/phase6/bc_candidate_scorer.hpp`
- Test: `tests/phase6/phase6_bc_candidate_scorer_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing test and its encoded-input fixture**

Create a valid `EncodedModelInputV1` fixture with `N` card-selection candidates, unique routing keys derived from each candidate's `source_index`, and the accepted encoded schema. Define callbacks that record the received candidate ordinals and return deterministic state/candidate representations. Assert the wished-for result API returns one score per candidate and that callback invocation order is `0..N-1`.

- [ ] **Step 2: Run the new target to verify the interface is absent**

Configure the Zig fallback and build only `phase6_bc_candidate_scorer_test`:

    cmake --preset dev-windows-zig
    cmake --build build/windows-zig --target phase6_bc_candidate_scorer_test --parallel 2

Expected result: the build fails because the new header and reference function do not yet exist.

- [ ] **Step 3: Add the minimal framework-neutral public interface**

Define `Phase6BcStateRepresentationV1` and `Phase6BcCandidateRepresentationV1` as callback-owned reference values containing only `std::vector<std::uint64_t>`. Define callback result wrappers with `std::optional<T>` and structured callback errors. Define:

    using Phase6BcStateEncoderV1 =
        std::function<Phase6BcCallbackResult<Phase6BcStateRepresentationV1>(
            const model::EncodedModelInputV1&)>;
    using Phase6BcCandidateEncoderV1 =
        std::function<Phase6BcCallbackResult<Phase6BcCandidateRepresentationV1>(
            const Phase6BcStateRepresentationV1&,
            const model::EncodedCandidate&)>;
    using Phase6BcCandidateScoringFunctionV1 =
        std::function<Phase6BcCallbackResult<double>(
            const Phase6BcStateRepresentationV1&,
            const Phase6BcCandidateRepresentationV1&)>;

    struct Phase6BcReferenceScorerV1 {
        Phase6BcStateEncoderV1 state_encoder;
        Phase6BcCandidateEncoderV1 candidate_encoder;
        Phase6BcCandidateScoringFunctionV1 candidate_scoring_function;
        std::optional<std::uint64_t> physical_candidate_capacity;
    };

    Phase6BcInferenceResult score_encoded_model_input_v1(
        const model::EncodedModelInputV1&,
        const Phase6BcReferenceScorerV1&) noexcept;

Reference `double` scores are execution/diagnostic values only. They are not canonical score bytes, response identity, checkpoint identity, or the later `ocgforge.phase6.inference_numeric.v1` contract.

- [ ] **Step 4: Wire the source into `ygo_phase6` and build the target**

Add `src/phase6/bc_candidate_scorer.cpp` to `ygo_phase6`, add the new test executable linked to `ygo_phase6`, and run:

    cmake --build build/windows-zig --target phase6_bc_candidate_scorer_test --parallel 2

Expected result: compilation succeeds and the test is green once the minimal pipeline is present.

## Task 2: Implement exact-domain scoring and deterministic selection

**Files:**
- Modify: `src/phase6/bc_candidate_scorer.cpp`
- Test: `tests/phase6/phase6_bc_candidate_scorer_test.cpp`

- [ ] **Step 1: Add a failing tie/selection assertion**

Give two candidates exactly equal finite scores and assert selection chooses the candidate whose existing `public_action_key` is bytewise smaller as unsigned UTF-8 bytes. Give a distinct higher score to a later candidate and assert its source ordinal and unchanged routing key are selected.

- [ ] **Step 2: Validate before callback execution**

Call `model::canonical_encoded_model_input_bytes` before invoking any callback. Convert its `std::invalid_argument` failure into `InvalidEncodedModelInput`. Reject an empty callback, a zero/undersized optional physical capacity, or an invalid candidate count. Do not copy, sort, deduplicate, truncate, or mutate candidates or routing keys.

- [ ] **Step 3: Implement the three-callable pipeline**

Invoke the state encoder once. For each `candidate_features[index]`, invoke the candidate encoder and scorer with that exact row and index, append exactly one score, and preserve source order. Convert missing/error callback results to structured reference errors without exposing callback-private diagnostics.

- [ ] **Step 4: Implement selection over the exact routing sidecar**

Reject non-finite scores and score-vector cardinality mismatches. Scan indexes `0..N-1` in source order. A strictly higher finite score replaces the best candidate; an exactly equal score compares only `encoded.routing_keys[index]` and the current best key using unsigned-byte lexicographic order. Return the selected local ordinal and the exact key at that same index. Do not use an unordered container or a candidate index as public identity.

- [ ] **Step 5: Run the focused scorer test**

    cmake --build build/windows-zig --target phase6_bc_candidate_scorer_test --parallel 2
    .\\build\\windows-zig\\phase6_bc_candidate_scorer_test.exe

Expected result: the focused test passes, including callback order, exact `N` scores, source-order/key pairing, and deterministic ties.

## Task 3: Prove capacity and fail-closed behavior

**Files:**
- Test: `tests/phase6/phase6_bc_candidate_scorer_test.cpp`
- Modify: `src/phase6/bc_candidate_scorer.cpp` only if a test exposes a missing failure path

- [ ] **Step 1: Add the capacity boundary tests**

Run the reference scorer with valid encoded domains of `N=24`, `N=25`, and `N=129` and a capacity of at least `N`; assert exactly `N` callbacks, scores, and source-order results. Repeat with capacity `N-1`; assert `CandidateCapacityTooSmall`, no callback invocation, no candidate mutation, and no selected fallback.

- [ ] **Step 2: Add malformed-output and callback-failure tests**

Assert fail-closed results for a missing state encoder, state encoder failure, candidate encoder failure, scoring-function failure, NaN, positive infinity, negative infinity, and wrong score cardinality passed to the public selection helper. Assert no alternate policy or candidate-zero/first-candidate result is returned.

- [ ] **Step 3: Add the public selection helper**

Expose and test:

    Phase6BcSelectionResult select_phase6_candidate_v1(
        const model::EncodedModelInputV1&,
        const std::vector<double>& scores) noexcept;

This helper shares encoded-input validation, finite-score validation, exact `N` checking, source-order scanning, and the bytewise tie rule. It returns only the existing sidecar key and local ordinal, never response bytes or internal semantic keys.

- [ ] **Step 4: Re-run the focused target**

    cmake --build build/windows-zig --target phase6_bc_candidate_scorer_test --parallel 2
    ctest --test-dir build/windows-zig -R ^phase6_bc_candidate_scorer_test$ --output-on-failure

Expected result: all capacity, malformed-output, finite-score, and tie tests pass.

## Task 4: Prove public-input and architecture boundaries

**Files:**
- Test: `tests/phase6/phase6_bc_candidate_scorer_test.cpp`

- [ ] **Step 1: Add paired encoded-input equality coverage**

Use two encoded inputs with identical accepted public fields, candidate rows, routing sidecars, and model-input source values but different private-only markers kept outside the encoded type. Assert byte identity and selected-key equality. The reference interface has no parameter or field for `CoreHost`, `PlayerObservation`, internal semantic keys, response bytes, continuation IDs, or hidden passcodes.

- [ ] **Step 2: Add a source inspection assertion**

Search the new public header/source/test interface for forbidden backend and private-boundary tokens. Confirm the candidate encoder receives an `EncodedCandidate` and not a public key, raw internal candidate, or engine object.

- [ ] **Step 3: Run all focused Phase-5/Phase-6 checks**

    ctest --test-dir build/windows-zig -R ^(phase6_bc_candidate_scorer_test|logical_model_input_test|encoded_model_input_test|model_batch_layout_test|model_supervision_sample_test|phase6_supervision_split_inspector_test)$ --output-on-failure
    python -B tests/model/logical_model_public_boundary_test.py include/ygo/model src/model include/ygo/phase6 src/phase6
    git diff --check

Expected result: all selected CTest tests pass, the public-boundary script passes, and the diff check emits no diagnostics.

## Task 5: Final verification and delivery

**Files:**
- No additional source files beyond the files above.

- [ ] **Step 1: Build and run the normal non-long regression**

Build the Zig fallback and run the repository's normal non-long CTest set. Exclude only `P4A_HEAVY_REPLAY`, `M4_HEAVY_LIFECYCLE`, and `M4_ACCEPTANCE_SCALE`; do not run neural training or GPU work.

- [ ] **Step 2: Run short repository checks**

Run the Rules/Deck identity script, Teacher binding script, Python suite, public-boundary script, and `git diff --check`. Report native MSVC and hosted CI separately if not run.

- [ ] **Step 3: Audit the final scope**

Verify the branch is based on `origin/main = 00dc11cca3977a1d59ff8d7a0a1e716eaa5d69a2`, only the Task-3 implementation/test/CMake/plan files are changed, no dependency manifest changed, and no training/checkpoint artifact exists.

- [ ] **Step 4: Commit and push once**

    git add -- CMakeLists.txt include/ygo/phase6/bc_candidate_scorer.hpp src/phase6/bc_candidate_scorer.cpp tests/phase6/phase6_bc_candidate_scorer_test.cpp docs/superpowers/plans/2026-09-02-phase6-task3-framework-neutral-bc-scorer.md
    git diff --cached --check
    git commit -m "feat: add framework-neutral phase 6 candidate scorer"
    git push -u origin chris/phase6-task3-framework-neutral-bc-scorer

Stop after pushing for independent review. Task 4 remains unauthorized.
