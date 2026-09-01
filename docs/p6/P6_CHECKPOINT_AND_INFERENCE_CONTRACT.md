# OCGForge Phase 6 Task 1 — Checkpoint and Inference Contract

## Status and scope

**Status:** CURRENT / AUTHORIZED — documentation-only contract freeze.

This document freezes the OCGForge-owned checkpoint manifest, canonical weight
export boundary, deterministic inference request/response binding, and
fail-closed neural-policy behavior. It does not choose PyTorch or JAX, define
one neural architecture, add a serialization dependency, generate weights, or
start training.

The accepted Phase-5 model-facing contracts remain the sole source of model
input meaning. The words **MUST**, **MUST NOT**, and **FAIL CLOSED** are
normative.

## 1. Contract identifiers and authority

| Surface | Contract identity | Authority |
| --- | --- | --- |
| training run manifest | `ocgforge.phase6.training_run.v1` | Phase-6 provenance contract |
| canonical weight export | `ocgforge.phase6.canonical_weight_export.v1` | OCGForge export boundary |
| checkpoint manifest | `ocgforge.phase6.checkpoint_manifest.v1` | immutable OCGForge checkpoint identity |
| inference request | `ocgforge.phase6.inference_request.v1` | current-decision request binding |
| inference response | `ocgforge.phase6.inference_response.v1` | one validated score/selection response |
| inference numeric comparison | `ocgforge.phase6.inference_numeric.v1` | declared deterministic score comparison |

The physical training framework, tensor container, device, worker, process,
directory, and transport are implementation details. A physical artifact is
usable only after it resolves to and validates against the OCGForge manifest
and identity contracts below.

## 2. Training-run provenance

`TrainingRunManifestV1` records how a training run was conducted. Its identity
is distinct from both the semantic model/checkpoint identity and the physical
framework state:

```text
TrainingRunManifestV1 {
    schema_id: "ocgforge.phase6.training_run.v1"
    training_contract_identity: "ocgforge.phase6.bc_contract.v1"
    source_dataset_identity: exact dataset_semantic_id
    dataset_split_identity: exact Phase-6 split identity
    phase5_model_contract_identities: exact Phase-5 IDs
    card_vocabulary_identity: exact model_card_vocabulary.v1 identity
    model_architecture_config_identity: immutable architecture/config identity
    behavior_policy_source_identities: exact eligible Teacher source IDs
    opponent_policy_source_identities: exact opponent source IDs when relevant
    training_code_commit: immutable source commit
    framework_backend_identity: backend/adapter identity
    framework_version: exact implementation version
    optimizer_configuration: canonical future training configuration
    learning_rate_schedule: canonical future schedule configuration
    batch_configuration: physical execution configuration
    gradient_accumulation_configuration: explicit training configuration
    training_rng_contract_identity: versioned training RNG contract
    training_seed: explicit seed or seed derivation identity
    initial_checkpoint_identity: optional immutable checkpoint identity
    precision_mode: explicit execution provenance
    device_and_distributed_provenance: execution provenance
    final_exported_checkpoint_identity: exact immutable checkpoint identity
}
```

The Phase-5 identities MUST include the logical model-input, encoded
model-input, model-batch-layout, and card-vocabulary contract identities. The
manifest MUST record the concrete `CardVocabularyV1` identity, not merely the
catalog name or database path.

The training-run identity is:

```text
phase6_training_run.v1.<lowercase hexadecimal SHA-256 of canonical manifest bytes>
```

All configuration values that affect the run are canonicalized under this
manifest. `training_code_commit` MUST be an immutable commit, not a branch or
mutable workspace. Framework version, optimizer, learning-rate schedule,
batching, precision, devices, world size, distributed strategy, and execution
environment are provenance; they MUST NOT silently become gameplay, sample,
model-input, or checkpoint semantic identity.

The manifest MUST distinguish:

```text
semantic model/checkpoint identity
training-run identity
framework-native training state
execution-environment identity
```

Changing an optimizer, batch size, device, or worker layout can create a new
training-run identity without changing the semantic checkpoint identity when
the canonical exported model/config content and all checkpoint identity
inputs remain equal.

## 3. Canonical weight export boundary

Training backends may retain native state such as:

```text
PyTorch optimizer/DDP/FSDP state
JAX optimizer/XLA sharding state
gradient scaler state
worker-local random state
temporary or resumable framework checkpoints
```

Those are training execution artifacts. They are not automatically OCGForge
inference weights and MUST NOT be loaded as a canonical checkpoint solely
because a backend can deserialize them.

The required future export flow is:

```text
framework-native training state
        ↓ explicit export adapter
canonical inference weight representation
        ↓ exact canonical export bytes
canonical exported weight-content identity
        ↓ manifest validation
OCGForge checkpoint identity
```

The canonical export contract MUST define an ordered, framework-neutral
representation of inference parameters, including:

- the architecture/config identity;
- parameter names in an explicit canonical order;
- logical shapes and declared semantic numeric representation;
- exact parameter contents;
- the export codec identity and its canonical bytes;
- no optimizer, gradient, sharding, device, process, or cache state.

The exact tensor serialization library is intentionally not selected here.
Safetensors or another exact serialization may later implement the export
codec, but the library is not checkpoint authority. The canonical export
codec and content digest MUST be versioned before a checkpoint can be
accepted.

Equivalent inference weights exported from different distributed layouts or
from different training frameworks MUST be capable of producing the same
canonical weight-content identity when their canonical tensor/config bytes are
identical. Bit-identical training across PyTorch/JAX, GPU models, or device
counts is not required by this contract.

## 4. Immutable checkpoint manifest

`CheckpointManifestV1` is the OCGForge-owned semantic wrapper around canonical
inference weights:

```text
CheckpointManifestV1 {
    schema_id: "ocgforge.phase6.checkpoint_manifest.v1"
    checkpoint_schema_version: "ocgforge.phase6.checkpoint_manifest.v1"
    model_architecture_config_identity: exact immutable identity
    phase5_input_contract_identities: exact logical/encoded/layout IDs
    card_vocabulary_identity: exact model_card_vocabulary.v1 identity
    dataset_identity: exact dataset_semantic_id
    dataset_split_identity: exact Phase-6 split identity
    training_contract_identity: exact BC training contract identity
    parent_checkpoint_identity: optional immutable checkpoint identity
    canonical_weight_export_contract_identity: exact export contract identity
    canonical_weight_content_identity: exact content identity
}
```

The checkpoint identity is:

```text
phase6_checkpoint.v1.<lowercase hexadecimal SHA-256 of canonical manifest bytes>
```

The canonical identity input includes the fields above in the listed order,
using the accepted Phase-3/Phase-5 primitive encoding. It binds at least:

```text
checkpoint schema/version
model architecture/config
Phase-5 input contracts
CardVocabulary identity
dataset identity and split identity
BC training contract identity
parent checkpoint identity when applicable
canonical exported weight-content identity
```

The checkpoint manifest MAY reference `training_run_identity`, hardware,
framework, exporter process, or publication records as provenance sidecars,
but those values do not automatically enter checkpoint semantic identity. A
checkpoint provenance reference MUST resolve to an immutable training-run
manifest; a mutable path is not enough.

No mutable alias is an identity:

```text
latest
best
prod
run-17
branch name
physical checkpoint directory
Hugging Face Hub mutable ref
```

An alias MAY be a user-interface locator only when it resolves to one exact
`checkpoint_identity` and the resolved manifest/content is revalidated. A
publication to a future artifact hub MUST pin an exact revision and canonical
content identity before use.

Changing architecture/config, Phase-5 input contract, vocabulary, dataset,
split, training contract, parent, export contract, or canonical weight content
requires a new checkpoint identity. Replacing bytes under an old identity is
invalid.

## 5. Deterministic inference request

The inference adapter receives a current public decision and one immutable
checkpoint. It creates this semantic request before invoking the learner:

```text
InferenceRequestV1 {
    schema_id: "ocgforge.phase6.inference_request.v1"
    checkpoint_identity: exact immutable checkpoint identity
    model_input_identity: exact Phase-5 model-input identity
    ordered_candidate_domain_identity: exact current ordered-domain identity
    public_semantic_decision_id: optional exact public decision identity
    perspective_player: u8
    decision_index: u64
    request_identity: immutable digest of these fields
}
```

`ordered_candidate_domain_identity` is derived by the runner from the exact
current public candidate vector. When the accepted public observation carries
the existing `public_candidate_domain_digest`, that identity is used after
recomputation. If the observation context has no request kind and therefore no
existing digest, the runner uses the versioned ordered-key digest:

```text
ocgforge.phase6.ordered_candidate_domain.v1
+ candidate count
+ exact public_action_key values in source order
```

The runner MUST validate that this domain is the same domain bound into the
Phase-5 model input. A caller-provided identity is an integrity check, not an
independent authority. The request identity is:

```text
phase6_inference_request.v1.<lowercase hexadecimal SHA-256 of canonical request bytes>
```

The live V2 submission token may remain in the environment control plane for
freshness validation, but it is not model input and is not part of the public
model-input identity. The current public decision identity, model-input
identity, checkpoint identity, and ordered-domain identity together prevent a
response for decision N from being applied to decision N+1.

## 6. Deterministic inference response

The semantic response is:

```text
InferenceResponseV1 {
    schema_id: "ocgforge.phase6.inference_response.v1"
    request_identity: exact request identity
    checkpoint_identity: exact checkpoint identity
    model_input_identity: exact model-input identity
    ordered_candidate_domain_identity: exact domain identity
    score_count: u32 = N
    scores: finite score[N] in exact source order
    selected_candidate_ordinal: u32
    selected_public_action_key: exact key at that ordinal
    response_identity: immutable response digest
}
```

The response MUST satisfy all of these checks before the Environment can
resolve it:

1. the request identity is the currently outstanding request;
2. checkpoint, model-input, and ordered-domain identities match exactly;
3. `score_count` equals the exact current candidate count `N`;
4. every score is finite and representable under the declared numerical
   contract;
5. the selected ordinal is in `0..N-1`;
6. the selected key equals the existing public key at that ordinal; and
7. the response has not already been consumed.

`selected_candidate_ordinal` is only a frame-local coordinate used to verify
the routing sidecar. The semantic selection and externally resolvable identity
remain the existing `public_action_key`; the ordinal is not replay identity,
public action identity, or a replacement for the key.

The response selects one supplied public candidate; it never submits response
bytes or an internal semantic key. The Environment alone resolves the
selected `public_action_key` and advances the current decision.

A stale, duplicated, late, wrong-checkpoint, wrong-input, wrong-domain,
wrong-length, non-finite, malformed, or invalid-selection response is
rejected and fails closed. It MUST NOT be applied to a later frame, retried
with another policy, or repaired by choosing a different ordinal.

## 7. Deterministic selection and numerical comparison

The initial BC inference mode is deterministic and non-sampling:

```text
higher finite score wins
exact equal-score ties → bytewise-ascending existing public_action_key
```

The tie rule is `ocgforge.phase6.bc.inference_tiebreak.v1`. It compares only
the validated keys in the current routing sidecar; it does not feed keys to a
learned feature extractor and does not reorder candidate rows. Duplicate keys
are a domain failure before this rule can run. Backend-dependent unordered
iteration MUST NOT choose a candidate.

The semantic cross-implementation acceptance result is the selected
`public_action_key`. A score vector is diagnostic evidence and may be
compared across implementations only under the explicitly declared,
versioned `ocgforge.phase6.inference_numeric.v1` contract. No implicit
rounding or tolerance is permitted. A later numerical contract MUST state its
absolute/relative tolerance, score representation, and action-ambiguity rule;
if the permitted numerical interval could change the selected key, the
deterministic gate fails closed rather than accepting a backend-dependent
choice.

For identical accepted model inputs, checkpoint/configuration, and
deterministic inference mode, a frozen checkpoint MUST reproduce the same
selected `public_action_key`. The implementation may use different physical
devices or tensor containers only when the declared numerical contract still
proves that semantic result.

The full exact-domain capacity rule remains:

```text
N candidates → N scores → one supplied key
```

The existing Phase-5 witnesses `N=24`, `N=25`, and `N=129` are mandatory
future tests. A physical width smaller than `N` is a structured failure. No
truncation, top-K reduction, domain split, candidate reconstruction, or
candidate-zero fallback is allowed.

## 8. Failure and quarantine semantics

These conditions MUST NOT invoke another gameplay policy:

```text
model timeout
model process crash
transport or serialization failure
missing output
NaN or Inf score
wrong output length
stale, duplicated, or late response
wrong checkpoint identity
wrong model-input identity
wrong ordered candidate-domain identity
invalid selected ordinal or public key
insufficient candidate capacity
privacy or public-input validation failure
```

The future runner records a structured neural-policy failure and quarantines
the affected evaluation/training run according to its own accepted runner
contract. It may close an episode as `FAILED` or `INTERRUPTED` only under the
existing environment/trajectory meanings; it MUST NOT fabricate an action,
winner, reward, or trusted admission. A fallback-assisted game is never a
neural-policy win and is never counted as neural-policy acceptance evidence.

No Teacher, RandomLegal, heuristic, first-candidate, candidate-zero, or
retry-with-another-policy fallback exists at this boundary.

## 9. Privacy and inspection boundary

Requests, responses, checkpoint manifests, and normal diagnostics MUST contain
only accepted public model fields and immutable public/provenance identities.
They MUST NOT contain hidden passcodes, opponent-private deck/hand identity,
private locators, internal semantic keys, raw engine pointers, response bytes,
submission tokens, hidden-derived hashes, or omniscient debug state.

The future model-input inspector consumes only the accepted public observation,
public candidate vector, and Phase-5 representations. It must be able to show
the self/opponent perspective, context, zones, visible/redacted identities,
public references, candidate kinds/optional fields/order/count, and selected
label without querying `CoreHost` or private `PlayerObservation` state.

The paired-hidden-world gate requires equal logical/encoded input identities,
equal model-input identity, equal deterministic score outputs under the
declared numeric contract, and equal selected public key for two worlds with
different hidden information but equal public observation/domain.

## 10. Deferred deployment boundary

Project Ignis / EDOPro Bot Adapter V1 is not part of Task 1. Its trigger is:

```text
first useful trained checkpoint
+ canonical checkpoint identity
+ frozen inference contract
+ successful frozen evaluation
```

The adapter must later remain a thin deployment boundary and may not become a
second legality, heuristic, or bot engine. Human games from it begin as
evaluation/debug evidence; trusted human-demonstration import requires a
separate admission contract.

The accepted positive-lethal limitation is unchanged:

```text
POSITIVE_LETHAL_CAPABILITY = BLOCKED_BY_ACCEPTED_CURRENT_ACTION_CONTRACT
```

## 11. Related authority and non-goals

- [BC contract](P6_BC_CONTRACT.md)
- [Dataset and split contract](P6_DATASET_AND_SPLIT_CONTRACT.md)
- [Phase-5 model contract](../p5/P5_MODEL_CONTRACT.md)
- [Policy provenance v1](../contracts/policy-provenance-v1.md)
- [Trusted trajectory v1](../contracts/trusted-trajectory-v1.md)
- [Phase-6 evaluation plan](P6_EVALUATION_PLAN.md)

Task 1 does not authorize PyTorch, JAX, Accelerate, Safetensors, Hugging Face
Datasets, Trackio, `huggingface_hub`, neural network code, optimizer code,
training, GPU usage, checkpoint generation, RL, self-play, search, arbitrary
decks, or Project Ignis/EDOPro integration.
