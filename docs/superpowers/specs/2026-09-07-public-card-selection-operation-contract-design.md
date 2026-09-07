# Public Card Selection Operation Contract Design

## Status

Design remediation draft for review. Core public-operation and identity
decisions are approved; the downstream closure is now made explicit here.
Implementation is not authorized.

This document defines the public semantic contract and migration closure for
the native `MSG_SELECT_UNSELECT_CARD` operation. It does not authorize changes
to the Teacher, Environment behavior, continuation engine, production Task7
budgets, RUN_A, RUN_B, performance branches, datasets, or training.

The design is based on the accepted Task7 evidence at:

```text
bcac2b31bdd50b69df9b397cbdb0eb01d786bebf
```

The accepted finding is:

```text
PUBLIC_UNSELECT_OPERATION_SEMANTICS_MISSING=CONFIRMED
TEACHER_CONTINUATION_COMMITMENT_LOSS=CONFIRMED
```

## 1. Problem and decision

The native UNSELECT prompt has two public-to-the-acting-player action roles:

```text
currently selected card
    -> choosing it removes it from the selected set

currently unselected/selectable card
    -> choosing it adds it to the selected set
```

The internal decoder knows these roles, but the current public projection
publishes both as `ActionKind::CardSelection`. The internal `semantic_key`
prefix is not public-safe identity and cannot be used by a Teacher, model, or
public replay.

The contract therefore introduces a typed card-selection operation distinct
from adapter-local continuation operation:

```text
CardSelectionOperation:
    None
    Select
    Unselect
```

The names describe the candidate action, not the list from which the
candidate was read.

```text
selected list
    -> CardSelectionOperation::Unselect

unselected/selectable list
    -> CardSelectionOperation::Select
```

This information is already part of the legal interaction exposed by the
engine to the acting player. Publishing it does not reveal hidden card
identity and does not change the legal domain, ordering, or engine response.

## 2. Scope and non-goals

In scope:

- typed internal decoder metadata;
- typed public candidate metadata;
- canonical public action identity versioning;
- public domain and public decision identity closure;
- episodic environment and environment identity closure;
- trusted trajectory, shard, restricted replay, admission, and dataset
  compatibility classification;
- Phase-5 model logical, encoded, batch, and supervision closure;
- historical-reader and negative cross-version rules;
- required codec, privacy, replay, and determinism tests.

Out of scope:

- Teacher continuation retention or scoring;
- changing candidate order or candidate cardinality;
- changing `source_index` response semantics;
- changing `ContinuationOperation`;
- changing `PlayerObservation` or `public_safe_state.v1`;
- changing ocgcore, engine responses, or continuation mechanics;
- relabeling or regenerating existing Phase-6 data;
- training, RUN_A, RUN_B, or performance work.

## 3. Ownership and data flow

The operation is carried through typed auxiliary metadata at the existing
boundaries:

```text
MSG_SELECT_UNSELECT_CARD
        |
        v
Decision Protocol decoder
    ActionCandidate.card_selection_operation
        |
        | internal semantic_key remains private and unchanged
        v
Public projection
    EnvironmentActionCandidate.card_selection_operation
        |
        v
public_action_identity.v2
        |
        v
public_candidate_domain.v2
        |
        v
public_semantic_decision_identity.v2
        |
        v
episodic_environment.v3
        |
        +--> trusted_trajectory.v2 / replay / admission
        |
        +--> model_logical_input.v2 / model_encoded_input.v2
```

The Decision Protocol remains the authority for legality and response bytes.
The Environment remains the authority for the complete ordered public domain,
public-key membership, frame-local internal binding, and engine advancement.
The Teacher and model receive only the public typed value.

No layer may infer the operation from:

- `semantic_key` text;
- public-key lexical differences;
- candidate ordinal or `source_index` alone;
- candidate order;
- hidden state, CoreHost, raw protocol bytes, or pointers.

## 4. Typed values and validation

### 4.1 Internal auxiliary value

The internal protocol candidate receives:

```cpp
enum class CardSelectionOperation : std::uint8_t {
    None = 0,
    Select = 1,
    Unselect = 2,
};
```

This field is decoder metadata. It is not added to the existing internal
action-identity or internal candidate-domain codecs. The existing
`semantic_key`, exact response bytes, and `EngineTrace v2` meanings remain
unchanged.

### 4.2 Public value

The public candidate receives a separate public type:

```cpp
enum class PublicCardSelectionOperation : std::uint8_t {
    None = 0,
    Select = 1,
    Unselect = 2,
};
```

The public value is safe even when the source reference is a
`RedactedSlot`: it describes the public action role, not the hidden card.

### 4.3 Layered validation

The isolated action-key codec has no request-kind input. Its structural rules
are therefore limited to the candidate fields:

```text
action_kind != CardSelection
    -> operation MUST be None

action_kind == CardSelection
    -> None, Select, and Unselect are structurally valid enum values
```

The Environment projection has request context and applies the semantic
rules:

```text
request.kind == UnselectCard
AND action_kind == CardSelection
    -> operation MUST be Select or Unselect

request.kind != UnselectCard
AND action_kind == CardSelection
    -> operation MUST be None

Finish or Cancel
    -> operation MUST be None
```

An invalid combination fails the complete public frame closed. It does not
drop, rewrite, sort, or default an individual candidate.

The decoder sets `Select` while reading the currently unselected list and
`Unselect` while reading the currently selected list. All unrelated
card-selection requests use `None`. `ContinuationOperation` remains limited to
`Pick`, `AssignAmount`, `Finish`, `Cancel`, and `Bypass`.

## 5. Canonical public action identity v2

`public_action_identity.v1` remains frozen and retains its current canonical
bytes and `public_action.v1.` key prefix. It remains readable according to its
original meaning.

The new action identity is:

```text
ocgforge.public_action_identity.v2
public_action.v2.<lowercase hexadecimal canonical descriptor bytes>
```

The v2 descriptor uses the same length-prefixed string and optional-value
rules as v1, with one new fixed `u8` field immediately after `action_kind`:

| Order | Field | Encoding |
| ---: | --- | --- |
| 0 | identity domain | string `ocgforge.public_action_identity.v2` |
| 1 | identity schema | string `ocgforge.public_action_identity.v2` |
| 2 | action kind | canonical lower-case token |
| 3 | card-selection operation | `u8`: `0=None`, `1=Select`, `2=Unselect` |
| 4 | typed choice | existing v1 choice encoding |
| 5 | source reference | existing public reference encoding |
| 6 | target reference | existing public reference encoding |
| 7 | phase | optional `u32be` |
| 8 | position | optional `u8` |
| 9 | source index | optional `u32be` |
| 10 | amount | optional signed `i32` bit pattern |
| 11 | continuation operation | existing public continuation token |

The v2 codec rejects a non-`None` card-selection operation for every action
kind other than `card_selection`. The operation is a semantic identity input,
so otherwise identical `Select` and `Unselect` descriptors produce different
public action keys.

The descriptor still contains no passcode. A visible passcode may justify a
public reference, but it is never copied into the action key. A redacted
reference remains only its current public locator.

## 6. Public domain and decision identity

The ordered public candidate-domain digest becomes:

```text
ocgforge.public_candidate_domain.v2
```

Its canonical bytes retain the v1 field order but use the v2 domain string and
the ordered `public_action.v2` keys. The complete candidate vector and its
authoritative order remain unchanged.

The public semantic decision identity becomes:

```text
ocgforge.public_semantic_decision_identity.v2
```

Its field order may remain structurally identical, but its identity domain,
schema, and consumed candidate-domain identity are v2. This is a semantic
version change even if the field order is unchanged; retaining the v1 name
would give one identity version two meanings.

`public_environment_observation.v1` and `public_safe_state.v1` remain
unchanged. The new operation is candidate semantics, not observation state.

## 7. Environment and episode identity closure

The accepted `episodic_environment.v2` public DTO and field layout are frozen.
The public candidate field therefore requires:

```text
ocgforge.episodic_environment.v3
ocgforge.environment_identity.v3
```

Environment identity v3 retains the existing environment identity field order,
but its top-level identity/schema and child contract values identify the v3
environment, public action v2, public domain v2, and public decision v2. The
internal action/domain/decision identities remain v1 values.

The environment semantic ID changes because its canonical child-contract
inputs change. Rules-bundle, ocgcore, CardScripts, database, format, duel
mode, deck, and required-script-closure inputs do not change.

`ocgforge.episode_identity.v1` remains unchanged. Its canonical bytes and
field order are retained; it consumes the new environment semantic ID as its
parent value. The implementation must not hard-code the v2 environment name
when decoding an episode against a validated configuration.

## 8. Migration matrix

The following matrix distinguishes an own encoding change from a value that
only consumes a new child identity.

| Surface | Decision | Reason |
| --- | --- | --- |
| internal `action_identity.v1` | retain | semantic key and response path unchanged |
| internal `candidate_domain.v1` | retain | internal ordered semantic-key vector unchanged |
| internal `semantic_decision_identity.v1` | retain | internal decision inputs unchanged |
| `public_action_identity.v1` | retain historically | v1 bytes and meaning are frozen |
| `public_action_identity.v2` | add | canonical action bytes gain operation |
| `public_candidate_domain.v1` | retain historically | v1 keys remain v1 keys |
| `public_candidate_domain.v2` | add | domain consumes v2 action keys |
| `public_semantic_decision_identity.v1` | retain historically | v1 decision IDs remain interpretable |
| `public_semantic_decision_identity.v2` | add | child domain identity and semantic meaning change |
| `public_environment_observation.v1` | retain | no observation bytes or context fields change |
| `episodic_environment.v2` | retain historically | accepted DTO layout is frozen |
| `episodic_environment.v3` | add | public candidate DTO gains a semantic field |
| `environment_identity.v2` | retain historically | old v2 child-identity closure remains readable |
| `environment_identity.v3` | add | v3 environment and child identity closure |
| `episode_identity.v1` | retain | codec is unchanged; parent environment ID changes |
| internal Decision Protocol v1 | retain | raw message, legality, and response semantics unchanged |
| `ygo.engine_trace.v2` | retain | internal trace semantics unchanged |

## 9. Trusted trajectory, replay, admission, and dataset closure

The current trusted trajectory contract hard-codes the V2 environment and
serializes the complete public candidate DTO. It cannot silently consume the
new candidate field. The new logical trajectory chain is:

```text
ocgforge.trusted_trajectory.v2
ocgforge.trajectory_shard.v2
ocgforge.restricted_replay_evidence.v2
ocgforge.restricted_collection_evidence_bundle.v2
ocgforge.public_gameplay_trajectory_identity.v2
ocgforge.trajectory_record_identity.v2
ocgforge.admission_receipt.v2
ocgforge.dataset_manifest.v2
ocgforge.dataset_identity.v2
```

### 9.1 Trusted trajectory v2

Trajectory v2 changes the fixed environment binding from
`episodic_environment.v2` to v3 and adds `card_selection_operation` to the
canonical public candidate record. Its public frame/request codecs use the v2
public action, domain, and decision identities.

The following remain unchanged in meaning and ownership:

- policy provenance and RNG values;
- transition classes;
- successor tags;
- exact internal response ownership;
- public observation and safe-state bytes;
- candidate order and complete-domain requirements.

### 9.2 Shard and replay

`trajectory_shard.v1` currently invokes the v1 envelope decoder and therefore
cannot be retargeted silently. Shard v2 wraps and validates trajectory-v2
envelopes while retaining the same atomic whole-entry and digest rules.

Restricted replay evidence v2 changes its fixed environment contract value to
v3. It remains restricted admission evidence, never learner input and never a
public action identity. Internal replay still uses the unchanged internal
semantic keys only inside the trusted replay boundary.

Replay must reject all cross-version combinations before mutation:

```text
episodic_environment.v2 + trusted_trajectory.v1
    -> historical accepted

episodic_environment.v3 + trusted_trajectory.v1
    -> rejected

episodic_environment.v2 + trusted_trajectory.v2
    -> rejected

episodic_environment.v3 + trusted_trajectory.v2
    -> eligible after all v2/v3 gates
```

No v1/v2 artifact is relabeled as v3. A migration, if ever authorized, must
replay from the original authoritative source and issue new identities; it
must not rewrite old bytes in place.

### 9.3 Public gameplay and trusted-record identities

The public gameplay and trusted-record identity codecs include the trajectory
and environment contract values and canonical public decision records. Their
own v2 identities are required so that the same advertised identity version
does not cover both v1 and v2 trajectory meanings.

### 9.4 Admission receipt and dataset manifest

Admission receipt v2 binds v2 candidate-shard and restricted-evidence
artifacts. Even though the current receipt field layout is small, its producer
and accepted replay contract change; v1 receipts remain historical.

Dataset manifest v2 is required because the current manifest explicitly pins
`trusted_trajectory.v1`. Dataset identity v2 binds the v2 manifest contract
and its member closure.

The restricted collection evidence bundle has its own canonical container and
must be versioned independently:

```text
ocgforge.restricted_collection_evidence_bundle.v1 -> v2
```

Bundle v2 retains the exact v1 field order:

```text
bundle domain:string = ocgforge.restricted_collection_evidence_bundle.v2
bundle schema:string = ocgforge.restricted_collection_evidence_bundle.v2
candidate_shard_artifact_sha256:string
interrupted_entry_count:u32be
interrupted_entries[]:
    episode_envelope_sha256:string
    canonical RestrictedReplayEvidenceV2 bytes
rng_initialization_entry_count:u32be
rng_initialization_entries[]:
    policy_rng_initialization_identity:string
    initialization_material:bytes
```

The bundle validator must decode `RestrictedReplayEvidenceV2`, validate the
v3 environment binding, preserve the existing strict digest/order rules, and
reject a v1 bundle containing v2 nested evidence. The old v1 bundle reader
retains its old nested v1 meaning.

Policy provenance, policy-artifact, participant-assignment, and policy-RNG
codecs retain their own v1 encodings when their field layouts do not change.
New policy-artifact values may carry a new action-adapter identity, producing
new content IDs without silently changing the old codec's meaning.

The Task7 collection authority also needs a distinct policy/collection
closure. The current policy action adapter identity is v1, and the current
Task7 job/schedule bind the v2 environment and old Teacher artifact values:

```text
ocgforge.policy.public_action_key.v1
    -> ocgforge.policy.public_action_key.v2

phase6_task7_dataset_collection_job.v1
    -> phase6_task7_dataset_collection_job.v2

phase6_task7_dataset_collection_schedule.v1
    -> phase6_task7_dataset_collection_schedule.v2
```

The policy-artifact and participant-assignment codecs may retain their own
codec versions if their field layouts are unchanged. Their content IDs must
be recomputed from the v2 action-adapter identity, so new PolicyArtifact IDs
and, transitively, new ParticipantPolicyAssignment IDs are required.

The Teacher continuation remediation is a separate semantic implementation
change in Stage B. `TeacherPolicyBindingV1` does not contain the action-adapter
identity in its canonical bytes; it binds the Teacher core artifact identity,
strategy profile, score/fallback/tie-break contracts, and diagnostic
contracts. Therefore changing only
`ocgforge.policy.public_action_key.v1` to v2 cannot produce the new Teacher
binding identity. Before any new artifact, assignment, or collection job is
issued, Stage B MUST:

```text
Teacher continuation-remediation
    -> new teacher_core_artifact_identity
    -> recompute TeacherPolicyBindingV1 content identity

ocgforge.policy.public_action_key.v1
    -> ocgforge.policy.public_action_key.v2
    -> new PolicyArtifact content identity
    -> new ParticipantPolicyAssignment content identity
```

The exact new `teacher_core_artifact_identity` value is intentionally frozen
by the authorized Teacher-fix slice, not invented by this public-operation
design. Reusing the old `ocgforge.policy.teacher_core.v1` producer/artifact
identity for the changed continuation behavior is forbidden. The
`TeacherPolicyBindingV1` codec itself may remain v1 because its field schema
does not change; its content ID must nevertheless be recomputed from the new
Teacher core artifact identity. The v2 job then binds `episodic_environment.v3`,
the new policy artifact and binding IDs, and the same ordered 16
seed/placement/starting-player coordinates. It does not alter the coordinates
or silently replace a job.

## 9.5 Exact trajectory and evidence byte deltas

Every successor below retains the existing primitive encodings, vector order,
strict sorting, and fail-closed validation unless a delta is listed
explicitly. This prevents an implementer from choosing a new field position
or silently changing an old v1 meaning.

### Trusted trajectory v2 candidate

The v2 public candidate record is:

```text
0  candidate schema:string = ocgforge.trusted_trajectory.v2
1  action kind:string
2  card_selection_operation:u8 (0=None, 1=Select, 2=Unselect)
3  public_action_key:string = public_action.v2 key
4  typed choice:optional {kind:u8, value:u64be, response_index:optional u32be}
5  source reference:optional {kind:u8, observation_locator:string}
6  target reference:optional {kind:u8, observation_locator:string}
7  phase:optional u32be
8  position:optional u8
9  source index:optional u32be
10 amount:optional signed i32 as u32be bits
11 continuation operation:string
12 submits engine response:boolean
```

The v2 request codec retains the v1 order:

```text
0  request schema:string = ocgforge.trusted_trajectory.v2
1  request kind:string
2  player:u8
3  candidates:vector of TrustedTrajectoryV2 candidate bytes
4  continuation:optional existing public continuation bytes
```

The v2 frame snapshot retains the v1 order, with the renamed field
`environment_contract_id` at the former `v2_contract_id` position:

```text
0  frame schema:string = ocgforge.trusted_trajectory.v2
1  environment_contract_id:string = ocgforge.episodic_environment.v3
2  episode semantic ID:string
3  public semantic decision ID:string
4  decision index:u64be
5  acting player:u8
6  public observation:existing canonical public-observation bytes
7  public observation digest:string
8  public decision request:TrustedTrajectoryV2 request bytes
9  public candidate-domain digest:string
```

The v2 decision-record and envelope codecs retain their v1 field order after
replacing their trusted-trajectory schema/domain and nested v3 frame/request
values. Transition classes, successor tags, policy attribution, and closure
semantics are unchanged.

The v2 episode manifest retains the current manifest order. The former
misleading `v2_contract_id` source/API field is named
`environment_contract_id` and carries the v3 value:

```text
0  manifest schema/domain:string = ocgforge.trusted_trajectory.v2
1  environment_contract_id:string = ocgforge.episodic_environment.v3
2  environment semantic ID:string
3  environment identity input:bytes
4  episode identity schema:string = ocgforge.episode_identity.v1
5  episode semantic ID:string
6  episode identity input:bytes
7  policy provenance envelope:existing canonical bytes
8  collection disposition:existing canonical bytes
```

The v2 public frame snapshot and decision-record structures use the same
renaming at their former `v2_contract_id` position. No v2 type may expose a
field named `v2_contract_id` whose value is actually a v3 contract.

### Restricted replay evidence v2

The exact v2 evidence bytes retain the existing field order:

```text
0  evidence schema:string = ocgforge.restricted_replay_evidence.v2
1  environment_contract_id:string = ocgforge.episodic_environment.v3
2  episode semantic ID:string
3  closure kind:u8 = 1 (INTERRUPTED)
4  interruption reason:u8
5  engine-process budget:u64be
6  semantic-action budget:u64be
7  observed engine-process count:u64be
8  observed semantic-action count:u64be
9  final engine-step index:u64be
```

### Shard, gameplay, record, receipt, and dataset successors

The exact deltas for the remaining persisted identities are:

| Successor | Canonical order | Explicit delta |
| --- | --- | --- |
| `trajectory_shard.v2` | v1 shard order retained | domain/schema v2; envelope decoder is trajectory v2 |
| `public_gameplay_trajectory_identity.v2` | v1 identity order retained | domain/schema v2; trusted trajectory v2; environment contract v3 |
| `trajectory_record_identity.v2` | v1 identity order retained | domain/schema v2; trusted trajectory v2; nested v2 public-gameplay ID |
| `admission_receipt.v2` | v1 receipt order retained | domain/schema/admission ID v2; candidate shard and restricted bundle are v2 |
| `dataset_identity.v2` | v1 identity order retained | domain/schema v2; trusted trajectory contract v2; same sorted record-ID vector |
| `dataset_manifest.v2` | v1 manifest order retained | domain/schema v2; dataset-identity schema v2; trusted trajectory v2; member fields/order unchanged |
| `ocgforge.phase6.bc_sample_identity.v1` | `ocgforge.phase6.bc_sample_identity.v2` | derived sample binds trajectory/model v2 identities |

For `admission_receipt.v2`, the entry fields remain:

```text
trajectory_record_id:string
public_gameplay_trajectory_id:string
environment_semantic_id:string
episode_semantic_id:string
episode_envelope_sha256:string
closure_kind:u8
```

Only their accepted successor identity prefixes and the enclosing contract
closure change. Receipt v1 remains bound to v1 records and v1 shard/evidence
artifacts.

For the two collection-evidence layers, the v2 bundle embeds the exact v2
restricted-evidence bytes shown above; it does not flatten or duplicate their
fields. Interrupted entries and RNG initialization entries retain their v1
ordering and atomic validation rules.

The exact identity byte orders for the remaining successors are:

```text
TrajectoryShardV2:
0  shard domain:string = ocgforge.trajectory_shard.v2
1  shard schema:string = ocgforge.trajectory_shard.v2
2  entry count:u32be
3..n entries in existing ascending envelope-digest order:
       episode-envelope SHA-256:string
       envelope length:u32be
       canonical TrustedTrajectoryV2 envelope bytes

PublicGameplayTrajectoryIdentityV2:
0  identity domain:string = ocgforge.public_gameplay_trajectory_identity.v2
1  identity schema:string = ocgforge.public_gameplay_trajectory_identity.v2
2  trajectory contract:string = ocgforge.trusted_trajectory.v2
3  environment contract:string = ocgforge.episodic_environment.v3
4  environment semantic ID:string
5  episode identity schema:string = ocgforge.episode_identity.v1
6  episode semantic ID:string
7  public record count:u32be
8..n canonical public decision-record bytes in decision-index order
n+1 canonical public closure bytes

TrajectoryRecordIdentityV2:
0  identity domain:string = ocgforge.trajectory_record_identity.v2
1  identity schema:string = ocgforge.trajectory_record_identity.v2
2  trajectory contract:string = ocgforge.trusted_trajectory.v2
3  public gameplay trajectory ID:string
4  canonical policy-provenance envelope bytes
5  policy decision-attribution count:u32be
6..n policy decision-attribution bytes in decision-index order
n+1 collection disposition bytes

DatasetIdentityV2:
0  identity domain:string = ocgforge.dataset_identity.v2
1  identity schema:string = ocgforge.dataset_identity.v2
2  trusted trajectory contract:string = ocgforge.trusted_trajectory.v2
3  member count:u32be
4..n sorted unique trajectory-record IDs

DatasetManifestV2:
0  manifest domain:string = ocgforge.dataset_manifest.v2
1  manifest schema:string = ocgforge.dataset_manifest.v2
2  dataset identity schema:string = ocgforge.dataset_identity.v2
3  trusted trajectory contract:string = ocgforge.trusted_trajectory.v2
4  dataset semantic ID:string
5  member count:u32be
6..n member fields in existing order:
       trajectory-record ID
       public-gameplay trajectory ID
       admission-receipt ID
       candidate-shard artifact SHA-256
       episode-envelope SHA-256

AdmissionReceiptV2:
0  admission domain:string = ocgforge.admission_receipt.v2
1  admission schema:string = ocgforge.admission_receipt.v2
2  admission contract ID:string = ocgforge.admission_receipt.v2
3  candidate-shard artifact SHA-256:string
4  restricted-bundle artifact SHA-256:string
5  entry count:u32be
6..n entry fields in existing order:
       trajectory-record ID
       public-gameplay trajectory ID
       environment semantic ID
       episode semantic ID
       episode-envelope SHA-256
       closure kind:u8
```

The lexical identity prefixes also change with these successor domains:

```text
public_gameplay_trajectory.v2.<digest>
trajectory_record.v2.<digest>
admission_receipt.v2.<digest>
dataset_manifest.v2 / dataset identity v2 as specified by their contracts
bc_sample.v2.<digest>
```

## 10. Phase-5 model closure

The model layer must retain the new operation as a candidate feature. It may
not leave it only in routing metadata because Teacher/model equivalence needs
the same public semantic input.

The new model-facing identity set is:

```text
ocgforge.model_logical_input.v2
ocgforge.model_encoded_input.v2
ocgforge.model_input_identity.v2
ocgforge.model_batch_layout.v2
ocgforge.model_supervision_sample.v2
```

`model_card_vocabulary.v1` remains unchanged; the operation is a small fixed
category and does not alter passcode-to-vocabulary mapping.

### 10.1 Logical and encoded candidate fields

`LogicalCandidateV2` adds:

```text
card_selection_operation:
    None | Select | Unselect
```

`EncodedCandidateV2` adds a fixed categorical code:

```text
0 = None
1 = Select
2 = Unselect
```

The field is placed immediately after encoded action kind. Presence is not
optional in v2; `None` is the explicit category. Candidate count, source
order, routing-key separation, locator-token rules, redaction, and all
card-vocabulary rules remain unchanged.

The exact logical candidate bytes retain the current logical-v1 candidate
order, with the new field inserted after the action-kind token:

```text
0  action_kind:string
1  card_selection_operation:u8 (0=None, 1=Select, 2=Unselect)
2  public_action_key:string (routing value, not a learned feature)
3  choice:optional {kind:u8, value:u64be, response_index:optional u32be}
4  source_reference:optional {kind:u8, public_locator:string}
5  target_reference:optional {kind:u8, public_locator:string}
6  phase:optional u32be
7  position:optional u8
8  source_index:optional u32be
9  amount:optional signed i32 as u32be bits
10 continuation_operation:string
11 submits_engine_response:boolean
```

`current_entity_ordinal` is not part of either logical reference entry and is
not written to the canonical `LogicalCandidateV2` bytes. It may remain an
in-memory value used while resolving a public reference and may be represented
by the encoded candidate codec's existing derived-reference representation,
but it must not become a new logical-input byte or a second logical reference
identity. The logical contract writes only the public locator string shown
above.

The exact encoded candidate row retains the current encoded-v1 order, with
the new `u8` immediately after the `action_kind_code`:

```text
0  action_kind_code:u16be
1  card_selection_operation_code:u8 (0=None, 1=Select, 2=Unselect)
2  choice:optional {kind:u8, value:u64be, response_index:optional u32be}
3  source_reference:optional encoded public reference
4  target_reference:optional encoded public reference
5  phase:optional u32be
6  position:optional u8
7  source_index:optional u32be
8  amount:optional signed i32 as u32be bits
9  continuation_operation_code:u8
10 submits_engine_response:boolean
```

`public_action_key` remains the parallel routing entry in
`LogicalCandidateRouting`/`routing_keys`; it is not duplicated as a learned
candidate feature. The logical and encoded v2 top-level codecs retain their
v1 field order except for their successor schema IDs and this candidate-field
insertion.

### 10.2 Batch and supervision

Model batch layout v2 is required because the encoded candidate row and its
canonical batch representation gain a field. Ragged offsets, padding widths,
row masks, and candidate order keep their v1 semantics.

Model supervision sample v2 is required because it binds a v2 model-input
identity and a v2 trajectory source. Its selected-key/ordinal relationship
remains unchanged; candidate ordinal remains local label metadata and never
becomes action identity.

The Phase-5 model-input identity v2 retains the current identity order:

```text
0  identity domain:string = ocgforge.model_input_identity.v2
1  identity schema:string = ocgforge.model_input_identity.v2
2  logical input schema:string = ocgforge.model_logical_input.v2
3  encoded input schema:string = ocgforge.model_encoded_input.v2
4  card vocabulary identity:string
5  canonical LogicalModelInputV2 bytes
6  canonical EncodedModelInputV2 bytes
```

The batch and supervision successor codecs retain their respective v1 field
orders and change only their successor schema/child-contract values plus the
encoded candidate-row bytes where those rows are embedded.

The model-input inspector, BC scorer, and inference request/response layers
must reject v1 model inputs when configured for v2 and must not accept v2
inputs through v1 validators. Their successor contracts are required because
their accepted input, model-input identity, or public-domain binding changes.

### 10.3 Phase-6 model and evaluation closure

The current Phase-6 contract freeze names the following direct V1 bindings:

```text
ocgforge.phase6.model_input_inspection.v1
ocgforge.phase6.bc_contract.v1
ocgforge.phase6.bc_candidate_scorer.v1
ocgforge.phase6.inference_request.v1
ocgforge.phase6.inference_response.v1
ocgforge.phase6.ordered_candidate_domain.v1
ocgforge.phase6.task4.numeric_projection.v1
ocgforge.phase6.task4.corpus_authority.v1
ocgforge.phase6.dataset_membership.v1
```

Each receives an explicit v2 successor before a future V3/V2 public-action
collection can be admitted:

| Current contract | Successor | Reason |
| --- | --- | --- |
| `ocgforge.phase6.model_input_inspection.v1` | `ocgforge.phase6.model_input_inspection.v2` | validates model-input-v2 values |
| `ocgforge.phase6.bc_contract.v1` | `ocgforge.phase6.bc_contract.v2` | prerequisite closure changes |
| `ocgforge.phase6.bc_candidate_scorer.v1` | `ocgforge.phase6.bc_candidate_scorer.v2` | encoded candidate row changes |
| `ocgforge.phase6.inference_request.v1` | `ocgforge.phase6.inference_request.v2` | model/domain child identities change |
| `ocgforge.phase6.inference_response.v1` | `ocgforge.phase6.inference_response.v2` | request/response binding changes |
| `ocgforge.phase6.ordered_candidate_domain.v1` | `ocgforge.phase6.ordered_candidate_domain.v2` | public action keys are v2 |
| `ocgforge.phase6.task4.numeric_projection.v1` | `ocgforge.phase6.task4.numeric_projection.v2` | candidate numeric schema gains operation code |
| `ocgforge.phase6.task4.corpus_authority.v1` | `ocgforge.phase6.task4.corpus_authority.v2` | authority binds v2 model inputs |
| `ocgforge.phase6.dataset_membership.v1` | `ocgforge.phase6.dataset_membership.v2` | membership source chain changes |
| `ocgforge.phase6.bc_architecture_config.v1` | `ocgforge.phase6.bc_architecture_config.v2` | canonical candidate width changes from 28 to 29 |
| `ocgforge.phase6.task7.input_materialization.v1` | `ocgforge.phase6.task7.input_materialization.v2` | materialized candidate table gains operation code |
| `ocgforge.phase6.task7.input_materialization_config.v1` | `ocgforge.phase6.task7.input_materialization_config.v2` | KAT/config child-contract vector changes |
| `ocgforge.phase6.task7.dataset_collection_job.v1` | `ocgforge.phase6.task7.dataset_collection_job.v2` | environment/policy provenance bindings change |
| `ocgforge.phase6.task7.dataset_collection_schedule.v1` | `ocgforge.phase6.task7.dataset_collection_schedule.v2` | ordered v2 job-identity vector changes |
| `ocgforge.policy.public_action_key.v1` | `ocgforge.policy.public_action_key.v2` | policy action-adapter semantics change |
| `ocgforge.phase6.task5.evaluation_execution.v1` | `ocgforge.phase6.task5.evaluation_execution.v2` | prerequisite contract vector changes |
| `ocgforge.phase6.task4.smoke_corpus.v2` | `ocgforge.phase6.task4.smoke_corpus.v3` | future reissued corpus would bind model/public v2 values |
| `ocgforge.phase6.bc_sample_identity.v1` | `ocgforge.phase6.bc_sample_identity.v2` | sample source and model identities change |

The Phase-6 architecture identity is also directly affected. The current
architecture fixes the candidate row width at 28 and includes that width in
its canonical identity. Its successor is:

```text
ocgforge.phase6.bc_architecture_config.v1
    -> ocgforge.phase6.bc_architecture_config.v2

phase6_architecture_config.v1.<digest>
    -> phase6_architecture_config.v2.<digest>
```

Architecture v2 retains the exact current field order and values except for
the successor identity values, the successor numeric projection identity, and:

```text
candidate row width:u32 = 29
```

The Task4 numeric projection v2 is positional and fully specified as follows;
the new operation is not appended at an implementation-chosen offset:

```text
CandidateNumericRowV2 width = 29

v2[0]      = normalized action_kind_code
v2[1]      = normalized_u8(card_selection_operation_code)
v2[2..28]  = former CandidateNumericRowV1[1..27], in the existing order
              and with the existing normalization/presence rules
```

The operation code uses the public categorical mapping `0=None`, `1=Select`,
`2=Unselect` and the existing Task4 `u8` normalization exactly:

```text
normalized_u8(x) = IEEE-754 binary32(float(x) * (1.0F / 255.0F))

None     -> normalized_u8(0) = 0 / 255
Select   -> normalized_u8(1) = 1 / 255
Unselect -> normalized_u8(2) = 2 / 255
```

The displayed fractions identify the exact input value to the existing
binary32 normalization; the stored feature is the resulting finite binary32
value. Every former v1 feature therefore retains its relative order; only the
new operation feature occupies index 1 and shifts the former v1 indices 1
through 27 by one position. The additional row position is the fixed
`card_selection_operation_code`.
`canonical_weight_export.v1` does not require a version bump merely because
the architecture identity changes: its generic tensor/weight codec remains
the same. A future weight manifest must nevertheless bind the v2 architecture
identity and cannot reuse a v1 weight-content claim for a v2 shape.

The Task7 non-smoke materializer has its own direct successor closure:

```text
ocgforge.phase6.task7.input_materialization.v1
    -> ocgforge.phase6.task7.input_materialization.v2

ocgforge.phase6.task7.input_materialization_config.v1
    -> ocgforge.phase6.task7.input_materialization_config.v2
```

Materialization v2 retains the current table, column, row, offset, padding,
and routing rules. Its Phase-5 contract vector changes to the v2 logical,
encoded, input-identity, and batch contracts, while `model_card_vocabulary.v1`
remains present. The candidate table adds one exact categorical column:

```text
card_selection_operation_code:u8
    0=None
    1=Select
    2=Unselect
```

That column is immediately after the existing action-kind code. The exact
candidate-table descriptor vector is the v1 vector with exactly one inserted
descriptor at position 1; every other descriptor retains its old position
relative to the other v1 descriptors:

```text
candidate table column descriptor[index]

0  ("action_kind_code", "U16", 1, "required", "zero")
1  ("card_selection_operation_code", "U8", 1, "required", "zero")
2  ("choice_present", "Bool", 0, "required", "false")
3  ("choice_kind_code", "U8", 1, "required", "zero")
4  ("choice_value", "U64", 4, "required", "zero")
5  ("choice_response_index", "P<U32>", 2, "optional", "zero")
6  ("source_reference", "CR", 0, "composite_defined", "not_applicable")
7  ("target_reference", "CR", 0, "composite_defined", "not_applicable")
8  ("phase", "P<U32>", 2, "optional", "zero")
9  ("position", "P<U8>", 1, "optional", "zero")
10 ("source_index", "P<U32>", 2, "optional", "zero")
11 ("amount", "P<I32>", 2, "optional", "zero")
12 ("continuation_operation_code", "U8", 1, "required", "zero")
13 ("submits_engine_response", "Bool", 0, "required", "false")
```

The exact v2 rule-descriptor vector retains the v1 entries at indices 0
through 9 and appends one descriptor at index 10:

```text
0  { rule_id:"candidate_cardinality",             rule_value:"N_TO_N" }
1  { rule_id:"candidate_order",                  rule_value:"SOURCE_ORDER" }
2  { rule_id:"candidate_split",                  rule_value:"FORBIDDEN" }
3  { rule_id:"routing_key_learned_feature",      rule_value:"NO" }
4  { rule_id:"raw_locator_learned_feature",      rule_value:"NO" }
5  { rule_id:"padding_semantic",                 rule_value:"NO" }
6  { rule_id:"ragged_authority",                 rule_value:"RAGGED_FIRST" }
7  { rule_id:"padded_equivalence",               rule_value:"EXACT_UNPAD" }
8  { rule_id:"globals_chain_length_source",      rule_value:"DISTINCT" }
9  { rule_id:"chain_state_length_source",        rule_value:"DISTINCT" }
10 { rule_id:"card_selection_operation_code",
     rule_value:"U8_CODE_0_NONE_1_SELECT_2_UNSELECT" }
```

The v2 materialization configuration bytes retain the current top-level field
order, but bind the successor materialization/schema IDs, Phase-5 contract
vector, candidate table descriptor vector above, column order, and rule
descriptor vector above.
Its configuration identity and KAT are new values; the v1 materialization
identity and KAT remain historical.

The v2 configuration identity has this lexical form:

```text
phase6_task7_input_materialization_config.v2.<lowercase SHA-256 digest>
```

The exact top-level v2 materialization configuration order is:

```text
0  configuration schema:string = ocgforge.phase6.task7.input_materialization_config.v2
1  materialization schema:string = ocgforge.phase6.task7.input_materialization.v2
2  Phase-5 contract vector:
       model_logical_input.v2
       model_encoded_input.v2
       model_card_vocabulary.v1
       model_input_identity.v2
       model_batch_layout.v2
3  limb-order token:string = u16_most_significant_first
4  integer tensor type:string = torch.int64
5  boolean tensor type:string = torch.bool
6  reference descriptor vector:existing order
7  table descriptor vector:existing order plus candidate operation column
8  rule descriptor vector:existing order plus operation-code rule
```

The v2 known-answer vector is a three-field evidence record over these exact
bytes. It is not part of the configuration byte stream and is not a substitute
for the descriptor vectors:

```text
CONFIG_CANONICAL_BYTES_LENGTH=8264
CONFIG_CANONICAL_BYTES_SHA256=298cc5b9a8e27349cfea67e3df53adea57bb51c0cae1467e169d3482f7966162
CONFIGURATION_IDENTITY=phase6_task7_input_materialization_config.v2.298cc5b9a8e27349cfea67e3df53adea57bb51c0cae1467e169d3482f7966162
```

An independent reconstruction of the v2 grammar must reproduce all three
values. The SHA-256 input has no appended newline, document hash, file path,
Git commit, device, framework version beyond the frozen physical type tokens,
batch composition, padding width, or runtime provenance.

Task7 collection authority also requires successor orchestration identities:

```text
ocgforge.phase6.task7.dataset_collection_job.v1
    -> ocgforge.phase6.task7.dataset_collection_job.v2

ocgforge.phase6.task7.dataset_collection_schedule.v1
    -> ocgforge.phase6.task7.dataset_collection_schedule.v2
```

Job v2 retains the current canonical field order and the exact 16-coordinate
schedule. It changes the environment contract to
`ocgforge.episodic_environment.v3`, binds the v2 public-action adapter and new
Teacher artifact/binding identities, and uses new job/schedule identity
prefixes. The seed, placement, starting-player, budget, and collection
coordinates are not changed.

The current `ocgforge.phase6.task4.smoke_corpus.v2` and its smoke/acceptance evidence
remain historical. If a future smoke corpus is ever authorized for the new
closure, it requires a new derivation version rather than relabeling the
existing v2 corpus. The same rule applies to Phase-6 Task5 evaluation,
first-divergence, and report contracts whose canonical prerequisite vectors
currently contain the old environment, trajectory, admission, dataset, or
model contract IDs.

In particular, a future Task5 execution contract must use
`ocgforge.phase6.task5.evaluation_execution.v2` with a successor prerequisite
vector. Its existing v1 contract, report, evaluation job, first-divergence,
and replay/admission values remain historical until a separately authorized
Task5 migration specifies their exact child-identity closure. No Task5
execution is part of this design.

The following are not automatically versioned solely because a downstream
identity changed:

```text
ocgforge.model_card_vocabulary.v1
ocgforge.phase6.dataset_split.v1
ocgforge.phase6.inference_numeric.v1
ocgforge.phase6.bc_objective.v1
ocgforge.phase6.bc.inference_tiebreak.v1
```

They may retain their own codec versions only if their own canonical fields,
code tables, and meanings remain unchanged and their future parent/config
closure explicitly names the v2/v3 contracts. Any one of them that currently
hard-codes an old child contract must receive a successor before future use.
No current Phase-6 artifact is rewritten or reissued by this design.

## 11. Phase-6 historical compatibility

Existing Phase-6 contracts and artifacts remain historical:

```text
ocgforge.episodic_environment.v2
ocgforge.trusted_trajectory.v1
ocgforge.trajectory_shard.v1
ocgforge.restricted_replay_evidence.v1
ocgforge.restricted_collection_evidence_bundle.v1
ocgforge.admission_receipt.v1
ocgforge.dataset_manifest.v1
ocgforge.dataset_identity.v1
ocgforge.public_gameplay_trajectory_identity.v1
ocgforge.trajectory_record_identity.v1
ocgforge.model_logical_input.v1
ocgforge.model_encoded_input.v1
ocgforge.model_input_identity.v1
ocgforge.model_batch_layout.v1
ocgforge.model_supervision_sample.v1
ocgforge.phase6.task7.input_materialization.v1
ocgforge.phase6.bc_sample_identity.v1
```

They remain readable only under their original contract closure. They must
not be relabeled as v3/v2 values and must not be mixed with new v3 public
frames or v2 model inputs.

Any future Phase-6 authority, collection, evaluation, or materialization
contract that hard-codes the old chain must receive an explicit successor
version before it can authorize a new V3 collection. Existing Phase-6
datasets are neither migrated nor regenerated by this design.

The historical/current split is therefore explicit:

```text
old V2 public environment + trajectory/model V1 chain
    -> remains readable with original semantics

new V3 public environment + public identity V2
    -> requires trajectory/replay/admission/dataset/model successor closure

mixed old/new chain
    -> fail closed before mutation or admission
```

## 12. Privacy and determinism invariants

The operation is safe to publish because it is derived from the acting
player's current legal prompt, not from hidden card identity. The projection
continues to use only:

```text
PlayerObservation-derived public observation
complete public candidate domain
typed decoder metadata
```

The following remain forbidden:

```text
semantic_key
raw protocol bytes
exact internal response bytes
CoreHost
hidden card identity
private locator
pointer or object address
submission token in semantic identity
```

Canonical output must remain independent of candidate map iteration, process
identity, wall time, host path, thread scheduling, and compiler incidental
ordering. `Select` and `Unselect` must be encoded by explicit enum values,
not by enum memory layout or string spelling inferred from internal keys.

## 13. Required implementation acceptance

Implementation is not authorized by this document. When separately
authorized, the implementation must prove at least:

### Decoder and projection

```text
selected-list candidate -> internal Unselect -> public Unselect
unselected-list candidate -> internal Select -> public Select
unrelated CardSelection -> None
Finish / Cancel -> None
invalid request/operation combination -> whole-frame fail closed
```

### Identity and replay

```text
otherwise identical Select and Unselect public descriptors differ
v1 golden vectors remain byte-identical
v1 readers reject v2 keys/contracts
v2/v3 replay resolves the exact current public key
internal semantic key and response bytes never cross the public boundary
```

### Downstream

```text
trusted trajectory v2 round trip
trajectory shard v2 round trip
restricted replay v2 round trip
restricted collection evidence bundle v2 round trip
admission v2 cross-version rejection
dataset manifest v2 binding
logical/encoded model v2 round trip
model batch v2 lossless reconstruction
model supervision v2 identity binding
Task7 materialization/config v2 round trip and new KAT
Task7 collection job/schedule v2 identity closure
policy action-adapter v2 and new Teacher provenance IDs
old Phase-6 artifacts remain historical and unmodified
```

### Privacy and determinism

```text
paired hidden worlds preserve public operation semantics
redacted card identity remains redacted
fresh processes produce identical stdout/stderr/exit code
complete candidate count and order are unchanged
```

## 14. Sequencing and authorization boundary

The authorized order is:

```text
design review
    -> explicit implementation authorization

A. public semantic prerequisite
    -> internal decoder auxiliary metadata
    -> public DTO and public action/domain/decision v2
    -> episodic environment v3 / environment identity v3
    -> public privacy, replay, and determinism tests

B. Teacher remediation
    -> retained Goal/Line commitment
    -> Select continuation progress
    -> Unselect no generic progress bonus
    -> PLACE progress
    -> exact Hiita Job0 regression

C. before RUN_A
    -> trusted trajectory/replay/evidence/bundle v2
    -> shard/admission/dataset v2
    -> Task7 collection job/schedule v2
    -> new policy action-adapter and Teacher provenance IDs

D. before new ML materialization or training
    -> model logical/encoded/input/batch/supervision v2
    -> Task7 materialization/config v2
    -> Phase-6 BC/inference/numeric/architecture successor closure
    -> only then consider a new data or training execution
```

The semantic prerequisite is intentionally separable from the downstream
trajectory/model migrations. It may establish the public operation contract
and unlock the Teacher remediation after its own acceptance. It does not
authorize RUN_A, RUN_B, collection, materialization, or training. Each later
stage requires its own exact-head authorization and independent review.
