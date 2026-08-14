# OCGForge M2 Player Observation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a deterministic, perspective-safe PlayerObservation, filtered visible event history, static match context, mechanics projections, decision integration, and adversarial Windows verification over the unchanged pinned OCGForge M1 baseline.

**Architecture:** Keep CoreHost as a raw, read-only OCG C API owner and put all semantic projection in focused observation modules. Decode pinned query/message bytes into internal snapshots, apply explicit perspective/privacy rules, emit a variable-length semantic observation, and hash its canonical UTF-8 JSON serialization. Keep the existing player-view-v1 and M1 trace semantics compatible while extending ygo.engine_trace.v2 decision records with safe observation metadata.

**Tech Stack:** C++17, CMake/Ninja, pinned OCG API 11.0, repository-local Zig Windows fallback, Python 3 standard library tests and coverage-inventory validation, SHA-256 already provided by ygo::trace.

---

## File map

Create these focused public headers and implementations:

- include/ygo/observation/observed_zone.hpp — semantic zones, positions, locators, and zone counts.
- include/ygo/observation/observed_card.hpp — optional static/current card fields, redaction, counters, and link markers.
- include/ygo/observation/relationship.hpp — safe Xyz, equip, and target edges.
- include/ygo/observation/chain_state.hpp — public chain links and safe source/target references.
- include/ygo/observation/visible_event.hpp — filtered event kinds/payloads and event session state.
- include/ygo/observation/match_context.hpp — MatchKnowledgeConfig, static deck context, and policy metadata.
- include/ygo/observation/player_observation.hpp — top-level semantic contract and decision context.
- include/ygo/observation/observation_builder.hpp — read-only builder/session API and candidate resolution.
- include/ygo/observation/serialization.hpp — canonical serialization and observation hashing API.
- src/observation/query_decoder.cpp and src/observation/query_decoder.hpp — pinned query-field and card-query byte decoding.
- src/observation/card_projection.cpp — catalog/current field projection and typed redaction.
- src/observation/zone_projection.cpp — pinned slot-to-semantic-zone mapping and counts.
- src/observation/relationship_projection.cpp — overlay/equip/target graph projection.
- src/observation/chain_projection.cpp — query-field chain projection.
- src/observation/event_projection.cpp — framed engine-event decoder and visible payload filter.
- src/observation/observation_builder.cpp — builder/session orchestration.
- src/observation/serialization.cpp — canonical JSON and SHA-256.

Modify the existing boundary files:

- include/ygo/core/core_host.hpp and src/core/core_host.cpp — expose copied static card metadata and safe fixture-script loading without exposing engine internals.
- src/core/card_data_reader.hpp and src/core/card_data_reader.cpp — return copied catalog records.
- tools/prepare_card_data.py — decode pinned CDB packed Pendulum scales and Link markers into OCG_CardData fields.
- include/ygo/trace/engine_trace.hpp and src/trace/engine_trace.cpp — add perspective-safe observation metadata to v2 records and semantic hash input.
- CMakeLists.txt — compile the observation library, generated M2 fixture data, tests, and probe.
- README.md, docs/contracts/player-observation-v1.md, docs/contracts/engine-trace-v2.md — document the public contracts and scope honesty.

Create tests and fixtures:

- tests/observation/observation_contract_test.cpp — explicit optionals, enum serialization, and hash input.
- tests/observation/query_decoder_test.cpp — exact pinned query buffer parsing and malformed input rejection.
- tests/observation/privacy_projection_test.cpp — paired-world non-interference, own visibility, face-down redaction, and deck-order invariance.
- tests/observation/mechanics_projection_test.cpp — real pinned Link/Xyz/Fusion/Synchro/Pendulum/counter/equip fixtures.
- tests/observation/event_projection_test.cpp — visible events, chain history, randomization boundaries, and event redaction.
- tests/observation/decision_consistency_test.cpp — decision context and candidate/observation locator resolution.
- tests/observation/observation_determinism_test.py — repeated and independent-process canonical observation hashes.
- tests/observation/coverage_inventory_test.py — coverage JSON schema and documentation consistency.
- tests/observation/fixtures/m2_setup.lua — repository-owned deterministic setup script loaded through OCG_LoadScript; it does not alter the pinned rules bundle.
- tools/ygo_observation_probe/main.cpp — fixture/seed/player/output diagnostic CLI.
- docs/observation/OBSERVATION_FIELD_COVERAGE.md and docs/observation/observation_field_coverage.json — field-by-field authority and visibility inventory.
- docs/observation/EVENT_COVERAGE.md and docs/observation/event_coverage.json — event family coverage inventory.

---

### Task 1: Add the semantic observation contract and canonical serializer

Files:
- Create the public observation headers listed above.
- Create src/observation/serialization.cpp and src/observation/serialization.hpp.
- Create tests/observation/observation_contract_test.cpp.
- Modify CMakeLists.txt to compile the new source and test.

- [ ] Step 1: Write the failing contract test.

The first test constructs a variable-length observation with a known card, one
redacted card, one relationship, a null optional, and one visible event. It
asserts explicit null, stable enum names, ordered arrays, no fixed-capacity
fields, and a 64-character SHA-256 hash.

    TEST_CASE("canonical observation preserves explicit absence and ordering") {
        ygo::observation::PlayerObservation observation;
        observation.schema_version = "ygo.player_observation.v1";
        observation.perspective_player = 0;
        observation.entities.push_back(make_known_fixture_card(2));
        observation.entities.push_back(make_redacted_fixture_card());
        observation.relationships.push_back({
            ygo::observation::RelationshipKind::XyzMaterial,
            observation.entities[1].locator,
            observation.entities[0].locator,
        });
        observation.globals.turn_count = std::nullopt;
        observation.visible_events.push_back(make_shuffle_event(7));

        const auto bytes = ygo::observation::canonical_serialize(observation);
        require(bytes.find("\"turn_count\":null") != std::string::npos,
                "absent global was not serialized as null");
        require(bytes.find("XYZ_MATERIAL") != std::string::npos,
                "relationship enum was not canonical");
        require(ygo::observation::observation_hash(observation).size() == 64,
                "observation hash is not SHA-256 hex");
        require(bytes.find("max_cards") == std::string::npos &&
                    bytes.find("max_entities") == std::string::npos,
                "authoritative observation contains a fixed tensor cap");
    }

- [ ] Step 2: Run the focused test and verify the expected missing-contract failure.

Run:

    cmake --preset dev-windows-zig
    cmake --build --preset dev-windows-zig --parallel
    ctest --preset dev-windows-zig -R observation_contract_test --output-on-failure

Expected: compile/test failure because ygo::observation and the serializer do not exist.

- [ ] Step 3: Implement the minimal typed contract.

Use std::optional for absent values, vectors for all collections, and stable
string enum encodings. Define ObservationLocator from current public semantic
coordinates only; never store pointers, field IDs, or opaque core addresses.
Put the observation hash outside the hash input and serialize all object keys
in one fixed order.

- [ ] Step 4: Run the focused test and then the M1 regression suite.

Run the focused command, then:

    cmake --build --preset dev-windows-zig --parallel
    ctest --preset dev-windows-zig --output-on-failure
    python -m unittest discover -s tests/python -v

Expected: the new test and all existing tests pass.

- [ ] Step 5: Refactor only while the focused tests remain green.

Keep serialization field order in one function and reuse ygo::trace::sha256_string.

---

### Task 2: Expose the static catalog and correct derived card-data decoding

Files:
- Modify tools/prepare_card_data.py.
- Modify src/core/card_data_reader.hpp and src/core/card_data_reader.cpp.
- Modify include/ygo/core/core_host.hpp and src/core/core_host.cpp.
- Create tests/observation/card_catalog_test.cpp.

- [ ] Step 1: Write the failing catalog test.

Assert that a real pinned Link card has link_rating from the low-byte CDB
level and link_marker from the CDB DEF field, that a real Pendulum card has
low-byte level plus packed left/right scales, and that an ordinary card keeps
ordinary DEF. Assert CoreHost::static_card_data returns a copy with no raw
pointer ownership.

- [ ] Step 2: Verify the test fails against the current generated-data path.

Run:

    ctest --preset dev-windows-zig -R card_catalog_test --output-on-failure

Expected: failure showing Link markers/scales are zero or packed into the wrong field.

- [ ] Step 3: Implement the smallest derived-data correction.

In tools/prepare_card_data.py use:

    TYPE_LINK = 0x04000000
    TYPE_PENDULUM = 0x01000000
    raw_level = int(level)
    decoded_level = raw_level & 0xFF
    lscale = (raw_level >> 16) & 0xFF if card_type & TYPE_PENDULUM else 0
    rscale = (raw_level >> 24) & 0xFF if card_type & TYPE_PENDULUM else 0
    link_marker = int(defense) if card_type & TYPE_LINK else 0
    decoded_defense = 0 if card_type & TYPE_LINK else int(defense)

Write those values in the existing generated TSV shape. Add a copied
StaticCardData value type and a CoreHost::static_card_data(code) lookup.

- [ ] Step 4: Run the focused catalog test and regenerate fixture data.

Run:

    cmake --build --preset dev-windows-zig --parallel
    ctest --preset dev-windows-zig -R card_catalog_test --output-on-failure

Expected: PASS with real pinned card metadata.

- [ ] Step 5: Run all current regressions.

Run the full CTest and Python baseline commands. Do not modify the lock file or
any .cache/rules_bundle checkout.

---

### Task 3: Decode pinned query snapshots and map physical slots to semantic zones

Files:
- Create src/observation/query_decoder.hpp and src/observation/query_decoder.cpp.
- Create src/observation/zone_projection.cpp.
- Create tests/observation/query_decoder_test.cpp and tests/observation/zone_projection_test.cpp.
- Keep all reads routed through CoreHost::query, query_location, query_field, and query_count.

- [ ] Step 1: Write failing raw-buffer tests.

Build exact little-endian buffers matching the pinned OCG_DuelQueryField,
OCG_DuelQueryLocation, and card::get_infos layouts. Test every supported query
field, null slot marker, counters payload, link payload, overlay count, and
malformed/truncated input. Assert query flags remain engine-native numeric
values until semantic projection.

- [ ] Step 2: Run the focused tests and observe missing decoder failures.

Run:

    ctest --preset dev-windows-zig -R "query_decoder_test|zone_projection_test" --output-on-failure

Expected: failure because the query decoder and zone projection do not exist.

- [ ] Step 3: Implement exact bounded readers.

Use a cursor with explicit remaining-byte checks. Decode the query location prefix,
per-card length/flag/value records, QUERY_TARGET_CARD, QUERY_EQUIP_CARD,
QUERY_OVERLAY_CARD, QUERY_COUNTERS, QUERY_OWNER, QUERY_IS_PUBLIC,
QUERY_LSCALE, QUERY_RSCALE, QUERY_LINK, and QUERY_IS_HIDDEN. Reject invalid
lengths and trailing records instead of guessing.

Map pinned locations as follows: LOCATION_MZONE to MONSTER_ZONE,
LOCATION_SZONE to SPELL_TRAP_ZONE unless the sequence is the pinned FZONE or
PZONE slot, LOCATION_GRAVE to GRAVEYARD, LOCATION_REMOVED to BANISHED,
LOCATION_EXTRA to EXTRA_DECK, LOCATION_HAND to HAND, and LOCATION_DECK to
MAIN_DECK. Use LOCATION_PZONE, LOCATION_FZONE, LOCATION_STZONE, LOCATION_MMZONE,
and LOCATION_EMZONE only as semantic helpers; do not fabricate physical zones.

- [ ] Step 4: Run focused tests and M1 regressions.

Expected: all query/zone tests pass and all 22 existing CTest tests plus the
three Python tests remain green.

---

### Task 4: Implement perspective filtering, entity projection, and match context

Files:
- Implement src/observation/card_projection.cpp, privacy_filter.cpp,
  zone_projection.cpp, and observation_builder.cpp.
- Implement public types in observed_card.hpp, observed_zone.hpp,
  match_context.hpp, player_observation.hpp, and observation_builder.hpp.
- Create tests/observation/privacy_projection_test.cpp.

- [ ] Step 1: Write failing paired-world and positive-visibility tests.

Cover these assertions:

    require(observe(world_a, 0).bytes == observe(world_b, 0).bytes,
            "hidden opponent hand changed Player 0 observation");
    require(observe(world_a, 0).hash == observe(world_b, 0).hash,
            "hidden opponent hand changed Player 0 observation hash");
    require(observe(revealed_a, 0).bytes != observe(revealed_b, 0).bytes,
            "public reveal did not change observation");
    require(no_current_deck_entities(observe(world_a, 0)),
            "current deck order leaked");
    require(redacted_face_down_has_no_identity_features(observe(world_a, 0)),
            "face-down card retained identity-derived features");
    require(own_hand_identity_is_visible(observe(world_a, 0)),
            "own hand was over-redacted");

Add separate worlds for hidden hand identity, deck order, hidden Extra Deck,
face-down field identity, and a mirrored Player 1 perspective.

- [ ] Step 2: Run the privacy test before production implementation.

Run:

    ctest --preset dev-windows-zig -R privacy_projection_test --output-on-failure

Expected: failure because the builder and semantic projection do not exist.

- [ ] Step 3: Implement MatchKnowledgeConfig and snapshot projection.

Default own_decklist_known=true and opponent_decklist_known=false. Store deck
lists as static sorted multisets in match_context; never put their current order
in zones or entities. Include duel flags, seat, policy booleans, and deck-list
hashes/contents according to the explicit policy.

Project counts for every pinned zone. Project entities only when identity is
legitimately visible: owner/controller card for the acting player, public
face-up/public cards, and own Extra Deck cards. Omit hidden deck/hand entries.
For a hidden face-down entity retain only safe position/zone/controller facts;
set identity_known=false, passcode=null, and omit type, stats, levels, ranks,
link fields, scales, counters that could be identity-derived, and script/effect
identifiers. Represent absent values as null.

Use a locator derived from current semantic coordinates. Never copy engine field
IDs, pointers, object addresses, or raw opaque IDs. Public collection ordering
is serialization-only and documented as not a player-known order.

- [ ] Step 4: Run privacy tests and verify hashes.

Expected: paired worlds match byte-for-byte and hash-for-hash; own information
and public reveals differ exactly in the fields listed by the visibility policy.

- [ ] Step 5: Run the M1 regression suite.

Run full CTest and Python tests before beginning mechanics work.

---

### Task 5: Add relationships and mechanics state projections

Files:
- Implement relationship.hpp, relationship_projection.cpp, chain_state.hpp,
  and card feature fields.
- Add CoreHost::load_fixture_script and fixture script support.
- Create tests/observation/mechanics_projection_test.cpp.
- Add M2 fixture card list and CMake dependency inputs.

- [ ] Step 1: Write failing real-core fixture assertions.

For each setup fixture, query actual CoreHost and assert:

- Fusion: real Fusion card on field retains Fusion type and current stats.
- Synchro: real Synchro card retains Level/current combat state and zone.
- Xyz: real Xyz card exposes Rank, ATK/DEF, overlay count, and one explicit
  XYZ_MATERIAL relationship; material identity is null unless proven public.
- Link: real Link card exposes link rating, typed marker bits, ATK, and no
  fabricated DEF.
- Pendulum: real Pendulum card exposes decoded scales and PZONE semantic
  placement; a face-up Pendulum in Extra is public to the opponent and a
  face-down Extra card is not.
- Counters/equip/targets: typed counter collections and visible relationships
  are represented without raw engine IDs.

- [ ] Step 2: Run the mechanics test before implementing setup/projection.

Run:

    ctest --preset dev-windows-zig -R mechanics_projection_test --output-on-failure

Expected: fixture/projection symbols are absent or assertions fail.

- [ ] Step 3: Implement pinned mechanics projection.

Use QUERY_OVERLAY_CARD for material count and overlay slots, and query each
material only to the extent visibility permits. Use QUERY_EQUIP_CARD,
QUERY_TARGET_CARD, and QUERY_COUNTERS to form typed edges/collections. Use
current engine QUERY_TYPE, QUERY_LEVEL, QUERY_RANK, QUERY_ATTACK,
QUERY_DEFENSE, QUERY_BASE_ATTACK, QUERY_BASE_DEFENSE, QUERY_ATTRIBUTE,
QUERY_RACE, QUERY_STATUS, QUERY_LSCALE, QUERY_RSCALE, and QUERY_LINK for
current state. Use CoreHost::static_card_data only for printed/base metadata.
Do not synthesize Level for Xyz or DEF for Link.

Load m2_setup.lua through the public OCG_LoadScript API before the duel starts.
Use Debug.AddCard/Duel.Overlay and pinned public setup helpers for deterministic
state; do not edit .cache/rules_bundle.

- [ ] Step 4: Run mechanics fixture tests and inspect canonical hashes.

Expected: each fixture prints a stable observation hash and assertions pass for
both perspectives where applicable.

- [ ] Step 5: Run all previous tests.

No M0/M1 test may be weakened or removed.

---

### Task 6: Implement chain state and perspective-filtered visible events

Files:
- Implement visible_event.hpp, event_projection.cpp, chain_projection.cpp,
  and ObservationSession in observation_builder.*.
- Create tests/observation/event_projection_test.cpp.
- Add docs/observation/EVENT_COVERAGE.md and docs/observation/event_coverage.json.

- [ ] Step 1: Write failing event and knowledge-boundary tests.

Feed framed pinned messages to a session and assert structured events for
turn/phase, movement, summon/set, draw count, shuffle, LP change, chain
activation/resolution, target/equip, counters, and win. Assert hidden codes
are omitted, a shuffle event survives, and a known card crossing into a hidden
randomized zone loses its prior locator.

- [ ] Step 2: Run focused event tests and confirm red failure.

Run:

    ctest --preset dev-windows-zig -R event_projection_test --output-on-failure

- [ ] Step 3: Implement bounded frame decoding and filtering.

Decode existing length-prefixed engine frames and store only structured
payloads. Support MSG_NEW_TURN, MSG_NEW_PHASE, MSG_WIN, MSG_MOVE, MSG_DRAW,
MSG_SHUFFLE_DECK, MSG_SHUFFLE_HAND, MSG_SHUFFLE_EXTRA, MSG_CONFIRM_DECKTOP,
MSG_CONFIRM_EXTRATOP, MSG_SET, summon messages, MSG_POS_CHANGE, MSG_LPUPDATE,
chain messages, MSG_CARD_TARGET, MSG_BECOME_TARGET, MSG_EQUIP, MSG_UNEQUIP,
MSG_ADD_COUNTER, MSG_REMOVE_COUNTER, and public reveal/move boundaries. Use
the current snapshot/privacy policy to decide whether an event code and
locator are visible. Never serialize raw frame bytes or localized text.

Maintain turn count, turn player, phase, terminal winner/reason, and monotonic
event index in the session. Expose only events since the prior observation
cursor; repeated observation calls do not consume or mutate the engine.

- [ ] Step 4: Write and validate event coverage inventories.

Each event family must have exactly one of SUPPORTED,
NOT_NEEDED_FOR_M2_FIXTURES, DEFERRED, or PRIVATE_INTERNAL, with a rationale
and implementation test reference. JSON and Markdown inventories must contain
the same family names.

- [ ] Step 5: Run event, privacy, and regression tests.

Expected: randomization boundaries remain visible without preserving hidden
identity, and all M0/M1 tests remain green.

---

### Task 7: Integrate typed decisions, candidate consistency, and trace v2

Files:
- Modify observation/player_observation.hpp and observation_builder.hpp for
  DecisionContext and locator resolution.
- Modify include/ygo/trace/engine_trace.hpp and src/trace/engine_trace.cpp.
- Create tests/observation/decision_consistency_test.cpp.
- Extend tests/trace/trace_semantics_test.cpp without removing M1 checks.

- [ ] Step 1: Write failing decision/trace tests.

Assert that a visible candidate source/target resolves to the matching
observation locator, an unobservable hidden candidate fails closed or uses an
explicit unknown locator, and an intermediate continuation retains the same
observation hash and engine step. Assert v2 trace JSON includes
perspective_player, observation_schema, and observation_hash, and that a
changed visible observation changes the semantic gameplay hash while raw
transport-only differences remain excluded.

- [ ] Step 2: Run focused tests and observe missing fields.

Run:

    ctest --preset dev-windows-zig -R "decision_consistency_test|trace_semantics_test" --output-on-failure

- [ ] Step 3: Implement safe decision context and v2 fields.

Add optional safe locator references derived from candidate coordinates; do not
copy complete candidate lists into card entities. Preserve existing trace
fields, continuation immobility, candidate counts, raw artifact hash, and
semantic gameplay hash behavior. Add per-step observation metadata and include
only observation hash/schema/perspective in the training-safe trace.

- [ ] Step 4: Run focused tests and all M1 tests.

Expected: existing trace consumers still parse v2, intermediate records remain
response-free, and observation changes are represented semantically.

---

### Task 8: Add the observation probe and canonical fixture artifacts

Files:
- Create tools/ygo_observation_probe/main.cpp.
- Add its CMake target and compile definitions.
- Create tests/observation/observation_determinism_test.py.
- Add fixture README files under fixtures/m2/.

- [ ] Step 1: Write the failing CLI/determinism test.

Invoke:

    build/windows-zig/ygo_observation_probe.exe --fixture m2.link --seed 1 --player 0 --output artifacts/m2/link-p0.json

Assert canonical JSON is valid, contains schema_version and observation_hash,
has no raw engine fields, and that two independent processes produce identical
bytes/hashes for the same fixture/seed/player.

- [ ] Step 2: Run the test before adding the tool.

Expected: executable/fixture is missing.

- [ ] Step 3: Implement fixture selection and output.

Support --fixture, --seed, --player, and --output. Emit only canonical
perspective-safe observation plus a concise stderr diagnostic. Use the same
builder/session path as library tests; do not create a second serializer.

- [ ] Step 4: Run repeated and independent-process tests.

Expected: observation bytes and hashes match across processes, while changing
seed changes only legitimately changed gameplay/observation state.

---

### Task 9: Add field coverage inventory and CI gates

Files:
- Create docs/observation/OBSERVATION_FIELD_COVERAGE.md and
  docs/observation/observation_field_coverage.json.
- Create tests/observation/coverage_inventory_test.py.
- Modify the Windows CI workflow under .github/workflows/.
- Modify CMakeLists.txt to register all stable M2 CTest tests.

- [ ] Step 1: Write the failing inventory validator.

Require every listed field to classify as exactly one of
EXPOSED_PUBLIC, EXPOSED_PRIVATE_TO_OWNER, REDACTED_WHEN_HIDDEN,
STATIC_CONTEXT, DERIVED, INTENTIONALLY_NOT_EXPOSED, or OUT_OF_SCOPE_M2,
with a nonempty rationale and source/test reference. Require event inventory
families to use the four event statuses from Task 6.

- [ ] Step 2: Run the validator and observe missing inventory failure.

Run:

    python tests/observation/coverage_inventory_test.py

- [ ] Step 3: Add complete conservative inventories.

Inventory all relevant pinned query fields, card fields, global fields,
relationships, zone semantics, and event families. Mark unproven visibility
rules PENDING in the final report rather than claiming unsupported exposure.

- [ ] Step 4: Register Windows-only CI gates.

Keep configure/build/CTest/Python M0/M1 gates and add observation contract,
privacy, mechanics, event, decision, determinism, and inventory tests. Do not
add Linux jobs or weaken existing failures.

- [ ] Step 5: Run the focused inventory test.

Expected: PASS with JSON/Markdown names and classifications in sync.

---

### Task 10: Full verification, audit, and final acceptance report

Files:
- Modify docs/contracts/player-observation-v1.md,
  docs/contracts/engine-trace-v2.md, README.md, and fixture READMEs only when
  the verified implementation differs from the claims already recorded there;
  every change must state the exact verified behavior.
- No commits, pushes, tags, PRs, or upstream changes.

- [ ] Step 1: Run clean Windows configure/build.

    cmake --preset dev-windows-zig
    cmake --build --preset dev-windows-zig --parallel

- [ ] Step 2: Run all CTest gates.

    ctest --preset dev-windows-zig --output-on-failure

Record exact counts and any PENDING tests separately.

- [ ] Step 3: Run Python unit and observation tests.

    python -m unittest discover -s tests/python -v
    python tests/observation/observation_determinism_test.py --probe build/windows-zig/ygo_observation_probe.exe
    python tests/observation/coverage_inventory_test.py

- [ ] Step 4: Verify pinned authority and worktree safety.

Run:

    git status --short --branch
    git rev-parse HEAD
    git diff --stat
    git diff --check
    python tools/verify_rules_bundle.py --lock third_party/rules_bundle.lock.json --cache .cache/rules_bundle

Confirm the starting HEAD remains recorded, the lock bundle ID and four
component revisions are unchanged, and no ignored build artifact is mistaken
for a source change.

- [ ] Step 5: Produce the final M2 acceptance matrix.

Report PASS/PENDING/NOT APPLICABLE for every pasted M2 criterion, including
privacy/adversarial tests, positive reveal, hidden deck order, hidden Extra
Deck, face-down field, anti-tracking, own private data, mechanics fixtures,
chain/counter evidence, candidate consistency, side-effect freedom, repeated
and independent-process observation hashes, semantic gameplay hash regression,
CTest, Python tests, CI status, diff check, and known limitations. Use M2
FINAL PASS only if every required criterion is proven; otherwise report M2
FINAL ACCEPTANCE PENDING with the exact evidence gap.
