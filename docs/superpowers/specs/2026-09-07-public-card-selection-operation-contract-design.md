# Public Card Selection Operation Contract Design

## Status

Design draft for review. The design text is authorized; implementation is not.

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
v2 environment + v2 trajectory -> accepted under historical rules
v3 environment + v2 trajectory -> rejected
v2 environment + v3 trajectory -> rejected
v3 environment + v3 trajectory -> eligible after all v3 checks
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

Policy provenance, policy-artifact, participant-assignment, and policy-RNG
codecs retain their own v1 encodings when their field layouts do not change.
New policy-artifact values may carry a new action-adapter identity, producing
new content IDs without silently changing the old codec's meaning.

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

### 10.2 Batch and supervision

Model batch layout v2 is required because the encoded candidate row and its
canonical batch representation gain a field. Ragged offsets, padding widths,
row masks, and candidate order keep their v1 semantics.

Model supervision sample v2 is required because it binds a v2 model-input
identity and a v2 trajectory source. Its selected-key/ordinal relationship
remains unchanged; candidate ordinal remains local label metadata and never
becomes action identity.

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
| `phase6.model_input_inspection.v1` | `phase6.model_input_inspection.v2` | validates model-input-v2 values |
| `phase6.bc_contract.v1` | `phase6.bc_contract.v2` | prerequisite closure changes |
| `phase6.bc_candidate_scorer.v1` | `phase6.bc_candidate_scorer.v2` | encoded candidate row changes |
| `phase6.inference_request.v1` | `phase6.inference_request.v2` | model/domain child identities change |
| `phase6.inference_response.v1` | `phase6.inference_response.v2` | request/response binding changes |
| `phase6.ordered_candidate_domain.v1` | `phase6.ordered_candidate_domain.v2` | public action keys are v2 |
| `phase6.task4.numeric_projection.v1` | `phase6.task4.numeric_projection.v2` | candidate numeric schema gains operation code |
| `phase6.task4.corpus_authority.v1` | `phase6.task4.corpus_authority.v2` | authority binds v2 model inputs |
| `phase6.dataset_membership.v1` | `phase6.dataset_membership.v2` | membership source chain changes |

The current `phase6.task4.smoke_corpus.v2` and its smoke/acceptance evidence
remain historical. If a future smoke corpus is ever authorized for the new
closure, it requires a new derivation version rather than relabeling the
existing v2 corpus. The same rule applies to Phase-6 Task5 evaluation,
first-divergence, and report contracts whose canonical prerequisite vectors
currently contain the old environment, trajectory, admission, dataset, or
model contract IDs.

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
episodic_environment.v2
trusted_trajectory.v1
admission_receipt.v1
dataset_manifest.v1
model_*_v1
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
admission v2 cross-version rejection
dataset manifest v2 binding
logical/encoded model v2 round trip
model batch v2 lossless reconstruction
model supervision v2 identity binding
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
    -> internal decoder auxiliary field
    -> public DTO and v2 identity codecs
    -> episodic v3 / environment v3 closure
    -> trajectory/replay/admission/model successors
    -> focused cross-version/privacy/determinism tests
    -> Teacher retention fix
    -> exact Job0 regression
    -> independent review
    -> only then consider RUN_A
```

This design authorizes none of the implementation or execution steps after
the review boundary.
