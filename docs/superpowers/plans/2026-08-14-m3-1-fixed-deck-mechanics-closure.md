# OCGForge M3.1 Fixed-Deck Mechanics Closure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task with verification checkpoints.

**Goal:** Close every remaining fixed-deck mechanics row for the pinned Swordsoul Tenyi ML v1 versus Salamangreat ML v1 matchup, or classify it with evidence-supported `PUBLIC_API_LIMITATION` / `NOT_APPLICABLE_FIXED_MATCHUP`; leave `PENDING` only for a genuine unresolved correctness blocker.

**Architecture:** Extend the existing script-backed fixture runner and deterministic conformance policy only at the OCGForge adapter/test boundary. Each closure fixture loads a legal precondition, lets the pinned CardScript and ocgcore emit the decision domains, submits semantic actions from those domains, and checks resolved engine state through PlayerObservation/visible events. The pinned rules bundle, upstream sources, fixed decks, and privacy model remain unchanged.

**Tech Stack:** CMake/Ninja, C++17, pinned ocgcore public API 11.0, pinned CardScripts/BabelCDB, PowerShell, Python `unittest`, JSON/Markdown evidence.

**Spec:** User-provided `OCGForge — M3.1: Fixed-Deck Mechanics Conformance Closure` specification in the current task.

## Global Constraints

- Preserve the exact locked decks, hashes, ratios, Extra Deck contents, format metadata, and no-side-deck policy.
- Keep bundle `6fbbd212ae4be2df36170dcbfcdf5c46aaaa0e3091cf815c2d0261fd01640ea4` unchanged.
- Do not update or patch ocgcore, CardScripts, or BabelCDB.
- Do not add ML, search, MCTS, self-play, performance work, new decks, Linux, side decking, BO3, or unrelated mechanics.
- Do not reset, checkout away, discard, commit, push, tag, or open a PR.
- Do not promote a path from full-game completion alone; every `ENGINE_VERIFIED` row needs direct fixture/test evidence.
- Retain `INDIVIDUAL_XYZ_MATERIAL_QUERY` as a proven public API limitation and never expose individual overlay identity.
- Do not describe seat mirroring as start-player mirroring while the pinned API exposes only starting-player partition `[0]`.

---

### Task 1: Baseline record and authoritative closure inventory

**Files:**
- Create: `docs/m3/m3_1_closure.json`
- Create: `docs/m3/M3_1_CLOSURE.md`
- Modify: `artifacts/m3/m3_1_baseline.json`

**Interfaces:**
- Consumes: `docs/m3/m3_acceptance_matrix.json`, `docs/m3/mechanics_coverage.json`, `docs/m3/CARD_COMPATIBILITY.md`, and `docs/m3/public_api_gaps.json`.
- Produces: one row for each authoritative `PENDING` mechanic with mechanic ID, deck, passcode, script, old status, reachability, message families, fixture strategy, required observation, closure result, evidence test/fixture, and notes.

- [ ] Record `git status`, branch, HEAD, recent log, diff stat, full diff review, and `git diff --check` before edits.
- [ ] Run `cmake --preset dev-windows-zig`, `cmake --build --preset dev-windows-zig --parallel`, `ctest --preset dev-windows-zig --output-on-failure`, and `python -m unittest discover -s tests/python -v`.
- [ ] Run the existing compatibility audit, full 16-game suite, determinism/CRLF semantic replay, and rules-bundle verification; write the exact observed baseline counts/hashes.
- [ ] Read the authoritative matrix and emit exactly the 16 current pending rows into `m3_1_closure.json`; do not derive the list only from the task text.

### Task 2: Swordsoul closure fixtures

**Files:**
- Create or modify: `fixtures/m3_ss10_*.lua`, `fixtures/m3_ss13_*.lua`, `fixtures/m3_ss16_*.lua`, `fixtures/m3_ss17_*.lua`
- Modify: `tests/m3/runtime/m3_fixture_test.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tools/m3/coverage.py`

**Interfaces:**
- Consumes: the existing `CoreHost`, `ObservationSession`, `DeterministicConformancePolicy`, complete candidate domains, and visible event projection.
- Produces: direct evidence for Blackout banished trigger, Ashuna restriction expiry, reachable Chengying behavior, and a distinct Qixing interaction chain; any unreachable sub-path receives script/reachability evidence rather than an invented pass.

- [ ] For Blackout, use the official script to banish Blackout and assert the official trigger, real Token creation/properties, legal decisions, observation state, and deterministic hash; never call `Duel.CreateToken` in setup.
- [ ] For Ashuna, assert the pre-effect Extra domain, the active Wyrm-only domain, and the domain after the engine-defined expiry boundary; do not infer expiry from elapsed steps.
- [ ] For Chengying, isolate reachable ATK/banish/trigger behavior and record exactly which sub-paths are public and observable; do not claim unexercised destruction replacement.
- [ ] For Qixing, create an actual opposing activation that legitimately triggers Qixing and assert chain entry, legal target/affected card, resolution, LP/state consequence, and event projection separately from Longyuan burn.
- [ ] Run each focused fixture immediately; only then update its matrix row and closure-table row.

### Task 3: Salamangreat closure fixtures and reachability classifications

**Files:**
- Create or modify: `fixtures/m3_sg07_*.lua` through `fixtures/m3_sg19_*.lua`
- Modify: `tests/m3/runtime/m3_fixture_test.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tools/m3/coverage.py`

**Interfaces:**
- Consumes: exact Deck B passcodes, pinned scripts, Xyz aggregate/material relationships, Link continuation protocol, and owner/controller projection.
- Produces: direct evidence or justified final classification for Jack Jaguar/Weasel/Falco, Miragestallio, Sunlight Wolf, Raging/Pyro Phoenix, Heatleo, Roar, Rage, Promethean Princess, and Hiita.

- [ ] Reconcile each current row before adding a fixture so SG-05/06 and SG-20/21 are not duplicated.
- [ ] For Miragestallio, verify Xyz summon, material count/attachment, detach acceptance, material-count decrement, Deck summon, and FIRE restriction without exposing individual material identity.
- [ ] For every Link/recovery/restriction path, assert real material toggles, placement, engine result, and PlayerObservation fields; helpers may establish only preconditions.
- [ ] If a branch is not reachable with the locked scripts/decks, record script/source evidence and classify only that branch `NOT_APPLICABLE_FIXED_MATCHUP`; difficulty alone is never sufficient.
- [ ] Keep unresolved correctness issues `PENDING` with an explicit blocker.

### Task 4: Shared interaction, Battle, privacy, and start-player reconciliation

**Files:**
- Modify: `tools/m3/coverage.py`
- Modify: `tools/m3/cli.py`
- Modify or create: `tests/m3/test_m3_1_consistency.py`
- Modify: `docs/m3/PUBLIC_API_GAPS.md`
- Modify: `docs/m3/public_api_gaps.json`

**Interfaces:**
- Consumes: existing INT-01..INT-05 and BTL-01 evidence plus pinned public API behavior.
- Produces: machine-enforced classification invariants, precise interaction statuses, battle evidence, and current API gap inventory.

- [ ] Keep engine execution separate from the missing generic negated-status bit; promote only when all consequences are otherwise proven.
- [ ] Reconcile Battle Phase entry, attack declaration, complete target domain, direct attack where legal, damage, destruction, and MSG_WIN using the existing dedicated hash unless a missing condition is demonstrated.
- [ ] Prove start-player control is absent from the pinned public API and document seat mirror versus start-player partition accurately.
- [ ] Re-run hidden hand/S/T/Extra/face-down, candidate-to-observation, owner/controller, knowledge-transition, and paired-world privacy checks.
- [ ] Add a consistency test enforcing evidence for `ENGINE_VERIFIED`, API rows for `PUBLIC_API_LIMITATION`, rationale/source evidence for `NOT_APPLICABLE_FIXED_MATCHUP`, and blockers for `PENDING`.

### Task 5: Documentation and final acceptance artifacts

**Files:**
- Modify: `docs/m3/M3_ACCEPTANCE_MATRIX.md`
- Modify: `docs/m3/m3_acceptance_matrix.json`
- Modify: `docs/m3/MECHANICS_COVERAGE.md`
- Modify: `docs/m3/mechanics_coverage.json`
- Modify: `docs/m3/M3_1_CLOSURE.md`
- Modify: `docs/m3/m3_1_closure.json`
- Modify: `artifacts/m3/final_verification.json`

**Interfaces:**
- Consumes: focused fixture hashes, full-game results, determinism results, privacy results, bundle verification, and compatibility audit.
- Produces: counts that agree across JSON/Markdown, complete per-row closure history, fresh M3.1 recommendation, and M3.5 API-gap actions.

- [ ] Regenerate coverage and compatibility documents from machine-readable sources.
- [ ] Validate that every prior pending row has exactly one final classification and that no row is silently dropped or renamed.
- [ ] Record before/after counts, every path's old/new status and evidence, exact hashes, API limitations, and hosted CI as `NOT RUN`.

### Task 6: Final regression and handoff

**Files:**
- No source changes after final verification unless a root-cause fix reopens the relevant gate.

**Interfaces:**
- Consumes: the final source and documentation state.
- Produces: verified M3.1 report and no repository integration side effects.

- [ ] Run CMake configure/build, full CTest, full Python suite, compatibility audit, deck/manifest validation, mechanics/closure validation, privacy, full games, determinism, semantic replay, CRLF replay, bundle verification, and `git diff --check`.
- [ ] Verify rules lock, deck hashes, bundle commits, and upstream source trees are unchanged.
- [ ] Report `M3 FINAL PASS` only when `PENDING == 0`; otherwise report `M3 FINAL ACCEPTANCE PENDING` with every genuine blocker.
- [ ] Confirm no commit, push, tag, PR, reset, or checkout-away operation occurred.
