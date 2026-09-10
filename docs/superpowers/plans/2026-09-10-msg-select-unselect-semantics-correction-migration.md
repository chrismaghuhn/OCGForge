# MSG_SELECT_UNSELECT_CARD Semantics Correction Migration Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Replace the inverted public interpretation of MSG_SELECT_UNSELECT_CARD with an explicit corrected generation while preserving historical V1 and V2 bytes, validators, and replay behavior.

**Architecture:** Keep the current V2/V3 path immutable as historical evidence. Add a corrected public action/domain/decision generation, bind it to a new Episodic Environment V4 and Environment Identity V4, then carry that generation through explicit Teacher, Runner, trajectory, replay, collection, and Task7 successors. Reuse only contracts whose semantic meaning and canonical field layout are unchanged.

**Tech Stack:** C++20, CMake/Ninja, CTest, the pinned ocgcore/CardScripts bundle, existing canonical byte writers/readers, SHA-256 identity helpers, public observation DTOs, and the repository's fail-closed provenance validators.

---

## Frozen source and non-goals

The accepted design has one frozen root. Implementation is sequential; every
task starts from the independently reviewed and accepted head of the previous
task:

~~~
FROZEN_ROOT_BASE=5dbe4651bed25615b759ebfde4328018605fe6ea
TASK_0_DESIGN_HEAD=ACCEPTED_DESIGN_HEAD
IMPLEMENTATION_TASK_1_BASE=ACCEPTED_DESIGN_HEAD
IMPLEMENTATION_TASK_N_BASE=ACCEPTED_TASK_N_MINUS_1_HEAD
NEXT_TASK_AUTHORIZATION=NO_UNTIL_PREVIOUS_TASK_REVIEW_PASS
NO_PARALLEL_SEMANTIC_MIGRATION_SLICES=YES
~~~

The root is never rebased or rewritten. The symbolic task-base values are
resolved to the exact accepted commit before each implementation task begins;
an implementation task cannot start from the root while omitting an accepted
predecessor.

Pinned semantic evidence:

~~~
ocgcore     = 9a0c558c2d686542f7914a6d529fd7aa57746aed
CardScripts = f337c87018ca723c1aded5143e616bb649555273
~~~

The migration preserves:

~~~
first wire list  → Select
second wire list → Unselect
None=0, Select=1, Unselect=2
candidate membership and supplied order
internal protocol identities and exact response bytes
rules, decks, format, flags, seed derivation, and no-policy-RNG behavior
Teacher score dimensions, fallback ordering, and F4 tie-break
~~~

The migration does not authorize budgets, retries, RUN_A, RUN_B, training,
deck/rules changes, or performance work. No current V1/V2 decoder or validator
is widened.

## Successor surface map

| Task | New owned surface | Historical surface preserved |
| --- | --- | --- |
| 1 | Protocol corrected decode entry point | decode_unselect and internal protocol values |
| 2 | Public action/domain/decision V3 | Public identity V1/V2 |
| 3 | Episodic Environment V4 and Environment Identity V4 | Episodic Environment V2/V3 and identity V2/V3 |
| 4 | Teacher State/Delta/Ranking/Policy V3 | Teacher V1/V2 state and policy paths |
| 5 | TeacherRunnerV4 and trajectory V3 recorder/replay core | TeacherRunnerV3 and trajectory V1/V2 |
| 6 | Collection V3 shard/evidence/admission/receipt/dataset | Collection V1/V2 |
| 7 | Task7 V3 authority and model-materialization successor | Task7 V1/V2 and model V1 |
| 8 | Short Hiita reproducer | No semantic fallback or card-specific branch |
| 9 | Bounded Job0 validation | No Task7 collection retry |

Each task ends with focused tests and a cross-generation rejection matrix. No
task may modify an earlier generation to accept a later value.

## Task 1: Add the corrected protocol decode surface

**Files:**

- Modify: include/ygo/protocol/message_decoder.hpp
- Modify: src/protocol/message_decoder.cpp
- Test: tests/protocol/decision_family_test.cpp
- Create: tests/protocol/select_unselect_operation_successor_test.cpp

- [ ] **Step 1: Freeze the corrected mapping test**

Construct the existing four-field MSG_SELECT_UNSELECT_CARD fixture with one
card in each wire list and call protocol::decode_messages_v4. Assert:

~~~cpp
request.candidates[0].card_selection_operation ==
    CardSelectionOperation::Select;
request.candidates[1].card_selection_operation ==
    CardSelectionOperation::Unselect;
request.candidates[2].card_selection_operation ==
    CardSelectionOperation::None;
~~~

Also assert candidate count, source order, source indices, and exact response
bytes equal the historical decoder output. The test must call an explicit
successor entry point; it must not change historical expectations.

- [ ] **Step 2: Implement the corrected private leaf parser**

Keep decode_unselect unchanged for the historical path. Add a private
decode_unselect_v4 helper in src/protocol/message_decoder.cpp; do not export
the leaf helper as an independent public decoder authority. Its loops use
wire-semantic names and mappings:

~~~cpp
read select_list_count;
candidate.card_selection_operation = CardSelectionOperation::Select;

read unselect_list_count;
candidate.card_selection_operation = CardSelectionOperation::Unselect;
~~~

The enum, request schema, semantic keys, source indices, and response encoding
remain shared internal protocol values. Task 1 exposes only the full
decode_messages_v4 entry point, which is the sole corrected runtime decoder.

- [ ] **Step 3: Add the reachable full decoder successor**

Keep the historical full entry point unchanged:

~~~cpp
DecodedMessage decode_messages(
    const std::vector<std::uint8_t>& bytes,
    std::uint64_t engine_step_index = 0);
~~~

Add the public full successor entry point in the same protocol header:

~~~cpp
DecodedMessage decode_messages_v4(
    const std::vector<std::uint8_t>& bytes,
    std::uint64_t engine_step_index = 0);
~~~

Implement decode_messages_v4 by reusing the existing full-dispatch parsers for
every message family. Only MSG_SELECT_UNSELECT_CARD dispatches to the private
corrected leaf from Step 2. For every other message family, compare the two
full decoder outputs and require equal request family, candidate membership,
candidate order, source indices, and exact response bytes. Do not call the
historical full decoder and patch its result after decoding.

- [ ] **Step 4: Run protocol compatibility gates**

Run:

~~~powershell
cmake --preset dev-windows
cmake --build --preset dev-windows --target decision_family_test select_unselect_operation_successor_test --parallel
ctest --preset dev-windows -R "^(decision_family_test|select_unselect_operation_successor_test)$" --output-on-failure
~~~

Require historical operation expectations, corrected first Select/second
Unselect, unchanged candidate order, unchanged response bytes, and a public
decode_messages_v4 entry point that is independently buildable before any
Environment work begins. The test must not use a TestAccess seam.

Task 1 closes this ownership gate:

~~~text
TASK1_CORRECTED_TEST_ENTRYPOINT=protocol::decode_messages_v4
TASK1_PRIVATE_LEAF_DIRECTLY_TESTED=NO
FULL_CORRECTED_DECODER_IMPLEMENTED_IN_TASK1=YES
EPISODE_DRIVER_ROUTING_IMPLEMENTED_IN_TASK3=YES
HISTORICAL_DECODE_MESSAGES_UNCHANGED=YES
TASK1_HAS_NO_ENVIRONMENT_CHANGE=YES
TASK3_HAS_NO_PROTOCOL_SEMANTIC_IMPLEMENTATION=YES
TASK1_INDEPENDENTLY_BUILDABLE_AND_TESTABLE=YES
~~~

## Task 2: Implement public identity V3

**Files:**

- Modify: include/ygo/environment/public_action_identity.hpp
- Modify: src/environment/public_action_identity.cpp
- Modify: src/environment/public_action_identity_internal.hpp
- Test: tests/episodic/public_action_identity_test.cpp
- Create: tests/episodic/public_action_identity_v3_test.cpp

- [ ] **Step 1: Add explicit V3 constants and functions**

Add:

~~~cpp
inline constexpr std::string_view kPublicActionIdentityV3SchemaId =
    "ocgforge.public_action_identity.v3";
inline constexpr std::string_view kPublicCandidateDomainV3SchemaId =
    "ocgforge.public_candidate_domain.v3";
inline constexpr std::string_view kPublicSemanticDecisionIdentityV3SchemaId =
    "ocgforge.public_semantic_decision_identity.v3";
inline constexpr std::string_view kPublicActionKeyV3Prefix =
    "public_action.v3.";
~~~

Add V3 action bytes/key/validator, V3 candidate-domain bytes/digest, V3
public-decision bytes/ID, and a V3 frame-local resolver. Keep every V1/V2
function and validator unchanged.

- [ ] **Step 2: Define the exact V3 byte replacements**

Use the V2 field order and primitive widths, with explicit V3 markers:

~~~text
public action:
  v3 identity domain string
  v3 identity schema string
  action kind string
  operation:u8 (0=None, 1=Select, 2=Unselect)
  existing typed public descriptor fields in existing order

candidate domain:
  v3 domain string
  request kind string
  candidate_count:u32be
  ordered public_action.v3 key strings

public decision:
  v3 domain string
  v3 schema string
  episode ID
  decision index:u64be
  acting player:u8
  request kind
  public observation digest
  V3 candidate-domain digest
~~~

Reject V1/V2 keys, empty or duplicate domains, invalid operation values, and
non-CardSelection operations carrying Select or Unselect. Recompute every V3
key from its public descriptor.

- [ ] **Step 3: Add successor goldens**

Freeze exact bytes and SHA-256 values for None, Select, and Unselect V3 keys,
the corrected two-list domain, and the corrected public decision identity.
Leave every historical V1/V2 expected value untouched.

- [ ] **Step 4: Run identity gates**

Run:

~~~powershell
cmake --build --preset dev-windows --target public_action_identity_test public_action_identity_v3_test --parallel
ctest --preset dev-windows -R "^(public_action_identity_test|public_action_identity_v3_test)$" --output-on-failure
~~~

Require V1/V2/V3 cross-generation rejection and fresh-process equality of V3
bytes.

## Task 3: Add Episodic Environment V4 and Environment Identity V4

**Files:**

- Modify: include/ygo/environment/episodic_environment.hpp
- Modify: src/environment/episodic_environment.cpp
- Modify: include/ygo/environment/episode_driver.hpp
- Modify: src/environment/episode_driver.cpp
- Modify: include/ygo/trajectory/identity_resolver.hpp
- Modify: src/trajectory/identity_resolver.cpp
- Test: tests/episodic/episodic_identity_test.cpp
- Create: tests/episodic/episode_driver_v4_decode_routing_test.cpp
- Create: tests/episodic/episodic_environment_v4_public_operation_test.cpp
- Create: tests/episodic/episodic_environment_v4_identity_test.cpp

- [ ] **Step 1: Add explicit V4 configuration**

Add:

~~~cpp
inline constexpr std::string_view kEpisodicEnvironmentV4ContractId =
    "ocgforge.episodic_environment.v4";
inline constexpr std::string_view kEnvironmentIdentityV4SchemaId =
    "ocgforge.environment_identity.v4";

static CertifiedEnvironmentConfig canonical_v4();
~~~

canonical_v4 derives all rules/deck/script/database values from the certified
canonical configuration, binds public identity V3, uses environment identity
V4, and recomputes the V4 environment semantic ID. Mixed contract fields
reject.

- [ ] **Step 2: Bind EpisodeDriver routing to the validated environment**

The full decoder successor is already implemented and independently tested by
Task 1. Add a private driver decode profile with exactly two values:

~~~cpp
enum class EpisodeDriverDecodeProfile : std::uint8_t {
    Historical,
    CorrectedV4,
};
~~~

The existing public EpisodeDriver(EpisodeDriverConfig) constructor delegates
to Historical. The V4 EpisodicEnvironment construction path alone may use a
private/friend-only EpisodeDriver constructor carrying CorrectedV4, and it may
do so only after validating CertifiedEnvironmentConfig::canonical_v4(). The
profile is stored in EpisodeDriver::Impl and selects the complete decoder at
the single live call site:

~~~cpp
const auto decoded =
    decode_profile == EpisodeDriverDecodeProfile::Historical
        ? protocol::decode_messages(current_raw_message, engine_step)
        : protocol::decode_messages_v4(current_raw_message, engine_step);
~~~

The profile is not a public EpisodeDriverConfig field, callback, CLI option, or
caller-provided independent authority. V2/V3 environment construction always
uses Historical; V4 construction always uses CorrectedV4. A direct historical
EpisodeDriver caller cannot select the corrected profile. This task does not
modify protocol decoding or the corrected leaf implementation.

- [ ] **Step 3: Add V4 resolver entry points**

Expose the same value-owned argument shapes as the accepted V3 resolver:

~~~cpp
DecodeResult<environment::CertifiedEnvironmentConfig>
decode_environment_identity_input_v4(
    const std::vector<std::uint8_t>& bytes) noexcept;

DecodeResult<environment::EpisodeSpec> decode_episode_identity_input_v4(
    const std::vector<std::uint8_t>& bytes,
    const environment::CertifiedEnvironmentConfig& config) noexcept;

bool is_current_certified_environment_v4(
    const environment::CertifiedEnvironmentConfig& config) noexcept;
~~~

The V4 environment decoder accepts only environment_identity.v4, verifies the
canonical field order, hashes and re-encodes the input, and compares bytes
exactly. The V4 episode decoder accepts only a V4 parent environment and sets
the V4 episode contract. Historical resolver functions remain generation-pure.

- [ ] **Step 4: Test the V4 public boundary and routing**

Prove:

~~~text
V4 reset/step emits V4 frames
first list is Select
second list is Unselect
candidate membership/order equal the historical projection
exact response bytes are equal
V2/V3 identity values are rejected by V4
V4 values are rejected by V2/V3 paths
V3 environment uses decode_messages
V4 environment uses decode_messages_v4
V3 environment cannot use decode_messages_v4
V4 environment cannot use decode_messages
non-UNSELECT message families are semantically equal across full decoders
~~~

Run:

~~~powershell
cmake --build --preset dev-windows --target episode_driver_v4_decode_routing_test episodic_environment_v4_public_operation_test episodic_environment_v4_identity_test --parallel
ctest --preset dev-windows -R "^(episode_driver_v4_decode_routing_test|episodic_environment_v4_public_operation_test|episodic_environment_v4_identity_test|episodic_identity_test)$" --output-on-failure
~~~

## Task 4: Implement Teacher V3 state and policy surfaces

**Files:**

- Create: include/ygo/teacher/strategy_state_v3.hpp
- Create: src/teacher/strategy_state_v3.cpp
- Create: include/ygo/teacher/teacher_decision_v3.hpp
- Create: src/teacher/teacher_decision_v3.cpp
- Create: include/ygo/teacher/teacher_core_v3.hpp
- Create: src/teacher/teacher_core_v3.cpp
- Create: include/ygo/policy/teacher_v3.hpp
- Create: src/policy/teacher_v3.cpp
- Modify: include/ygo/policy/production_provenance.hpp
- Modify: src/policy/production_provenance.cpp
- Create: tests/teacher/teacher_v3_public_action_successor_test.cpp
- Create: tests/teacher/teacher_v3_state_successor_test.cpp

- [ ] **Step 1: Define explicit V3 state/result types**

Copy the V2 logical field shape into explicit V3 types, with every public-key
validator changed to is_public_action_key_v3:

~~~cpp
struct EpisodeLocalStrategyStateV3 final {
    std::string strategy_profile_id;
    std::optional<std::string> active_goal_id;
    std::optional<std::string> active_line_id;
    std::vector<std::string> completed_line_node_ids;
    std::vector<std::string> achieved_goal_ids;
    std::vector<PublicFactValue> public_resource_facts;
    std::vector<PublicFactValue> public_restriction_facts;
    std::vector<PublicFactValue> public_threat_facts;
    std::optional<std::uint64_t> last_accepted_decision_index;
    std::optional<std::string> last_accepted_public_action_key;
};
struct TeacherStateDeltaV3 final {
    std::string strategy_profile_id;
    std::optional<std::uint64_t> base_last_accepted_decision_index;
    std::optional<std::string> base_last_accepted_public_action_key;
    std::string proposed_for_public_action_key;
    std::optional<std::string> active_goal_id;
    std::optional<std::string> active_line_id;
    std::vector<std::string> completed_line_node_ids;
    std::vector<std::string> achieved_goal_ids;
    std::vector<PublicFactValue> public_resource_facts;
    std::vector<PublicFactValue> public_restriction_facts;
    std::vector<PublicFactValue> public_threat_facts;
    std::vector<std::string> invalidation_reason_ids;
};
struct StrategyReconciliationResultV3 final {
    EpisodeLocalStrategyStateV3 state;
    std::vector<std::string> invalidation_reason_ids;
};
struct TeacherRankingResultV3 final {
    TeacherRankingStatus status = TeacherRankingStatus::InvalidInput;
    std::vector<CandidateEvaluation> evaluations;
    std::optional<std::string> selected_public_action_key;
    std::optional<ScoreVector> selected_score_vector;
    std::optional<TeacherFallbackLevel> fallback_level;
    std::optional<TeacherStateDeltaV3> proposed_state_delta;
};
~~~

Do not add a generation enum and do not widen V2 validators. Rejection
preservation, accepted-transition checks, and public reconciliation meaning
remain unchanged.

- [ ] **Step 2: Reuse scoring without changing strategy semantics**

Share private evaluation helpers where useful, but retain all accepted score
dimensions, profile predicates, fallback ordering, and F4 lexical tie-break.
The native continuation contribution remains:

~~~text
Select card candidate = generic retained progress +1
Unselect card candidate = 0
Cancel/Finish = 0
~~~

Do not add a Hiita branch, candidate-name branch, first-candidate fallback,
candidate truncation, or hidden-state source.

- [ ] **Step 3: Add V3 policy factories and provenance**

Add:

~~~cpp
teacher::TeacherPolicyBindingV1 make_teacher_policy_binding_v3(
    const teacher::StrategyProfileV1& profile);

trajectory::PolicyArtifact make_teacher_policy_artifact_v3(
    const teacher::StrategyProfileV1& profile);

TeacherPolicySessionCreateResultV3 create_teacher_policy_session_v3(
    const teacher::StrategyProfileV1& profile,
    const teacher::TeacherPolicyBindingV1& policy_binding,
    const trajectory::PolicyArtifact& artifact,
    const trajectory::ParticipantPolicyAssignment& assignment) noexcept;
~~~

TeacherPolicySessionCreateResultV3 has the same value/error shape as the
accepted V2 creation result: an optional TeacherPolicySessionV3 and an
optional policy::PolicyError. TeacherPolicySessionV3 contains a
DeterministicTeacherPolicyV3, the shared PolicyArtifact, and the shared
ParticipantPolicyAssignment.

Reuse the V1 binding/artifact schemas while producing new content identities:

~~~text
producer = ocgforge.policy.teacher_core.v3
action adapter = ocgforge.policy.public_action_key.v3
observation adapter = ocgforge.policy.public_observation.v1
sampling = ocgforge.policy.deterministic_lexicographic_argmax.v1
policy RNG = ocgforge.no_policy_rng.v1
~~~

The V2 binding/artifact values and IDs are not reused.

- [ ] **Step 4: Test Teacher V3**

Positive tests require corrected Hiita inputs to score Select A and Select B
at +1 and Cancel at 0. Negative tests cover V2 key in V3 state, V3 key in V2
state, mixed evaluation keys, wrong producer/action adapter, extra
provenance, and stale state/delta keys.

Run:

~~~powershell
cmake --build --preset dev-windows --target teacher_v3_public_action_successor_test teacher_v3_state_successor_test --parallel
ctest --preset dev-windows -R "^(teacher_v3_public_action_successor_test|teacher_v3_state_successor_test|teacher_v3_hiita_commitment_test)$" --output-on-failure
~~~

Historical Teacher V1/V2 tests must remain green.

## Task 5: Add Runner V4 and corrected trajectory core

**Files:**

- Create: include/ygo/policy/teacher_runner_v4.hpp
- Create: src/policy/teacher_runner_v4.cpp
- Create: include/ygo/policy/teacher_runner_v4_trajectory.hpp
- Create: src/policy/teacher_runner_v4_trajectory.cpp
- Create: include/ygo/trajectory/types_v3.hpp
- Create: include/ygo/trajectory/codec_v3.hpp
- Create: src/trajectory/codec_v3.cpp
- Create: include/ygo/trajectory/recorder_v3.hpp
- Create: src/trajectory/recorder_v3.cpp
- Create: include/ygo/trajectory/trajectory_identity_v3.hpp
- Create: src/trajectory/trajectory_identity_v3.cpp
- Create: tests/teacher/teacher_runner_v4_trajectory_test.cpp
- Create: tests/trajectory/trusted_trajectory_v3_codec_test.cpp
- Create: tests/trajectory/trusted_trajectory_v3_recorder_test.cpp

- [ ] **Step 1: Define V4 runner bindings**

Require:

~~~text
episodic_environment.v4
environment_identity.v4
public_action.v3
TeacherPolicySessionV3
~~~

The V3 runner is untouched. The V4 runner passes complete V4 frames to
TeacherCoreV3 and copies accepted public values to the V3 trajectory
recorder without rebuilding candidate semantics.

- [ ] **Step 2: Define V3 trajectory bytes**

Use explicit successor DTOs. Replace each V2 trajectory marker and
public-key/domain/decision dependency with V3 values while preserving
transition/closure numeric meanings, order, and privacy exclusions:

~~~text
no submission token
no engine step in public trajectory
no internal semantic key
no raw response bytes
no hidden card identity
~~~

Gameplay identity V3 contains corrected public records and closure only.
Record identity V3 adds the existing shared policy provenance and collection
attribution split.

- [ ] **Step 3: Add strict V3 trajectory validation**

Reject V1/V2 keys, mixed domains, V2 environment contracts, noncanonical bytes,
duplicate selected keys, incorrect domain digests, incorrect public decision
IDs, and V2/V3 envelope mixing. Require canonical decode/re-encode equality.

- [ ] **Step 4: Run bounded Runner/Recorder tests**

Exercise:

~~~text
V4 reset
idle_command / Hiita
MSG_SELECT_UNSELECT_CARD first list = Select
Teacher chooses Select rather than Cancel
one accepted continuation
V3 envelope seal
~~~

Prove V2 runner/trajectory rejection, unchanged response bytes and candidate
order, and absence of private/engine fields.

Run:

~~~powershell
cmake --build --preset dev-windows --target teacher_runner_v4_trajectory_test trusted_trajectory_v3_codec_test trusted_trajectory_v3_recorder_test --parallel
ctest --preset dev-windows -R "^(teacher_runner_v4_trajectory_test|trusted_trajectory_v3_codec_test|trusted_trajectory_v3_recorder_test)$" --output-on-failure
~~~

Do not run a full Job0 or Task7 schedule here.

## Task 6: Implement corrected replay and collection successors

**Files:**

- Create: include/ygo/trajectory/replay_v3.hpp
- Create: src/trajectory/replay_v3.cpp
- Create: include/ygo/trajectory/restricted_evidence_v3.hpp
- Create: src/trajectory/restricted_evidence_v3.cpp
- Create: include/ygo/trajectory/shard_v3.hpp
- Create: src/trajectory/shard_v3.cpp
- Create: include/ygo/trajectory/admission_v3.hpp
- Create: src/trajectory/admission_v3.cpp
- Create: include/ygo/trajectory/receipt_v3.hpp
- Create: src/trajectory/receipt_v3.cpp
- Create: include/ygo/trajectory/dataset_manifest_v3.hpp
- Create: src/trajectory/dataset_manifest_v3.cpp
- Create: tests/trajectory/trusted_trajectory_v3_replay_admission_test.cpp
- Create: tests/trajectory/trusted_trajectory_v3_collection_boundary_test.cpp

- [ ] **Step 1: Reconstruct V4 with the V4 resolver**

Replay calls only the V4 identity resolver, reconstructs
CertifiedEnvironmentConfig::canonical_v4(), verifies exact environment and
episode identity bytes/IDs, and replays complete ordered V3 candidate domains.
No replay-local decoder is created.

- [ ] **Step 2: Add strict V3 downstream boundaries**

Each V3 surface validates only its own generation:

~~~text
V3 envelope in V3 shard/evidence/admission/receipt/dataset = accepted when valid
V2 envelope in V3 surface = rejected
V3 envelope in V2 surface = rejected
mixed V2/V3 collection = rejected
~~~

Receipt and dataset validation bind exact envelope, shard, evidence,
trajectory-record, and public-gameplay IDs. No receipt or manifest derives
gameplay semantics from metadata.

- [ ] **Step 3: Preserve failure and privacy semantics**

Interrupted evidence remains restricted. Failed/quarantined artifacts remain
non-admissible. Public gameplay identity excludes collection provenance;
record identity includes the existing shared provenance split. No private state,
submission token, engine step, or raw response enters public values.

- [ ] **Step 4: Run replay/collection tests**

Run:

~~~powershell
cmake --build --preset dev-windows --target trusted_trajectory_v3_replay_admission_test trusted_trajectory_v3_collection_boundary_test --parallel
ctest --preset dev-windows -R "^(trusted_trajectory_v3_replay_admission_test|trusted_trajectory_v3_collection_boundary_test)$" --output-on-failure
~~~

Cover valid V3 replay, tampered action/domain/decision IDs, wrong identity
bytes, V1/V2 rejection, missing evidence, failed/quarantined rejection, and
canonical re-encode failure.

## Task 7: Close Task7 V3 authority and future materialization boundary

**Files:**

- Create: include/ygo/phase6/task7_dataset_authority_provisioning_v3.hpp
- Create: src/phase6/task7_dataset_authority_provisioning_v3.cpp
- Create: tests/phase6/phase6_task7_dataset_authority_provisioning_v3_test.cpp
- Create: include/ygo/model/logical_model_input_v2.hpp
- Create: include/ygo/model/encoded_model_input_v2.hpp
- Create: include/ygo/model/model_supervision_sample_v2.hpp
- Create: include/ygo/phase6/supervision_dataset_v2.hpp
- Create: src/model/logical_model_input_v2.cpp
- Create: src/model/encoded_model_input_v2.cpp
- Create: src/model/model_supervision_sample_v2.cpp
- Create: src/phase6/supervision_dataset_v2.cpp

This task is a later implementation boundary. It is not authorized by the
current design-only slice.

- [ ] **Step 1: Bind Task7 V3 job/schedule/authority values**

Use:

~~~text
environment = episodic_environment.v4
public action adapter = policy.public_action_key.v3
Teacher producer = policy.teacher_core.v3
trajectory = trusted_trajectory.v3
replay/evidence/shard/admission/receipt/dataset = V3 successors
~~~

Keep curriculum, rules bundle, decks, seeds, placement order, starting-player
order, and run-control values unchanged. The schedule remains 16 fixed jobs;
no hash sorting, replacement, retry, or partial authority is permitted.

- [ ] **Step 2: Keep split/vocabulary reuse narrow**

TrainingDatasetSplitV1 may be reused only after its validator proves that it
binds an accepted dataset identity and exact episode IDs without assuming V1
trajectory keys. CardVocabularyV1 may be reused only from public V4
observations. An empty partition remains fail closed.

- [ ] **Step 3: Implement the model/materialization successor only at its boundary**

The model successor carries public_action.v3.* and Select/Unselect operation
metadata rather than dropping it. Existing V1 materialization continues to
reject V3 values. No V1 model function is widened.

- [ ] **Step 4: Test authority rejection**

Require V1/V2/V3 rejection for job, authority, dataset, model, and mixed
membership values. Prove deterministic schedule/job bytes and no partial or
failed authority. The real 16-job collection is not run by this plan.

## Task 8: Short Hiita reproducer

**Files:**

- Create: tests/teacher/teacher_runner_v4_hiita_reproducer_test.cpp
- Create: tests/episodic/episodic_environment_v4_hiita_reproducer_test.cpp

- [ ] **Step 1: Exercise the first corrected boundary**

Use the existing public-safe fixture and stop after a small bounded number of
decisions. Assert:

~~~text
idle_command / Hiita
first material domain is complete and ordered
first material operation is Select
Cancel operation is None
Teacher retains commitment and uses F0
Select score is greater than Cancel score
one material is actually selected
~~~

- [ ] **Step 2: Prove the old short loop is absent**

Assert that the corrected path does not immediately return to the same
idle_command / Hiita state through Cancel. This is a bounded local proof, not
a claim about complete duel termination.

- [ ] **Step 3: Preserve privacy and response behavior**

Compare paired public-equivalent fixtures, candidate order, and exact response
bytes. Do not inspect hidden state. Do not run Job0 to budget.

Run:

~~~powershell
cmake --build --preset dev-windows --target teacher_runner_v4_hiita_reproducer_test episodic_environment_v4_hiita_reproducer_test --parallel
ctest --preset dev-windows -R "^(teacher_runner_v4_hiita_reproducer_test|episodic_environment_v4_hiita_reproducer_test)$" --output-on-failure
~~~

## Task 9: Bounded Job0 validation before any RUN_A reconsideration

**Files:**

- No production files are changed by this validation task.
- Use corrected-generation diagnostics and a fresh output directory.

- [ ] **Step 1: Run exactly Job0 with unchanged budgets**

Use:

~~~text
root seed = 4
placement = NORMAL
starting player = 0
engine process budget = 20000
semantic action budget = 20000
~~~

No retry, budget increase, replacement seed, fallback substitution, or manual
artifact edit is allowed.

- [ ] **Step 2: Require corrected-generation evidence**

The result must report V4 environment, V3 action/domain/decision,
V3 trajectory/replay/admission values, and first corrected material selection.
It must not be called a Task7 authority or RUN_A result.

- [ ] **Step 3: Compare fresh processes**

Run two fresh Job0 processes only after the short reproducer passes. Compare
semantic frame/domain/action streams, closure class, counters, and replay
result. Exclude timestamps, PIDs, paths, and build hashes from semantic equality.

- [ ] **Step 4: Stop for review**

A successful bounded Job0 run does not authorize the fixed 16-job Task7
collection. Explicit authorization is required after independent review.

## Cross-version acceptance matrix

Every implementation task must preserve:

~~~text
historical V1 bytes and goldens unchanged
historical V2/V3 bytes and goldens unchanged
first list maps to Select in corrected generation
second list maps to Unselect in corrected generation
Select and Unselect keys remain distinct
candidate membership and order unchanged
exact engine response bytes unchanged
V2 key in V3 domain rejected
V3 key in V2 domain rejected
episodic_environment.v2 + public identity V1 = accept historical
episodic_environment.v3 + public identity V2 = accept historical
episodic_environment.v4 + public identity V3 = accept corrected
episodic_environment.v4 + public identity V2 = reject
episodic_environment.v3 + public identity V3 = reject
V4 environment with V2 Teacher state rejected
corrected Teacher V3 state with V2 environment rejected
corrected Teacher V3 state with V3 environment rejected
TeacherRunnerV4 with environment V2 or V3 rejected
TeacherRunnerV4 with Teacher V1 or V2 rejected
historical TeacherRunnerV3 with environment V4 rejected
V2 trajectory in V3 replay/admission rejected
V3 trajectory in V2 replay/admission rejected
mixed collection generations rejected
failed/quarantined/interrupted clean authority rejected
public gameplay identity excludes collection provenance
record identity remains provenance-sensitive
no hidden/private value serialized
fresh-process corrected canonical bytes identical
~~~

## Verification commands for each implementation checkpoint

Every checkpoint begins with:

~~~powershell
git diff --check
git status --short
~~~

Relevant regression set:

~~~powershell
cmake --preset dev-windows
cmake --build --preset dev-windows --parallel
ctest --preset dev-windows -R "trajectory|teacher|episodic|identity|protocol|phase6" --output-on-failure
~~~

Historical V1 tests must run rather than be cited from old evidence. A heavy
historical replay test may be recorded as NOT_RUN only when it is outside the
changed owner path and focused successor tests cover the new boundary; the
report must state the exact command and reason.

## Delivery and authorization gates

The design-only commit contains only:

~~~text
docs/superpowers/specs/2026-09-10-msg-select-unselect-semantics-correction-design.md
docs/superpowers/plans/2026-09-10-msg-select-unselect-semantics-correction-migration.md
~~~

No production implementation is part of this design commit. Later
implementation commits use a branch derived from the accepted design commit,
keep every successor generation explicit, run applicable tests, and stop for
independent review before the next boundary.

Current authorization remains:

~~~text
PRODUCTION_MIGRATION_IMPLEMENTATION_AUTHORIZED=NO
TEACHER_BEHAVIOR_CHANGE_AUTHORIZED=NO
F4_CHANGE_AUTHORIZED=NO
BUDGET_CHANGE_AUTHORIZED=NO
TASK7_RUN_A_RETRY_AUTHORIZED=NO
TASK7_RUN_B_AUTHORIZED=NO
TRAINING_AUTHORIZED=NO
~~~
