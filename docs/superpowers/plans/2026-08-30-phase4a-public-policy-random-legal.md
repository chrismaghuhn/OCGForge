# Phase 4A Public Policy Substrate and RandomLegal Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (- [ ]) syntax for tracking.

**Goal:** Build the public-only Phase 4A substrate, production SHA-256-counter RandomLegal policy, provenance registration, and trusted trajectory integration without changing accepted gameplay, observation, or trajectory wire semantics.

**Architecture:** Refactor the existing safe-state parser and serializer around one typed const-only public view. Extract public Environment DTOs from the lifecycle header, expose a small ygo::policy selector seam, and keep V2 freshness and trajectory attribution in a separate runner. Reuse the existing SHA-256 and Phase-3A/3B identity codecs.

**Tech Stack:** C++17, CMake/Ninja, ygo::trace::sha256_bytes, CTest, Python unittest/subprocess checks, PowerShell acceptance scripts, and the pinned Windows rules bundle.

---

## Fixed base and constraints

- Branch: chris/phase4a-public-policy-random-legal.
- Exact base: d61824b8436598ed461f83b3b73c291c5420c191.
- Design commit: d4b035040c1040a1a2f7bbf2e5ada480b3c973f5.
- The seven files under docs/research/teacher_strategy/ are read-only input and must not be edited, copied, or regenerated.
- Do not implement TeacherCore, either StrategyProfile, model/tensor adapters, search, RL, self-play, wrappers, arbitrary-deck support, or cloud infrastructure.
- Do not change rules/decks, PlayerObservation v1, public observation bytes/digest, public action identity, EpisodicEnvironment V2, EngineTrace v2, or Phase-3A/3B canonical codecs.
- A policy-origin StepRejected stops and quarantines collection. It never retries, rewinds the policy RNG, or chooses another candidate.

## File map

Create:

- include/ygo/environment/public_decision.hpp
- include/ygo/environment/public_safe_state.hpp
- src/environment/public_safe_state.cpp
- include/ygo/policy/policy.hpp
- include/ygo/policy/rng.hpp
- src/policy/rng.cpp
- include/ygo/policy/random_legal.hpp
- src/policy/random_legal.cpp
- include/ygo/policy/runner.hpp
- src/policy/runner.cpp
- include/ygo/policy/production.hpp
- src/policy/production.cpp
- include/ygo/trajectory/production_provenance.hpp
- src/trajectory/production_provenance.cpp
- docs/contracts/policy-rng-sha256-counter-v1.md
- docs/p4a/P4A_PUBLIC_FACT_MATRIX.md
- tools/p4a/phase4a_clean_checkout_acceptance.ps1
- tests/environment/public_safe_state_test.cpp
- tests/policy/policy_boundary_test.py
- tests/policy/rng_test.cpp
- tests/policy/random_legal_test.cpp
- tests/policy/policy_runner_integration_test.cpp
- tests/policy/public_fact_matrix_test.py
- tests/policy/policy_determinism_test.py
- tools/policy_random_legal_probe/main.cpp

Build registration:

- Add `ygo_policy` as a static library containing the policy sources, link it publicly to `ygo_trajectory`, and link `ygo_trajectory` publicly to `ygo_m4` as today.
- Link policy tests and the policy probe to `ygo_policy`.
- Add `public_safe_state.cpp` to `ygo_m4` and the public safe-state test to CTest.

Modify only:

- include/ygo/environment/episodic_environment.hpp
- src/environment/public_environment_observation.cpp
- src/environment/public_environment_observation_decoder.cpp
- CMakeLists.txt

Generate:

- docs/p4a/p4a_acceptance.json
- docs/p4a/P4A_ACCEPTANCE.md

## Task 1: Verify the clean baseline

**Files:** none.

- [ ] Step 1: Verify branch ancestry and untouched main.

~~~powershell
git status --short --branch
git log -2 --oneline
git merge-base --is-ancestor d61824b8436598ed461f83b3b73c291c5420c191 HEAD
git -C C:\yogiohML status --short --branch
~~~

Expected: the implementation worktree has only the committed design file and the main checkout is clean.

- [ ] Step 2: Run the baseline build and CTest suite.

~~~powershell
cmake --preset dev-windows-zig
cmake --build --preset dev-windows-zig --parallel
ctest --preset dev-windows-zig --output-on-failure
~~~

Expected: every baseline command exits zero. Record the exact test count and keep any pre-existing failure separate.

## Task 2: Add one typed PublicSafeState codec authority

**Files:** include/ygo/environment/public_safe_state.hpp, src/environment/public_safe_state.cpp, tests/environment/public_safe_state_test.cpp, src/environment/public_environment_observation.cpp, src/environment/public_environment_observation_decoder.cpp, and CMakeLists.txt.

- [ ] Step 1: Write the RED test.

Construct a perspective-safe PlayerObservation with globals, zones, known and redacted entities, relationships, chain data, visible events, and match context. Assert valid decode, typed accessor values, and typed re-encode byte equality. A C++17 source guard in the same test reads the public header and asserts that the public visible-event type does not declare engine_step_index.

~~~cpp
const auto bytes = canonical_public_safe_state_bytes(source);
const auto decoded = decode_canonical_public_safe_state(bytes);
require(decoded, "valid safe-state bytes did not decode");
require(canonical_public_safe_state_bytes(*decoded.value) == bytes,
        "typed safe-state round-trip changed bytes");
require(decoded.value->globals().life_points == source.globals.life_points,
        "typed view changed public globals");
~~~

- [ ] Step 2: Run the RED test.

~~~powershell
cmake --build --preset dev-windows-zig --target public_safe_state_test
ctest --preset dev-windows-zig -R '^public_safe_state_test$' --output-on-failure
~~~

Expected: compilation fails because the typed view and target do not exist.

- [ ] Step 3: Implement PublicSafeStateView and its const accessors.

Expose const accessors for ObservedPlayerGlobals, ObservedZone values, ObservedCard values, Relationship values, ChainState, a dedicated PublicSafeVisibleEvent without engine_step_index, and MatchContext. Keep storage private and construction restricted to the codec implementation. Expose a decode result containing either a typed value or a diagnostic.

- [ ] Step 4: Move the existing safe-state serializer and validator into the shared source.

Copy only the fields encoded by ocgforge.public_safe_state.v1 from PlayerObservation. Convert the existing Cursor, property parser, target parser, and read_safe_state grammar into typed reads. Preserve exact field order, sort rules, enum codes, redaction checks, and trailing-byte rejection. The outer decoder must call this typed decoder rather than maintain a second parser.

- [ ] Step 5: Keep the old serializer overload byte-identical.

Implement canonical_public_safe_state_bytes(PlayerObservation) by constructing the typed view and invoking the typed encoder. The existing public observation serializer must continue to emit the same nested bytes and outer digest.

- [ ] Step 6: Add strict negative tests.

Reject truncation, trailing bytes, invalid presence/boolean bytes, invalid enum codes, unsorted entity/event records, duplicate locators/event indices, hidden identity fields, face-up plus face-down cards, and unknown static-deck identities. Mutation helpers parse the known layout and do not use undocumented byte offsets.

~~~powershell
cmake --build --preset dev-windows-zig --target public_safe_state_test public_action_identity_test
ctest --preset dev-windows-zig -R '^(public_safe_state_test|public_action_identity_test)$' --output-on-failure
~~~

Expected: new typed round-trip and negatives pass, and existing public-safe goldens remain unchanged.

- [ ] Step 7: Commit.

~~~powershell
git add include/ygo/environment/public_safe_state.hpp src/environment/public_safe_state.cpp src/environment/public_environment_observation.cpp src/environment/public_environment_observation_decoder.cpp tests/environment/public_safe_state_test.cpp CMakeLists.txt
git commit -m "feat: expose typed public safe-state view"
~~~

## Task 3: Isolate public candidate DTOs from lifecycle metadata

**Files:** include/ygo/environment/public_decision.hpp, include/ygo/environment/episodic_environment.hpp, include/ygo/policy/policy.hpp, tests/policy/policy_boundary_test.py, and CMakeLists.txt.

- [ ] Step 1: Write the RED dependency guard.

The guard reads public_decision.hpp and every selector-facing policy header. It fails on DecisionFrame, SubmissionToken, CoreHost, PlayerObservation, engine_step_index, semantic_key, raw_message, or response_bytes, and requires episodic_environment.hpp to include public_decision.hpp.

~~~powershell
python tests/policy/policy_boundary_test.py
~~~

Expected: failure before the extracted header exists.

- [ ] Step 2: Extract public DTO declarations verbatim.

Move EnvironmentDecisionKind, EnvironmentActionKind, EnvironmentActionCandidate, EnvironmentContinuationView, and EnvironmentDecisionRequest into public_decision.hpp. Leave all lifecycle types in episodic_environment.hpp. Do not change values or wire behavior.

- [ ] Step 3: Define the selector seam.

~~~cpp
struct PolicyInput final {
    const environment::PublicEnvironmentObservation& observation;
    const std::vector<environment::EnvironmentActionCandidate>& candidates;
};

struct PolicyRngCursorSpan final {
    std::string policy_rng_identity;
    std::string policy_rng_contract_identity;
    std::string policy_rng_stream_id;
    std::string policy_rng_initialization_identity;
    std::uint64_t pre_cursor = 0;
    std::uint64_t post_cursor = 0;
};

struct PolicySelectionResult final {
    std::string public_action_key;
    PolicyRngCursorSpan rng;
};
~~~

Add structured PolicyError and a PolicySelection result. No selector result contains a candidate-vector index.

- [ ] Step 4: Add a compile-only include test and commit.

~~~cpp
#include "ygo/environment/public_decision.hpp"
#include "ygo/environment/public_environment_observation.hpp"
#include "ygo/policy/policy.hpp"

int main() {
    return 0;
}
~~~

~~~powershell
cmake --build --preset dev-windows-zig --target policy_boundary_compile_test
python tests/policy/policy_boundary_test.py
git add include/ygo/environment/public_decision.hpp include/ygo/environment/episodic_environment.hpp include/ygo/policy/policy.hpp tests/policy/policy_boundary_test.py CMakeLists.txt
git commit -m "refactor: isolate public policy input seam"
~~~

Expected: the include test succeeds and the guard finds no forbidden selector dependency.

## Task 4: Implement and freeze the SHA-256-counter RNG

**Files:** rng header/source/test, RNG contract document, CMakeLists.txt.

- [ ] Step 1: Write the RED golden-vector tests.

Use root 0x0123456789abcdef, episode ID episode.v1. plus 64 a characters, assignment ID participant_policy_assignment.v1. plus 64 b characters, and stream ID player0. Freeze initialization material, initialization identity, block zero/one, all eight lanes, and cursor traces.

- [ ] Step 2: Implement exact initialization material.

~~~text
string ocgforge.policy_rng.sha256_counter.v1
string ocgforge.policy_rng.sha256_counter.v1
u64be explicit policy RNG root seed
string episode semantic ID
string participant policy assignment ID
string policy RNG stream ID
~~~

Reject empty/noncanonical stream tokens and empty semantic identifiers. Never derive this root from EpisodeSpec.root_seed.

- [ ] Step 3: Implement exact raw-word blocks.

~~~text
string ocgforge.policy_rng.sha256_counter.block.v1
string policy_rng_initialization_identity
u64be block index
~~~

Hash with ygo::trace::sha256_bytes, decode lowercase hex, interpret four consecutive eight-byte groups as big-endian u64, select cursor / 4 and cursor % 4, and increment once. Refuse consumption at UINT64_MAX; never wrap.

- [ ] Step 4: Implement rejection sampling.

~~~cpp
if (n == 0) return failure(PolicyErrorCode::EmptyCandidateDomain);
if (n == 1) return RngResult{0, std::nullopt};
const std::uint64_t threshold = static_cast<std::uint64_t>(-n) % n;
for (;;) {
    const auto raw = next_raw_u64();
    if (!raw) return raw;
    if (*raw.value >= threshold)
        return RngResult{*raw.value % n, std::nullopt};
}
~~~

The n == 1 path consumes zero words. Every rejection consumes one word.

- [ ] Step 5: Test exhaustion and stream separation.

Use a test-only friend to set a valid initialized RNG at UINT64_MAX; do not add a public reseed path. Assert no wrap, exact cursor spans, unchanged cursor for n == 1, and changed identities/first blocks for changed root, episode, assignment, or stream.

- [ ] Step 6: Document, verify, and commit.

~~~powershell
cmake --build --preset dev-windows-zig --target policy_rng_test
ctest --preset dev-windows-zig -R '^policy_rng_test$' --output-on-failure
git add include/ygo/policy/rng.hpp src/policy/rng.cpp tests/policy/rng_test.cpp docs/contracts/policy-rng-sha256-counter-v1.md CMakeLists.txt
git commit -m "feat: add SHA-256 counter policy RNG"
~~~

## Task 5: Register production provenance and RandomLegal

**Files:** production provenance and policy headers/sources, selector tests, CMakeLists.txt.

- [ ] Step 1: Write RED registry tests.

Require this exact typed mapping:

~~~text
ProducerImplementation -> ocgforge.policy.random_legal.v1
InferenceAdapter       -> ocgforge.policy.direct_execution.v1
ObservationAdapter     -> ocgforge.policy.public_observation.v1
ActionAdapter          -> ocgforge.policy.public_action_key.v1
SamplingContract       -> ocgforge.policy.uniform_below_u64.v1
PolicyRngContract      -> ocgforge.policy_rng.sha256_counter.v1
~~~

Sampling capabilities are complete=true and deterministic=false. The default resolver remains no-RNG-only. RandomLegal paired with no_policy_rng or deterministic sampling is rejected.

- [ ] Step 2: Implement the explicit production resolver.

Register each identity in exactly one category, add canonical-material and cursor-uniqueness callbacks to the RNG descriptor, and retain the no-policy-RNG entry. No production source may contain ocgforge.test. identities.

- [ ] Step 3: Implement artifact and participant setup.

Build one PolicyArtifact with PolicyKind::RandomLegal, the six production identities, no model/search/demonstration identity, and the existing computed artifact ID. Build epoch-zero participant assignments using the accepted Normal/Mirror seat-to-deck mapping. Build initialization and stream identities with explicit policy roots and stream IDs.

- [ ] Step 4: Write and implement RandomLegal RED/GREEN tests.

Use three valid public candidates in deliberate nonlexical order. Assert exact key membership, unchanged count/membership/order/domain digest, empty-domain failure, invalid-key failure, no candidate-zero fallback, and no result index.

~~~powershell
cmake --build --preset dev-windows-zig --target policy_rng_test random_legal_test trajectory_codec_test
ctest --preset dev-windows-zig -R '^(policy_rng_test|random_legal_test|trajectory_codec_test)$' --output-on-failure
python tests/policy/policy_boundary_test.py
rg -n 'ocgforge\.test\.' src/policy src/trajectory/production_provenance.cpp include/ygo/policy include/ygo/trajectory/production_provenance.hpp
git add include/ygo/trajectory/production_provenance.hpp src/trajectory/production_provenance.cpp include/ygo/policy/production.hpp src/policy/production.cpp include/ygo/policy/random_legal.hpp src/policy/random_legal.cpp tests/policy/rng_test.cpp tests/policy/random_legal_test.cpp CMakeLists.txt
git commit -m "feat: add production RandomLegal provenance and selector"
~~~

Expected: focused tests pass and production source has no test identity.

## Task 6: Integrate V2, TrajectoryRecorder, and admission

**Files:** runner header/source, integration test, CMakeLists.txt.

- [ ] Step 1: Write the RED integration test.

Create canonical V2 environment, production provenance with explicit policy roots, and TrajectoryRecorder with the production resolver. Run a bounded episode. Every accepted record must have Cursor mode, pre/post cursors, the exact acting assignment, and post_cursor >= pre_cursor.

- [ ] Step 2: Implement runner lifecycle.

The runner may receive DecisionFrame for lifecycle only. It constructs PolicyInput from frame.public_observation and frame.request.candidates, builds ActionSelection using the current token, calls environment.step, and passes accepted results to TrajectoryRecorder.

The runner alone adds decision index and assignment to the existing PolicyRngDecisionProvenance:

~~~cpp
attribution.decision_index = frame.decision_index;
attribution.acting_policy_assignment_id = assignment_id;
attribution.policy_rng_identity = selection.rng.policy_rng_identity;
attribution.policy_rng_contract_identity = selection.rng.policy_rng_contract_identity;
attribution.policy_rng_stream_id = selection.rng.policy_rng_stream_id;
attribution.policy_rng_initialization_identity =
    selection.rng.policy_rng_initialization_identity;
attribution.mode = trajectory::PolicyRngMode::Cursor;
attribution.pre_cursor = selection.rng.pre_cursor;
attribution.post_cursor = selection.rng.post_cursor;
~~~

- [ ] Step 3: Implement fail-closed rejection flow.

On StepRejected, call recorder.on_step_rejected(rejected, true), perform one administrative interruption, pass the unchanged pre-rejection frame to on_interrupt_accepted, seal the quarantined envelope, and never retry. On policy/RNG failure, return a structured policy error without submitting a key.

- [ ] Step 4: Verify the complete existing trajectory path and commit.

Build CandidateTrajectoryShard, sorted restricted RNG initialization evidence, verify admission, issue VerifiedAdmissionReceipt, and validate DatasetManifest through existing APIs. Quarantined envelopes must fail clean admission.

~~~powershell
cmake --build --preset dev-windows-zig --target policy_runner_integration_test trajectory_replay_admission_test
ctest --preset dev-windows-zig -R '^(policy_runner_integration_test|trajectory_replay_admission_test)$' --output-on-failure
git add include/ygo/policy/runner.hpp src/policy/runner.cpp tests/policy/policy_runner_integration_test.cpp CMakeLists.txt
git commit -m "feat: integrate RandomLegal with trusted trajectory admission"
~~~

## Task 7: Add P4A-G00 and privacy/determinism evidence

**Files:** public-fact matrix, matrix validator, probe, deterministic test, CMakeLists.txt.

- [ ] Step 1: Write the RED matrix validator.

Require rows for turn player, phase, life points, visible field/GY/banished cards, known hand, zone counts, card properties, chain/source/targets, public effect descriptions, event/summon/movement history, shuffle boundaries, once-per-turn history, candidate source/target/kind/amount/position/phase, and contract-known deck context. Each row has the requirement, exact public source, DIRECT, SAFE_DERIVATION, or BLOCKED, and executable evidence.

~~~powershell
python tests/policy/public_fact_matrix_test.py
~~~

Expected: failure before the matrix exists.

- [ ] Step 2: Write the matrix.

Map direct facts to PublicSafeStateView and V2 DTO fields. Mark hidden opponent hand/deck order, Foxy top-deck identity, persistent hidden identity, unavailable effect-use/restriction facts, omitted event families, and absent candidate metadata BLOCKED with exact missing-source reasons and anti-side-channel tests. Do not weaken a blocked research requirement.

- [ ] Step 3: Add independent-process and paired-world tests.

The probe accepts explicit policy root, episode ID, assignment ID, stream ID, and ordered public candidates, then emits only selected public keys and cursor spans. Equal public inputs in independent processes and paired hidden worlds must match. Interleaved episodes must equal isolated runs; a new episode starts at cursor zero.

- [ ] Step 4: Verify and commit.

~~~powershell
python tests/policy/public_fact_matrix_test.py
cmake --build --preset dev-windows-zig --target policy_random_legal_probe random_legal_test
python tests/policy/policy_determinism_test.py
ctest --preset dev-windows-zig -R '^random_legal_test$' --output-on-failure
git add docs/p4a/P4A_PUBLIC_FACT_MATRIX.md tests/policy/public_fact_matrix_test.py tools/policy_random_legal_probe/main.cpp tests/policy/policy_determinism_test.py CMakeLists.txt
git commit -m "test: prove Phase 4A privacy and determinism"
~~~

## Task 8: Generate and verify all P4A gates

**Files:** acceptance script, generated JSON/Markdown, CMakeLists.txt.

- [ ] Step 1: Generate evidence from commands.

Run focused CTest targets, policy Python tests, full CTest, existing Python verification, required M4 acceptance, and rules/deck identity checks. Write JSON first and derive Markdown. Record command, exit code, source SHA, preset, and generated hashes. PASS is legal only after the corresponding command exits zero; otherwise use FAIL, NOT_RUN, SKIPPED, or BLOCKED.

- [ ] Step 2: Run the full local verification.

~~~powershell
cmake --preset dev-windows-zig
cmake --build --preset dev-windows-zig --parallel
ctest --preset dev-windows-zig --output-on-failure
python -m unittest discover -s tests -p 'test_*.py' -v
python tools/verify_rules_bundle.py --lock third_party/rules_bundle.lock.json --cache .cache/rules_bundle
powershell -NoProfile -ExecutionPolicy Bypass -File tools/p4a/phase4a_clean_checkout_acceptance.ps1
~~~

The generated report contains P4A-G00 and P4A-G01 through P4A-G29 with exact statuses and no unexecuted PASS.

- [ ] Step 3: Verify a clean checkout.

Create a fresh temporary clone at the exact implementation head, materialize the pinned rules bundle with repository tooling, run the same script, and compare semantic outputs/evidence identities. Do not hand-edit generated evidence or copy build directories.

- [ ] Step 4: Commit generated evidence.

~~~powershell
git add tools/p4a/phase4a_clean_checkout_acceptance.ps1 docs/p4a/p4a_acceptance.json docs/p4a/P4A_ACCEPTANCE.md CMakeLists.txt
git commit -m "test: record Phase 4A acceptance evidence"
git status --short --branch
git diff d61824b8436598ed461f83b3b73c291c5420c191...HEAD --check
~~~

Final handoff reports starting SHA, branch/head SHA, files, production identities, semantic/internal changes, privacy/determinism/replay implications, executed commands, exact G00-G29 statuses, evidence hashes, limitations, blocked future Teacher facts, hosted CI, and PR URL/number. It does not self-declare final acceptance or merge readiness.

## Plan self-review

- One safe-state grammar preserves bytes and digests.
- Selector headers exclude lifecycle/private fields.
- Policy returns PolicyRngCursorSpan; the runner alone builds trajectory provenance.
- Initialization binds explicit policy root, episode, participant assignment, and stream.
- Rejection sampling, n == 1, and exhaustion semantics are explicit.
- Production registry categories are distinct and the default resolver stays unchanged.
- RandomLegal consumes the complete supplied domain and never falls back to candidate zero.
- Policy-origin rejection produces no record for the rejected action, quarantines, and never retries.
- Existing Phase-3A/3B schemas and codecs remain unchanged.
- P4A-G00 records blocked facts instead of using private state or weakening the future Teacher.
