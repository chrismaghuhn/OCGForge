# PR #3 Review Findings Implementation Plan

> **For agentic workers:** Execute this plan autonomously with review checkpoints. Keep M4.2–M4.3.6 work and M5 outside this change.

**Goal:** Resolve the PR #3 review findings while preserving PR #3 as the M4 parallel-simulation foundation/baseline checkpoint.

**Architecture:** Keep the raw `coordinator_elapsed_us` protocol field for compatibility, but expose its derived report domain as end-to-end `dispatch_to_receipt`. Regenerate the baseline report from the existing measured matrix through the repository generator with no acceptance evidence, so a clean checkout truthfully reports `M4 BASELINE ACCEPTANCE PENDING`. Make the integrity tests validate that committed status directly instead of skipping when measured artifacts are absent. Add a hosted Windows CI invocation for the fast M4 acceptance/integrity suites and update only the minimum project navigation/status documents.

**Tech Stack:** Python `unittest`, JSON-schema/report generation in `tools/m4`, GitHub Actions Windows, Markdown project-status docs.

---

### Task 1: Freeze and characterize the PR #3 starting point

1. Verify the isolated worktree, branch, exact HEAD, clean status, and `git diff --check`.
2. Inspect the committed baseline, acceptance manifest/final-verification files, report schema, integrity tests, and current project-navigation files from `origin/main`.
3. Preserve the M4.3.x worktrees and do not import their changes.

### Task 2: Make clean-checkout baseline status auditable

1. Regenerate `docs/m4/m4_baseline.json` and `docs/m4/M4_BASELINE.md` using the repository baseline generator and the genuine existing matrix artifacts as input, with no acceptance evidence, producing `M4 BASELINE ACCEPTANCE PENDING`.
2. Remove only the committed PASS-only manifest/final-verification artifacts whose referenced evidence is absent from a clean checkout.
3. Add integrity tests that fail if a clean checkout claims PASS without available hash-verifiable evidence, and assert the committed pending contract instead of silently skipping that acceptance check.
4. Keep scenario tests that require optional measured rows clearly separate from the committed-status contract.

### Task 3: Correct coordinator timing terminology

1. Rename the derived aggregate timing bucket from `coordinator_ipc` to `dispatch_to_receipt` in the report generator, schema, tests, and generated baseline evidence.
2. Document that `coordinator_elapsed_us` measures end-to-end dispatch write/flush through validated result receipt, includes worker wait, and is not isolated IPC CPU.
3. Search the active M4 documentation and generated evidence for the obsolete IPC interpretation and update only affected derived/normative references.

### Task 4: Update hosted CI and project navigation

1. Add a Windows CI step that runs the fast M4 benchmark-integrity/acceptance test modules without running the 448-game matrix.
2. Restore the current README, `CURRENT_PROJECT_STATE`, and `ROADMAP` from `origin/main` into the PR worktree and make the narrow M4 foundation/baseline-status updates.
3. Do not claim optimization completion, general ML readiness, or M5.

### Task 5: Run narrow review-fix verification

1. Run the directly affected M4 Python test modules, including the hosted-CI command locally.
2. Run the repository’s applicable CTest command and project Python/M3/M4 regression commands that are available without the expensive benchmark matrix.
3. Validate JSON/schema/hash integrity, privacy/candidate gates where available, `git diff --check`, and the final diff/status.
4. Report exact fresh commands and results; do not merge or push automatically.
