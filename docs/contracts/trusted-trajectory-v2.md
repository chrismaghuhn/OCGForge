# OCGForge Trusted Trajectory V2 Contract

Status: A0 architecture contract freeze. Production implementation is deferred to the
bounded migration slices in the companion plan. This document is normative for the
successor boundary and is subject to independent review before A1 implementation.

## 1. Authority and scope

This contract freezes the successor boundary required for recording the public
semantics emitted by EpisodicEnvironment V3 and the Teacher Runner V3 policy layer.
It does not implement the successor. It does not widen, reinterpret, or migrate any
historical V1 codec.

The authority chain is:

~~~
ocgcore
  -> Decision Protocol
  -> EpisodeDriver
  -> EpisodicEnvironment V3
  -> ygo::trajectory V2
  -> later persistence, replay, admission, and dataset successors
~~~

The V2 trajectory layer consumes immutable accepted public values. It never accesses
CoreHost, raw engine state, raw protocol messages, internal semantic keys, internal
candidate digests, response bytes, submission tokens, engine steps, pointers, caches,
private locators, or hidden card identity.

This A0 document changes no production code. The implementation boundary remains:

~~~
PRODUCTION_CODE_CHANGED=NO
TEACHER_RUNNER_V3_TRAJECTORY_WIRING=NO
ADMISSION_MIGRATION_IMPLEMENTED=NO
DATASET_MIGRATION_IMPLEMENTED=NO
MODEL_MIGRATION_IMPLEMENTED=NO
TASK7_MIGRATION_IMPLEMENTED=NO
~~~

## 2. Versioned contract identities

The historical contracts remain immutable:

| Surface | Historical identity | Successor identity |
| --- | --- | --- |
| trusted trajectory | ocgforge.trusted_trajectory.v1 | ocgforge.trusted_trajectory.v2 |
| restricted replay evidence | ocgforge.restricted_replay_evidence.v1 | ocgforge.restricted_replay_evidence.v2 |
| public gameplay trajectory identity | ocgforge.public_gameplay_trajectory_identity.v1 | ocgforge.public_gameplay_trajectory_identity.v2 |
| trajectory record identity | ocgforge.trajectory_record_identity.v1 | ocgforge.trajectory_record_identity.v2 |

There is no aliasing and no dual-version acceptance under one identity. A V1
validator remains V1-only. A V2 validator remains V2-only.

The accepted lower-layer bindings are:

~~~
trusted_trajectory.v1 -> episodic_environment.v2
trusted_trajectory.v2 -> episodic_environment.v3

episodic_environment.v2 -> environment_identity.v2
episodic_environment.v3 -> environment_identity.v3

public_action.v1.* -> public_action_identity.v1
public_action.v2.* -> public_action_identity.v2

public_candidate_domain.v1 -> public_candidate_domain.v1
public_candidate_domain.v2 -> public_candidate_domain.v2

public_semantic_decision_identity.v1 -> public_semantic_decision_identity.v1
public_semantic_decision_identity.v2 -> public_semantic_decision_identity.v2
~~~

## 3. Contracts intentionally shared

The following contracts remain V1 because their semantic meaning and canonical bytes
are unchanged in this successor:

~~~
ocgforge.policy_provenance.v1
ocgforge.policy_artifact_identity.v1
ocgforge.participant_policy_assignment_identity.v1
ocgforge.policy_rng_initialization_identity.v1
ocgforge.policy_rng_stream_identity.v1
ocgforge.policy_rng_decision_provenance.v1
ocgforge.no_policy_rng.v1
ocgforge.episode_identity.v1
ocgforge.public_environment_observation.v1
ocgforge.public_safe_state.v1
~~~

The shared values are embedded through their existing canonical codecs. A successor
trajectory does not create a fake V2 name for an unchanged child contract.

## 4. Current V1 source inventory

The following inventory was taken from the exact A0 base before editing. It records
why the current path cannot accept V3/public_action.v2 values and where each future
successor is owned.

| Component | Current contract binding | V2 successor required? | Reuse allowed? | Future migration owner | Reason |
| --- | --- | --- | --- | --- | --- |
| include/ygo/trajectory/types.hpp | V1 logical types, trusted_trajectory.v1, episodic_environment.v2, V1 identity domains | Yes | Shared provenance, observation, safe-state, and enum values | TTV2-A1 | Historical structs contain v2_contract_id and V1 defaults; explicit V2 structs are required. |
| include/ygo/trajectory/codec.hpp | V1 encode/decode API | Yes | Byte reader/writer primitives and shared child codecs | TTV2-A1 | Public codec functions are generation-specific and must not become a version union. |
| src/trajectory/codec.cpp | V1 candidate validator and canonical bytes; V1 frame/domain/decision validation | Yes | Primitive writers/readers and unchanged child codecs | TTV2-A1/A2 | Candidate validation recomputes environment::public_action_key and accepts only V1 keys; operation metadata is absent. |
| include/ygo/trajectory/recorder.hpp | V1 TrajectoryRecorder and V1 frame/closure types | Yes | Public transition and closure enum meanings | TTV2-A2 | Recorder snapshots V2-environment frames into V1 logical values and must not drop operation metadata. |
| src/trajectory/recorder.cpp | Hard-coded EpisodicEnvironment V2 checks and V1 snapshots | Yes | Shared policy/RNG validation | TTV2-A2/A4 | V3 frames cannot be recorded by the historical recorder without reinterpretation. |
| include/ygo/trajectory/restricted_evidence.hpp | Restricted replay evidence V1 and collection bundle V1 | Yes for replay evidence; bundle deferred | Shared RNG evidence child values | TTV2-A3 | Replay evidence binds directly to environment V2; the bundle is a separate downstream persistence surface. |
| src/trajectory/restricted_evidence.cpp | V1 evidence schema and V2-environment validation | Yes for replay evidence; bundle deferred | Shared policy RNG registry | TTV2-A3 | The V1 decoder must reject V3 evidence rather than accept a widened meaning. |
| include/ygo/trajectory/admission.hpp | V1 envelope/admission interfaces | Yes, later | Shared logical transition meanings | TTV2-A3 | Admission currently consumes V1 envelopes and V2-environment replay values. |
| src/trajectory/admission.cpp | V1 frame/envelope checks and V2 environment reconstruction | Yes, later | Shared replay invariants where byte-identical | TTV2-A3 | It must not silently admit a V2 trajectory into V1 persistence. |
| include/ygo/trajectory/receipt.hpp | admission_receipt.v1 | Not in A0; successor decision deferred | Existing receipt primitives may be reused later | TTV2-A3/A5 | Receipt IDs currently require trajectory_record.v1 and public_gameplay_trajectory.v1. |
| src/trajectory/receipt.cpp | V1 record/gameplay identity prefixes and V1 receipt bytes | Not in A0; successor decision deferred | Existing hashing primitives | TTV2-A3/A5 | Widening this validator would make a V1 receipt ambiguous. |
| include/ygo/trajectory/shard.hpp | V1 candidate shard contract | Not in A0; successor decision deferred | Entry framing may be reusable | TTV2-A3/A5 | Persistence successor must be decided after the V2 envelope core exists. |
| src/trajectory/shard.cpp | V1 shard validation | Not in A0; successor decision deferred | Byte framing only if formally unchanged | TTV2-A3/A5 | A0 must not make a V1 shard accept V2 envelopes. |
| tests/trajectory/** | V1 fixtures, V1 keys, V1 goldens, V2-environment replay fixtures | No historical changes | Existing tests remain regression gates | TTV2-A1/A3 | Historical vectors must remain untouched; V2 tests belong to successor slices. |

The current DatasetManifest, AdmissionReceipt, RestrictedCollectionEvidenceBundle,
candidate shard, model input, and Task7 materialization surfaces are downstream of
this A0 contract. They remain V1-bound until a later authorized migration.

## 5. Explicit V2 logical types

The successor uses explicit generation types. Historical V1 structs are not changed
into a union and do not gain a generation enum.

### 5.1 EpisodeManifestV2

The exact logical fields and canonical order are:

1. trusted_trajectory_contract_id: fixed string
   ocgforge.trusted_trajectory.v2.
2. episodic_environment_contract_id: fixed string
   ocgforge.episodic_environment.v3.
3. environment_semantic_id: lower-case SHA-256 identity string.
4. environment_identity_input: exact non-empty V3 environment identity input bytes.
5. episode_identity_schema_id: fixed string ocgforge.episode_identity.v1.
6. episode_semantic_id: lower-case SHA-256 identity string.
7. episode_identity_input: exact non-empty episode identity input bytes.
8. PolicyProvenanceEnvelope: existing V1 canonical child bytes.
9. CollectionDisposition: existing V1 canonical child bytes.

The field name v2_contract_id is forbidden in EpisodeManifestV2. The environment
field is explicitly named episodic_environment_contract_id.

The manifest validator must verify:

* both fixed contract IDs;
* SHA-256 of each identity input equals its semantic ID;
* the environment input decodes as the V3 environment identity;
* the episode input decodes against that environment identity;
* the episode schema is episode_identity.v1;
* policy provenance and disposition pass their existing canonical validators.

### 5.2 PublicFrameSnapshotV2

The exact logical fields and canonical order are:

1. episodic_environment_contract_id: fixed V3 contract string.
2. episode_semantic_id: string.
3. public_semantic_decision_id: V2 public decision identity string.
4. decision_index: u64 big-endian.
5. acting_player: u8, only 0 or 1.
6. PublicEnvironmentObservation: existing V1 canonical observation bytes.
7. public_observation_digest: string.
8. EnvironmentDecisionRequest: V2 request bytes with the complete ordered
   candidate domain.
9. public_candidate_domain_digest: V2 domain digest string.

submission_token and engine-step values are not fields of this logical type and
are not present in its canonical bytes.

### 5.3 DecisionRecordV2

DecisionRecordV2 retains the public record and collection attribution as separate
logical concerns:

1. frame: PublicFrameSnapshotV2.
2. selected_public_action_key: one V2 public action key.
3. transition_class: the existing TransitionClass numeric value.
4. successor: the existing Successor logical value.
5. acting_policy_assignment_id: shared V1 participant-assignment identity.
6. policy_rng_decision_provenance: shared V1 policy-RNG decision provenance.

The public gameplay canonical projection of this type contains only fields 1–4.
Fields 5–6 are collection provenance and are excluded from public gameplay identity.
The full V2 collection-record canonical bytes contain the public record bytes
followed by the shared V1 attribution bytes.

### 5.4 Closures and envelope

The explicit V2 types are:

* TerminalClosureV2
* InterruptedClosureV2
* FailedClosureV2
* EpisodeClosureV2
* EpisodeEnvelopeV2

Their field meanings remain those of the historical closure types. The V2 envelope
contains EpisodeManifestV2, an ordered vector of DecisionRecordV2, and
EpisodeClosureV2. No closure enum is renumbered.

EpisodeEnvelopeV2 is not accepted by a V1 envelope decoder. Persistence and
admission acceptance are deferred to TTV2-A3.

### 5.5 RestrictedReplayEvidenceV2

The exact logical fields and canonical order are:

1. restricted_replay_evidence_contract_id: fixed string
   ocgforge.restricted_replay_evidence.v2.
2. trusted_trajectory_contract_id: fixed string
   ocgforge.trusted_trajectory.v2.
3. episodic_environment_contract_id: fixed string
   ocgforge.episodic_environment.v3.
4. episode_semantic_id: string.
5. closure_kind: u8, fixed value 1 for the interrupted-evidence surface.
6. interruption_reason: u8 using the existing InterruptionReason values.
7. engine_process_budget: u64 big-endian, nonzero.
8. semantic_action_budget: u64 big-endian, nonzero.
9. observed_engine_process_count: u64 big-endian and not above its budget.
10. observed_semantic_action_count: u64 big-endian and not above its budget.
11. final_engine_step_index: u64 big-endian.

The V2 replay validator reconstructs V3, not V2, and verifies the exact environment
and episode identity inputs from the manifest before using this evidence.

## 6. Canonical primitive encoding

All V2 trajectory canonical bytes use the existing deterministic byte primitives:

| Primitive | Encoding |
| --- | --- |
| string/token | u32be byte length followed by UTF-8 bytes |
| byte vector | u32be byte length followed by raw bytes |
| bool | u8, only 0 or 1 |
| u8 | one byte |
| u16 | u16be |
| u32 | u32be |
| u64 | u64be |
| signed i32 | two's-complement u32be bit pattern |
| optional value | presence u8, then the value when present |
| vector | count u32be, then each element in order |
| nested canonical value | raw bytes of the named child codec, with no extra wrapper |

No host endianness, compiler layout, padding, pointer value, unordered iteration, or
implicit enum serialization is permitted.

## 7. V2 candidate and request canonicalization

### 7.1 Candidate bytes

The V2 candidate schema is the fixed string ocgforge.trusted_trajectory.v2. The
complete candidate byte order is:

| Position | Field | Encoding |
| ---: | --- | --- |
| 0 | candidate schema | string |
| 1 | action kind | lower-case action-kind token string |
| 2 | card selection operation | required u8: 0=None, 1=Select, 2=Unselect |
| 3 | public action key | string, public_action.v2.* |
| 4 | choice | optional PublicChoice: presence u8, kind u8, value u64be, optional response index u32be |
| 5 | source reference | optional: presence u8, reference kind u8, public locator string |
| 6 | target reference | optional: presence u8, reference kind u8, public locator string |
| 7 | phase | optional u32be |
| 8 | position | optional u8 |
| 9 | source index | optional u32be |
| 10 | amount | optional i32 as two's-complement u32be |
| 11 | continuation operation | canonical continuation-operation token string |
| 12 | submits engine response | bool u8 |

current_entity_ordinal, internal semantic keys, response bytes, submission tokens,
and engine steps are not candidate fields.

The nested choice/reference validation remains the existing public validation:
unknown kinds, invalid presence values, invalid response-index combinations, and
empty locators fail closed.

### 7.2 Request bytes

The V2 request uses the V2 candidate schema as its schema marker. Its exact order is:

1. candidate schema string;
2. decision-kind token string;
3. player u8;
4. candidate count u32be;
5. each V2 candidate in supplied order;
6. continuation presence u8;
7. when present, the existing continuation fields encoded with the V2 schema marker.

The candidate vector is never sorted, truncated, deduplicated, or regenerated by the
trajectory layer.

### 7.3 Frame bytes

The V2 frame schema marker is the trusted trajectory V2 contract string. Its exact
order is:

1. frame schema string;
2. episodic environment contract string;
3. episode semantic ID string;
4. public semantic decision ID string;
5. decision index u64be;
6. acting player u8;
7. raw public_environment_observation.v1 canonical bytes;
8. public observation digest string;
9. raw V2 request bytes;
10. public candidate-domain digest string.

There is no submission token, engine step, raw message, or internal response field.

### 7.4 Successor and closure bytes

The existing transition and successor numeric values are reused without renumbering:

| Existing value | Numeric code |
| --- | ---: |
| AtomicEngineResponse | 0 |
| IntermediateContinuation | 1 |
| FinalContinuationResponse | 2 |
| NextFrame | 0 |
| Terminal | 1 |
| Interrupted | 2 |
| Failed | 3 |
| NextDecisionRecord | 0 |
| InterruptionPendingUnactedFrame | 1 |

For a V2 public record, bytes are:

1. trusted trajectory V2 schema string;
2. raw V2 frame bytes;
3. selected V2 public action key string;
4. transition class u8;
5. successor kind u8;
6. for NextFrame, target kind u8, next decision index u64be, and next V2
   public semantic decision ID string.

For a V2 closure, the schema marker is the trusted trajectory V2 string followed by:

* terminal: kind u8=0, winner u8, win reason u8, semantic action count u64be,
  optional last index, terminal observation 0 raw bytes, digest string, terminal
  observation 1 raw bytes, digest string;
* interrupted: kind u8=1, record count u64be, pending-frame presence u8, and the
  raw V2 pending frame when present;
* failed: kind u8=2, failure code u8, failure stage u8, mutation flag bool, record
  count u64be.

The enum meanings and numeric codes are reused because their semantics are unchanged.
V2 does not invent a new transition or closure code.

## 8. V2 validation rules

### 8.1 Candidate validation

Each V2 candidate must satisfy all of the following:

* the action kind and all public descriptor fields are canonical;
* the operation code is exactly 0, 1, or 2;
* public_action_key_v2(candidate descriptor) equals the stored key;
* the stored key passes the authoritative V2 validator;
* UnselectCard plus CardSelection requires Select or Unselect;
* every non-UnselectCard CardSelection candidate requires None;
* Finish and Cancel require None;
* every non-CardSelection action requires None;
* no internal key, raw response, or private value is consulted.

An invalid operation is rejected; it is never defaulted to None.

### 8.2 Frame and domain validation

A V2 frame must use the V3 environment contract, a V2 public decision identity, and a
non-empty complete ordered candidate vector. The candidate keys must be valid V2 keys,
unique, and homogeneous. A V1 key, mixed V1/V2 domain, malformed key, or empty domain
rejects the entire frame.

The validator recomputes:

~~~
public_candidate_domain_digest_v2(
    request.kind token,
    ordered [candidate.public_action_key...]
)
~~~

The canonical domain bytes are:

1. ocgforge.public_candidate_domain.v2;
2. request-kind token string;
3. candidate count u32be;
4. each ordered V2 public-action key string.

The recomputed digest must equal the stored frame digest.

The validator then builds the existing PublicSemanticDecisionIdentityInput values
from the V2 frame:

1. ocgforge.public_semantic_decision_identity.v2;
2. the same V2 schema again as the domain marker;
3. episode semantic ID string;
4. decision index u64be;
5. acting player u8;
6. request-kind token string;
7. public observation digest string;
8. V2 candidate-domain digest string.

The recomputed public_semantic_decision_id_v2 must equal the stored frame value.
V1 identity helpers are forbidden in this path.

### 8.3 Selected action validation

DecisionRecordV2.selected_public_action_key must be a valid V2 key and must occur
exactly once in the complete ordered frame domain. A candidate index is not a semantic
identity and cannot substitute for the selected key.

Transition classification remains fail-closed:

* AtomicEngineResponse requires no continuation and a selected candidate that submits
  the engine response;
* IntermediateContinuation requires a continuation and a selected candidate that does
  not submit the engine response;
* FinalContinuationResponse requires a continuation and a selected candidate that
  submits the engine response.

Successor indices, episode IDs, and next decision identities must remain coherent.

## 9. Identity domains

### 9.1 Public gameplay trajectory identity V2

The domain is:

~~~
ocgforge.public_gameplay_trajectory_identity.v2
~~~

The exact canonical input order is:

1. identity domain string;
2. identity domain string again as the canonical domain marker;
3. trusted trajectory contract string ocgforge.trusted_trajectory.v2;
4. episodic environment contract string ocgforge.episodic_environment.v3;
5. environment semantic ID string;
6. episode identity schema string ocgforge.episode_identity.v1;
7. episode semantic ID string;
8. record count u32be;
9. each public V2 decision-record byte vector in record order;
10. public V2 closure bytes.

The digest is SHA-256 of those bytes and is named with the
public_gameplay_trajectory.v2. prefix. A Failed closure has no public gameplay
identity, as in the historical split.

This identity contains only public gameplay semantics and identity inputs. It excludes
policy artifacts, participant assignments, workers, build paths, process IDs, time,
restricted evidence, collection disposition, and all other collection provenance.

### 9.2 Trajectory record identity V2

The domain is:

~~~
ocgforge.trajectory_record_identity.v2
~~~

The exact canonical input order is:

1. identity domain string;
2. identity domain string again as the canonical domain marker;
3. trusted trajectory contract string ocgforge.trusted_trajectory.v2;
4. public gameplay trajectory identity V2 string;
5. raw shared policy_provenance.v1 bytes;
6. record count u32be;
7. each raw shared policy-decision-attribution byte vector in record order;
8. raw shared CollectionDisposition bytes.

The digest is SHA-256 of those bytes and is named with the
trajectory_record.v2. prefix. It is collection provenance, not learner input.
Changing collection provenance changes this identity while leaving the public gameplay
identity unchanged.

## 10. Restricted replay V2

ocgforge.restricted_replay_evidence.v2 binds to both:

~~~
ocgforge.trusted_trajectory.v2
ocgforge.episodic_environment.v3
~~~

Replay must reconstruct CertifiedEnvironmentConfig::canonical_v3() and the V3
EpisodeSpec. Before replay it must verify:

* the manifest environment contract is V3;
* the exact V3 environment identity input decodes and hashes to the manifest
  environment semantic ID;
* the exact episode identity input decodes against that V3 environment identity and
  hashes to the manifest episode semantic ID;
* the replay evidence binds to the same episode semantic ID;
* every replayed frame uses V2 candidate/domain/decision identities;
* the engine and semantic budgets and observed counts satisfy the existing bounds.

Historical restricted replay V1 remains V2-environment-only and rejects V3 evidence.
The restricted collection evidence bundle is a separate persistence surface and is not
migrated by A0.

## 11. Version-purity matrix

| Environment | Public action/domain/decision | Trusted trajectory | Result |
| --- | --- | --- | --- |
| episodic_environment.v2 | V1 | trusted_trajectory.v1 | allowed historical path |
| episodic_environment.v3 | V2 | trusted_trajectory.v2 | allowed successor path |
| episodic_environment.v2 | V2 | trusted_trajectory.v2 | reject |
| episodic_environment.v3 | V1 | trusted_trajectory.v1 | reject |
| either | public_action.v1 inside trajectory V2 | V2 | reject |
| either | public_action.v2 inside trajectory V1 | V1 | reject |
| either | mixed V1/V2 candidate domain | either | reject |
| either | mixed frame/envelope generations | either | reject |

Domain and decision identity in this table are frame-version bindings, not separately
submitted agent fields.

## 12. Historical V1 preservation

The following values and goldens remain byte-for-byte unchanged:

~~~
trusted_trajectory.v1 candidate/frame/record/manifest/envelope bytes
restricted_replay_evidence.v1 bytes
public_gameplay_trajectory_identity.v1
trajectory_record_identity.v1
all existing trajectory codec goldens
all existing V1 environment/action/domain/decision values
~~~

No V1 validator is widened to accept a V2 key or V3 environment. No old golden is
updated to match a successor output.

## 13. Privacy and determinism

V2 records only public values already supplied by EpisodicEnvironment V3. The V2
trajectory layer must not add or infer:

* hidden card identity or private locators;
* CoreHost or raw engine state;
* raw protocol bytes or response bytes;
* internal semantic keys;
* submission tokens or engine steps;
* pointer/object identity;
* worker, build path, PID, wall-clock, or filesystem ordering.

The supplied candidate order is authoritative and preserved exactly. When an ordering
is part of an identity, the order is encoded directly. When a collection is declared
canonical by an existing shared codec, its existing strict ordering rules remain in
force. Fresh processes must produce identical V2 canonical bytes for identical public
inputs.

Paired public-equivalent worlds must produce identical V2 frames, candidate bytes,
domain digests, public decision identities, public gameplay identities, and selected
public action semantics.

## 14. Deferred downstream boundaries

A0 does not migrate or modify:

* CandidateTrajectoryShard;
* RestrictedCollectionEvidenceBundle;
* AdmissionReceipt;
* DatasetManifest;
* dataset identity;
* model logical or encoded input;
* Task7 materialization or collection;
* TeacherRunnerV3 trajectory wiring;
* RUN_A, RUN_B, training, self-play, or model generation.

No V1 persistence, replay, admission, receipt, shard, dataset, or model validator may
implicitly accept a V2 trajectory artifact. The successor decision for those surfaces
is made after the V2 logical envelope and replay core exist.

## 15. Acceptance matrix

The following gates are frozen for the implementation slices. Semantic, privacy,
identity, replay, and determinism failures are BLOCKER severity.

| Gate | Requirement | Severity |
| --- | --- | --- |
| TTV2-G01 | historical V1 bytes unchanged | BLOCKER |
| TTV2-G02 | V3 frame accepted only by trajectory V2 | BLOCKER |
| TTV2-G03 | V2-environment frame rejected by trajectory V2 | BLOCKER |
| TTV2-G04 | public_action.v1 rejected by trajectory V2 | BLOCKER |
| TTV2-G05 | public_action.v2 rejected by trajectory V1 | BLOCKER |
| TTV2-G06 | mixed V1/V2 domain rejected | BLOCKER |
| TTV2-G07 | Select and Unselect produce distinct candidate bytes | BLOCKER |
| TTV2-G08 | card-selection operation uses exact codes 0/1/2 | BLOCKER |
| TTV2-G09 | complete candidate order preserved | BLOCKER |
| TTV2-G10 | duplicate domain rejected | BLOCKER |
| TTV2-G11 | selected key occurs exactly once | BLOCKER |
| TTV2-G12 | V2 candidate-domain digest recomputes exactly | BLOCKER |
| TTV2-G13 | V2 public decision identity recomputes exactly | BLOCKER |
| TTV2-G14 | paired hidden worlds produce identical public V2 semantics | BLOCKER |
| TTV2-G15 | submission token excluded | BLOCKER |
| TTV2-G16 | engine step excluded | BLOCKER |
| TTV2-G17 | internal semantic key excluded | BLOCKER |
| TTV2-G18 | restricted replay V2 reconstructs V3 environment identity exactly | BLOCKER |
| TTV2-G19 | public gameplay identity is independent of collection provenance | BLOCKER |
| TTV2-G20 | trajectory record identity changes when collection provenance changes | BLOCKER |
| TTV2-G21 | fresh-process canonical bytes are identical | BLOCKER |
| TTV2-G22 | no downstream V1 artifact silently accepts V2 trajectory | BLOCKER |

The A0 documentation gates are:

* the source inventory covers all listed V1 surfaces;
* the V2 candidate, frame, identity, and replay byte orders are explicit;
* the version-purity matrix is present;
* historical V1 goldens are not changed;
* no production file is modified.

## 16. Implementation authority

The next authorized step is TTV2-A1 only after independent review of this A0
contract. A0 does not authorize production changes.

~~~
TRUSTED_TRAJECTORY_V2_A1_IMPLEMENTATION_AUTHORIZED=NO
TRAJECTORY_ADMISSION_V2_AUTHORIZED=NO
TEACHER_RUNNER_V3_TRAJECTORY_WIRING_AUTHORIZED=NO
TASK7_RUN_A_AUTHORIZED=NO
TASK7_RUN_B_AUTHORIZED=NO
TRAINING_AUTHORIZED=NO
~~~
