# Phase 6 Task 4A PyTorch Smoke Infrastructure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement and validate Task-4A infrastructure for admitted corpus projection, canonical numeric/configuration/checkpoint codecs, a small provisional PyTorch scorer, fail-closed inference binding, and CUDA preflight with zero optimizer steps.

**Architecture:** C++ remains the DatasetManifest/AdmissionReceipt/Phase-6 materialization authority and emits a rebuildable derived corpus. Python owns the provisional PyTorch execution adapter and OCGForge-owned canonical codecs. The model receives numeric state/candidate surfaces only; public keys remain control-plane sidecars.

**Tech Stack:** Existing C++17/CMake, Python standard library, and the installed provisional CUDA-enabled PyTorch build. No optimizer step, no training run, no Task-5 evaluation, and no PyTorch-primary ADR.

---

## Task 1: Record the Task-4A numeric and sequencing contract

**Files:**
- Create: `docs/superpowers/specs/2026-09-02-phase6-task4a-pytorch-smoke-infrastructure-design.md`
- Create: `docs/superpowers/plans/2026-09-02-phase6-task4a-pytorch-smoke-infrastructure.md`
- Modify: `docs/p6/P6_CHECKPOINT_AND_INFERENCE_CONTRACT.md`
- Modify: `docs/p6/P6_IMPLEMENTATION_PLAN.md`

- [ ] **Step 1: Add the accepted Task-4A supplement**

Document `ocgforge.phase6.inference_numeric.v1`, `f32_ieee754_be.v1`, the canonical weight tensor field order, the Task-4A projection identity, the required configuration sub-identities, GPU provenance exclusions, and the Task-4A/4B sequencing gate. Keep Task 5 NOT AUTHORIZED and do not add a PyTorch-primary ADR.

- [ ] **Step 2: Validate the documentation**

Run the repository Markdown link checker if available and `git diff --check`. Confirm all Task-4A statements say zero optimizer steps and no acceptance-smoke result.

## Task 2: Define pure canonical codecs and tests first

**Files:**
- Create: `tools/phase6/__init__.py`
- Create: `tools/phase6/task4_codec.py`
- Create: `tests/phase6/phase6_task4a_codec_test.py`

- [ ] **Step 1: Write RED tests**

Test big-endian length framing, exact binary32 bytes, finite rejection, architecture/config identities, ordered tensor export, training-run sub-identities, checkpoint-manifest identity, derived-corpus digest verification, and mutation rejection. Test that hardware/execution provenance is excluded from checkpoint semantic bytes.

- [ ] **Step 2: Implement the standard-library codecs**

Use explicit `struct.pack/unpack`, UTF-8 length prefixes, fixed field order, SHA-256, and no pickle/`torch.save`/third-party serialization. Reject non-finite values and malformed/trailing bytes.

- [ ] **Step 3: Run the codec target**

Run `python -B tests/phase6/phase6_task4a_codec_test.py`; expected result is PASS with no ML import and no optimizer activity.

## Task 3: Add the admitted C++ smoke-corpus probe

**Files:**
- Create: `tools/phase6_task4_corpus_probe/main.cpp`
- Create: `src/phase6/task4_numeric_projection.hpp`
- Create: `src/phase6/task4_numeric_projection.cpp`
- Modify: `CMakeLists.txt`
- Create: `tests/phase6/phase6_task4a_corpus_test.py`

- [ ] **Step 1: Write the corpus validation test**

Invoke the built probe with its fixed job set. Assert the output schema, derived-artifact digest, DatasetManifest identity, split identity, source sample/model identities, nonempty train partition, exact candidate/key cardinality, and numeric row widths. Mutate a numeric row or source identity and assert the Python loader rejects it.

- [ ] **Step 2: Implement fixed admitted collection**

Use only fixed TeacherRunner seeds/jobs and the accepted Swordsoul/Salamangreat Teacher-v1 provenance. Build one validated DatasetManifest from the admitted receipts, call the existing Phase-6 materializer, and fail closed if any receipt, trajectory, label, or train partition is invalid. Never search across seeds for a favorable split.

- [ ] **Step 3: Implement numeric projection**

Project the Task-3 state-only and namespaced candidate inputs into deterministic numeric rows. Preserve all candidate rows/order and selected ordinal metadata; exclude locator strings, routing keys, private/internal values, and candidate ordinals from network features. Compute the derived-artifact content digest over canonical emitted bytes.

- [ ] **Step 4: Build and run the corpus test**

Build `phase6_task4_corpus_probe`, then run the Python corpus test. The test must perform no optimizer operation.

## Task 4: Implement the small provisional PyTorch architecture

**Files:**
- Create: `tools/phase6/task4_model.py`
- Create: `tests/phase6/phase6_task4a_model_test.py`

- [ ] **Step 1: Write RED model tests**

Test state pooling, candidate encoding, exact `N` logits, no action-family head, no ordinal/key/string feature input, finite forward output, and capacity `N-1` rejection for `N=24/25/129`. Use deterministic fixture rows only for representation tests; do not treat them as training labels.

- [ ] **Step 2: Implement the PyTorch model**

Implement the declared Linear/ReLU/mean-max architecture with variable candidate rows and no fixed global action vocabulary. Validate tensor dtype/device/shape before forward and fail closed on non-finite logits.

- [ ] **Step 3: Run model tests**

Run the model test on CPU only. Confirm no optimizer object is stepped and no GPU acceptance claim is emitted.

## Task 5: Implement checkpoint export/reload and inference binding

**Files:**
- Create: `tools/phase6/task4_inference.py`
- Create: `tests/phase6/phase6_task4a_inference_test.py`

- [ ] **Step 1: Write RED binding/failure tests**

Cover canonical export/reload into a fresh model, weight/checkpoint mutation, wrong architecture/config, wrong checkpoint, wrong model input, wrong ordered domain, stale/duplicate response, wrong score count, non-finite score, invalid ordinal/key, and capacity-too-small. Assert no fallback value is returned.

- [ ] **Step 2: Implement OCGForge-owned checkpoint and runner**

Export canonical f32 big-endian tensor bytes and checkpoint manifest; validate all identities on load. Keep optimizer/framework state separate. Create and consume single-use request/response envelopes, validate exact N finite scores, apply bytewise key ties, and keep keys outside model tensors.

- [ ] **Step 3: Run inference contract tests**

Run the fresh-reload and failure tests with a deterministic untrained model. These tests establish codec/runner behavior only, not a trained-checkpoint or gameplay claim.

## Task 6: Implement CUDA preflight without optimization

**Files:**
- Modify: `tools/phase6/task4_inference.py` or create `tools/phase6/task4_cuda.py`
- Create: `tests/phase6/phase6_task4a_cuda_preflight_test.py`

- [ ] **Step 1: Write preflight tests**

Use injectable device probes to test CUDA unavailable, zero device count, wrong device index, and wrong expected GPU. Every rejection must report zero optimizer steps and no CPU fallback.

- [ ] **Step 2: Implement the real preflight**

Require `torch.cuda.is_available()`, `torch.cuda.device_count() >= 1`, `cuda:0`, and the expected GPU name. Do not construct an optimizer or call `step()`.

- [ ] **Step 3: Run the preflight check**

Run the CPU-testable injected checks and a read-only local CUDA preflight. Record the environment as provenance only; do not start training.

## Task 7: Task-4A final verification and delivery

**Files:**
- No new files beyond the files above.

- [ ] **Step 1: Run all Task-4A focused tests**

Run codec, corpus, model, inference, preflight, relevant Phase-5/6 tests, Python regressions, Rules/Deck, Teacher Binding, public/source-boundary checks, and `git diff --check`.

- [ ] **Step 2: Build and run normal non-long CTest**

Use the working local Zig fallback. Exclude `P4A_HEAVY_REPLAY`, `M4_HEAVY_LIFECYCLE`, and `M4_ACCEPTANCE_SCALE`; do not run long soak, benchmark, Task-5 gameplay, or optimizer steps.

- [ ] **Step 3: Audit the zero-step gate and scope**

Verify `ACTUAL_OPTIMIZER_STEPS=0`, no training artifact, no checkpoint claimed as a trained result, no dependency beyond the provisional PyTorch runtime, no modified Phase-5/Teacher/rules/deck semantics, and exactly the planned files.

- [ ] **Step 4: Commit and push once**

    git add -- CMakeLists.txt docs/p6/P6_CHECKPOINT_AND_INFERENCE_CONTRACT.md docs/p6/P6_IMPLEMENTATION_PLAN.md docs/superpowers/specs/2026-09-02-phase6-task4a-pytorch-smoke-infrastructure-design.md docs/superpowers/plans/2026-09-02-phase6-task4a-pytorch-smoke-infrastructure.md tools/phase6 src/phase6 tools/phase6_task4_corpus_probe tests/phase6
    git diff --cached --check
    git commit -m "feat: add phase 6 task 4a smoke infrastructure"
    git push -u origin chris/phase6-task4-pytorch-smoke-runner

Stop after pushing for independent review. Task 4B and Task 5 remain unauthorized.
