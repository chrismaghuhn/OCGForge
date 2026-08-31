# OCGForge Phase 4B Task 1 — TeacherCore and StrategyProfile Contract Freeze

**Status:** normative Phase-4B Task-1 design and contract freeze; no production implementation claim

**Issue:** [#16](https://github.com/chrismaghuhn/OCGForge/issues/16)

**Base:** `2493046c967f4718dbbf4a63098b37edb0b5a336` (`origin/main`, verified live on 2026-08-31)

**Frozen Phase-4A baseline:** `H_exec = 66d30967018c6ce106d131c7e02147ffaf194a56`, `H_evidence = 6ab937a61ac39eecfb7bc91174ca0ba92b3edd09`, acceptance schema `ocgforge.phase4a_acceptance.v2`, and recorded `G00–G29 = PASS`. This document does not modify, rerun, or reinterpret that evidence.

The words **MUST**, **MUST NOT**, **SHOULD**, and **FAIL CLOSED** are normative. Research documents under `docs/research/teacher_strategy/` are inputs only. The accepted contracts and executable evidence remain authoritative.

## 1. Scope, authority, and non-goals

Phase 4B adds a deterministic Teacher policy above the accepted Phase-4A public-policy substrate. Task 1 freezes the contracts and the implementation sequence. It does not add C++ or Python production behavior, change public environment bytes, alter the rules bundle, change locked decks, or amend Phase-3 or Phase-4A versioned contracts.

Authority is ordered as follows:

1. the pinned rules bundle and its ordered repository patchset;
2. accepted Phase-3 contracts, accepted Phase-4A public contracts, and their current implementation;
3. current executable acceptance evidence;
4. this Phase-4B contract freeze;
5. the non-normative Teacher research pack.

This task does not authorize:

- neural networks, Behavior Cloning, PPO, IMPALA, R2D2, APPO, self-play, league training, MCTS, CFR, or search-assisted Teacher behavior;
- arbitrary-deck support or a new legality implementation;
- new hidden/private observation fields;
- a Gym/PettingZoo wrapper or a Phase-4C frozen evaluation harness;
- mutation of `fixtures/decks/`, `third_party/rules_bundle.lock.json`, or the pinned rules materialization;
- a Teacher-specific trajectory, replay, or admission bypass.

## 2. Authoritative Teacher boundary

Every future Teacher decision has exactly this shape:

```text
existing ygo::policy::PolicyInput
  = PublicEnvironmentObservation
  + complete ordered EnvironmentActionCandidate[]
  + immutable StrategyProfile
  + participant/episode-local public-safe strategy state
        ↓
TeacherCore
        ↓
existing PolicySelection with exactly one existing public_action_key
```

The legal domain remains owned by `ygo::environment` and the Phase-4A public projection. The Teacher evaluates desirability only. It never asks a second legality oracle whether a move, target, material set, zone, chain response, continuation, or response byte is legal.

### 2.1 Exact allowed input types

The per-decision selector surface MUST remain the existing `ygo::policy::PolicyInput` from `include/ygo/policy/policy.hpp`:

```cpp
struct PolicyInput final {
    const environment::PublicEnvironmentObservation& observation;
    const std::vector<environment::EnvironmentActionCandidate>& candidates;
};
```

TeacherCore may additionally receive only:

| Input | Allowed use | Ownership rule |
| --- | --- | --- |
| `PolicyInput.observation` | Decode the accepted `ocgforge.public_environment_observation.v1` and, when needed, its canonical `ocgforge.public_safe_state.v1` bytes. | `ygo::environment` owns projection and visibility. The Teacher never receives a caller-chosen state blob. |
| `PolicyInput.candidates` | Read every current public candidate descriptor in the published order. | `ygo::environment` owns completeness, membership, ordering, and public action identity. |
| validated immutable `StrategyProfileV1` | Read static roles, public predicates, resources, goals, lines, recovery edges, interaction rules, and preferences. | `ygo::teacher` owns profile meaning and content identity. |
| `EpisodeLocalStrategyStateV1` | Reconcile bounded public-safe plan/history facts for the same participant and episode. | The owning Teacher session owns lifecycle and isolation. |
| fixed TeacherCore semantic configuration | Use the versioned score, fallback, predicate, and tie-break contracts selected by the immutable Teacher binding. | Configuration is artifact-bound and is not an ad hoc per-call input. |

`PublicSafeStateView` is derived only by strict decoding of `observation.canonical_safe_state_bytes()`. A convenience overload that accepts `DecisionFrame`, `PlayerObservation`, `CoreHost`, `EpisodeSpec`, `ActionSelection`, a raw byte vector, or an arbitrary typed state is outside the Teacher boundary and MUST NOT be added.

The policy runner may see a `DecisionFrame` and a `SubmissionToken` because it must call the existing V2 environment. It is an adapter/control-plane layer. It copies only the public observation and candidate vector into `PolicyInput`; it MUST NOT pass the frame, token, engine-step value, internal decision identity, or internal candidate identity into TeacherCore.

### 2.2 Explicitly unavailable facts

The current `PolicyInput` does not contain `EnvironmentDecisionRequest`, `EnvironmentContinuationView`, or the internal protocol request. It contains candidate-level continuation operation metadata only. Therefore the following are **BLOCKED** for Phase-4B v1 unless they are already represented in the public observation or in a current candidate descriptor:

- continuation-wide `selected_indices`, `remaining_indices`, `assigned_amounts`, `min_count`, `max_count`, `target_sum`, `required_amount`, masks, `can_finish`, and `can_cancel`;
- an exact request family when `PublicObservationDecisionContext.kind` is absent;
- exact effect-use or temporary-restriction facts omitted by the accepted public-safe state/event vocabulary;
- opponent hidden hand identities, hidden deck order, hidden Extra Deck identities, or hidden physical identity after a knowledge-destroying transition;
- internal semantic keys, protocol decision IDs, continuation IDs, response bytes, raw engine messages, engine/query caches, pointers, PID, wall time, thread identity, provider identity, or scheduling/completion order;
- any strategic fact that cannot be derived from the two public inputs and the immutable profile.

When a researched line needs one of these facts, the implementation MUST mark that line/rule unsupported or use an explicitly declared coarser public fallback. It MUST NOT read a private source, infer a hidden value, reconstruct legality, or silently pretend the fact is known. A future public-input extension would require a separate versioned contract decision; Task 1 does not make that extension.

## 3. Layer ownership

| Concern | Owning layer | Frozen boundary |
| --- | --- | --- |
| Legal candidate construction, completeness, order, and public-key projection | `ygo::environment` / Phase 4A | The supplied complete ordered vector is authoritative. The Teacher cannot filter, deduplicate, truncate, reorder, or fabricate it. |
| Teacher policy execution | `ygo::policy` | A future `DeterministicTeacherPolicy` adapts the existing `PolicyInput` and `PolicySelection` to V2. It owns session lifecycle, not strategy meaning. |
| TeacherCore | `ygo::teacher` | Interprets public state, reconciles state, extracts features, controls goals/lines, evaluates every candidate, resolves deterministically, and emits derived explanation data. |
| StrategyProfile | `ygo::teacher` immutable artifact | Supplies deck/matchup-specific data. It never generates legal actions or owns runtime mutable state. |
| Profile identity and content binding | `ygo::teacher` codec plus existing `ygo::trajectory::PolicyArtifact` metadata field | `StrategyProfileV1` and `TeacherPolicyBindingV1` are content-addressed. The binding is carried in existing `PolicyArtifact.artifact_metadata_identity`; no new trajectory field is introduced. |
| Episode-local strategy state | the participant's `DeterministicTeacherPolicy` session | State is bounded, public-derived, participant/episode-local, reset explicitly, and committed only after accepted V2 transitions. |
| Candidate feature extraction | generic `TeacherCore` | Reads only public observation, current public candidate metadata, accepted public history, and profile references. It does not own legality. |
| Candidate scoring and ranking | generic `TeacherCore` / deterministic resolver | One logical evaluation record per supplied candidate; fixed lexicographic signed-integer score vector; no candidate-index authority. |
| Deterministic tie-breaking | versioned `ocgforge.policy.public_key_tiebreak.v1` contract owned by `TeacherCore` | After all score dimensions tie, choose the bytewise-ascending existing `public_action_key`. Duplicate/invalid keys fail closed. |
| Fallback behavior | versioned `ocgforge.policy.teacher_fallback.v1` contract owned by `TeacherCore` | F0 through F4 are explicit, complete-domain stages. Fallback level is visible in derived policy metadata. |
| Teacher diagnostics/explanation | derived `ygo::teacher` audit layer | Optional, public-safe, canonical only when emitted, and excluded from gameplay identity, replay inputs, and admission requirements. |
| Teacher policy provenance | existing `ygo::trajectory` Phase-3 contracts, populated by `ygo::policy` | Existing `PolicyArtifact`, participant assignment, `PolicyRngDecisionProvenance`, and `PolicyProvenanceEnvelope` remain the owners. Teacher adds no competing provenance schema. |
| Trajectory attribution and admission | existing `TrajectoryRecorder` → shard → semantic replay → admission path | Accepted Teacher actions use the same canonical `DecisionRecord`, `CandidateTrajectoryShard`, replay, receipt, and dataset path as every other trusted policy. |

No layer may duplicate the authority of another layer. In particular, a profile rule may say that a supplied candidate advances a goal; it may not enumerate the candidates that should have existed.

## 4. Teacher execution and result contract

The future policy uses the existing `PolicySelection` as its gameplay-facing result. A successful deterministic Teacher selection is:

```text
PolicySelection.value.public_action_key = one existing current candidate key
PolicySelection.value.rng_cursor = absent
```

The Teacher never returns a candidate vector index, internal semantic key, response selector as a vector coordinate, response bytes, continuation ID, token, or raw engine value. The runner copies the selected public key into the existing V2 `ActionSelection` together with the current control-plane fields that never enter TeacherCore.

The pure Teacher evaluation result is derived policy data with this shape:

```text
TeacherRankingResult
- status: SELECTED | INVALID_INPUT | BLOCKED | UNSUPPORTED
- evaluations: CandidateEvaluation[]       // exact input order and exact count
- selected_public_action_key: optional string
- selected_score_vector: optional ScoreVector
- fallback_level: optional F0..F4
- explanation: optional TeacherDecisionExplanation
- proposed_state_delta: optional TeacherStateDelta
- diagnostic: optional structured public-safe failure
```

Each `CandidateEvaluation` contains:

```text
- public_action_key
- evaluation_status: SUPPORTED | NOT_APPLICABLE | UNSUPPORTED | INVALID
- fixed public feature values
- fixed signed-integer score vector when the selected stage supports it
- matched profile intent/goal/line IDs
- public-safe reason IDs
```

The evaluation vector remains aligned with the supplied candidate vector. That alignment is an evidence shape, not a candidate identity. The selected action identity is the exact public key.

For a valid, nonempty domain:

```text
evaluations.size() == candidates.size()
each candidate contributes exactly one evaluation record
every supplied candidate remains in the ranking evidence
selected_public_action_key occurs exactly once in the supplied domain
```

An empty candidate vector at an actionable frame, a malformed public observation, a noncanonical candidate descriptor, an invalid public key, or a duplicate public key produces no selection. These are structural checks on the delivered authoritative vector and public observation. Once they pass, TeacherCore treats the supplied vector as authoritative; it does not determine whether the upstream legal domain is complete. Environment/Phase 4A owns that completeness guarantee. Teacher acceptance proves preservation as `N` supplied candidates to `N` stable evaluation records with the same ordered keys, not legal-domain completeness. A structurally malformed vector is never repaired by choosing an element that happens to be present.

## 5. Immutable StrategyProfile v1

### 5.1 Normative profile contents

The v1 profile is a declarative artifact for one exact locked deck against one exact locked opponent deck. The following components are normative and sufficient for the first Swordsoul Tenyi and Salamangreat slices:

1. card-role catalog;
2. public resource model;
3. public candidate-intent catalog;
4. goal catalog;
5. partial-order line graph;
6. recovery edges;
7. public interaction map;
8. deterministic integer preference table.

The profile contains no executable code, exact future action script, candidate index, internal semantic key, response bytes, hidden card instance, hidden order, pointer, path, or mutable episode state. A profile may describe all cards in the exact bound deck and the exact static opponent deck list because those are immutable configuration; it may not claim that a particular unseen copy is currently in a hidden hand or deck position.

### 5.2 Binding and value model

The canonical `StrategyProfileV1` content fields occur in this order:

```text
domain:string = ocgforge.strategy_profile.v1
schema:string = ocgforge.strategy_profile.v1
matchup_id:string
rules_bundle_id:string
format_id:string
duel_mode:string
duel_flags:u64be
own_deck_role:u8
own_deck_id:string
own_deck_sha256:string
opponent_deck_role:u8
opponent_deck_id:string
opponent_deck_sha256:string
card_roles:vector<CardRoleEntry>
resources:vector<ResourceDefinition>
candidate_intents:vector<CandidateIntentDefinition>
goals:vector<GoalDefinition>
lines:vector<LineDefinition>
recovery_edges:vector<RecoveryEdge>
interactions:vector<InteractionRule>
preferences:vector<PreferenceEntry>
```

The first v1 accepted matchup is `ocgforge.matchup.swordsoul_salamangreat.v1`, with `rules_bundle_id` `3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f`, `format_id` `TCG_ADVANCED_2026_05_18`, `duel_mode` `DUEL_MODE_MR5`, and `duel_flags` `190464`. The profile binding must match the live `CertifiedEnvironmentConfig` and `EpisodeSpec` assignment. The exact deck identities are:

| Profile role | Deck ID | SHA-256 |
| --- | --- | --- |
| first locked deck | `ocgforge.swordsoul_tenyi.ml_v1` | `8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7` |
| second locked deck | `ocgforge.salamangreat.ml_v1` | `6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188` |

Two initial profiles are expected: one for Swordsoul Tenyi as the own deck against Salamangreat, and one for Salamangreat as the own deck against Swordsoul Tenyi. Seat assignment, starting player, participant, and assignment epoch remain outside profile content and are validated through the existing `ParticipantPolicyAssignment`.

The profile entry types are:

```text
CardRoleEntry
- passcode:u32be
- role_ids:vector<string>

ResourceDefinition
- resource_id:string
- public_fact_id:string
- max_value:u32be
- preservation_priority:i32
- conversion_priority:i32

PredicateRef
- scope:u8: OBSERVATION | CANDIDATE | ACCEPTED_PUBLIC_HISTORY | PROFILE_STATIC
- predicate_id:string
- arguments:vector<PredicateAtom>

PredicateAtom
- kind:u8: TOKEN | U64 | I32 | PASSCODE | BOOLEAN
- value encoding: TOKEN=length-prefixed UTF-8 string, U64=u64be, I32=signed i32 bits, PASSCODE=u32be, BOOLEAN=u8 0/1

CandidateIntentDefinition
- intent_id:string
- public_predicates:vector<PredicateRef>

GoalDefinition
- goal_id:string
- priority:i32
- preconditions:vector<PredicateRef>
- completion_predicates:vector<PredicateRef>
- stop_predicates:vector<PredicateRef>

LineDefinition
- line_id:string
- goal_id:string
- applicability_predicates:vector<PredicateRef>
- required_resources:vector<ResourceRequirement>
- optional_resources:vector<string>
- nodes:vector<LineNode>
- dependencies:vector<NodeDependency>
- recovery_edge_ids:vector<string>

LineNode
- node_id:string
- candidate_intent_ids:vector<string>
- completion_predicates:vector<PredicateRef>
- preserve_resource_ids:vector<string>
- stop_predicates:vector<PredicateRef>

NodeDependency
- predecessor_node_id:string
- successor_node_id:string

ResourceRequirement
- resource_id:string
- minimum:u32be

RecoveryEdge
- recovery_edge_id:string
- source_kind:u8: GOAL | LINE | NODE
- source_id:string
- invalidation_reason_ids:vector<string>
- preconditions:vector<PredicateRef>
- candidate_intent_ids:vector<string>
- target_goal_id:string
- target_line_id:optional<string>
- preserve_resource_ids:vector<string>
- confidence_cap:u8

InteractionRule
- interaction_id:string
- trigger_predicates:vector<PredicateRef>
- candidate_intent_ids:vector<string>
- timing_priority:i32
- preserve_resource_ids:vector<string>

PreferenceEntry
- dimension:u8: one of the fixed score dimensions in section 7
- subject_kind:u8: GLOBAL | GOAL | LINE | INTENT | RESOURCE | INTERACTION
- subject_id:string
- value:i32
```

IDs are lowercase ASCII canonical tokens. Card roles use passcodes, not localized names. A predicate scope has no `PRIVATE`, `ENGINE`, or `INTERNAL` value. Predicate IDs and their argument meanings must be registered by the immutable TeacherCore artifact; an unknown predicate or incompatible argument fails profile validation.

The line graph is a directed acyclic partial order. A node dependency expresses a required predecessor; independent nodes remain reorderable when the current engine domain supplies both choices. A line node and recovery edge describe candidate intent, not a future action. The profile never stores a candidate key to execute later.

The profile enum codes are fixed: `own_deck_role` and `opponent_deck_role` use the accepted `DeckRole` codes `0=FirstLockedDeck` and `1=SecondLockedDeck`; predicate scopes are `0=OBSERVATION`, `1=CANDIDATE`, `2=ACCEPTED_PUBLIC_HISTORY`, `3=PROFILE_STATIC`; predicate atoms are `0=TOKEN`, `1=U64`, `2=I32`, `3=PASSCODE`, `4=BOOLEAN`; recovery sources are `0=GOAL`, `1=LINE`, `2=NODE`; preference subjects are `0=GLOBAL`, `1=GOAL`, `2=LINE`, `3=INTENT`, `4=RESOURCE`, `5=INTERACTION`; confidence caps/classes are `0=HIGH`, `1=MEDIUM`, `2=LOW`, `3=FALLBACK`. Unknown codes fail closed.

### 5.3 Canonical serialization and identity

Profile canonical bytes use the primitive encoding already accepted by Phase 3: UTF-8 length-prefixed strings, length-prefixed byte vectors, fixed-width big-endian unsigned integers, signed `i32` two's-complement bits, boolean `0`/`1`, and `u32be` vector counts. The profile content identity is:

```text
strategy_profile_id =
  ocgforge.strategy_profile.v1.<64 lowercase hex>
```

The 64 lowercase hexadecimal value is SHA-256 of the canonical StrategyProfileV1 content bytes.

The derived ID is not included in the bytes hashed to create itself. A stored profile envelope may carry the ID after the content bytes, but a decoder MUST recompute and compare it. The profile ID never includes a filename, directory, symlink, host path, URI, branch, process identity, or publication location.

Canonical ordering is part of the contract:

- `card_roles` ascending by `passcode`; each `role_ids` vector strictly ascending and unique;
- `resources`, `candidate_intents`, `goals`, `lines`, `recovery_edges`, and `interactions` ascending by their ID; all referenced ID vectors strictly ascending and unique;
- line nodes ascending by `node_id`; node dependencies ascending by `(predecessor_node_id, successor_node_id)` and acyclic;
- preference entries ascending by `(dimension, subject_kind, subject_id)` and unique;
- every `PredicateRef` vector currently defined by StrategyProfileV1 is conjunction/set-like, sorted by canonical encoded bytes, and strictly unique; Phase-4B v1 has no order-significant PredicateRef alternative vector;
- a future ordered-alternative list requires an explicit versioned field/type and cannot reinterpret any v1 vector;
- no authoritative map or set is serialized directly.

The codec rejects unknown schema/domain values, invalid enum values, malformed UTF-8, invalid identity tokens, duplicate or unsorted entries, dangling references, duplicate predicate atoms where uniqueness is required, cycles, invalid confidence caps, out-of-range integers, missing required binding fields, and a mismatched derived ID or trailing bytes. The profile/binding validator additionally rejects mismatched deck/rules values. Neither component sorts, merges, defaults, or silently drops malformed data.

### 5.4 Immutable publication and compatibility

Profile bytes MUST be published through an immutable content-addressed policy registry. A filesystem path can locate bytes for a build, but it is never identity or semantic input. The registry verifies the profile ID before a Teacher session is created and retains the exact canonical bytes for the declared ID. Updating a profile creates new canonical bytes and a new ID; in-place replacement under an old ID is invalid.

`StrategyProfileV1` is exact-version only. A v1 reader rejects v2 or unknown profile domains and does not use aliases such as `latest`. Changing field order, enum meaning, predicate meaning, score meaning, binding semantics, or canonical encoding requires a new versioned domain. A behavior-preserving implementation optimization may retain the same ID only when the canonical profile bytes and all versioned meanings remain unchanged.

The profile is additionally bound through this immutable metadata artifact:

```text
TeacherPolicyBindingV1
- domain:string = ocgforge.teacher_policy_binding.v1
- schema:string = ocgforge.teacher_policy_binding.v1
- teacher_core_artifact_identity:string
- strategy_profile_id:string
- score_contract_identity:string
- fallback_contract_identity:string
- tie_break_contract_identity:string
- diagnostic_contract_identity:optional<string>

teacher_policy_binding_id =
  ocgforge.teacher_policy_binding.v1.<64 lowercase hex>
```

The 64 lowercase hexadecimal value is SHA-256 of the canonical TeacherPolicyBindingV1 bytes.

The binding is not a second trajectory/provenance system. It is immutable policy metadata carried in the existing Phase-3 `PolicyArtifact.artifact_metadata_identity` slot. A deterministic Teacher artifact MUST set that slot to the binding ID. The production policy resolver registers the exact content identity under the existing `ArtifactMetadataArtifact` category and validates the referenced profile bytes before execution.

The binding fields have these exact identity classes:

| Field | Required identity |
| --- | --- |
| `teacher_core_artifact_identity` | A canonical identity valid for the existing `ProvenanceKind::ProducerImplementation` category: either an exact versioned contract identity or a content-addressed implementation identity. |
| `strategy_profile_id` | The exact content identity `ocgforge.strategy_profile.v1.<64 lowercase hex>`. |
| `score_contract_identity` | The exact literal `ocgforge.policy.teacher_score.v1`. |
| `fallback_contract_identity` | The exact literal `ocgforge.policy.teacher_fallback.v1`. |
| `tie_break_contract_identity` | The exact literal `ocgforge.policy.public_key_tiebreak.v1`. |
| `diagnostic_contract_identity` | When present, the exact literal `ocgforge.teacher_decision_explanation.v1`. |

No path, mutable alias, arbitrary token, or unversioned identity is valid. Task 2 validates these canonical identity classes and exact literals. Production registry membership for the concrete Teacher producer remains a later policy-provenance integration task; it is not silently implied by profile codec validation.

## 6. Episode-local strategy state

Phase-4B v1 **does need** bounded mutable episode-local strategy state because a partial-order line needs current progress, recovery status, and public resource summaries. The state is advisory policy memory. Current public input remains authoritative at every decision.

### 6.1 Exact state fields

`EpisodeLocalStrategyStateV1` contains only:

```text
strategy_profile_id:string
active_goal_id:optional<string>
active_line_id:optional<string>
completed_line_node_ids:vector<string>
achieved_goal_ids:vector<string>
public_resource_facts:vector<PublicFactValue>
public_restriction_facts:vector<PublicFactValue>
public_threat_facts:vector<PublicFactValue>
last_accepted_decision_index:optional<u64be>
last_accepted_public_action_key:optional<string>
```

`PublicFactValue` is encoded as `fact_id:string`, `value_kind:u8`, the kind-specific value, and `validity_scope:u8`. The value kinds are `0=BOOLEAN`, `1=U64`, `2=I32`, and `3=TOKEN`; the validity scopes are `0=CURRENT_RECONCILIATION` and `1=ACCEPTED_PUBLIC_HISTORY`. Fact IDs and token values use canonical UTF-8 strings. Values are bounded by the registered fact definition and are checked before commit. It contains no physical-card identity. Public threat facts are threat classes and scalar severity only; a current target locator is read from the current candidate/observation and is never queued in state.

The state MUST NOT contain an episode root seed, engine-step index, submission token, internal semantic key, protocol decision ID, continuation ID, response bytes, raw message, private locator, hidden card identity, opponent hidden hand/deck fact, pointer, cache key, PID, wall time, thread/provider identity, or candidate vector index.

### 6.2 Lifecycle and reconciliation

1. **Reset:** construct an empty state bound to the validated profile ID. No state is carried across episodes, participants, seat assignments, or worker processes.
2. **Propose:** decode the current public observation, reconcile state against current public facts/events, evaluate the complete current domain, and return a proposed state delta. Proposal is pure and does not mutate trusted state.
3. **Accept:** after V2 returns `StepAccepted` and the next public frame/closure is valid, commit the delta for an actionable next frame and reconcile that next frame. Intermediate continuation actions obey the same rule. A terminal or run-interrupted closure ends the session; no future strategic state is required.
4. **Reject:** `StepRejected`, policy failure, malformed input, reset rejection, or failed interrupt commits zero state delta. The existing runner quarantines a policy-origin rejection and does not retry with another action.
5. **Gameplay interruption/recovery:** when an accepted engine transition changes the next public frame, observation reconciliation expires contradictory facts, invalidates dependent line nodes, records public invalidation reason IDs, and selects a current recovery/replan candidate. A run-control interruption closes the episode and discards the state.

If state and current public observation disagree, current public evidence wins. The Teacher removes unsupported memory, invalidates dependent goals/lines, and either uses an explicitly supported recovery/fallback or fails closed. It never synthesizes a missing resource.

### 6.3 Plan and line progress

The plan is represented by `active_goal_id`, `active_line_id`, and the set of completed line node IDs. Goal completion is recorded only after an accepted action and a subsequent public observation satisfy the goal's completion predicates. Public preconditions and resource requirements are evaluated against the current public state; they do not prove legality. Candidate intent edges match the current supplied candidate descriptor and cannot generate or queue an action.

The following invalidation classes are stable v1 reason IDs: `starter_not_resolved`, `expected_body_removed`, `resource_consumed`, `restriction_active`, `zone_unavailable`, `copy_unavailable`, `target_absent`, `payoff_answered`, `lethal_unproven`, and `public_state_contradiction`. A profile may use only registered reason IDs. A new meaning requires a new versioned reason contract or profile identity.

No field stores an expected future `public_action_key`, candidate vector position, target, material set, zone, response, or continuation. Every actionable frame is re-evaluated from current public input.

## 7. Complete-domain ranking and deterministic resolution

### 7.1 Exactly-once domain invariant

For every actionable frame for which Environment/Phase 4A has supplied an authoritative nonempty vector that passes TeacherCore's structural checks, TeacherCore performs one logical evaluation pass over that vector. It produces one evaluation record for every supplied candidate in the original order and resolves only from those records. This is preservation evidence, not an independent proof of legal-domain completeness. A profile, rule, fallback, or diagnostic may not remove a candidate from the evidence.

The implementation may use an internal lookup cache, but authoritative iteration uses the supplied vector order or an explicitly sorted canonical ID list. A cache is never authoritative, and cache iteration never decides the result.

### 7.2 Fixed score vector

The v1 score is a nine-component lexicographic vector, higher values preferred in every component:

```text
0 survival_or_guaranteed_lethal_class
1 active_goal_line_or_validated_recovery_progress
2 immediate_tactical_necessity
3 interaction_timing
4 public_target_value
5 resource_preservation_and_cost
6 engine_follow_up_value
7 battle_and_main_phase_2_value
8 profile_preference
```

The final equality completion is not a tenth numeric score. If all nine components are equal, `ocgforge.policy.public_key_tiebreak.v1` selects the bytewise-ascending `public_action_key`. Candidate order is never a tie-break.

The exact nine dimensions and their order are owned by the exact score contract `ocgforge.policy.teacher_score.v1`. That contract also owns the signed `i32` contribution range, signed `i64` accumulation, checked overflow/underflow behavior, and higher-is-better lexicographic comparison. The binding validation in Task 2 MUST require that exact score-contract literal.

Profile preference values and feature contributions are signed `i32` values in `[-1_000_000, +1_000_000]`. Score components are signed `i64`; every multiplication and addition is checked. Overflow, underflow, invalid conversion, or an out-of-range profile value is `INVALID` and is never wrapped, saturated, or silently clamped. A stage that cannot produce a total score for every candidate is not a valid ranking stage; TeacherCore proceeds to the next declared fallback stage or fails closed.

### 7.3 Unsupported evaluation behavior

- `NOT_APPLICABLE` means the rule has no contribution for this candidate and does not remove the candidate.
- `SUPPORTED` means the stage has a proven public evaluation for the candidate.
- `UNSUPPORTED` means a required public fact, predicate, or semantic mapping is not proven. It is not a low score and cannot win a ranking by default.
- `INVALID` means the input, profile, arithmetic, or candidate descriptor violates a contract.

If an active line is unsupported, TeacherCore invalidates that line and tries the next authoritative-domain stage. If a generic stage can evaluate every supplied candidate from public data, it may select with visible lower confidence. If no stage, including the explicit key-only completion, can be proven over the supplied structurally valid vector, the result is `BLOCKED` and no action is submitted.

## 8. Deterministic fallback hierarchy

Fallback is an explicit versioned policy contract:

```text
F0 named active line edge
↓
F1 explicit recovery edge or public replan
↓
F2 deck/profile strategic utility
↓
F3 generic public tactical-safe utility
↓
F4 bytewise public_action_key equality completion
```

F0–F4 operate on the same supplied authoritative vector and the same single `N`-record evaluation result. A stage may add stage-specific status, contributions, and temporary non-authoritative comparison values to those records, but it MUST NOT append another `CandidateEvaluation` for any candidate. No candidate may disappear when a higher stage is unsupported. A stage may be selected only when it provides a total, checked comparison for all candidates. F4 is permitted only when every supplied public key is valid and unique; it is a deterministic canonical completion mechanism, not a claim of strategic quality. It is always labeled `FALLBACK`/low confidence.

There is no implicit first-candidate, candidate-zero, random-device, `RandomLegal`, or retry fallback. Fallback provenance is visible in `TeacherDecisionExplanation` and future acceptance evidence. A fallback that cannot be proven public-only and complete is a failure, not a reason to continue.

## 9. Derived Teacher diagnostics and explanation

The optional diagnostic contract is `ocgforge.teacher_decision_explanation.v1`. A `TeacherDecisionExplanation` may contain:

```text
selected_public_action_key:string
selected_score_vector:ScoreVector
runner_up_score_vector:optional<ScoreVector>
confidence_class:u8: HIGH | MEDIUM | LOW | FALLBACK
fallback_level:u8: F0..F4
active_goal_id:optional<string>
active_line_id:optional<string>
active_line_node_id:optional<string>
matched_intent_ids:vector<string>
invalidation_reason_ids:vector<string>
relevant_public_feature_values:vector<PublicFactValue>
explanation_schema_id:string = ocgforge.teacher_decision_explanation.v1
```

If serialized, the diagnostic uses strict canonical bytes, sorted ID vectors, checked integers, and a content identity whose path is excluded. It contains only current public facts, profile IDs, public action keys, fixed scores, and registered policy reason IDs. It MUST NOT contain an internal semantic key, protocol/continuation ID, raw response/message, engine-step value, submission token, private locator, hidden identity, secret-derived hash, path, host, PID, wall time, thread, provider, or scheduler value.

Diagnostics are derived audit data. They do not affect action selection, public observation, public action identity, environment/episode/gameplay identity, trusted replay, or admission. An optional diagnostic may be absent without changing gameplay. If a test or publication profile declares that diagnostics are required, malformed or missing diagnostics make that diagnostic gate fail; they never authorize a different action or a trajectory bypass.

### 9.1 Identity separation

| Identity | Owner and effect of change |
| --- | --- |
| Public gameplay identity | Existing V2/public gameplay identity codecs. Optional explanation persistence is excluded; enabling or disabling it under the same contracts cannot change this identity. |
| Trajectory/collection record identity | Existing Phase-3 trajectory-record codec. It binds policy attribution, so a new Teacher binding or PolicyArtifact may produce a new record identity even when public gameplay is unchanged. |
| Policy/provenance identity | Existing PolicyArtifact, participant assignment, RNG provenance, and the immutable TeacherPolicyBinding metadata. It identifies the producer/profile/configuration and is not gameplay identity. |

Changing only whether an optional explanation payload is persisted, while keeping the same diagnostic contract and Teacher artifact, changes none of these identities. Changing diagnostic contract semantics/version, TeacherCore semantic behavior, StrategyProfile content, or TeacherPolicyBinding content requires the corresponding new binding/PolicyArtifact provenance and may change trajectory record identity; it does not automatically change public gameplay identity.

## 10. Provenance, trajectory, and replay compatibility

The deterministic Teacher uses the existing Phase-3 policy provenance owner:

```text
PolicyArtifact.policy_kind                    = DETERMINISTIC_HEURISTIC
PolicyArtifact.producer_implementation_identity = immutable teacher-core artifact identity
PolicyArtifact.inference_adapter_identity     = existing public direct-execution adapter identity
PolicyArtifact.observation_adapter_identity   = existing public-observation adapter identity
PolicyArtifact.action_adapter_identity        = existing public-action-key adapter identity
PolicyArtifact.sampling_contract_identity     = ocgforge.policy.deterministic_lexicographic_argmax.v1
PolicyArtifact.policy_rng_contract_identity   = ocgforge.no_policy_rng.v1
PolicyArtifact.artifact_metadata_identity     = ocgforge.teacher_policy_binding.v1.<64 lowercase hex>
```

The full identity `ocgforge.teacher_policy_binding.v1.<64 lowercase hex>` binds the TeacherCore artifact, `ocgforge.strategy_profile.v1.<64 lowercase hex>`, score contract, fallback contract, tie-break contract, and optional diagnostic contract. The existing `PolicyArtifact.policy_artifact_id` therefore changes when the Teacher artifact or profile binding changes. The existing `ParticipantPolicyAssignment` remains responsible for player, seat role, deck role, exact locked-deck identity, policy role, and assignment epoch. No profile field is added to the assignment identity.

For every accepted Teacher action, the runner creates the existing `PolicyRngDecisionProvenance` in `NONE` mode with the exact `ocgforge.no_policy_rng.v1` values. No Teacher RNG root, cursor, random stream, process entropy, or policy-generated seed exists in v1.

The only trusted action path is:

```text
TeacherPolicy
  → existing PolicySelection.public_action_key
  → existing V2 ActionSelection
  → EpisodicEnvironment.step()
  → TrajectoryRecorder
  → CandidateTrajectoryShard
  → restricted evidence where required
  → semantic replay
  → admission
  → AdmissionReceipt / DatasetManifest
```

Teacher-specific explanation data remains outside the canonical `DecisionRecord` and is never required to admit an otherwise valid trajectory. A rejected Teacher `step()` produces no canonical record, advances no trusted strategy state, and follows the existing policy-origin quarantine path. Public gameplay identity, trajectory/collection record identity, and policy/provenance identity remain distinct: enabling or disabling optional explanation persistence under the identical policy and diagnostic contracts changes none of the selected action, public gameplay identity, or record identity; changing diagnostic contract semantics/version, the TeacherCore semantic artifact, the StrategyProfile, or the TeacherPolicyBinding requires a new binding and PolicyArtifact provenance and may change trajectory record identity while public gameplay identity can remain equal. Previously admitted Teacher trajectories are never reinterpreted under a new profile or core artifact.

## 11. Privacy and determinism invariants

Teacher decisions and state transitions MUST be independent of:

- unordered map/set iteration;
- pointer or address identity;
- filesystem path, symlink, or artifact location;
- wall time, PID, thread identity, scheduling, process completion order, or provider/cloud identity;
- raw engine state, private observations, hidden locators, secret-derived hashes, or stale internal caches;
- candidate vector positions as semantic identity;
- floating-point comparison or host-specific overflow behavior.

All authoritative IDs are stable lowercase tokens, content identities, or existing accepted public keys. All authoritative arrays have explicit canonical order. An implementation may use unordered containers for lookup only; it must iterate a sorted stable view when iteration affects output.

The mandatory paired-world property is:

```text
same acting PublicEnvironmentObservation bytes
+ same ordered public candidate vector
+ same immutable profile/binding
+ same participant strategy state
+ different opponent-private hand/deck order/face-down identity
        ⇒ identical selected key, ranking evidence, explanation, and next state delta
```

The property also covers hidden physical identity across a knowledge-destroying transition. The Teacher may retain only public aggregate/history facts authorized by the accepted observation contract; it clears facts that depend on destroyed physical identity.

## 12. Semantic contract versus implementation choice

The following introduce versioned meaning and require the identity/version rules above:

- the exact `PolicyInput` boundary and no-private-field rule;
- profile field meanings, binding fields, canonical encodings, identity prefixes, and ordering;
- public predicate scopes and registered predicate meanings;
- fixed score dimensions, integer ranges, arithmetic failure behavior, and lexicographic ordering;
- F0–F4 fallback semantics and the no-first-candidate rule;
- state fields, reset/commit/reject/reconcile semantics, participant isolation, and no-action-queue rule;
- explanation fields and the derived/noncanonical boundary;
- the mapping to existing Phase-3 policy provenance and `NONE` RNG;
- trajectory/replay/admission reuse and compatibility behavior.

These are pure implementation choices as long as the frozen semantics remain byte- and behavior-equivalent:

- C++ class/file decomposition under `include/ygo/teacher/` and `src/teacher/`;
- `std::vector` versus another value container behind the public DTOs;
- lookup indexes or caches that never determine iteration order;
- JSON/YAML or C++ authoring input used to build canonical profile bytes, provided the path/source bytes are not identity and strict validation produces the same canonical content;
- allocation strategy, reserve sizes, helper function boundaries, and probe formatting;
- whether a focused test is C++ or Python when it asserts the same semantic evidence.

An implementation choice becomes semantic if it changes an emitted ID, byte order, candidate consideration, score, tie-break, fallback, state transition, diagnostic meaning, or provenance mapping. Such a change requires a new task decision before implementation.

## 13. Required negative and privacy tests

Before Phase 4B acceptance, executable tests MUST cover at least:

| Test | Required result |
| --- | --- |
| Equal public worlds with different opponent hidden hands/deck order/face-down identity | Selected key, complete evaluation shape, explanation, and state evolution are identical. |
| Hidden physical identity changed across a shuffle/randomization boundary | No stale physical identity survives in state, diagnostics, or selection. |
| Strategically bad but legal supplied candidate | Candidate remains in evaluation evidence and loses only through public score dimensions. |
| Candidate reorder with equal strategic scores | Supplied order is preserved in evidence; final equality uses bytewise public-key order, never vector position. |
| Empty or structurally malformed supplied candidate vector | No selection, no state commit, structured failure; TeacherCore does not diagnose upstream legal-domain completeness. |
| Authoritative supplied vector with `N` candidates | Exactly `N` stable evaluation records, the same ordered public-action-key vector, and no omission, fabrication, filtering, or truncation. Any environment-provided domain digest remains an upstream/runner evidence comparison, not a Teacher reconstruction. |
| Missing continuation-wide public fact | The dependent rule/line is `BLOCKED` or the declared lower fallback is used; no private lookup. |
| Malformed profile: unknown version, duplicate/out-of-order entry, dangling reference, cycle, bad identity, wrong binding, trailing bytes | Profile publication/session creation fails closed; no default profile or partial profile is used. |
| Same canonical profile bytes at different paths | Same profile ID and same decision; changing bytes creates a new ID. |
| Rejected/stale/nonmember action | Zero strategy-state advancement; existing runner quarantine and no retry. |
| Interleaved episodes/participants | Decisions and state equal isolated runs; no cross-session mutable state. |
| Independent processes with different PIDs/scheduling | Same public inputs produce identical keys, score vectors, fallback, explanations, and state deltas. |
| Changed explanation only | Gameplay/public semantic identity and admission behavior remain unchanged. |

## 14. Proposed Phase-4B gate matrix

These are proposed future gates, not Task-1 evidence. Every gate has a named evidence producer, a precise PASS condition, and fail-closed behavior. Unless stated otherwise, commands use the native Windows build at `build/dev-windows`; each future CTest target is expected to be registered in `CMakeLists.txt`. Every CTest command includes `--no-tests=error`. The gate/evidence validator MUST assert the exact number of selected tests: every single-target row expects `1`, and P4B-G14's six-name regression expression expects `6`; zero or any other count is not PASS.

| Gate | Invariant | Exact executable evidence | PASS condition | Failure behavior |
| --- | --- | --- | --- | --- |
| P4B-G00 | Public-only Teacher boundary | `ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^teacher_policy_boundary_compile_test$"` and `python -B tests/teacher/teacher_public_boundary_test.py` | Headers and runtime instrumentation expose only `PolicyInput`, immutable profile, and local public-safe state; forbidden types are absent. | No Teacher selection is trusted; report a boundary blocker. |
| P4B-G01 | Preservation of the authoritative supplied domain | `ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^teacher_domain_preservation_test$"` | For an upstream-supplied vector of `N` candidates, the result contains exactly `N` stable records in supplied order with the same ordered public-action-key vector; this gate does not test legal-domain completeness. | No action; diagnose omission, fabrication, duplication, filtering, or truncation. Upstream completeness remains an Environment/Phase 4A responsibility. |
| P4B-G02 | Deterministic ranking and tie-break | `ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^teacher_ranking_test$"` | Fixed score vector ranks correctly; exact ties use bytewise public-key order and never candidate position. | No action; ranking is invalid. |
| P4B-G03 | Equal-public-world privacy | `python -B tests/teacher/teacher_paired_world_test.py` | Paired hidden worlds with equal public inputs yield identical key, evaluation evidence, explanation, and state delta. | Privacy gate fails; private dependency is a BLOCKER. |
| P4B-G04 | Canonical StrategyProfile identity | `ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^strategy_profile_codec_test$"` | Strict encode/decode round-trip is byte-identical and recomputed `ocgforge.strategy_profile.v1.<64 lowercase hex>` matches; path changes do not matter. | Profile is rejected; no fallback profile is loaded. |
| P4B-G05 | Malformed profile fail-closed | `ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^strategy_profile_negative_test$"` | Unknown, duplicate, dangling, cyclic, out-of-range, wrong-binding, and trailing-byte profiles all fail before session creation. | No policy session or action is created. |
| P4B-G06 | Exact deck/matchup/rules binding | `python -B tests/teacher/teacher_profile_binding_test.py` | Own/opponent deck IDs and hashes, matchup, format, mode, flags, and rules bundle match certified V2 input for both profile roles and seat mappings. | Profile activation is `BLOCKED`; arbitrary-deck use is forbidden. |
| P4B-G07 | Episode/participant state isolation | `ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^teacher_strategy_state_test$"` | Reset, interleaving, mirror seats, and separate participants produce isolated state equal to isolated execution. | State is discarded and the gate fails. |
| P4B-G08 | Rejected action has zero state advancement | `ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^teacher_rejected_transition_test$"` | Rejected/stale/nonmember actions commit no delta, create no record, and follow existing quarantine semantics. | Stop without retry; collection is quarantined or failed as existing V2 requires. |
| P4B-G09 | Plan invalidation and recovery | `ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^teacher_recovery_test$"` | Removed/negated resources, restrictions, targets, zones, and copy budgets invalidate stale nodes and select a current public recovery/fallback. | No queued action survives; return structured `BLOCKED` if recovery is unproven. |
| P4B-G10 | Independent-process determinism | `python -B tests/teacher/teacher_determinism_test.py --probe build/dev-windows/teacher_probe.exe` | Fresh processes reproduce keys, score vectors, fallback levels, explanations, and state deltas for the same corpus. | Determinism gate fails; no semantic acceptance claim. |
| P4B-G11 | Explicit deterministic fallback without duplicate evaluations | `ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^teacher_fallback_test$"` | F0–F4 are explicit; all stages operate on the same `N` stable evaluation records, may annotate stage status/contributions or temporary comparisons, and never append or drop records; no first-candidate, random, or retry path exists; unprovable fallback blocks. | No action and structured diagnostic. |
| P4B-G12 | Existing policy provenance recording | `ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^teacher_provenance_test$"` | Existing `PolicyArtifact`, binding metadata, participant assignment, deterministic sampling identity, and `NONE` RNG attribution validate through the production resolver. | No trusted Teacher record; provenance is invalid. |
| P4B-G13 | Trusted trajectory compatibility | `ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^teacher_runner_trajectory_test$"` | Teacher actions flow through `TrajectoryRecorder`, candidate shard, semantic replay, admission, receipt, and dataset identity with no special bypass. | Reject/quarantine the run; do not issue a Teacher-specific receipt. |
| P4B-G14 | Phase-4A public-policy regression | `ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^(public_safe_state_test|public_action_identity_test|policy_boundary_compile_test|policy_rng_test|random_legal_test|policy_runner_integration_test)$"`, plus `python -B tests/policy/policy_boundary_test.py` and `python -B tests/policy/public_fact_matrix_test.py` | Existing Phase-4A public boundary, RandomLegal, safe-state, public-key, and runner tests remain green; no Phase-4A byte or ownership meaning changes. | Phase-4B integration stops; investigate regression. |
| P4B-G15 | No hidden identity after knowledge-destroying transitions | `ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^teacher_knowledge_boundary_test$"` | Shuffle/randomization clears destroyed physical identity; public redacted slots remain nonphysical. | Privacy gate fails; no profile/Teacher acceptance. |
| P4B-G16 | Diagnostic safety and semantic separation | `ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^teacher_explanation_test$"` | Emitted explanations are canonical, public-safe, deterministic, optional, and absent from gameplay/replay identity. | Diagnostic publication fails; action identity remains unchanged. |
| P4B-G17 | Immutable profile publication and compatibility | `ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^teacher_profile_registry_test$"` | Exact content IDs resolve once; old IDs cannot be overwritten or aliased; profile/core changes create new bindings/artifacts. | Reject publication/session creation. |
| P4B-G18 | Public-fact availability is fail-closed | `python -B tests/teacher/teacher_public_fact_matrix_test.py` | Every profile fact is classified `DIRECT`, `SAFE_DERIVATION`, or `BLOCKED`; blocked facts never reach private state. | Mark the capability `BLOCKED`; do not lower the privacy boundary. |

### 14.1 Task Gate

A Task Gate is the narrow gate for one implementation commit:

```text
affected build
→ affected focused CTest/Python tests
→ relevant Phase-4A/Phase-3 short regression tests
→ git diff --check
→ commit
→ STOP
```

It does not require the old Phase-4A 20–40 minute Heavy Replay or lifecycle tests when their owning layers and relevant invariants are unchanged. A focused test may never be promoted to a broader Phase-4B or Phase-4A acceptance claim.

### 14.2 Integration Gate

The Integration Gate runs all P4B focused gates required by the changed tasks, the Phase-4A regression set P4B-G14, and the short Teacher runner/trajectory admission fixture. It verifies both profile roles, normal and mirror seat mapping, and starting-player 0/1. Heavy Phase-4A gates are added only when Phase-4B code changes their owning environment/trajectory semantics, materially threatens their invariant, or first integrates through that long-running path.

### 14.3 Phase-4B Final Acceptance

Final acceptance requires a clean checkout at the exact implementation head, all P4B-G00–G18 gates, a generated machine-readable report, and a locked fixed-matchup Teacher matrix covering both profile roles, both seat assignments, both starting players, deterministic reruns, recovery scenarios, and admitted Teacher trajectories. The final matrix is Phase-4B behavior/provenance acceptance, not a claim of Phase-4C frozen evaluation strength or arbitrary-deck support.

The final run may reuse repository-recorded Phase-4A heavy evidence only as frozen baseline evidence. It may claim a fresh heavy PASS only when the exact heavy command runs at the current Phase-4B context and the owning-layer condition justifies it.

## 15. Task-1 completion boundary

Task 1 is complete only when this contract freeze and the implementation plan are committed on the dedicated Task-1 branch. No production source, test source, locked deck, rules input, generated Phase-4A evidence, or accepted Phase-3/4A contract changes belong in the Task-1 commit.

The next authorized task is Task 2, immutable StrategyProfile identity/codec and binding implementation. It requires explicit authorization after this commit; this document does not authorize it.
