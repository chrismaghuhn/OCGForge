# OCGForge M3 Mechanics Conformance and Fixed Decks Implementation Plan

> For agentic workers: REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Add the complete M3 correctness slice: two exact fixed 40/15 decks, pinned BabelCDB/CardScripts compatibility evidence, real-card mechanics fixtures, privacy-safe observations, deterministic full games, replay-equivalent traces, and an explicit acceptance matrix.

**Architecture:** Keep the pinned ocgcore and rules bundle immutable. Extend the existing C++ public-API adapter with a small M3 fixture runner that loads real main/Extra Deck cards, decodes every required public decision family, builds PlayerObservation for both perspectives, and records fail-closed semantic traces. Keep catalog/script auditing and machine-readable report generation in Python, where SQLite and filesystem inspection are already used by the repository. Use narrow descriptor-backed fixtures and shared runner code rather than card-specific mechanics in OCGForge; all card behavior must continue to come from the pinned core plus CardScripts.

**Tech Stack:** C++17, CMake/Ninja, pinned ocgcore public C API 11.0, pinned ProjectIgnis/CardScripts Lua, pinned BabelCDB SQLite, Python 3 unittest, PowerShell, Windows-only Zig fallback preset.

**Execution boundary:** The user explicitly forbids commits, pushes, tags, PRs, ocgcore/CardScripts/BabelCDB updates, Linux CI, and deck substitutions. All implementation slices remain uncommitted in the current worktree. A fixture that cannot be proven remains PENDING and blocks the full-game acceptance gate.

---

## File map and responsibilities

### Fixed-deck and audit tooling

- Create fixtures/decks/swordsoul_tenyi_ml_v1.ydk: exact 40 Main / 15 Extra Deck A passcodes, with #main, #extra, and !side sections and no side cards.
- Create fixtures/decks/salamangreat_ml_v1.ydk: exact 40 Main / 15 Extra Deck B passcodes, with the same section contract.
- Create fixtures/decks/ocgforge.matchup.swordsoul_salamangreat.v1.json: canonical matchup manifest, stable ordered passcode arrays, counts, hashes, bundle commits, and selected canonical passcode policy.
- Create tools/m3/__init__.py: package marker and tool version.
- Create tools/m3/decks.py: strict YDK parser, ordered section validation, count validation, canonical byte hashing, and 110-slot expansion.
- Create tools/m3/catalog.py: pinned BabelCDB lookup, exact-name resolution, type/zone checks, decoded metadata, and deterministic duplicate-name handling.
- Create tools/m3/scripts.py: CardScripts path resolution for official/shared/core implementations, required-script classification, and load evidence.
- Create tools/m3/audit.py: deterministic compatibility JSON/Markdown generation and machine-verifiable manifest comparison.
- Create tools/m3/cli.py: audit, instantiate-inputs, and validate-docs subcommands used by CMake and tests.
- Create tests/m3/test_deck_manifest.py: red/green tests for YDK parsing, exact counts, hash stability, duplicate-name canonicalization, and manifest invariants.
- Create tests/m3/test_card_compatibility.py: red/green tests for all 110 slots, CDB metadata, script evidence, and report status vocabulary.

### C++ M3 runtime and fixture runner

- Modify include/ygo/core/rules_bundle.hpp: extend FixtureDeck with extra_deck, section-aware counts, and preserve legacy .deck loading behavior.
- Modify src/core/rules_bundle.cpp: parse YDK sections without changing legacy M0/M1 passcode-file semantics; hash exact source bytes; reject side-deck content for the locked matchup.
- Modify include/ygo/core/core_host.hpp and src/core/core_host.cpp: load Extra Deck cards through public OCG_DuelNewCard with LOCATION_EXTRA, expose strict required-script configuration, and retain existing main-deck callers.
- Modify src/core/card_data_reader.cpp and tools/prepare_card_data.py: materialize all M3 deck passcodes into the generated callback table while retaining decoded level/rank and Link metadata conventions.
- Modify src/core/script_reader.hpp and src/core/script_reader.cpp: distinguish legitimate no-script vanilla cards from a required effect-card script failure using an explicit required-code set; never silently downgrade an audited effect card.
- Create include/ygo/m3/fixture_harness.hpp: public runner types for fixture setup, process outcomes, decision records, observation snapshots, metrics, and fail-closed diagnostics.
- Create src/m3/fixture_harness.cpp: process the pinned core, ingest visible events, build both perspective observations, enforce candidate/observation consistency, and record semantic gameplay traces.
- Create include/ygo/m3/conformance_policy.hpp: test-only policy interface that accepts only PlayerObservation and DecisionRequest data.
- Create src/m3/conformance_policy.cpp: deterministic fixture preferences followed by canonical semantic-key tie-breaking; no omniscient state, direct card access, or fabricated response.
- Create tools/ygo_m3_probe/main.cpp: command-line fixture/full-game runner with JSONL evidence output, explicit unsupported/retry/core-error accounting, and replay mode.
- Create tests/m3/runtime/m3_harness_contract_test.cpp: test runner invariants before real-card fixtures are added.
- Create tests/m3/runtime/m3_observation_consistency_test.cpp: real pinned-core candidate/observation checks for visible source and target locators.

### Protocol and observation extensions, only when a pinned trace proves they are needed

- Modify src/protocol/message_decoder.cpp, src/protocol/continuation.cpp, include/ygo/protocol/*, and src/protocol/response_builder.cpp only through a failing real-message test. Preserve complete domains, continuation pause semantics, and fail-closed unsupported messages.
- Modify include/ygo/observation/* and src/observation/* only for an OCGForge projection defect demonstrated by a real fixture. Do not add hidden-card tracking or individual Xyz-material identity.

### Fixture descriptors and focused test modules

- Create fixtures/m3/swordsoul/*.json and narrowly scoped Lua setup scripts under fixtures/m3/swordsoul/ for SS-01 through SS-18.
- Create fixtures/m3/salamangreat/*.json and setup scripts under fixtures/m3/salamangreat/ for SG-01 through SG-21.
- Create fixtures/m3/interactions/*.json and setup scripts for hand traps, Called by the Grave, and battle scenarios.
- Create focused test modules under tests/m3/swordsoul/, tests/m3/salamangreat/, tests/m3/interactions/, tests/m3/full_game/, and tests/m3/determinism/. Each module asserts engine state, decision protocol, and observation evidence for its own fixture group.

### M3 documentation and evidence

- Create docs/m3/card_compatibility.json and docs/m3/CARD_COMPATIBILITY.md.
- Create docs/m3/mechanics_coverage.json and docs/m3/MECHANICS_COVERAGE.md.
- Create docs/m3/public_api_gaps.json and docs/m3/PUBLIC_API_GAPS.md.
- Create docs/m3/FIXED_MATCHUP.md.
- Create docs/m3/M3_ACCEPTANCE_MATRIX.md.
- Create artifacts/m3/ as ignored local evidence output for fixture traces, full-game JSONL, replay traces, and deterministic hash comparisons.
- Modify CMakeLists.txt, README.md, and existing fixture documentation only to register and explain M3 artifacts without changing M0/M1/M2 guarantees.

---

## Task 1: Freeze the current authority and add the plan

**Files:**
- Create: docs/superpowers/plans/2026-08-14-m3-mechanics-conformance.md
- Test: repository status and rules verification

- [ ] Step 1: Record current refs and worktree state.

Run:

~~~powershell
git status
git branch --show-current
git rev-parse HEAD
git log -3 --oneline
git diff --stat
git diff
git diff --check
~~~

Expected before implementation: clean main, no diff-check errors, and the M2 observation commit present in history. Preserve any externally created commits and do not reset them.

- [ ] Step 2: Verify the pinned bundle.

Run:

~~~powershell
python tools/verify_rules_bundle.py --lock third_party/rules_bundle.lock.json --cache .cache/rules_bundle
~~~

Expected: ok=true, bundle 6fbbd212ae4be2df36170dcbfcdf5c46aaaa0e3091cf815c2d0261fd01640ea4, core 9a0c558c2d686542f7914a6d529fd7aa57746aed, CardScripts f337c87018ca723c1aded5143e616bb649555273, BabelCDB 89ad6837b0766a52984d8c715a7d5d4f8447946b, API 11.0.

---

## Task 2: Build the strict YDK and catalog foundation

**Files:**
- Create: fixtures/decks/swordsoul_tenyi_ml_v1.ydk
- Create: fixtures/decks/salamangreat_ml_v1.ydk
- Create: tools/m3/__init__.py
- Create: tools/m3/decks.py
- Create: tools/m3/catalog.py
- Create: tests/m3/test_deck_manifest.py

- [ ] Step 1: Write failing parser tests.

Cover the exact section grammar:

~~~python
assert parse_ydk(text).main == expected_40
assert parse_ydk(text).extra == expected_15
assert parse_ydk(text).side == []
with self.assertRaises(DeckFormatError):
    parse_ydk("#main\n1\n#extra\n")
~~~

Also assert that comments, blank lines, duplicate passcodes, and source-byte hashing are deterministic while section order and counts remain strict.

- [ ] Step 2: Run the focused tests and confirm the intended failure.

Run:

~~~powershell
python -m unittest tests.m3.test_deck_manifest -v
~~~

Expected: import/API failures because the new parser does not exist yet.

- [ ] Step 3: Implement the parser and write the locked lists.

Use canonical script-backed primary passcodes for duplicate printed names. The selected passcodes must be recorded in the manifest and compatibility report; alternate same-name CDB rows remain audit evidence and are never silently substituted. The canonical IDs identified in the pinned data include 55273560 for Incredible Ecclesia, 14558127 for Ash Blossom, 97268402 for Effect Veiler, 24224830 for Called by the Grave, 41463181 for Heatleo, and 73642296 for Ghost Belle.

- [ ] Step 4: Run the focused tests and confirm green.

Run:

~~~powershell
python -m unittest tests.m3.test_deck_manifest -v
~~~

Expected: parser, count, duplicate, and hash tests pass.

---

## Task 3: Extend runtime deck loading without regressing legacy fixtures

**Files:**
- Modify: include/ygo/core/rules_bundle.hpp
- Modify: src/core/rules_bundle.cpp
- Modify: include/ygo/core/core_host.hpp
- Modify: src/core/core_host.cpp
- Modify: tools/prepare_card_data.py
- Modify: CMakeLists.txt
- Create: tests/m3/runtime/deck_loader_test.cpp

- [ ] Step 1: Add failing tests for YDK main/Extra loading.

Assert that the new loader returns exactly 40 main and 15 Extra cards, preserves ordered passcodes and source hash, rejects a nonempty side section for the locked matchup, and leaves existing .deck fixtures accepted as main-only legacy inputs.

- [ ] Step 2: Run the focused test and confirm it fails.

Run:

~~~powershell
cmake --build --preset dev-windows-zig --target deck_loader_test --parallel
ctest --preset dev-windows-zig -R deck_loader_test --output-on-failure
~~~

Expected: target or API failure before implementation.

- [ ] Step 3: Implement section-aware loading and Extra Deck registration.

Call OCG_DuelNewCard with LOCATION_EXTRA for Extra Deck entries, retain source ordering for deterministic setup, and keep CoreHost::load_deck behavior compatible for M0/M1 main-only callers. Do not add private core access.

- [ ] Step 4: Register M3 decks in generated card-data dependencies.

Update the materializer to read both .deck and .ydk passcode sections, then regenerate the ignored TSV during the build. The generated callback data must include all selected M3 passcodes and preserve existing decoded Link/level conventions.

- [ ] Step 5: Run the focused and legacy tests.

Run:

~~~powershell
cmake --preset dev-windows-zig
cmake --build --preset dev-windows-zig --parallel
ctest --preset dev-windows-zig -R "deck_loader_test|core_lifecycle_test|m1_engine_fixture_test" --output-on-failure
~~~

Expected: all selected tests pass and the rules lock remains byte-identical.

---

## Task 4: Produce the deterministic 110-slot compatibility audit

**Files:**
- Create: tools/m3/scripts.py
- Create: tools/m3/audit.py
- Create: tools/m3/cli.py
- Create: fixtures/decks/ocgforge.matchup.swordsoul_salamangreat.v1.json
- Create: docs/m3/card_compatibility.json
- Create: docs/m3/CARD_COMPATIBILITY.md
- Modify: tests/m3/test_card_compatibility.py

- [ ] Step 1: Write failing audit tests.

Assert:

~~~python
manifest["main_deck_count"] == {"deck_a": 40, "deck_b": 40}
manifest["extra_deck_count"] == {"deck_a": 15, "deck_b": 15}
len(slot_rows) == 110
len(unique_passcodes) == expected_unique_count
all(row["cdb_row_exists"] for row in slot_rows)
all(row["script_resolution"]["load_result"] == "PASS" for effect cards)
~~~

Validate the status vocabulary exactly: PASS_STATIC_ONLY, PASS_ENGINE_PATH, PENDING_ENGINE_PATH, MISSING_CDB, MISSING_SCRIPT, SCRIPT_LOAD_FAILURE, ENGINE_FAILURE.

- [ ] Step 2: Run the tests and observe the missing audit output.

Run:

~~~powershell
python -m unittest tests.m3.test_card_compatibility -v
~~~

Expected: report/manifest loading failures.

- [ ] Step 3: Implement catalog and script resolution.

Read only .cache/rules_bundle/babelcdb/cards.cdb; resolve exact names, preserve all 110 slots, and report alternate exact-name rows. Validate type compatibility for Main versus Extra Deck, and include type, ATK, DEF, Level/Rank, Link rating/markers, Race, Attribute, and Pendulum scales where present. Resolve official, legitimate shared/core paths, and reject missing effect scripts; normal cards may be scriptless only when CDB metadata proves that no effect script is required.

- [ ] Step 4: Generate the manifest and inventories.

Run:

~~~powershell
python -m tools.m3.cli audit --deck-a fixtures/decks/swordsoul_tenyi_ml_v1.ydk --deck-b fixtures/decks/salamangreat_ml_v1.ydk --database .cache/rules_bundle/babelcdb/cards.cdb --scripts .cache/rules_bundle/cardscripts --lock third_party/rules_bundle.lock.json --manifest fixtures/decks/ocgforge.matchup.swordsoul_salamangreat.v1.json --json docs/m3/card_compatibility.json --markdown docs/m3/CARD_COMPATIBILITY.md
~~~

The command must fail nonzero for any missing CDB row, invalid zone type, missing required script, unstable ordering, or count/hash mismatch.

- [ ] Step 5: Run focused tests and verify the reports.

Run:

~~~powershell
python -m unittest tests.m3.test_deck_manifest tests.m3.test_card_compatibility -v
python -m tools.m3.cli audit --check-existing --deck-a fixtures/decks/swordsoul_tenyi_ml_v1.ydk --deck-b fixtures/decks/salamangreat_ml_v1.ydk --database .cache/rules_bundle/babelcdb/cards.cdb --scripts .cache/rules_bundle/cardscripts --lock third_party/rules_bundle.lock.json --manifest fixtures/decks/ocgforge.matchup.swordsoul_salamangreat.v1.json --json docs/m3/card_compatibility.json --markdown docs/m3/CARD_COMPATIBILITY.md
~~~

Expected: 110-slot audit passes with explicit per-slot and per-unique-card evidence.

---

## Task 5: Instantiate every unique card against the pinned public core

**Files:**
- Modify: include/ygo/core/core_host.hpp
- Modify: src/core/core_host.cpp
- Modify: src/core/script_reader.hpp
- Modify: src/core/script_reader.cpp
- Create: include/ygo/m3/fixture_harness.hpp
- Create: src/m3/fixture_harness.cpp
- Create: tests/m3/runtime/card_instantiation_test.cpp
- Modify: CMakeLists.txt

- [ ] Step 1: Write the failing C++ instantiation test.

Load both exact fixed decks, start a fresh pinned duel, place each unique selected code through the public fixture-card path, process until the card is accepted or the core reports a callback/script/query failure, and assert that every unique card has a non-error result. Assert effect-card codes never use the missing-script fallback.

- [ ] Step 2: Run it and capture the first real failure.

Run:

~~~powershell
cmake --build --preset dev-windows-zig --target card_instantiation_test --parallel
ctest --preset dev-windows-zig -R card_instantiation_test --output-on-failure
~~~

Expected initially: target/API failures or a concrete card-script/core diagnostic, not a fabricated pass.

- [ ] Step 3: Implement strict script requirements and the narrow runner.

Feed the compatibility audit’s required effect-code set into the callback context. Keep legitimate vanilla cards scriptless, but fail with a named SCRIPT_LOAD_FAILURE when an effect code requested by the pinned core cannot load. Record process calls, query failures, emitted message families, and core callback logs per code.

- [ ] Step 4: Run the instantiation test and refresh compatibility statuses.

Run:

~~~powershell
cmake --preset dev-windows-zig
cmake --build --preset dev-windows-zig --parallel
ctest --preset dev-windows-zig -R card_instantiation_test --output-on-failure
python -m tools.m3.cli audit --check-existing --deck-a fixtures/decks/swordsoul_tenyi_ml_v1.ydk --deck-b fixtures/decks/salamangreat_ml_v1.ydk --database .cache/rules_bundle/babelcdb/cards.cdb --scripts .cache/rules_bundle/cardscripts --lock third_party/rules_bundle.lock.json --manifest fixtures/decks/ocgforge.matchup.swordsoul_salamangreat.v1.json --json docs/m3/card_compatibility.json --markdown docs/m3/CARD_COMPATIBILITY.md
~~~

Any nonpassing required card remains PENDING and stops the later full-game gate.

---

## Task 6: Establish the observation-safe conformance runner

**Files:**
- Create: include/ygo/m3/conformance_policy.hpp
- Create: src/m3/conformance_policy.cpp
- Create: tests/m3/runtime/m3_harness_contract_test.cpp
- Create: tests/m3/runtime/m3_observation_consistency_test.cpp
- Modify: CMakeLists.txt

- [ ] Step 1: Write failing harness contract tests.

Test that the runner rejects MSG_RETRY and unknown interactive messages, never submits automatic responses, uses complete decoded candidate sets, pauses between continuation steps, builds both perspective observations, fails when a visible candidate locator is absent, and preserves redacted Xyz material entities and XYZ_MATERIAL edges.

- [ ] Step 2: Run the contract tests and observe missing APIs.

Run:

~~~powershell
cmake --build --preset dev-windows-zig --target m3_harness_contract_test m3_observation_consistency_test --parallel
ctest --preset dev-windows-zig -R "m3_harness_contract_test|m3_observation_consistency_test" --output-on-failure
~~~

- [ ] Step 3: Implement the runner around existing public APIs.

For each process message, decode the complete DecisionRequest, build the acting player’s observation, attach decision context, check every visible source/target candidate, and record the raw message family plus ordered semantic keys. Store only privacy-safe observation hashes and visible events in the trace. Fixture setup may use load_fixture_script; effect behavior must never be reimplemented in C++.

- [ ] Step 4: Run the harness tests and the entire existing suite.

Run:

~~~powershell
cmake --build --preset dev-windows-zig --parallel
ctest --preset dev-windows-zig --output-on-failure
python -m unittest discover -s tests/python -v
~~~

Expected: all existing M0/M1/M2 CTest tests and the Python suite remain green before targeted M3 mechanics are added.

---

## Task 7: Add the mechanics coverage and API-gap inventories before fixtures

**Files:**
- Create: docs/m3/mechanics_coverage.json
- Create: docs/m3/MECHANICS_COVERAGE.md
- Create: docs/m3/public_api_gaps.json
- Create: docs/m3/PUBLIC_API_GAPS.md
- Create: docs/m3/FIXED_MATCHUP.md

- [ ] Step 1: Write inventory schema tests.

Require every SS/SG/shared/battle row to contain fixture ID, card/path, status, engine evidence, protocol evidence, observation evidence, message families, and blocker text when status is pending. Require the known INDIVIDUAL_XYZ_MATERIAL_QUERY row with CONFIRMED_PINNED_PUBLIC_API_LIMITATION.

- [ ] Step 2: Implement deterministic inventory generation.

Use statuses ENGINE_VERIFIED, PROTOCOL_VERIFIED, OBSERVATION_VERIFIED, PENDING_ENGINE_FIXTURE, PUBLIC_API_LIMITATION, and OUT_OF_SCOPE_M3. SG-08 and SG-09 must explicitly retain the redacted-material limitation; no raw material code may enter an observation hash.

- [ ] Step 3: Run documentation-schema tests.

Run:

~~~powershell
python -m unittest tests.m3.test_card_compatibility -v
python -m tools.m3.cli validate-docs --docs docs/m3
~~~

---

## Task 8: Implement Swordsoul conformance fixtures in focused groups

**Files:**
- Create: fixtures/m3/swordsoul/ss01_moye_token.json through ss18_tenyi_links.json
- Create: setup scripts required by those descriptors under fixtures/m3/swordsoul/
- Create: focused C++ test modules under tests/m3/swordsoul/
- Modify: docs/m3/mechanics_coverage.json and docs/m3/MECHANICS_COVERAGE.md

- [ ] Step 1: Add red tests for SS-01 and SS-02.

Use deterministic pre-effect setup only. Assert Mo Ye’s real summon/reveal/token path, token type/level/attribute/race/ATK/DEF/controller/location, the actual Synchro material decision family, Chixiao field state, and correct material transitions.

- [ ] Step 2: Run SS-01/SS-02 and record actual message families.

Run the focused executables and require no MSG_RETRY, complete candidate domains, one final response per engine decision, and observation hashes for both perspectives. Record MSG_SELECT_CARD, MSG_SELECT_SUM, MSG_SELECT_UNSELECT_CARD, or the actual emitted alternative only after observing it.

- [ ] Step 3: Add SS-03 through SS-07.

Cover simultaneous Mo Ye/Chixiao triggers and ordering, Chixiao search and negate, Longyuan discard/token/level-10/burn path, and the actual Swordsoul-token Extra Deck restriction before and after it expires. Use ocgcore candidate sets for legality.

- [ ] Step 4: Add SS-08 through SS-11.

Cover Baxia multi-target return, Baxia destruction/revival, Blackout’s exactly-one-own-Wyrm plus exactly-two-opponent continuation, the banished Blackout trigger, and Heavenly Dragon Circle’s real cost/message family plus search result.

- [ ] Step 5: Add SS-12 through SS-18.

Cover Tenyi no-effect conditions, Ashuna restriction, Vishuda return, Adhara recovery, Chengying dynamic state, Qixing interaction, and Monk/Shaman Link procedures with real zone candidates.

- [ ] Step 6: Run every Swordsoul group before moving on.

Run:

~~~powershell
ctest --preset dev-windows-zig -R "m3_swordsoul" --output-on-failure
python -m tools.m3.cli validate-docs --docs docs/m3
~~~

Each verified row needs real pinned-core evidence. Any unsupported required decision or missing observation field remains pending and blocks later gates.

---

## Task 9: Implement Salamangreat conformance fixtures in focused groups

**Files:**
- Create: fixtures/m3/salamangreat/sg01_fire_balelynx.json through sg21_link_zone.json
- Create: setup scripts required by those descriptors under fixtures/m3/salamangreat/
- Create: focused C++ test modules under tests/m3/salamangreat/
- Modify: docs/m3/mechanics_coverage.json, docs/m3/MECHANICS_COVERAGE.md, and docs/m3/public_api_gaps.json

- [ ] Step 1: Add red tests for SG-01 through SG-04.

Cover Salamangreat of Fire to Balelynx, Balelynx Sanctuary search, Field Zone state, and the actual Sanctuary-enabled same-name reincarnation Link procedure. Do not label a normal Link Summon as reincarnation.

- [ ] Step 2: Add SG-05 through SG-07.

Cover Gazelle optional hand summon, multiple valid Deck-to-GY candidates, and legitimate Spinny/Foxy/Jack Jaguar/Weasel/Falco graveyard paths.

- [ ] Step 3: Add SG-08 through SG-10.

Cover Miragestallio Xyz legality, material count/rank/typed redacted relationships, detach cost and Deck summon, and the FIRE restriction before/after the effect. Mark exact detached-material identity as observation-limited by the pinned public API.

- [ ] Step 4: Add SG-11 through SG-15.

Cover Sunlight Wolf linked-zone recovery, Spell/Trap recovery, Raging Phoenix normal/reincarnation/search, Pyro Phoenix reincarnation payoff, and Heatleo target/reincarnation behavior.

- [ ] Step 5: Add SG-16 through SG-21.

Cover Roar negate/recovery, Rage target-count/removal, Promethean Princess restriction/revival/destruction, Hiita opponent-owned FIRE revival with owner/controller projection, multi-material Link continuation PICK/PICK/FINISH, and complete Link-zone candidate placement.

- [ ] Step 6: Run every Salamangreat group.

Run:

~~~powershell
ctest --preset dev-windows-zig -R "m3_salamangreat" --output-on-failure
python -m tools.m3.cli validate-docs --docs docs/m3
~~~

Do not claim observation verification for individual Xyz material identity.

---

## Task 10: Add shared interaction and battle fixtures

**Files:**
- Create: fixtures/m3/interactions/*.json
- Create: tests/m3/interactions/handtrap_interaction_test.cpp
- Create: tests/m3/interactions/called_by_grave_test.cpp
- Create: tests/m3/interactions/battle_conformance_test.cpp
- Modify: docs/m3/mechanics_coverage.json and docs/m3/MECHANICS_COVERAGE.md

- [ ] Step 1: Add red handtrap tests.

Exercise Ash Blossom, Effect Veiler, Infinite Impermanence hand/set paths, Ghost Belle graveyard interaction, optional chain decline, valid/invalid timing, target selection, negation, and observation/history changes.

- [ ] Step 2: Add Called by the Grave.

Run one representative chain that targets a relevant handtrap or graveyard monster; assert chain order, target candidates, negation/banish result, and visible events.

- [ ] Step 3: Add real Battle Phase scenarios.

Use both fixed decks to cover attack declaration, attack targets, direct attack, battle destruction, damage calculation, LP updates, multiple attacks where available, lethal detection, and terminal MSG_WIN. The driver may choose only engine-provided legal attacks; it must not evaluate battle outcomes itself.

- [ ] Step 4: Run shared tests and privacy checks.

Run:

~~~powershell
ctest --preset dev-windows-zig -R "m3_interactions" --output-on-failure
~~~

Update coverage only from evidence emitted by the runner.

---

## Task 11: Close candidate/observation and hidden-information regressions

**Files:**
- Create: tests/m3/runtime/real_deck_privacy_test.cpp
- Create: tests/m3/runtime/real_deck_candidate_observation_test.cpp
- Modify: docs/m3/M3_ACCEPTANCE_MATRIX.md

- [ ] Step 1: Add paired-world red tests.

Change only hidden opponent handtrap identity, hidden set-card identity, or hidden Extra Deck composition and assert the observing player’s canonical observation bytes/hashes remain equal. Reveal/activate the card and assert the appropriate observations diverge.

- [ ] Step 2: Add visible candidate consistency checks.

Assert source/target locators for Synchro, Link, graveyard, banished, Blackout, and Hiita decisions resolve in the acting player’s current observation. Keep the documented Xyz-material exception only for unproven individual identity.

- [ ] Step 3: Run the regression tests.

Run:

~~~powershell
cmake --build --preset dev-windows-zig --parallel
ctest --preset dev-windows-zig -R "m3_.*privacy|m3_.*candidate" --output-on-failure
~~~

---

## Task 12: Implement the test-only full-game policy and readiness gate

**Files:**
- Modify: include/ygo/m3/conformance_policy.hpp
- Modify: src/m3/conformance_policy.cpp
- Create: tests/m3/full_game/full_game_readiness_test.cpp
- Modify: docs/m3/M3_ACCEPTANCE_MATRIX.md

- [ ] Step 1: Add the readiness gate test.

Refuse to start full games unless compatibility, required-card instantiation, all mandatory mechanics rows, and required decision-family coverage are green. Refuse to continue when a required row is pending.

- [ ] Step 2: Implement deterministic policy selection.

Use only PlayerObservation, DecisionRequest, complete ActionCandidate arrays, continuation state, and fixture ID. Preferences may choose required conformance paths, but tie-breaking must be semantic-key ordered and independent of hidden state, pointers, map order, time, and random fallback.

- [ ] Step 3: Run readiness tests.

Run:

~~~powershell
ctest --preset dev-windows-zig -R full_game_readiness_test --output-on-failure
~~~

The test must remain fail-closed while any required mechanic is pending.

---

## Task 13: Run the 16-game fixed-deck gate

**Files:**
- Create: tests/m3/full_game/full_fixed_deck_test.py
- Create: artifacts/m3/full_games/ locally
- Modify: tools/ygo_m3_probe/main.cpp
- Modify: docs/m3/M3_ACCEPTANCE_MATRIX.md

- [ ] Step 1: Define eight deterministic seed bundles and mirrored seats.

Use two starting-player assignments for each seed, real shuffled exact 40-card Main Decks, exact 15-card Extra Decks, and the locked duel flags/format metadata. Do not use manual board setup in this gate.

- [ ] Step 2: Run the full-game driver.

For every game record seed, deck hashes, starting player, winner/reason, turns, decisions, continuation steps, candidate maxima/mean, process calls, observation entities/events, unsupported count, retry count, core-error count, terminal status, and semantic gameplay hash.

- [ ] Step 3: Enforce acceptance conditions.

Reject any game with timeout, crash, MSG_RETRY, unsupported required decision, automatic decision, candidate truncation, invalid response, observation privacy violation, or engine divergence. If any required card path fails, keep final status M3 FINAL ACCEPTANCE PENDING.

- [ ] Step 4: Run and summarize the game gate.

Run:

~~~powershell
python tests/m3/full_game/full_fixed_deck_test.py --probe build/windows-zig/ygo_m3_probe.exe --games 16 --output artifacts/m3/full_games
~~~

Expected for a final pass: 16 terminal games and zero counts for all rejection categories.

---

## Task 14: Verify repeated/independent-process determinism and internal replay

**Files:**
- Create: tests/m3/determinism/full_game_determinism_test.py
- Create: tests/m3/determinism/action_trace_replay_test.py
- Modify: tools/ygo_m3_probe/main.cpp
- Create: artifacts/m3/determinism/ locally

- [ ] Step 1: Add same-process repeat tests.

Run at least two accepted seeds twice in one process and compare semantic gameplay hash, terminal result, decision sequence, ordered candidate keys, selected keys, and observation hashes.

- [ ] Step 2: Add independent-process repeat tests.

Launch the probe separately for the same seed/deck/start-player tuple and compare the same semantic fields. Do not compare undocumented artifact timestamps or process-specific paths.

- [ ] Step 3: Add internal semantic-action replay.

Read one recorded trace containing seed, deck hashes, ordered candidate semantic keys, and selected semantic keys; start a fresh duel and submit only the recorded semantic choices through freshly decoded candidate domains. Require identical observations, candidates, selected actions, and terminal result.

- [ ] Step 4: Run the determinism suite.

Run:

~~~powershell
python -m unittest tests.m3.determinism.full_game_determinism_test tests.m3.determinism.action_trace_replay_test -v
~~~

---

## Task 15: Close documentation and the M3 acceptance matrix

**Files:**
- Modify: docs/m3/M3_ACCEPTANCE_MATRIX.md
- Modify: docs/m3/CARD_COMPATIBILITY.md
- Modify: docs/m3/MECHANICS_COVERAGE.md
- Modify: docs/m3/FIXED_MATCHUP.md
- Modify: docs/m3/PUBLIC_API_GAPS.md
- Create/refresh: docs/m3/*.json machine-readable inventories
- Modify: README.md only for the verified M3 scope statement

- [ ] Step 1: Generate the acceptance matrix from evidence.

Include starting HEAD, final HEAD, branch, worktree status, files added/modified, bundle ID, deck IDs/counts/hashes/unique counts, all 110 slot statuses, missing scripts/CDB rows, every SS/SG/shared/battle fixture with message families and engine/observation outcomes, full-game counts, determinism hashes, replay result, privacy/candidate checks, CTest/Python counts, rules-lock result, diff-check result, hosted CI NOT RUN, known limitations, and PASS/PENDING/NOT APPLICABLE for every criterion.

- [ ] Step 2: Run documentation validation.

Run:

~~~powershell
python -m tools.m3.cli validate-docs --docs docs/m3
~~~

Require no stale hash, count, status, or unreferenced fixture IDs.

---

## Task 16: Full local regression and final evidence audit

**Files:**
- No new source files; verify all M3 artifacts

- [ ] Step 1: Run the required Windows build and tests.

Run:

~~~powershell
cmake --preset dev-windows-zig
cmake --build --preset dev-windows-zig --parallel
ctest --preset dev-windows-zig --output-on-failure
python -m unittest discover -s tests/python -v
python -m unittest discover -s tests/m3 -v
python tools/verify_rules_bundle.py --lock third_party/rules_bundle.lock.json --cache .cache/rules_bundle
git diff --check
~~~

- [ ] Step 2: Verify the rules lock did not change.

Run:

~~~powershell
git diff -- third_party/rules_bundle.lock.json
~~~

Expected: no output.

- [ ] Step 3: Verify no forbidden mutation occurred.

Run:

~~~powershell
git status --short --branch
git diff --name-status
~~~

Confirm no commits, pushes, tags, PRs, Linux CI, ocgcore/CardScripts/BabelCDB edits, deck substitutions, or hidden-card tracking changes.

- [ ] Step 4: Return the evidence-backed final recommendation.

Return M3 FINAL PASS only if every required acceptance criterion has fresh passing evidence. Otherwise return M3 FINAL ACCEPTANCE PENDING with exact blockers and the last passing stage; do not describe pending mechanics as verified.

---

## Plan self-review

- Spec coverage: Tasks 2–5 cover exact decks, hashes, 110-slot CDB/CardScripts audit, metadata, and unique-card instantiation. Tasks 6–11 cover protocol, observation, all Swordsoul/Salamangreat/shared/battle fixture groups, privacy, candidate consistency, and the known Xyz limitation. Tasks 12–14 cover policy, 16 real full games, determinism, and replay. Tasks 15–16 cover API gaps, documentation, regression, rules lock, diff-check, and final reporting.
- No placeholders: Every task names concrete files, tests, commands, and expected outcomes. A pending fixture is an explicit acceptance state, not an unfinished implementation placeholder.
- Type consistency: FixtureDeck owns main_deck, extra_deck, and sha256; the Python manifest mirrors those fields. ConformancePolicy consumes only PlayerObservation and DecisionRequest; FixtureHarness owns engine execution and evidence. The same fixture IDs flow into descriptors, coverage JSON, traces, and the acceptance matrix.
- Scope: The plan keeps the user’s full M3 target while splitting work into independently testable compatibility, harness, fixture, full-game, determinism, and documentation slices. No training policy, ML code, search, deck mutation, or upstream patch is introduced.
