# MSG_SELECT_UNSELECT_CARD Operation Semantics Correction — Design Freeze

## Status and authority

~~~
TASK=MSG_SELECT_UNSELECT_CARD_OPERATION_SEMANTICS_CORRECTION_MIGRATION_DESIGN_01
STATUS=AUTHORIZED DESIGN FREEZE
BASE=5dbe4651bed25615b759ebfde4328018605fe6ea
PRODUCTION_IMPLEMENTATION=NOT AUTHORIZED BY THIS DOCUMENT
RUN_A_RETRY=NOT AUTHORIZED
RUN_B=NOT AUTHORIZED
TRAINING=NOT AUTHORIZED
~~~

This document freezes the successor decision after the Job0
Hiita → Unselect → Cancel characterization. It does not amend the accepted
V2/V3 contracts and does not change production behavior.

The governing rule is:

~~~
same contract ID MUST NOT silently acquire different canonical semantics
~~~

## Decision

The correction uses an explicit successor generation.

~~~
DECISION=EXPLICIT_SUCCESSOR_GENERATION
V2_IN_PLACE_CORRECTION=FORBIDDEN
V2_ARTIFACT_REINTERPRETATION=FORBIDDEN
V2_TO_CORRECTED_GENERATION_AUTOMATIC_CONVERSION=FORBIDDEN
~~~

The corrected protocol meaning is:

~~~
MSG_SELECT_UNSELECT_CARD first wire list  = SELECT
MSG_SELECT_UNSELECT_CARD second wire list = UNSELECT
~~~

The operation enum values do not change:

~~~
None     = 0
Select   = 1
Unselect = 2
~~~

Only the mapping from the two engine wire lists to those values changes. The
candidate set, source order, selected response index, exact engine response
bytes, and engine legality remain unchanged.

## Proven source facts

The exact pinned runtime inputs are:

~~~
ocgcore     = 9a0c558c2d686542f7914a6d529fd7aa57746aed
CardScripts = f337c87018ca723c1aded5143e616bb649555273
~~~

The pinned Link procedure in
.cache/rules_bundle/cardscripts/proc_link.lua:170-183 passes cg as the first
argument and sg as the second argument to Group.SelectUnselect. It adds a
returned card to sg when it was not already present and removes it when it was
already present. Therefore cg is the selectable/add domain and sg is the
selected/remove domain.

The pinned core implementation in
.cache/rules_bundle/ocgcore/libgroup.cpp:262-296 copies its first argument
into core.select_cards and its second argument into core.unselect_cards. The
message writer in .cache/rules_bundle/ocgcore/playerop.cpp:411-425 serializes
core.select_cards first and core.unselect_cards second.

The current OCGForge decoder in
src/protocol/message_decoder.cpp:821-867 assigns Unselect to the first count
and Select to the second count. The current V3 projection in
src/environment/episodic_environment.cpp:961-990 copies that metadata into the
public V2 identity path. This proves the current inversion:

| Wire value | Pinned runtime meaning | Current OCGForge meaning |
| --- | --- | --- |
| first list / core.select_cards / cg | Select/add | Unselect |
| second list / core.unselect_cards / sg | Unselect/remove | Select |

The current normative V2 documents explicitly freeze that inverted mapping in
docs/contracts/public-action-identity-v2.md and
docs/contracts/episodic-environment-v3.md. Correcting the mapping under those
IDs would change an accepted contract's meaning.

## Characterized causal chain

The accepted public-safe characterization proves the following Job0 pattern:

~~~
same current public gameplay state
  → idle_command selects Hiita
  → first material list is labeled Unselect
  → Teacher receives no positive Select continuation progress
  → F4 ties and selects Cancel
  → Link summon is cancelled
  → same current public gameplay state
  → Hiita remains legal
  → repeat
~~~

The fixed Salamangreat profile and accepted Teacher scoring tests already give
positive continuation progress to Select and zero progress to Unselect and
Cancel. F4 is a downstream tie-break effect, not the primary cause. No
Hiita-specific policy change is authorized.

~~~
PRIMARY_CAUSAL_ROOT=PROVEN_MSG_SELECT_UNSELECT_CARD_OPERATION_DIRECTION_INVERSION
PRIMARY_OWNER=PROTOCOL / DECISION-CANDIDATE SEMANTICS
TEACHER_F4_PRIMARY_ROOT_CAUSE=NO
TEACHER_F4_DOWNSTREAM_EFFECT=YES
HIITA_SELECTION_BUG=NO_EVIDENCE
ENVIRONMENT_LEGALITY_BUG=NO_EVIDENCE
BUDGET_BUG=NO
~~~

## Corrected generation identifiers

The following IDs are frozen for the corrected successor. They are not aliases
for the existing IDs.

### Public environment and identity

~~~
ocgforge.public_action_identity.v3
ocgforge.public_candidate_domain.v3
ocgforge.public_semantic_decision_identity.v3
public_action.v3.

ocgforge.episodic_environment.v4
ocgforge.environment_identity.v4
~~~

public_action_identity.v3 uses the same descriptor field order and primitive
encodings as V2, but its identity-domain strings and key prefix are V3. The
operation codes remain 0/1/2. A corrected V3 public key is produced only from
the corrected wire mapping.

public_candidate_domain.v3 and public_semantic_decision_identity.v3 use the
same field order as their V2 predecessors with V3 domain/schema markers and V3
public action/domain values. Their validators accept only homogeneous V3
values.

episodic_environment.v4 is required because the environment identity input
currently commits to the public action, public candidate-domain, and public
semantic-decision schema IDs. Its canonical identity retains the current V3
field order with these exact substitutions:

~~~
environment identity schema        = ocgforge.environment_identity.v4
episodic environment contract      = ocgforge.episodic_environment.v4
public action identity             = ocgforge.public_action_identity.v3
public candidate-domain identity   = ocgforge.public_candidate_domain.v3
public decision identity           = ocgforge.public_semantic_decision_identity.v3
~~~

The following remain unchanged inside the environment identity:

~~~
ocgforge.decision_protocol.v1
ocgforge.action_identity.v1
ocgforge.candidate_domain.v1
ocgforge.semantic_decision_identity.v1
ocgforge.episode_identity.v1
ocgforge.public_environment_observation.v1
ocgforge.public_safe_state.v1
~~~

The exact V4 environment identity bytes retain the existing canonical field
order and replace only the listed generation bindings plus the recomputed
environment semantic ID.

### Teacher public-input generation

The current EpisodeLocalStrategyStateV2, TeacherStateDeltaV2, and
TeacherRankingResultV2 validate public_action.v2.*. The corrected path
therefore uses:

~~~
EpisodeLocalStrategyStateV3
TeacherStateDeltaV3
StrategyReconciliationResultV3
TeacherRankingResultV3
TeacherCoreV3
DeterministicTeacherPolicyV3
TeacherPolicySessionV3
~~~

The corrected production provenance values are:

~~~
ocgforge.policy.teacher_core.v3
ocgforge.policy.public_action_key.v3
~~~

These Teacher contracts and schemas are reused because their semantic meaning
and canonical field layout are unchanged:

~~~
ocgforge.strategy_profile.v1
ocgforge.teacher_policy_binding.v1
ocgforge.policy_artifact_identity.v1
ocgforge.participant_policy_assignment_identity.v1
ocgforge.policy.teacher_score.v1
ocgforge.policy.teacher_fallback.v1
ocgforge.policy.public_key_tiebreak.v1
ocgforge.policy.deterministic_lexicographic_argmax.v1
ocgforge.no_policy_rng.v1
ocgforge.policy.direct_execution.v1
ocgforge.policy.public_observation.v1
~~~

The binding and artifact schemas remain V1, but their content values and
resulting IDs are newly derived from teacher_core.v3 and
policy.public_action_key.v3. No old V2 binding or artifact ID is reused.
Score dimensions, fallback ordering, F4 tie-break, profile contents, and RNG
behavior remain unchanged.

Because the current TeacherRunnerV3 is bound to the V3 environment and V2
public keys, the corrected execution facade is an explicit successor:

~~~
TeacherRunnerV4
TeacherRunnerV4Config
TeacherRunnerV4TrajectoryRunner
~~~

The V3 runner remains historical and is not widened.

### Trusted trajectory and downstream generation

The corrected trajectory chain is:

~~~
ocgforge.trusted_trajectory.v3
ocgforge.restricted_replay_evidence.v3
ocgforge.public_gameplay_trajectory_identity.v3
ocgforge.trajectory_record_identity.v3

ocgforge.trajectory_shard.v3
ocgforge.restricted_collection_evidence_bundle.v3
ocgforge.admission_receipt.v3
ocgforge.dataset_manifest.v3
ocgforge.dataset_identity.v3
~~~

Each successor is V3-only and binds episodic_environment.v4 and the V3
public action/domain/decision generation where applicable. A V2 codec is never
widened.

Task7 orchestration values also receive explicit successors:

~~~
ocgforge.phase6.task7.dataset_collection_job.v3
ocgforge.phase6.task7.dataset_collection_schedule.v3
ocgforge.phase6.task7.dataset_collection.reference.v3
ocgforge.phase6.task7.dataset_authority.v3
~~~

Existing TrainingDatasetSplitV1 and CardVocabularyV1 may remain shared only
after their validators are proven generation-neutral and their source is the
corrected V3 dataset identity/public observations. Existing V1 model and
materialization contracts cannot consume V3 action keys. The future model
successor is required but is not implemented here:

~~~
ocgforge.model_logical_input.v2
ocgforge.model_encoded_input.v2
ocgforge.model_supervision_sample.v2
ocgforge.phase6.bc_sample_identity.v2
~~~

## Retained contracts and semantic invariants

These values remain shared and byte-compatible:

~~~
Decision Protocol numeric meanings and exact response bytes
internal action/domain/semantic-decision identities V1
episode_identity.v1
PlayerObservation and public observation/safe-state V1
policy provenance/artifact/assignment schemas V1
policy RNG identities and no-policy-RNG V1
Teacher score/fallback/tie-break contracts
StrategyProfile V1
rules bundle, format, duel mode, duel flags, and locked decks
~~~

The corrected migration MUST preserve:

~~~
candidate membership = unchanged
candidate order = unchanged
candidate response index = unchanged
exact engine response bytes = unchanged
engine legality = unchanged
hidden-information boundary = unchanged
episode configuration semantics = unchanged
~~~

The following values intentionally change:

~~~
card_selection_operation for the two MSG_SELECT_UNSELECT_CARD lists
public action keys
public candidate-domain digests
public semantic-decision IDs
environment semantic ID and environment identity schema
episode semantic ID when its parent environment identity changes
Teacher state/delta/ranking values that carry public action keys
trajectory, replay, collection, Task7, and future model identities
~~~

Changing an episode semantic ID in the corrected generation does not change
episode configuration semantics. episode_identity.v1 remains valid because its
codec and seed/seat/deck rules are unchanged; it is recomputed against the new
V4 environment identity.

## Exact backward-compatibility policy

| Generation | Accepted path | Policy |
| --- | --- | --- |
| Historical V1 | Episodic V2 + public action V1 + trusted trajectory V1 | Preserve bytes, validators, goldens, and behavior exactly. |
| Historical V2/V3 | Episodic V3 + public action V2 + trusted trajectory V2 | Preserve as historical evidence under its declared semantics; do not use it as corrected collection input. |
| Corrected successor | Episodic V4 + public action V3 + trusted trajectory V3 | New production path only; every boundary requires exact homogeneous generation. |

The historical V2 path MUST NOT be:

~~~
reinterpreted with first-list=Select semantics
replayed as if it were V3 corrected evidence
converted while retaining its old IDs
relabeled with V3/V4 contract IDs
admitted into the corrected dataset
~~~

New Task7 evidence MUST be regenerated from source execution through the full
corrected generation. A failed or partial historical V2 run is diagnostic
evidence only.

## Source inventory and ownership matrix

| Component | Current binding | Corrected successor required? | Reuse allowed? | Future owner | Reason |
| --- | --- | --- | --- | --- | --- |
| include/ygo/protocol/action_candidate.hpp | Internal CardSelectionOperation enum | No | Yes | Protocol | Numeric meanings already match the pinned runtime. |
| src/protocol/message_decoder.cpp | First list Unselect, second list Select | Yes, generation-specific decode entry point | Historical decoder remains | Protocol / Decision Protocol adapter | The current wire projection is inverted; protocol identity and response bytes remain unchanged. |
| include/ygo/environment/public_action_identity.hpp and .cpp | public_action_identity.v2 and public_action.v2.* | Yes, V3 API and namespace | V2 remains historical | Public identity | Operation metadata changes the public meaning and key generation. |
| Candidate-domain identity | public_candidate_domain.v2 | Yes, V3 | V2 remains historical | Public identity | Domain members change from V2 keys to corrected V3 keys. |
| Public decision identity | public_semantic_decision_identity.v2 | Yes, V3 | V2 remains historical | Public identity | It commits to the corrected domain identity and generation. |
| EpisodicEnvironment projection | episodic_environment.v3 | Yes, V4 | Public observation child values may be reused | Environment | The environment identity commits to changed public identity schemas. |
| Environment identity | environment_identity.v3 | Yes, V4 | V3 remains historical | Environment identity resolver | A same-ID re-encode would change canonical identity bytes. |
| episode_identity.v1 | Shared episode codec | No | Yes | Environment identity/replay | Seed, seat, deck, and episode semantics are unchanged. |
| Public observation/safe state | V1 contracts | No | Yes | Observation | No current public state semantics change. |
| EpisodeLocalStrategyStateV2 / TeacherStateDeltaV2 | Explicit V2 public-key validators | Yes, V3 | V2 remains historical | Teacher | Existing state fields reject V3 keys; V2 must not be widened. |
| TeacherRankingResultV2 / TeacherCoreV2 | V2 key validation and output | Yes, V3 | V2 remains historical | Teacher | Corrected public input requires a V3 result surface. |
| Teacher provenance values | teacher_core.v2 and policy.public_action_key.v2 | Yes, V3 values | Unchanged schemas reused | Policy provenance | Output generation changes; score/profile/RNG schema meanings do not. |
| TeacherRunnerV3 | V3 environment and V2 public-key bindings | Yes, V4 | V3 remains historical | Runner | Runner generation is part of the trusted execution boundary. |
| Trajectory DTO/codec/recorder | trusted_trajectory.v2 | Yes, V3 | V2 remains historical | Trajectory | Canonical values reference the corrected public generation. |
| Replay/evidence | restricted_replay_evidence.v2 | Yes, V3 | V2 remains historical | Replay | Replay must reconstruct V4 and verify V3 public semantics. |
| Trajectory identities | Gameplay/record identity V2 | Yes, V3 | V2 remains historical | Trajectory identity | Public records and collection bindings change. |
| Shard/admission/receipt/dataset | V2 downstream surfaces | Yes, V3 | V2 remains historical | Collection authority | V2 validators must not accept V3 artifacts. |
| Task7 job/schedule/authority | phase6 Task7 V2 values | Yes, V3 | V2 remains historical | Phase 6 | Job and authority identity must bind V4/V3 values. |
| Training split/vocabulary | Split V1 and CardVocabulary V1 | No, subject to closure tests | Yes | Phase 6 | Their source can remain generation-neutral if it binds the new dataset identity and public observations only. |
| Model/materialization | V1 model and sample contracts | Yes, future V2 | V1 remains historical | Model / Phase 6 | Existing validators reject V3 action keys and cannot drop operation metadata. |
| Rules/decks/scripts | Pinned certified bundle | No | Yes | Rules authority | The correction changes public interpretation, not legality or inputs. |

## Golden and replay inventory

Historical values remain untouched:

~~~
all public_action_identity.v2 leaf goldens
episodic_environment.v3 and environment_identity.v3 goldens
trusted_trajectory.v2 candidate/frame/record/manifest/envelope goldens
restricted_replay_evidence.v2 and V2 trajectory identities
all V1 action, environment, trajectory, replay, admission, dataset, and model goldens
~~~

The V2 leaf codec goldens remain valid for the same encoded operation values; the
mapping-specific V3/V4 tests receive new expected values. New successor goldens
are required for:

~~~
public_action.v3 Select and Unselect
public_candidate_domain.v3
public_semantic_decision_identity.v3
environment_identity.v4 and episode identity under V4
Teacher V3 state/delta/ranking values
trusted trajectory V3 and replay evidence V3
public gameplay/record identity V3
all V3 collection and Task7 authority artifacts
future model/materialization V2 values
~~~

No old expected hash may be edited to make a corrected implementation pass.

## Cross-version rejection matrix

Every successor boundary rejects before mutation:

~~~
V2 public action key in V3 public domain       → reject
V3 public action key in V2 public domain       → reject
V3 environment with V2 public identities      → reject
V4 environment with V2 Teacher state          → reject
V3 Teacher state with V2 environment           → reject
V2 trajectory in V3 replay/admission           → reject
V3 trajectory in V2 replay/admission           → reject
V2 envelope in V3 shard                        → reject
V3 envelope in V2 shard                        → reject
mixed V2/V3 collection artifacts               → reject
V2 Task7 authority in corrected V3 Task7 path  → reject
~~~

No validator may normalize a prefix, drop operation metadata, substitute a
candidate index, or reconstruct a different generation.

## Acceptance gates for the successor design

~~~
PINNED_CORE_WIRE_SEMANTICS=PROVEN
PINNED_CARDSCRIPTS_LINK_SEMANTICS=PROVEN
CURRENT_DECODER_INVERSION=PROVEN
CURRENT_PUBLIC_V2_CONTRACT_INVERSION=PROVEN
AFFECTED_CONTRACT_IDS_INVENTORIED=PASS
AFFECTED_GOLDENS_INVENTORIED=PASS
AFFECTED_REPLAY_TRAJECTORY_IDENTITIES_INVENTORIED=PASS
MIGRATION_VERSIONING_DECISION=EXPLICIT_SUCCESSOR_GENERATION
BACKWARD_COMPATIBILITY_POLICY=EXACT
CORRECT_OPERATION_MAPPING=FIRST_SELECT_SECOND_UNSELECT
CANDIDATE_MEMBERSHIP_CHANGE=NO
CANDIDATE_ORDER_CHANGE=NO
EXACT_RESPONSE_BYTES_CHANGE=NO
ENGINE_LEGALITY_CHANGE=NO
TEACHER_STRATEGY_SEMANTICS_CHANGED=NO
TEACHER_SCORE_DIMENSIONS_CHANGED=NO
FALLBACK_ORDER_CHANGED=NO
F4_TIE_BREAK_CHANGED=NO
HIITA_PROFILE_CHANGE_REQUIRED=NO
~~~

## Scope boundary

This design freeze does not authorize:

~~~
decoder or environment correction implementation
Teacher implementation
Runner implementation
trajectory/replay/admission implementation
Task7 retry or new collection
model materialization
RUN_A or RUN_B
training
budget changes
performance optimization
~~~

The next implementation may run only the short Hiita reproducer after the
successor public/protocol/Teacher/Runner boundaries are implemented and
independently reviewed. A new bounded Job0 validation precedes any future
request to reconsider Task7 RUN_A.
