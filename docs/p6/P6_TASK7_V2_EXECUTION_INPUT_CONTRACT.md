# Phase 6 Task7 V2 execution-input contract

## Status and authorization

```text
TASK=TASK7_V3_V2_EXECUTION_INPUT_CONTRACT_FREEZE_05
BASE=3c139bf2117d2f07b6a292c73ff457d0e0892187
BRANCH=chris/task9-bounded-job0-validation-09

TYPE=DOCUMENTATION_ONLY_CONTRACT_FREEZE
SOURCE_CODE_CHANGES_AUTHORIZED=NO
TEST_CODE_CHANGES_AUTHORIZED=NO
OPTIMIZER_STEPS_AUTHORIZED=0
GPU_TRAINING_AUTHORIZED=NO
CHECKPOINT_CREATION_AUTHORIZED=NO
MEANINGFUL_EVALUATION_AUTHORIZED=NO

FROZEN_DESIGN=YES
INDEPENDENT_REVIEW=PENDING
IMPLEMENTATION_AUTHORIZED=NO
```

This document defines the successor boundary between the accepted Task7 V3
dataset authority and a future physical model-execution path. It does not
implement that path and does not authorize a materializer, a backend adapter,
an architecture, training, a checkpoint, or meaningful evaluation.

The existing V1 materialization contract remains historical and unchanged. A
V2 input MUST NOT be downgraded to it or interpreted through it.

The project priority remains:

```text
correctness
→ determinism
→ information safety
→ complete legal decisions
→ replay/auditability
→ maintainability
→ performance
→ ML scale
```

## 1. Boundary and ownership

The semantic source remains the accepted public/replay path:

```text
Task7V3DatasetAuthority
        ↓
canonical V3 authority decode and validation
        ↓
DatasetManifestV3 + VerifiedAdmissionReceiptV3
        + admitted EpisodeEnvelopeV3 values
        + authority-issued TrainingDatasetSplitV1
        + authority-issued CardVocabularyV1
        ↓
ModelSupervisionSampleV2
LogicalModelInputV2
EncodedModelInputV2
        ↓
new Task7 physical materialization V2
        ↓
new model-batch layout V2
        ↓
new framework/backend tensor representation
```

Ownership is intentionally split:

| Boundary | Owner | This contract permits |
| --- | --- | --- |
| legality and candidate domain | certified V4 Environment and V3 public identities | consume only already admitted public frames |
| replay and admission | V3 replay/admission contracts | prove the source before materialization |
| dataset membership, split, vocabulary | Task7 V3 Authority | issue the binding source values |
| logical/encoded model input | `ygo::model` V2 | preserve public semantics and exact candidate order |
| physical materialization | future Task7 V2 implementation | encode validated V2 values without loss |
| backend tensors | future backend adapter | map physical V2 values without semantic reinterpretation |
| model architecture/training/checkpoint | later separately authorized task | not defined or implemented here |

The physical materializer is derived execution data. It is not a new legality,
replay, public-action, candidate-domain, dataset, or checkpoint authority.

## 2. Exact successor generations

The following generation chain is normative for the future implementation:

| Surface | Exact identity/schema | Status in this slice |
| --- | --- | --- |
| Task7 source authority | `ocgforge.phase6.task7.dataset_authority.v3` | existing accepted source |
| logical model input | `ocgforge.model_logical_input.v2` | existing V2 semantic source |
| encoded model input | `ocgforge.model_encoded_input.v2` | existing V2 semantic source |
| model-input identity | `ocgforge.model_input_identity.v2` | existing V2 semantic source |
| supervision sample | `ocgforge.model_supervision_sample.v2` | existing V2 semantic source |
| physical materialization | `ocgforge.phase6.task7.input_materialization.v2` | successor frozen here |
| materialization configuration | `ocgforge.phase6.task7.input_materialization_config.v2` | successor frozen here |
| materialized sample identity | `phase6_task7_materialized_sample.v2.<sha256>` | successor frozen here |
| materialized batch identity | `phase6_task7_materialized_batch.v2.<sha256>` | successor frozen here |
| model batch layout | `ocgforge.model_batch_layout.v2` | successor frozen here |
| backend tensor bundle | `ocgforge.phase6.task7.backend_tensor_bundle.v2` | representation frozen; adapter later |

The materialization configuration identity is:

```text
schema_id:
  ocgforge.phase6.task7.input_materialization_config.v2

materialization_schema_id:
  ocgforge.phase6.task7.input_materialization.v2

identity_prefix:
  phase6_task7_input_materialization_config.v2.
```

The materialized-sample and materialized-batch identities are physical
integrity identities over their canonical V2 bytes. They are not replacements
for `public_action.v3`, `public_candidate_domain.v3`,
`public_semantic_decision_identity.v3`, the dataset semantic identity, or the
model-input identity.

The identity is derived from the canonical body and is not serialized inside
that body. This avoids a self-referential identity field:

```text
materialized_sample_identity
  = phase6_task7_materialized_sample.v2.
    + lowercase_hex(SHA256(canonical_materialized_sample_body_v2))

materialized_batch_identity
  = phase6_task7_materialized_batch.v2.
    + lowercase_hex(SHA256(canonical_materialized_batch_body_v2))
```

## 3. Batch-layout decision

```text
BATCH_LAYOUT_DECISION=VERSIONED_SUCCESSOR
BATCH_LAYOUT_SCHEMA=ocgforge.model_batch_layout.v2
```

The existing `ocgforge.model_batch_layout.v1` is not reusable for this path.
Its candidate rows are `EncodedCandidate` V1 rows and do not contain
`card_selection_operation_code`. Reusing that layout would either drop the V2
field or reinterpret it through an unrelated field, both of which are
forbidden.

The V2 layout may reuse the V1 physical principles and table families, but it
has independent schema validation, canonical bytes, and cross-generation
rejection. Similar table names do not make V1 and V2 bytes interchangeable.

The V2 configuration bytes are canonical in this exact conceptual order:

```text
string(materialization_config_schema_id)
string(materialization_schema_id)
string(model_batch_layout_schema_id)
string(backend_tensor_bundle_schema_id)
vector<string>(ordered source contract IDs)
string(integer_limb_order_token)
string(integer_tensor_type_token)
string(boolean_tensor_type_token)
vector<ReferenceDescriptorV2>(canonical reference order)
vector<TableDescriptorV2>(canonical table order)
vector<RuleDescriptorV2>(canonical rule order)
```

The ordered source contract IDs are exactly:

```text
ocgforge.model_logical_input.v2
ocgforge.model_encoded_input.v2
ocgforge.model_supervision_sample.v2
ocgforge.model_card_vocabulary.v1
ocgforge.model_input_identity.v2
ocgforge.model_batch_layout.v2
```

The primitive tokens are exactly:

```text
u16_most_significant_first
torch.int64
torch.bool
```

The V2 reference and table descriptor vectors retain the accepted V1
descriptor order and rules, with every V1 model-input contract replaced by the
V2 source contract IDs above and with one additional candidate column:

```text
(card_selection_operation_code, U8, 1, required, zero)
```

inserted immediately after `action_kind_code` in the `candidates` table. No
other V1 column is removed, renamed, reordered, or given a new meaning. This
explicit delta is part of the V2 configuration bytes and therefore its
configuration identity.

### 2.1 Configuration known-answer test

The V2 configuration KAT is derived from the byte grammar above. As a
derivation check, the same encoder shape reproduces the accepted V1 KAT before
applying the declared V2 changes:

```text
V1_CONFIG_CANONICAL_BYTES_LENGTH=8133
V1_CONFIG_CANONICAL_BYTES_SHA256=20f394c888e959446fa263c3520f3dd3b1f48b3a23e58373da7153a691ab1e7a
```

The resulting V2 KAT is normative:

```text
CONFIG_CANONICAL_BYTES_LENGTH=8317
CONFIG_CANONICAL_BYTES_SHA256=ce39fdd472614f4fa9e622d93fb5628549dd3705e501e9d287e679fa307063b9
CONFIGURATION_IDENTITY=phase6_task7_input_materialization_config.v2.ce39fdd472614f4fa9e622d93fb5628549dd3705e501e9d287e679fa307063b9
CONFIG_CANONICAL_BYTES_PREFIX_HEX=000000356f6367666f7267652e7068617365362e7461736b372e696e7075745f6d6174657269616c697a6174696f6e5f
CONFIG_CANONICAL_BYTES_SUFFIX_HEX=495354494e435400000019636861696e5f73746174655f6c656e6774685f736f757263650000000844495354494e4354
```

The prefix and suffix are each exactly 48 bytes. Their equality with the
corresponding V1 prefix/suffix is expected: the V2 changes occur after the
shared initial fields and before the unchanged terminal rule descriptor.

The candidate-column position is explicit and zero-based:

```text
V2_DESCRIPTOR_ORDER_CHANGED=NO_EXCEPT_DECLARED_CANDIDATE_COLUMN_INSERTION
CARD_SELECTION_OPERATION_COLUMN_INDEX=1
CARD_SELECTION_OPERATION_COLUMN_INDEX_ZERO_BASED=1
CARD_SELECTION_OPERATION_COLUMN_INDEX_ONE_BASED=2
```

No other table, column, token meaning, order, or padding rule changes between
the accepted V1 descriptor vector and this V2 descriptor vector.

The V2 layout is:

```text
RaggedModelBatchV2
    = canonical semantic execution view

PaddedModelBatchV2
    = optional physical view
    = accepted only when exact unpadding reproduces RaggedModelBatchV2
```

Ragged input is authoritative. Padding width, bucket width, device, batch
composition, worker count, and allocation order are physical execution data
and are not semantic source authority.

## 4. Authority-to-dataset binding

The future materializer MUST consume a validated V3 authority capability or an
equivalent library-owned validated authority object. It MUST NOT accept a
caller-supplied manifest, receipt list, envelope list, split, or vocabulary as
an independent replacement for the authority.

The authority handoff MUST perform all of the following before producing any
physical bytes:

1. Decode the supplied canonical Task7 V3 authority bytes.
2. Re-encode them canonically and require byte equality.
3. Recompute and require the exact `phase6_task7_dataset_authority.v3.*`
   identity.
4. Validate the complete fixed Task7 V3 schedule and every job outcome.
5. Require every materialized record to be a clean, admitted V3 record whose
   envelope, receipt, shard, and manifest commitments resolve through that
   authority.
6. Require the authority-issued manifest, split, and vocabulary to be the
   values used for materialization.
7. Reject a missing, extra, duplicate, mixed-generation, failed, quarantined,
   or interrupted source rather than producing a partial physical dataset.

The binding vector for the physical materialization is:

```text
source_task7_authority_identity
source_dataset_manifest_identity
source_dataset_semantic_identity
source_training_dataset_split_identity
source_card_vocabulary_identity
source_trajectory_record_id
source_episode_semantic_id
source_public_semantic_decision_id
source_model_input_identity_v2
materialization_configuration_identity_v2
```

These values are integrity/provenance bindings. Filesystem paths, process
IDs, wall time, worker IDs, GPU/device details, Python object identity, and
temporary allocation details MUST NOT enter the semantic dataset identity,
model-input identity, or candidate identity.

### 4.1 Split binding

`TrainingDatasetSplitV1` remains the split representation, but the source is
the V3 authority. The materializer MUST:

```text
use authority.split
→ recompute the deterministic split from the authority's admitted episode IDs
→ require canonical split bytes and identity to match
→ reject any caller override
```

The materializer MUST NOT reassign an episode, use latest-wins behavior, or
derive a new split from physical batch order. Empty or mismatched partitions
remain fail-closed.

### 4.2 Vocabulary binding

`CardVocabularyV1` remains the vocabulary generation for this boundary. The
materializer MUST use the authority-issued canonical vocabulary and require:

```text
canonical vocabulary bytes = authority vocabulary bytes
vocabulary identity = authority vocabulary identity
```

An independently supplied vocabulary, a newly inferred vocabulary, a hidden
card identity, or a V1/V2 mixed vocabulary is rejected.

## 5. Source derivation and replay boundary

The source derivation order is fixed:

```text
validated Task7 V3 authority
    ↓
validated admitted V3 envelope/receipt association
    ↓
accepted public DecisionFrame
    ↓
project_logical_model_input_v2(...)
    ↓
encode_model_input_v2(...)
    ↓
materialize_model_supervision_sample_v2(...)
    ↓
V2 ragged batch/materialization
```

The materializer MUST NOT reconstruct candidates from a digest, action key,
trajectory label, or physical row. It consumes the complete ordered public
candidate domain already present in the accepted frame and V2 model inputs.

Replay remains upstream and unchanged:

```text
V3 trajectory
→ V4 environment reconstruction
→ exact V3 public frame/domain/decision validation
→ admission
→ only then V2 model-input/materialization derivation
```

Restricted replay evidence may be required to prove an interrupted source, but
it is not learner input and MUST NOT be copied into the V2 model-input,
materialization, tensor, or checkpoint feature surface.

## 6. Lossless candidate contract

For every accepted sample with `N` public candidates:

```text
N source candidates
→ N LogicalCandidateV2 values
→ N EncodedCandidateV2 values
→ N V2 physical candidate rows
→ N backend candidate rows
→ N score slots
```

The following candidate fields are mandatory and lossless:

| V2 semantic field | V2 physical representation | Rule |
| --- | --- | --- |
| `action_kind` | `action_kind_code` | exact code, no family collapse |
| `choice` | presence, kind, value, response-index fields | exact optionality and value |
| `source_reference` | typed reference expansion | exact public locator/reference semantics |
| `target_reference` | typed reference expansion | exact public locator/reference semantics |
| `phase` | optional integer limbs plus presence | exact value |
| `position` | optional integer limbs plus presence | exact value |
| `source_index` | optional integer limbs plus presence | exact value and source order |
| `amount` | signed integer limbs plus presence | exact signed value |
| `continuation_operation` | `continuation_operation_code` | exact code |
| `submits_engine_response` | boolean | exact value |
| `card_selection_operation` | `card_selection_operation_code` | exact V2 operation code |
| public routing key | one-to-one control sidecar | exact source-order pairing |

The operation codes remain:

```text
None     = 0
Select   = 1
Unselect = 2
```

`card_selection_operation_code` MUST be serialized as its own named V2 field.
It MUST NOT be:

```text
dropped
mapped into action_kind_code
mapped into continuation_operation_code
mapped into submits_engine_response
encoded only in a routing string
reconstructed later from a public action key
```

For a valid encoded V2 candidate, the existing request-context rules remain
binding:

```text
unselect_card + CardSelection → Select or Unselect
unselect_card + Cancel        → None
unselect_card + Finish        → None
other request kinds           → every candidate operation is None
```

The future materializer validates these rules again. A matching V2 public key
or digest does not excuse an invalid contextual operation.

## 7. Candidate order and routing

Candidate order is authoritative and is the order supplied by the accepted
V4 public frame. The future implementation MUST:

```text
preserve source order
preserve candidate cardinality
preserve 1:1 routing-key alignment
preserve the selected candidate ordinal
```

It MUST NOT sort, deduplicate, truncate, cap, filter, split, or replace a
candidate domain. A candidate-domain failure rejects the complete sample or
batch.

The ordered V3 `public_action_key` values remain routing/control metadata. They
are not a global action vocabulary and are not a license to derive hidden
features. The routing sidecar MUST have exactly one valid V3 public key per
physical candidate row in the same source order.

The selected label MUST satisfy:

```text
selected_public_action_key ∈ exact ordered V3 domain
selected candidate ordinal identifies exactly one row
ModelSupervisionSampleV2.selected_public_action_key
    = materialized routing key at selected ordinal
```

## 8. V2 physical materialization schema

The canonical V2 materialization packet is a typed, ragged-first packet. Its
top-level fields are conceptually:

```text
materialization_schema_id
materialization_configuration_identity
source_task7_authority_identity
source_dataset_manifest_identity
source_dataset_semantic_identity
source_training_dataset_split_identity
source_card_vocabulary_identity
ordered materialized sample records
```

Each materialized sample record contains:

```text
source_trajectory_record_id
source_episode_semantic_id
source_public_semantic_decision_id
source_model_input_identity_v2
model_supervision_sample_v2 schema and fields:
  model_input_identity
  source_public_semantic_decision_id
  selected_public_action_key
  candidate_ordinal
public_observation_digest
public_candidate_domain_digest
all V2 state tables
complete V2 candidate table
ordered V3 routing-key sidecar
canonical row masks and offsets
```

The sample identity is derived from the complete canonical sample body using
the identity rule in section 2; it is not another body field. The batch body
contains the source authority/dataset/split/vocabulary bindings once, then the
ordered sample bodies and their derived sample identities. The batch identity
is derived from that complete canonical batch body and is not serialized inside
it. Because a sample body is independently verifiable, it also carries the
same source binding fields listed in the sample grammar; the batch-level values
and every repeated sample-level value MUST compare byte-for-byte.

The V2 candidate table is the V1 candidate table's exact public field surface
plus the separately named:

```text
card_selection_operation_code : U8, required, zero padding
```

The V2 schema and configuration identity bind the complete table vector,
column vector, primitive representation, reference descriptors, row-order
rules, optional-presence rules, padding rules, candidate N-to-N rule, routing
sidecar rule, authority-binding fields, and canonical byte grammar.

The canonical sample body order is:

```text
string(materialization_schema_id)
string(materialization_configuration_identity)
string(source_task7_authority_identity)
string(source_dataset_manifest_identity)
string(source_dataset_semantic_identity)
string(source_training_dataset_split_identity)
string(source_card_vocabulary_identity)
string(source_trajectory_record_id)
string(source_episode_semantic_id)
string(source_public_semantic_decision_id)
string(source_model_input_identity_v2)
string(model_supervision_sample_v2.schema_id)
string(model_supervision_sample_v2.model_input_identity)
string(model_supervision_sample_v2.source_public_semantic_decision_id)
string(model_supervision_sample_v2.selected_public_action_key)
u32be(model_supervision_sample_v2.candidate_ordinal)
string(public_observation_digest)
optional_string(public_candidate_domain_digest)
canonical V2 table vector in configuration order
canonical V2 routing-key sidecar
canonical offsets, presence masks, and real-row masks
```

The canonical batch body order is:

```text
string(materialization_schema_id)
string(materialization_configuration_identity)
string(source_task7_authority_identity)
string(source_dataset_manifest_identity)
string(source_dataset_semantic_identity)
string(source_training_dataset_split_identity)
string(source_card_vocabulary_identity)
u32be(ordered sample count)
for each sample in declared order:
  string(derived materialized_sample_identity)
  bytes(length-prefixed canonical materialized sample body)
```

All `string`, vector, optional, integer, table, offset, and mask encodings use
the existing repository canonical byte grammar. A decoder MUST reject trailing
bytes, noncanonical encodings, duplicate sample identities, and any mismatch
between the declared ordered sample count and the body.

All integer values use an exact typed representation. The implementation may
use fixed base-2^16 limbs as the physical representation, but it MUST NOT use
lossy float normalization for semantic integer values, nullable values, codes,
or source indices.

The V2 materializer MUST reject the entire input on the first inconsistency;
it MUST NOT yield a valid subset, partial batch, fallback value, or Task4
smoke projection.

## 9. Backend tensor representation

The backend representation is a physical view of the V2 packet, not a new
semantic source. The frozen representation is:

```text
schema_id = ocgforge.phase6.task7.backend_tensor_bundle.v2
integer tensor token = torch.int64
boolean tensor token = torch.bool
```

The future PyTorch adapter MUST expose every V2 physical candidate row and
every real-row mask with exact cardinality. It MUST preserve the named
`card_selection_operation_code`, `continuation_operation_code`, and
`submits_engine_response` values. It MUST preserve source order and routing
alignment even if routing keys remain outside the learned tensor set.

The following remain forbidden in the learner-facing tensor surface:

```text
raw response bytes
submission tokens
engine steps as semantic features
CoreHost pointers or object IDs
private locators
hidden opponent cards
restricted replay evidence
filesystem paths or process metadata
```

The backend adapter MUST reject a V1 materialization packet, a V1 batch layout,
or a Task4 numeric smoke packet when a V2 tensor bundle is required. It MUST
not silently convert V2 limbs/codes to the lossy Task4 float rows.

No architecture width, embedding, scorer parameterization, optimizer, loss,
training schedule, device policy, or checkpoint identity is frozen by this
section. Those are later decisions.

## 10. Canonical bytes and determinism

For identical validated authority/input/configuration values, canonical V2
materialization bytes MUST be byte-identical across fresh processes.

Canonical V2 bytes include, in declared order:

```text
V2 materialization schema and configuration identities
authority/dataset/split/vocabulary binding identities
source record and V2 model-input identities
supervision label binding
public state table values
complete candidate table values
ordered routing sidecar
offsets, optional-presence masks, and real-row masks
```

They exclude:

```text
PID
wall time
thread scheduling
allocation address
filesystem path
Python object identity
GPU/device name
worker count
batch timing
```

Padded and ragged forms are equivalent only when exact unpadding reproduces the
same V2 ragged values, offsets, candidate order, operation codes, routing
sidecar, and masks. Padding is never a candidate, score slot, or semantic
feature.

## 11. Cross-version rejection matrix

The following combinations are normative:

| Combination | Result |
| --- | --- |
| V3 Authority + V2 logical/encoded input + V2 materializer | ACCEPT after full validation |
| V2 historical Authority + V2 materializer | REJECT; new path requires V3 Authority |
| V3 Authority + V1 materializer | REJECT |
| V2 logical/encoded input + V1 materializer | REJECT |
| V1 logical/encoded input + V2 materializer | REJECT |
| V2 encoded input + V1 batch layout | REJECT |
| V1 encoded input + V2 batch layout | REJECT |
| V2 materialization + V1 tensor decoder | REJECT |
| V1 materialization + V2 tensor bundle | REJECT |
| Task4 numeric projection + V2 materializer | REJECT |
| caller manifest/receipts without validated V3 Authority | REJECT |
| caller vocabulary differing from authority vocabulary | REJECT |
| mixed V1/V2 sample or batch | REJECT |
| duplicate or missing candidate routing keys | REJECT |

V1 contracts, V1 goldens, Task4 history, and the accepted V3 gameplay/replay
authority remain unchanged. No old identity may acquire V2 meaning.

## 12. Privacy and information safety

Allowed inputs are limited to:

```text
validated Task7 V3 authority bindings
admitted public EpisodeEnvelopeV3 frames
PublicEnvironmentObservation/public-safe state
complete ordered EnvironmentActionCandidate values
V3 public action/domain/decision identities
ModelSupervisionSampleV2 public label binding
authority-issued CardVocabularyV1
```

The learner/materializer MUST NOT inspect or derive from:

```text
CoreHost internals
raw engine card objects
private locators
opponent hidden hand/deck entries
unrevealed Extra Deck identities
raw response bytes
submission tokens
pointer/address/process data
RestrictedReplayEvidence contents
```

If a required V2 physical value cannot be obtained from the validated public
source, the materializer rejects. It does not infer, guess, or query an
omniscient source.

## 13. Acceptance gates for the next implementation

The following gates must pass before implementation is accepted:

```text
V2_MATERIALIZATION_SCHEMA_FROZEN=YES
V2_CONFIG_CANONICAL_BYTES_FROZEN=YES

CARD_SELECTION_OPERATION_LOSSLESS=YES
CONTINUATION_OPERATION_LOSSLESS=YES
SUBMITS_ENGINE_RESPONSE_LOSSLESS=YES

EXACT_N_TO_N=YES
SOURCE_ORDER_PRESERVED=YES
ROUTING_PAIRING_PRESERVED=YES

AUTHORITY_TO_DATASET_BINDING_DEFINED=YES
DATASET_SPLIT_BINDING_DEFINED=YES
VOCABULARY_BINDING_DEFINED=YES

BATCH_LAYOUT_DECISION=VERSIONED_SUCCESSOR
BATCH_LAYOUT_SCHEMA=ocgforge.model_batch_layout.v2

PRIVACY_BOUNDARY_UNCHANGED=YES
REPLAY_BOUNDARY_UNCHANGED=YES
V1_CONTRACTS_UNCHANGED=YES
TASK4_HISTORY_UNCHANGED=YES
```

The implementation must include negative coverage for:

```text
V2 input into V1 materializer
V1 input into V2 materializer
missing card_selection_operation_code
changed card_selection_operation_code
changed continuation_operation_code
changed submits_engine_response
candidate count mismatch
candidate order mismatch
routing-key pairing mismatch
authority/manifest mismatch
split mismatch
vocabulary mismatch
mixed-generation batch
padding/unpadding mismatch
hidden/private source attempt
```

## 14. Explicit non-goals and sequencing

This freeze authorizes no implementation. The intended dependency order for
separately authorized work is:

```text
06  C++ V3 Authority decode/validated handoff + V2 materializer/batch
 ↓
07  Python/PyTorch V2 tensor/backend binding
 ↓
08  architecture/training/checkpoint execution freeze
 ↓
first separately authorized meaningful training
```

Not part of this contract freeze:

```text
optimizer or training schedule
GPU execution
checkpoint creation
Task5C gameplay evaluation
Teacher changes
Environment changes
Protocol changes
replay/admission changes
dataset-authority changes
Task4 projection changes
```

```text
V2_MATERIALIZER_IMPLEMENTATION_AUTHORIZED=NO
PYTORCH_V2_IMPLEMENTATION_AUTHORIZED=NO
TRAINING_AUTHORIZED=NO
CHECKPOINT_CREATION_AUTHORIZED=NO
MEANINGFUL_EVALUATION_AUTHORIZED=NO
```
