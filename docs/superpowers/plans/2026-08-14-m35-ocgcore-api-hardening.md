# M3.5 ocgcore Public API Hardening Implementation Plan

> For agentic workers: execute this plan task-by-task with TDD and verification checkpoints. The milestone explicitly forbids commit, push, tag, and PR.

Goal: add two minimal public ocgcore capabilities, pin their repository patchset reproducibly, and revalidate OCGForge under the hardened canonical environment.

Architecture: keep the pinned base checkout immutable; create a derived build checkout from the exact base commit and apply two ordered tracked patches. Patch 0001 completes the existing overlay_seq query contract. Patch 0002 adds a pre-start setter without changing OCG_DuelOptions ABI. Include the patch metadata in the canonical rules identity.

Tech Stack: C++17, CMake, pinned ocgcore 11.0, Python 3 unittest/validation scripts, CTest, Windows Zig toolchain.

---

### Task 1: Freeze the approved design and reproduce the M3 baseline

Files:
- Create: docs/superpowers/specs/2026-08-14-m35-ocgcore-api-hardening-design.md
- Create: docs/superpowers/plans/2026-08-14-m35-ocgcore-api-hardening.md
- Test: existing CMake, CTest, Python, and tools/m3 validation commands

- [ ] Confirm branch main, HEAD a2554727c016d24c884bd8ae99561d95d0bc32ae, and record the existing dirty worktree without resetting or stashing.
- [ ] Run cmake --preset dev-windows-zig.
- [ ] Run cmake --build --preset dev-windows-zig --parallel.
- [ ] Run ctest --preset dev-windows-zig --output-on-failure and record the complete count.
- [ ] Run python -m unittest discover -s tests/python -v.
- [ ] Run python -m unittest discover -s tests/m3 -v.
- [ ] Run python tools/verify_rules_bundle.py --lock third_party/rules_bundle.lock.json --cache .cache/rules_bundle.
- [ ] Run python -m tools.m3.cli validate-docs --docs docs/m3.
- [ ] Run the existing deck audit, mechanics coverage, privacy, candidate/observation, canonical full-game, determinism, and semantic-replay commands and record their pre-M3.5 artifacts.

Expected baseline from M3: CTest 82/82, legacy Python 3/3, M3 Python 17/17, full games 16/16, PENDING 0.

### Task 2: Add RED direct-core tests for both public API gaps

Files:
- Create: tests/m3_5/core/xyz_material_query_test.cpp
- Create: tests/m3_5/core/starting_player_api_test.cpp
- Modify: CMakeLists.txt to register the two tests against the derived ocgcore target
- Test: the two new CTest targets

- [ ] Write the Xyz test against the existing OCG_QueryInfo convention with loc equal to LOCATION_MZONE | LOCATION_OVERLAY, seq equal to the parent slot, and overlay_seq 0/1/2. Assert that the current unpatched core returns empty for the individual records while the aggregate parent query still reports two materials.
- [ ] Write the starting-player test using the wished-for public signature OCG_DuelSetStartingPlayer. Cover default player 0, explicit 0, explicit 1, invalid 2, and a post-OCG_StartDuel call. Assert the missing symbol/API behavior is the expected RED failure, not a fixture typo.
- [ ] Run only the new tests and capture their failure output before changing production or core source.

### Task 3: Implement patch 0001 with the smallest existing-contract fix

Files:
- Create: third_party/patches/ocgcore/0001-fix-overlay-seq-parent-query.patch
- Modify in a disposable derived checkout only during development: ocgapi.cpp
- Test: tests/m3_5/core/xyz_material_query_test.cpp

- [ ] Create a disposable derived checkout from the exact base commit; never edit .cache/rules_bundle/ocgcore.
- [ ] Change only the overlay parent lookup so OCG_DuelQuery masks LOCATION_OVERLAY from info.loc before calling get_field_card. Keep overlay_seq as the sole material index.
- [ ] Preserve the existing null/length behavior for missing parents and out-of-range overlay_seq.
- [ ] Export the change as patch 0001 with stable relative paths and no machine paths.
- [ ] Rebuild the derived core and run the Xyz RED test; require index 0 and 1 to resolve, index 2 to return empty, and no crash.
- [ ] Extend the direct test to assert core-native ordering by comparing each returned code with the material order established by the legal fixture, not by sorting in OCGForge.

### Task 4: Implement patch 0002 with a narrow pre-start public setter

Files:
- Create: third_party/patches/ocgcore/0002-add-starting-player-control.patch
- Modify in the disposable derived checkout only: ocgapi.h, ocgapi.cpp, duel.h, duel.cpp or field.h/field.cpp, processor.cpp as required by the root-cause trace
- Test: tests/m3_5/core/starting_player_api_test.cpp

- [ ] Add OCG_DuelSetStartingPlayer(OCG_Duel, uint8_t) at the public API boundary without enlarging OCG_DuelOptions.
- [ ] Store a pre-start requested player with default 0 and a started state. Return failure for values above 1 and after start; never mutate an active turn.
- [ ] Set the Startup processor's first Turn to the stored player and leave deck ownership, controller values, seed, LP, and opening draw logic unchanged.
- [ ] Rebuild and run the direct test for default, explicit 0, explicit 1, invalid input, post-start rejection, MSG_NEW_TURN, and seat preservation.
- [ ] Export this independent change as patch 0002 after patch 0001, with no OCGForge-specific names.

### Task 5: Add deterministic patchset preparation and identity verification

Files:
- Create: tools/prepare_ocgcore_patchset.py
- Create: tools/verify_ocgcore_patchset.py
- Modify: tools/rules_bundle.py
- Modify: tools/verify_rules_bundle.py
- Modify: cmake/RulesBundle.cmake
- Modify: CMakeLists.txt
- Modify: third_party/rules_bundle.lock.json
- Test: tests/python/test_rules_bundle.py and new M3.5 patchset tests

- [ ] Define one canonical ordered patch manifest with filenames, individual SHA-256 values, and a combined SHA-256 over stable JSON containing only ordered names and hashes.
- [ ] Extend rules_affecting_inputs with the base core commit and patchset identity, preserving the pinned CardScripts, BabelCDB, API, format, and MR5 flags.
- [ ] Make verification reject missing, reordered, renamed, changed, or extra patch files and reject a base checkout whose HEAD is not the locked base commit.
- [ ] Make preparation create or refresh only .cache/derived/ocgcore, verify the base checkout, apply patches in order with git apply --check followed by git apply, and fail closed on any mismatch.
- [ ] Change CMake to use the derived patched source tree for ocgcore while leaving the immutable base cache as the verification source.
- [ ] Generate the new bundle ID from canonical JSON; assert it differs from ff8721aae1a17da6a72079e65ae75a05012c0c367b6f249651c1de713c1fbf91.
- [ ] Run RED patchset tamper tests before the implementation is considered green: change a patch byte, reverse order, and use the unpatched base; each must fail.

### Task 6: Integrate CoreHost and public capability checks

Files:
- Modify: include/ygo/core/core_host.hpp
- Modify: src/core/core_host.cpp
- Modify: include/ygo/core/rules_bundle.hpp
- Modify: include/ygo/m3/canonical_rules.hpp or the equivalent generated rules metadata
- Test: tests/m3_5/runtime/core_host_starting_player_test.cpp and existing lifecycle tests

- [ ] Add an optional CoreHost starting_player configuration. With no value, do not call the new setter; with 0 or 1, call it before OCG_StartDuel and throw a public CoreError on rejection.
- [ ] Add explicit hardened-capability metadata derived from the locked patchset/API identity; do not detect support by trying random behavior.
- [ ] Add a public CoreHost individual overlay query helper only if the existing query wrapper cannot express the corrected contract; otherwise keep the existing query path and update observation code to use it.
- [ ] Assert that an incompatible unpatched derived core cannot satisfy the hardened bundle lock.

### Task 7: Integrate observation identity and privacy

Files:
- Modify: src/observation/observation_builder.cpp and its focused headers only where required
- Modify: tests/observation/m2_1_xyz_api_test.cpp or replace its old limitation assertion with the new hardened behavior
- Create: tests/m3_5/observation/xyz_material_identity_test.cpp
- Create: tests/m3_5/observation/xyz_material_privacy_test.cpp

- [ ] Query each attached material by the public parent locator and overlay_seq, decode actual card state, and associate it with the existing overlay relationship.
- [ ] Set identity_known true only for a legitimately visible material; retain redacted material identity for hidden perspectives.
- [ ] Prove ordered slots, material count, detach decrement, detached non-resolution, and remaining-material resolution.
- [ ] Add paired worlds proving hidden identity changes do not alter canonical observation bytes or hash, while legitimately visible identity changes do.
- [ ] Remove aggregate overlay-code identity inference from the observation path.

### Task 8: Integrate explicit starting-player full-game matrix

Files:
- Modify: tests/m3/full_game/full_fixed_deck_test.py
- Modify: tests/m3/determinism/m3_determinism_test.py
- Modify: tools/m3 scripts that build CoreHostConfig or result manifests
- Create: tests/m3_5/full_game/m35_full_game_test.py
- Create: tests/m3_5/determinism/m35_determinism_test.py

- [ ] Generate 4 seeds × 2 seat assignments × 2 starting players with explicit seat_assignment and starting_player metadata.
- [ ] Require 16/16 terminal games with retries, unsupported decisions, automatic decisions, candidate truncation, and core errors all zero.
- [ ] Require canonical MR5 flags and the new bundle ID in every result.
- [ ] Assert both starting-player partitions and all four deck-first/deck-second semantic combinations without conflating seats and starting player.
- [ ] Prove independent-process equality and semantic replay separately for at least one start_player=0 and one start_player=1 configuration; retain CRLF replay.

### Task 9: Revalidate M3 mechanics and documentation

Files:
- Modify: docs/m3/PUBLIC_API_GAPS.md
- Modify: docs/m3/public_api_gaps.json
- Create: docs/m3_5/M3_5_ACCEPTANCE.md
- Create: docs/m3_5/PUBLIC_API_HARDENING.md
- Create: docs/m3_5/m35_acceptance.json
- Modify: docs/m3/MECHANICS_COVERAGE.md and docs/m3/mechanics_coverage.json
- Modify: docs/m3/M3_ACCEPTANCE_MATRIX.md and docs/m3/m3_acceptance_matrix.json only for the historical/API-gap transition

- [ ] Reclassify SG-10 only after the real individual query fixture passes; target ENGINE_VERIFIED 38, PROTOCOL_VERIFIED 7, PUBLIC_API_LIMITATION 0, PENDING 0 if the repository classification semantics support it.
- [ ] Mark both confirmed API gaps RESOLVED and keep FIXTURE_RUNNER_PUBLIC_SETUP_SCOPE as test infrastructure.
- [ ] Record old/new core identity, patchset hashes, old/new bundle IDs, API/ABI behavior, tests, privacy, matrix, determinism, and upstream handoff text without submitting it.
- [ ] Add machine-checkable invariants for patch identity, config equality, 45 mechanics rows, no pending rows, evidence presence, and full-game provenance.

### Task 10: Full verification and final status

Files:
- Test: all repository CTest, Python, M3, M3.5, rules, privacy, replay, determinism, and documentation commands

- [ ] Run cmake --preset dev-windows-zig.
- [ ] Run cmake --build --preset dev-windows-zig --parallel.
- [ ] Run ctest --preset dev-windows-zig --output-on-failure.
- [ ] Run python -m unittest discover -s tests/python -v.
- [ ] Run python -m unittest discover -s tests/m3 -v.
- [ ] Run python -m unittest discover -s tests/m3_5 -v.
- [ ] Run python tools/verify_rules_bundle.py --lock third_party/rules_bundle.lock.json --cache .cache/rules_bundle.
- [ ] Run all M3/M3.5 deck, mechanics, privacy, candidate/observation, MR5, full-game, determinism, semantic replay, and CRLF replay validators.
- [ ] Run git diff --check and inspect git status. Confirm no CardScripts, BabelCDB, deck, or base-cache source modifications.
- [ ] Report M3.5 FINAL PASS only if the patchset is reproducible and every acceptance gate is evidenced; otherwise report the exact blocker as M3.5 FINAL ACCEPTANCE PENDING.

---

Self-review:
- The plan keeps the base cache immutable and never uses a dirty checkout as canonical.
- Both API changes have independent red tests and independent patch identities.
- No OCGForge private core access or second overlay mechanism is introduced.
- Starting-player setter is pre-start only and default player 0 is preserved.
- Patch hashes affect the new bundle ID.
- The plan includes direct core tests, OCGForge integration, privacy, full-game partitions, determinism, replay, documentation, and final regression.
- No commit, push, tag, or PR step is included because the milestone explicitly forbids them.
