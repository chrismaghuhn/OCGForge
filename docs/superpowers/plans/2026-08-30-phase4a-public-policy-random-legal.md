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
- include/ygo/observation/observed_player_globals.hpp
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
- include/ygo/policy/production_provenance.hpp
- src/policy/production_provenance.cpp
- docs/contracts/policy-rng-sha256-counter-v1.md
- docs/p4a/P4A_PUBLIC_FACT_MATRIX.md
- tools/p4a/phase4a_clean_checkout_acceptance.ps1
- tests/environment/public_safe_state_test.cpp
- tests/policy/policy_boundary_test.py
- tests/policy/rng_test.cpp
- tests/policy/random_legal_test.cpp
- tests/policy/policy_runner_integration_test.cpp
- tests/policy/public_fact_matrix_test.py
- tests/policy/rules_deck_identity_test.py
- tests/policy/policy_determinism_test.py
- tools/policy_random_legal_probe/main.cpp

Build registration:

- Add `ygo_policy` as a static library containing the policy sources, link it publicly to `ygo_trajectory`, and link `ygo_trajectory` publicly to `ygo_m4` as today. `ygo_trajectory` must not link back to `ygo_policy`.
- Link policy tests and the policy probe to `ygo_policy`.
- Add `public_safe_state.cpp` to `ygo_m4` and the public safe-state test to CTest.

Modify only:

- include/ygo/environment/episodic_environment.hpp
- include/ygo/environment/public_environment_observation.hpp
- include/ygo/observation/player_observation.hpp
- src/environment/public_environment_observation.cpp
- src/environment/public_environment_observation_decoder.cpp
- CMakeLists.txt

Generate:

- docs/p4a/p4a_acceptance.json
- docs/p4a/P4A_ACCEPTANCE.md

## Frozen P4A-G00 through P4A-G29 acceptance matrix

This table is frozen before implementation. The acceptance generator may record only the listed evidence and condition; it may not redefine a gate after implementation.

| Gate | Invariant | Owning layer | Exact executable evidence | PASS condition | Failure classification |
|---|---|---|---|---|---|
| P4A-G00 | Public-fact sufficiency matrix is complete for the planned Teacher scope | docs/environment/policy | tests/policy/public_fact_matrix_test.py | Every required research fact has source, availability, evidence, and explicit BLOCKED reason where needed | BLOCKER |
| P4A-G01 | Existing full CTest remains green | repository | ctest --preset dev-windows-zig --output-on-failure | 100 percent of currently registered tests pass | FAIL |
| P4A-G02 | Existing Python verification remains green | repository | python -B -m unittest discover -s tests/python -v; python -B -m unittest discover -s tests/m3 -v; YGO_M4_WORKER=build/windows-zig/ygo_m4_worker.exe python -B -m unittest discover -s tests/m4 -v | Every expected test in all existing suites passes | FAIL |
| P4A-G03 | Required M4 acceptance remains green | M4 evidence | exact M4 verification command set recorded by phase4a_clean_checkout_acceptance.ps1 | Release CTest, repository Python, M3 Python, M4 Python, fixed-game, lifecycle, and soak evidence all meet their existing recorded conditions | FAIL |
| P4A-G04 | Rules bundle and locked deck identities are unchanged | rules/deck inputs | tests/policy/rules_deck_identity_test.py | Bundle hash, both deck IDs/hashes, and 40 Main/15 Extra counts equal the frozen values | BLOCKER |
| P4A-G05 | PublicSafeState strict typed decode succeeds | environment | public_safe_state_test | Valid canonical bytes decode to the typed view with all expected values | BLOCKER |
| P4A-G06 | Typed safe-state round-trip is byte-identical | environment | public_safe_state_test and public_action_identity_test | typed decode followed by canonical encode equals the original bytes and public digest | BLOCKER |
| P4A-G07 | Safe-state malformed/truncated/trailing/noncanonical inputs fail closed | environment | public_safe_state_test | Every listed negative input is rejected without a value | BLOCKER |
| P4A-G08 | Selector API has no private-state dependency | policy seam | policy_boundary_test.py and policy_boundary_compile_test | No forbidden symbol or transitive player_observation.hpp dependency reaches selector-facing headers | BLOCKER |
| P4A-G09 | Candidate domain is preserved exactly | environment/policy seam | random_legal_test domain instrumentation | Input count, membership, order, and public-domain digest equal the V2 frame | BLOCKER |
| P4A-G10 | Empty RandomLegal domain fails closed | policy | random_legal_test | Empty vector returns structured error and submits no key | BLOCKER |
| P4A-G11 | RNG initialization is canonical | policy RNG | policy_rng_test | Exact init bytes and Phase-3 identity recompute; environment root/episode identity has no effect | BLOCKER |
| P4A-G12 | SHA-256-counter golden vectors match | policy RNG | policy_rng_test | Contract, init, block, lane, and stream vectors equal frozen constants | BLOCKER |
| P4A-G13 | Cursor advancement and rejection semantics are exact | policy RNG | policy_rng_test | Raw words advance one cursor each; pre/post spans are exact; exhaustion never wraps | BLOCKER |
| P4A-G14 | Bounded sampler is unbiased under forced rejection | policy RNG | policy_rng_test forced-rejection fixture | Rejected words are consumed and the accepted result follows the threshold algorithm | BLOCKER |
| P4A-G15 | Production provenance registrations are typed and complete | policy composition | random_legal_test with make_production_policy_provenance_resolver | Every six production identity/category pairs resolves with correct capabilities | BLOCKER |
| P4A-G16 | RandomLegal with NONE RNG is rejected | trajectory/policy provenance | trajectory_codec_test and random_legal_test | PolicyKind::RandomLegal plus ocgforge.no_policy_rng.v1 cannot validate | BLOCKER |
| P4A-G17 | RandomLegal with deterministic sampling is rejected | trajectory/policy provenance | trajectory_codec_test and random_legal_test | RandomLegal plus deterministic sampling cannot validate | BLOCKER |
| P4A-G18 | No production identity uses ocgforge.test.* | policy composition | policy_boundary_test.py and source scan | No production header/source contains a test provenance identity | BLOCKER |
| P4A-G19 | Accepted RandomLegal key is an existing public_action_key | policy/V2 | random_legal_test | Returned key occurs exactly once in the supplied current vector | BLOCKER |
| P4A-G20 | No candidate-zero or first-candidate fallback exists | policy | random_legal_test and source scan | Empty/error paths return failure; all nonempty choices are RNG-selected, including forced nonzero vectors | BLOCKER |
| P4A-G21 | RandomLegal is deterministic across independent processes | policy RNG | policy_determinism_test.py and policy_random_legal_probe | Equal explicit policy inputs produce equal keys and cursor traces in separate processes | BLOCKER |
| P4A-G22 | Paired hidden worlds produce equal policy output | privacy | random_legal_test paired-world fixture | Equal public observation/candidates/RNG state produce equal key and policy-visible state | BLOCKER |
| P4A-G23 | Policy state resets and isolates episodes/participants | policy lifecycle | policy_determinism_test.py | Interleaved and isolated sequences match; new episode starts at cursor zero | BLOCKER |
| P4A-G24 | Policy-origin StepRejected creates zero new record and quarantine | policy/trajectory | policy_runner_integration_test | Rejected action is not recorded, collection is irreversibly quarantined, and no retry occurs | BLOCKER |
| P4A-G25 | RandomLegal accepted actions record trusted provenance | policy/trajectory | policy_runner_integration_test | Every accepted record has exact assignment, artifact, CURSOR mode, and pre/post cursor | BLOCKER |
| P4A-G26 | V2 semantic replay admission remains strict | trajectory admission | policy_runner_integration_test and trajectory_replay_admission_test | Regenerated frames/domains/keys and closure admit exactly the valid envelope | BLOCKER |
| P4A-G27 | Candidate shard, restricted evidence, receipt, and DatasetManifest integrate | trajectory persistence | policy_runner_integration_test | Existing shard, restricted evidence, receipt, and dataset APIs accept the valid output | BLOCKER |
| P4A-G28 | Public-fact matrix is complete and executable | docs/evidence | public_fact_matrix_test.py | All matrix rows are present, source-linked, and validated before acceptance generation | BLOCKER |
| P4A-G29 | Evidence is reproducible from a clean checkout | acceptance | phase4a_clean_checkout_acceptance.ps1 at H_exec and fresh clone | Same semantic results are generated; H_evidence minus H_exec contains only the two generated P4A reports | BLOCKER |

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

**Files:** include/ygo/observation/observed_player_globals.hpp, include/ygo/observation/player_observation.hpp, include/ygo/environment/public_environment_observation.hpp, include/ygo/environment/public_safe_state.hpp, src/environment/public_safe_state.cpp, tests/environment/public_safe_state_test.cpp, src/environment/public_environment_observation.cpp, src/environment/public_environment_observation_decoder.cpp, and CMakeLists.txt.

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

- [ ] Step 3: Factor public observation dependencies before implementing the view.

Move ObservedPlayerGlobals from player_observation.hpp to observed_player_globals.hpp. Change public_environment_observation.hpp to include only observed_zone.hpp for ObservationLocator and to forward-declare observation::PlayerObservation. Include player_observation.hpp only in public_environment_observation.cpp, where the projection function is implemented. PublicSafeStateView may include the narrow observation value headers but must not include player_observation.hpp transitively.

- [ ] Step 4: Implement PublicSafeStateView and its const accessors.

Expose const accessors for ObservedPlayerGlobals, ObservedZone values, ObservedCard values, Relationship values, ChainState, a dedicated PublicSafeVisibleEvent without engine_step_index, and MatchContext. Keep storage private and construction restricted to the codec implementation. Expose a decode result containing either a typed value or a diagnostic.

- [ ] Step 5: Move the existing safe-state serializer and validator into the shared source.

Copy only the fields encoded by ocgforge.public_safe_state.v1 from PlayerObservation. Convert the existing Cursor, property parser, target parser, and read_safe_state grammar into typed reads. Preserve exact field order, sort rules, enum codes, redaction checks, and trailing-byte rejection. The outer decoder must call this typed decoder rather than maintain a second parser.

- [ ] Step 6: Keep the old serializer overload byte-identical.

Implement canonical_public_safe_state_bytes(PlayerObservation) by constructing the typed view and invoking the typed encoder. The existing public observation serializer must continue to emit the same nested bytes and outer digest.

- [ ] Step 7: Add strict negative tests.

Reject truncation, trailing bytes, invalid presence/boolean bytes, invalid enum codes, unsorted entity/event records, duplicate locators/event indices, hidden identity fields, face-up plus face-down cards, and unknown static-deck identities. Mutation helpers parse the known layout and do not use undocumented byte offsets.

~~~powershell
cmake --build --preset dev-windows-zig --target public_safe_state_test public_action_identity_test
ctest --preset dev-windows-zig -R '^(public_safe_state_test|public_action_identity_test)$' --output-on-failure
~~~

Expected: new typed round-trip and negatives pass, and existing public-safe goldens remain unchanged.

- [ ] Step 8: Commit.

~~~powershell
git add include/ygo/environment/public_safe_state.hpp src/environment/public_safe_state.cpp src/environment/public_environment_observation.cpp src/environment/public_environment_observation_decoder.cpp tests/environment/public_safe_state_test.cpp CMakeLists.txt
git commit -m "feat: expose typed public safe-state view"
~~~

## Task 3: Isolate public candidate DTOs from lifecycle metadata

**Files:** include/ygo/environment/public_decision.hpp, include/ygo/environment/episodic_environment.hpp, include/ygo/environment/public_environment_observation.hpp, include/ygo/observation/observed_player_globals.hpp, include/ygo/policy/policy.hpp, tests/policy/policy_boundary_test.py, and CMakeLists.txt.

- [ ] Step 1: Write the RED dependency guard.

The guard reads public_decision.hpp, public_environment_observation.hpp, and every selector-facing policy header. It fails on DecisionFrame, SubmissionToken, CoreHost, PlayerObservation, engine_step_index, semantic_key, raw_message, or response_bytes. It also follows project-local include directives recursively and fails if player_observation.hpp is reachable from any selector-facing header. It requires episodic_environment.hpp to include public_decision.hpp and player_observation.hpp to include observed_player_globals.hpp.

~~~powershell
python tests/policy/policy_boundary_test.py
~~~

Expected: failure before the extracted header exists.

- [ ] Step 2: Extract public DTO declarations verbatim.

Move EnvironmentDecisionKind, EnvironmentActionKind, EnvironmentActionCandidate, EnvironmentContinuationView, and EnvironmentDecisionRequest into public_decision.hpp. Leave all lifecycle types in episodic_environment.hpp. Change public_environment_observation.hpp to forward-declare PlayerObservation and include only its narrow locator dependency. Do not change values or wire behavior.

- [ ] Step 3: Define the selector seam.

~~~cpp
enum class PolicyErrorCode : std::uint8_t {
    InvalidConfiguration,
    EmptyCandidateDomain,
    InvalidCandidateDomain,
    RngExhausted,
    LifecycleFailure,
};

struct PolicyError final {
    PolicyErrorCode code = PolicyErrorCode::InvalidConfiguration;
    std::string message;
};

struct PolicyInput final {
    const environment::PublicEnvironmentObservation& observation;
    const std::vector<environment::EnvironmentActionCandidate>& candidates;
};

struct PolicyRngCursorTransition final {
    std::uint64_t pre_cursor = 0;
    std::uint64_t post_cursor = 0;
};

struct PolicyExecutionBinding final {
    std::string policy_artifact_id;
    std::string participant_policy_assignment_id;
    std::string policy_rng_contract_identity;
    std::string policy_rng_stream_id;
    std::string policy_rng_initialization_identity;
    std::string policy_rng_identity;
};

struct PolicySelectionResult final {
    std::string public_action_key;
    std::optional<PolicyRngCursorTransition> rng_cursor;
};

struct PolicySelection final {
    std::optional<PolicySelectionResult> value;
    std::optional<PolicyError> error;
    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};
~~~

The generic selector result is usable by both stochastic and deterministic policies. RNG identities belong to the immutable runner/session binding, not the selector result. A deterministic future Teacher returns rng_cursor = nullopt. RandomLegal returns only PolicyRngCursorTransition; it never returns a candidate-vector index or claims a provenance identity.

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

Use root 0x0123456789abcdef, assignment ID participant_policy_assignment.v1. plus 64 b characters, and stream ID player0. Freeze initialization material, initialization identity, block zero/one, all eight lanes, and cursor traces. Include an independent episode semantic ID only as a negative-control input: changing it must not change any policy-RNG identity or raw word when the policy-owned root, participant assignment, and stream remain equal.

- [ ] Step 2: Implement exact initialization material.

~~~text
string ocgforge.policy_rng.sha256_counter.init.v1
string ocgforge.policy_rng.sha256_counter.v1
u64be explicit policy RNG root seed
string participant policy assignment ID
string policy RNG stream ID
~~~

Reject empty/noncanonical stream tokens and empty participant/stream identifiers. Never derive this root from EpisodeSpec.root_seed, episode_semantic_id, V2 seed material, or any other environment value. Independent episodes receive independent policy-owned roots or explicitly distinct policy streams.

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

Use a test-only friend to set a valid initialized RNG at UINT64_MAX; do not add a public reseed path. Assert no wrap, exact cursor spans, unchanged cursor for n == 1, and changed identities/first blocks for changed policy-owned root, participant assignment, or stream.

Changing only episode_semantic_id / EpisodeSpec.root_seed must not change initialization identity, stream output, or raw words.

- [ ] Step 6: Document, verify, and commit.

~~~powershell
cmake --build --preset dev-windows-zig --target policy_rng_test
ctest --preset dev-windows-zig -R '^policy_rng_test$' --output-on-failure
git add include/ygo/policy/rng.hpp src/policy/rng.cpp tests/policy/rng_test.cpp docs/contracts/policy-rng-sha256-counter-v1.md CMakeLists.txt
git commit -m "feat: add SHA-256 counter policy RNG"
~~~

## Task 5: Register production provenance and RandomLegal

**Files:** include/ygo/policy/production_provenance.hpp, src/policy/production_provenance.cpp, include/ygo/policy/production.hpp, src/policy/production.cpp, include/ygo/policy/random_legal.hpp, src/policy/random_legal.cpp, tests/policy/rng_test.cpp, tests/policy/random_legal_test.cpp, and CMakeLists.txt. The trajectory library is not modified by this task.

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

- [ ] Step 2: Implement the explicit production resolver in ygo::policy.

Register each identity in exactly one category, add canonical-material and cursor-uniqueness callbacks to the RNG descriptor, and construct a trajectory::ProvenanceResolver from the policy composition layer. The generic ygo_trajectory library remains unaware of these production registrations. No production source may contain ocgforge.test. identities.

- [ ] Step 3: Implement artifact and participant setup.

Build one PolicyArtifact with PolicyKind::RandomLegal, the six production identities, no model/search/demonstration identity, and the existing computed artifact ID. Build epoch-zero participant assignments using the accepted Normal/Mirror seat-to-deck mapping. Set both PolicyRole values explicitly in the caller-provided roles array, not by relying on struct defaults. Sort both participant assignments by participant_policy_assignment_id before creating the envelope. Build initialization and stream identities with explicit policy roots and stream IDs; episode_semantic_id is not an RNG input.

- [ ] Step 4: Write and implement RandomLegal RED/GREEN tests.

Use three valid public candidates in deliberate nonlexical order. Assert exact key membership, unchanged count/membership/order/domain digest, empty-domain failure, invalid-key failure, no candidate-zero fallback, and no result index.

~~~powershell
cmake --build --preset dev-windows-zig --target policy_rng_test random_legal_test trajectory_codec_test
ctest --preset dev-windows-zig -R '^(policy_rng_test|random_legal_test|trajectory_codec_test)$' --output-on-failure
python tests/policy/policy_boundary_test.py
rg -n 'ocgforge\.test\.' src/policy include/ygo/policy
git add include/ygo/policy/production_provenance.hpp src/policy/production_provenance.cpp include/ygo/policy/production.hpp src/policy/production.cpp include/ygo/policy/random_legal.hpp src/policy/random_legal.cpp tests/policy/rng_test.cpp tests/policy/random_legal_test.cpp CMakeLists.txt
git commit -m "feat: add production RandomLegal provenance and selector"
~~~

Expected: focused tests pass and production source has no test identity.

## Task 6: Integrate V2, TrajectoryRecorder, and admission

**Files:** include/ygo/policy/runner.hpp, src/policy/runner.cpp, tests/policy/policy_runner_integration_test.cpp, and CMakeLists.txt.

- [ ] Step 1: Write the RED integration test.

Create canonical V2 environment, production provenance with explicit policy roots, and TrajectoryRecorder with the production resolver. Run a bounded episode. Every accepted record must have Cursor mode, pre/post cursors, the exact acting assignment, and post_cursor >= pre_cursor.

- [ ] Step 2: Implement runner lifecycle.

The runner may receive DecisionFrame for lifecycle only. It constructs PolicyInput from frame.public_observation and frame.request.candidates, builds ActionSelection using the current token, calls environment.step, and passes accepted results to TrajectoryRecorder.

The runner alone adds decision index and assignment to the existing PolicyRngDecisionProvenance. It reads identities from the immutable PolicyExecutionBinding and reads only the optional cursor transition from the selector result:

~~~cpp
attribution.decision_index = frame.decision_index;
attribution.acting_policy_assignment_id = assignment_id;
attribution.policy_rng_identity = binding.policy_rng_identity;
attribution.policy_rng_contract_identity = binding.policy_rng_contract_identity;
attribution.policy_rng_stream_id = binding.policy_rng_stream_id;
attribution.policy_rng_initialization_identity = binding.policy_rng_initialization_identity;
if (selection.rng_cursor.has_value()) {
    attribution.mode = trajectory::PolicyRngMode::Cursor;
    attribution.pre_cursor = selection.rng_cursor->pre_cursor;
    attribution.post_cursor = selection.rng_cursor->post_cursor;
} else {
    attribution.mode = trajectory::PolicyRngMode::None;
    attribution.policy_rng_identity = trajectory::kNoPolicyRngContractId;
    attribution.policy_rng_contract_identity = trajectory::kNoPolicyRngContractId;
    attribution.policy_rng_stream_id = trajectory::kNoPolicyRngContractId;
    attribution.policy_rng_initialization_identity = trajectory::kNoPolicyRngContractId;
}
~~~

- [ ] Step 3: Implement fail-closed rejection flow.

Handle every ResetResult, StepResult, and InterruptResult alternative explicitly. On StepRejected, call recorder.on_step_rejected(rejected, true), perform one administrative interruption, pass the unchanged pre-rejection frame to on_interrupt_accepted, seal the quarantined envelope, and never retry. If the administrative interrupt returns InterruptRejected or EpisodeFailure, return a structured fail-closed runner result and do not claim a normal closure. On policy/RNG failure, return a structured policy error without submitting a key and without rewinding the RNG.

- [ ] Step 4: Verify the complete existing trajectory path and commit.

Build CandidateTrajectoryShard, sorted restricted RNG initialization evidence, verify admission, issue VerifiedAdmissionReceipt, and validate DatasetManifest through existing APIs. Quarantined envelopes must fail clean admission.

~~~powershell
cmake --build --preset dev-windows-zig --target policy_runner_integration_test trajectory_replay_admission_test
ctest --preset dev-windows-zig -R '^(policy_runner_integration_test|trajectory_replay_admission_test)$' --output-on-failure
git add include/ygo/policy/runner.hpp src/policy/runner.cpp tests/policy/policy_runner_integration_test.cpp CMakeLists.txt
git commit -m "feat: integrate RandomLegal with trusted trajectory admission"
~~~

## Task 7: Add P4A-G00 and privacy/determinism evidence

**Files:** docs/p4a/P4A_PUBLIC_FACT_MATRIX.md, tests/policy/public_fact_matrix_test.py, tests/policy/rules_deck_identity_test.py, tools/policy_random_legal_probe/main.cpp, tests/policy/policy_determinism_test.py, and CMakeLists.txt.

- [ ] Step 1: Write the RED matrix validator.

Require rows for turn player, phase, life points, visible field/GY/banished cards, known hand, zone counts, card properties, chain/source/targets, public effect descriptions, event/summon/movement history, shuffle boundaries, once-per-turn history, candidate source/target/kind/amount/position/phase, and contract-known deck context. Each row has the requirement, exact public source, DIRECT, SAFE_DERIVATION, or BLOCKED, and executable evidence.

~~~powershell
python tests/policy/public_fact_matrix_test.py
~~~

Expected: failure before the matrix exists.

- [ ] Step 2: Write the matrix.

Map direct facts to PublicSafeStateView and V2 DTO fields. Mark hidden opponent hand/deck order, Foxy top-deck identity, persistent hidden identity, unavailable effect-use/restriction facts, omitted event families, and absent candidate metadata BLOCKED with exact missing-source reasons and anti-side-channel tests. Do not weaken a blocked research requirement.

- [ ] Step 3: Add independent-process and paired-world tests.

The probe accepts explicit policy root, assignment ID, stream ID, and ordered public candidates, then emits only selected public keys and cursor spans. A separate negative-control invocation changes only the environment episode identity while keeping policy-owned inputs equal; the RNG initialization and outputs must remain equal. Equal public inputs in independent processes and paired hidden worlds must match. Interleaved episodes must equal isolated runs; a newly constructed policy starts at cursor zero.

- [ ] Step 4: Verify rules/deck identities and commit.

~~~powershell
python tests/policy/public_fact_matrix_test.py
python tests/policy/rules_deck_identity_test.py
cmake --build --preset dev-windows-zig --target policy_random_legal_probe random_legal_test
python tests/policy/policy_determinism_test.py
ctest --preset dev-windows-zig -R '^random_legal_test$' --output-on-failure
git add docs/p4a/P4A_PUBLIC_FACT_MATRIX.md tests/policy/public_fact_matrix_test.py tools/policy_random_legal_probe/main.cpp tests/policy/policy_determinism_test.py CMakeLists.txt
git commit -m "test: prove Phase 4A privacy and determinism"
~~~

## Task 8: Freeze H_exec, generate H_evidence, and verify all P4A gates

**Files:** tools/p4a/phase4a_clean_checkout_acceptance.ps1, docs/p4a/p4a_acceptance.json, docs/p4a/P4A_ACCEPTANCE.md. No CMake or production-source changes are allowed after H_exec.

- [ ] Step 1: Add the acceptance generator before the execution checkpoint.

The script runs the exact focused CTest targets, policy Python tests, full CTest, existing Python verification, required M4 evidence command set, and rules/deck identity test. It writes JSON first and derives Markdown from that JSON. It records the source SHA, command, exit code, exact output summary, and generated file hashes. PASS is legal only after the corresponding command exits zero; otherwise it emits FAIL, NOT_RUN, SKIPPED, or BLOCKED.

- [ ] Step 2: Create H_exec.

~~~powershell
git add include src tests tools docs/contracts/policy-rng-sha256-counter-v1.md docs/p4a/P4A_PUBLIC_FACT_MATRIX.md CMakeLists.txt
git commit -m "feat: implement Phase 4A public policy substrate"
git rev-parse HEAD
git tag --force p4a-h-exec HEAD
~~~

H_exec contains all implementation sources, tests, the public-fact matrix, RNG contract, CMake registration, and acceptance generator. The generated P4A JSON/Markdown files are not part of H_exec.

- [ ] Step 3: Run every gate at H_exec.

~~~powershell
cmake --preset dev-windows-zig
cmake --build --preset dev-windows-zig --parallel
ctest --preset dev-windows-zig --output-on-failure
python -B -m unittest discover -s tests/python -v
python -B -m unittest discover -s tests/m3 -v
$env:YGO_M4_WORKER = (Resolve-Path build/windows-zig/ygo_m4_worker.exe).Path
python -B -m unittest discover -s tests/m4 -v
python tests/policy/public_fact_matrix_test.py
python tests/policy/rules_deck_identity_test.py
python tests/policy/policy_determinism_test.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools/p4a/phase4a_clean_checkout_acceptance.ps1
~~~

Run the script from the clean H_exec worktree. Its report contains P4A-G00 and P4A-G01 through P4A-G29 with exact statuses and no unexecuted PASS. Capture the generated JSON/Markdown as H_evidence candidates, but do not commit them yet.

- [ ] Step 4: Reproduce H_exec in a fresh checkout.

Create a fresh temporary clone or worktree at the exact H_exec SHA, materialize the pinned rules bundle through repository tooling, run the same acceptance script, and compare semantic outputs and evidence identities with the first H_exec run. Do not copy research files or build directories and do not alter H_exec.

- [ ] Step 5: Create H_evidence with only generated reports.

~~~powershell
git diff --name-only p4a-h-exec..HEAD
git add docs/p4a/p4a_acceptance.json docs/p4a/P4A_ACCEPTANCE.md
git commit -m "docs: record Phase 4A acceptance evidence"
git diff --name-only p4a-h-exec..HEAD
git diff p4a-h-exec..HEAD --check
~~~

The tag p4a-h-exec records the exact H_exec SHA. The second diff command must print exactly docs/p4a/p4a_acceptance.json and docs/p4a/P4A_ACCEPTANCE.md. The acceptance generator remains in H_exec. No source, test, CMake, research, or contract file may enter H_evidence.

Final handoff reports starting SHA, branch/head SHA, files, production identities, semantic/internal changes, privacy/determinism/replay implications, executed commands, exact G00-G29 statuses, evidence hashes, limitations, blocked future Teacher facts, hosted CI, and PR URL/number. It does not self-declare acceptance or merge readiness.

## Plan self-review

- One safe-state grammar preserves bytes and digests.
- Selector headers exclude lifecycle/private fields.
- Policy returns an optional PolicyRngCursorTransition; the runner alone owns PolicyExecutionBinding and builds trajectory provenance.
- Initialization binds explicit policy root, participant assignment, and stream; it excludes environment and episode identity.
- Rejection sampling, n == 1, and exhaustion semantics are explicit.
- Production registry categories are distinct and the default resolver stays unchanged.
- RandomLegal consumes the complete supplied domain and never falls back to candidate zero.
- Policy-origin rejection produces no record for the rejected action, quarantines, and never retries.
- Existing Phase-3A/3B schemas and codecs remain unchanged.
- P4A-G00 records blocked facts instead of using private state or weakening the future Teacher.
