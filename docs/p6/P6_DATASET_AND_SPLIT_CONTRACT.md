# OCGForge Phase 6 Task 1 — Dataset and Split Contract

## Status and scope

**Status:** CURRENT / AUTHORIZED — documentation-only contract freeze.

This document freezes trusted BC dataset membership, sample derivation, and
the deterministic episode-level partition. It defines the semantic source of
training data and the rules for deriving physical rows. It does not materialize
a dataset, add a loader, add an ML dependency, or authorize Phase-6 Task 2.

The accepted Phase-3B trajectory/admission contracts and Phase-5 model
contracts remain unchanged. The words **MUST**, **MUST NOT**, and **FAIL
CLOSED** are normative.

## 1. Dataset authority

Trusted BC membership is the intersection of these already accepted values:

```text
one immutable DatasetManifest v1
+ its declared dataset_semantic_id
+ every member's verified admission receipt
+ whole-shard validation and semantic V2 replay
+ a clean trusted trajectory record
```

The `DatasetManifest` member list and the admitted `trajectory_record_id`
values are the only authority for membership. A loader MUST revalidate the
manifest members against their `AdmissionReceipt` commitments before exposing
them to materialization. An arbitrary file is not membership merely because it
can be parsed.

The following are derived physical representations only:

```text
JSONL, NumPy, Arrow, Parquet, pickle, mmap, tensor caches
HF Datasets, RLDS, Minari, RLlib, Reverb, TorchRL
shards, files, compression, cache directories, worker-local indexes
```

Deleting or rebuilding any such representation MUST NOT change the semantic
dataset identity. A derived representation MUST carry or resolve the exact
source `dataset_semantic_id`, split identity, derivation contract, and source
sample identities.

The relevant semantic identifiers are:

| Surface | Contract identity |
| --- | --- |
| Phase-3B dataset identity | `ocgforge.dataset_identity.v1` |
| Phase-3B manifest | `ocgforge.dataset_manifest.v1` |
| Phase-3B admission | `ocgforge.admission_receipt.v1` |
| Phase-6 dataset membership | `ocgforge.phase6.dataset_membership.v1` |
| Phase-6 split | `ocgforge.phase6.dataset_split.v1` |
| Phase-6 split identity | `ocgforge.phase6.dataset_split_identity.v1` |
| Phase-6 derived sample identity | `ocgforge.phase6.bc_sample_identity.v1` |

## 2. Eligible initial curriculum

The initial source corpus is limited to the fixed matchup and accepted
Teacher identities in [the BC contract](P6_BC_CONTRACT.md#4-initial-fixed-curriculum-and-eligible-behavior-sources):

```text
ocgforge.matchup.swordsoul_salamangreat.v1
rules bundle 3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f
ocgforge.swordsoul_tenyi.ml_v1
ocgforge.salamangreat.ml_v1
```

Only admitted trajectories whose participant assignments resolve to the two
accepted Teacher v1 `PolicyArtifact` identities for their acting deck roles
are eligible positive behavior demonstrations. The corpus MUST preserve the
exact profile, binding, producer, sampling contract, policy artifact, rules
bundle, and deck identities as collection provenance.

RandomLegal trajectories MUST NOT be promoted to positive Teacher labels. A
legal action from another source is not a Teacher demonstration unless a
later, separately versioned experiment contract explicitly admits it.

Teacher selection is behavior provenance, not an optimality oracle. The
materializer records the exact selected public key and its derived ordinal; it
does not label all other candidates as strategically negative.

## 3. Eligible trajectory and decision material

One eligible BC sample is derived from one accepted `DecisionRecord` in one
trusted, non-failed trajectory record. The record MUST include:

- a value-owned `PublicFrameSnapshot`;
- the complete ordered public candidate vector for that frame;
- a selected existing `public_action_key` occurring exactly once;
- a verified acting-participant policy assignment and policy provenance;
- a valid Phase-5 `LogicalModelInputV1` / `EncodedModelInputV1` derivation;
- an admission receipt proving whole-shard validation and semantic replay.

Continuation decisions are ordinary eligible decision records. Each current
continuation domain is materialized as its own exact-domain sample; no
continuation is flattened into an invented global action vocabulary. An
interrupted boundary with no accepted action does not produce a fake label.

Both participant perspectives, every accepted decision, every accepted
continuation decision, and every admitted shard that contains them remain in
scope when the source manifest admits them. The materializer MUST NOT drop a
decision because its domain is uncommon, its candidate count is large, or its
public state contains redaction/presence masks.

The source record's public action key is matched by exact string equality.
Missing, duplicated, malformed, or inconsistent selected keys reject the
sample and do not cause candidate removal or first-match repair.

## 4. Semantic sample identity

`ModelSupervisionSampleV1` remains the accepted Phase-5 derived value. Phase 6
adds a stable derived sample identity without changing the trusted trajectory:

```text
BCSampleV1 {
    schema_id: "ocgforge.phase6.bc_sample_identity.v1"
    trajectory_record_id: exact trusted record identity
    episode_semantic_id: exact V2 episode/duel identity
    public_semantic_decision_id: exact public decision identity
    model_input_identity: exact Phase-5 model-input identity
    selected_public_action_key: exact selected public key
    candidate_ordinal: zero-based ordinal derived from the current ordered domain
}
```

The sample identity is:

```text
bc_sample.v1.<lowercase hexadecimal SHA-256 of the canonical fields above>
```

Canonical fields use the accepted Phase-3/Phase-5 primitive encoding and this
exact order:

```text
identity domain
identity schema
trajectory_record_id
episode_semantic_id
public_semantic_decision_id
model_input_identity
selected_public_action_key
candidate_ordinal:u32be
```

The identity excludes shard name/path, row number, physical format,
compression, cache path, split, batch width, padding, framework, device,
process, worker order, timestamp, and training seed. It is a derived sample
identity, not a replacement for `trajectory_record_id`, public action
identity, or model-input identity.

Two rows with the same sample identity MUST have byte-equivalent semantic
values. A conflicting duplicate fails closed; it is not silently deduplicated.

## 5. Deterministic episode-level split

### 5.1 Split object

The semantic split is:

```text
TrainingDatasetSplitV1 {
    schema_id: "ocgforge.phase6.dataset_split.v1"
    source_dataset_identity: dataset_semantic_id
    split_contract_identity: "ocgforge.phase6.dataset_split.v1"
    split_seed_or_partition_identity:
        "ocgforge.phase6.split.fixed_80_10_10_sha256.v1"
    train_episode_ids: sorted unique episode_semantic_id values
    validation_episode_ids: sorted unique episode_semantic_id values
    test_episode_ids: sorted unique episode_semantic_id values
    split_identity: immutable content identity
}
```

The initial partition ratio is a deterministic 80/10/10 hash partition. It is
not a promise that every small corpus will have those exact sample counts.
The canonical partition input for one episode is the length-prefixed UTF-8
encoding of these strings in order:

```text
ocgforge.phase6.split.fixed_80_10_10_sha256.v1
episode_semantic_id
```

Let `h` be SHA-256 of those bytes, interpreted as an unsigned big-endian
`u64` from its first eight bytes, and let `bucket = h mod 1000`:

```text
000..799 → train
800..899 → validation
900..999 → test
```

This rule is the frozen `split_seed_or_partition_identity`; it does not use a
random library, framework RNG, process order, filesystem traversal, or host
identity. A change to the ratio, hash input, bucket boundaries, identity
domain, or grouping key requires a new split identity/version.

### 5.2 Split identity and auditability

`split_identity` has this lexical form:

```text
phase6_dataset_split.v1.<lowercase hexadecimal SHA-256>
```

Its canonical logical input is:

```text
identity domain:string = ocgforge.phase6.dataset_split_identity.v1
identity schema:string = ocgforge.phase6.dataset_split_identity.v1
source_dataset_identity:string
split_contract_identity:string
split_seed_or_partition_identity:string
train_episode_ids: vector<string> in unsigned UTF-8 order
validation_episode_ids: vector<string> in unsigned UTF-8 order
test_episode_ids: vector<string> in unsigned UTF-8 order
```

The three vectors MUST be disjoint, exhaustive over the source manifest's
unique episode identities, and ordered canonically. An implementation MAY
persist only the deterministic rule plus per-partition counts and sorted-ID
digests when the raw arrays are large, but an independent auditor MUST be
able to regenerate the arrays and recompute the same `split_identity`.

The split boundary is established before individual decisions are assigned:

```text
episode_semantic_id → one partition
all records/samples from that episode → that same partition
```

This includes both participant perspectives, all decisions, continuation
decisions, all shards, all encoded representations, and all physical cache
rows. The implementation MUST NOT hash or randomly assign individual
`DecisionRecord`, `ModelSupervisionSampleV1`, or physical row identities.

If multiple admitted trusted records describe the same duel/episode, they are
grouped by their exact `episode_semantic_id` before partitioning. Adding or
removing source manifest members changes `source_dataset_identity` and hence
the split identity; no old split may be silently reused for a new dataset.

## 6. Derived materialization rules

Every materializer MUST preserve the Phase-5 exact-domain invariants:

```text
N source candidates
→ N logical candidate records
→ N encoded candidate rows
→ N real loss rows
```

It MUST NOT silently:

- drop candidates or decisions;
- turn redacted values into known identities;
- remove presence masks;
- reorder candidates or state collections;
- silently reorder semantic samples without an explicit derived-order contract;
- change selected keys or labels;
- deduplicate semantically distinct rows;
- treat padding as a semantic candidate;
- fit a physical width by truncating, top-K selection, or domain splitting.

If a physical layout cannot represent `N`, it fails closed. The existing
`N=24`, `N=25`, and `N=129` witnesses remain required future capacity checks.
The sample's split is inherited from its source `episode_semantic_id`; batch
composition, padding, and re-sharding cannot change it.

Normalization, tokenization, encoding, masks, bucketing, recurrent windows,
or any cache projection require an explicitly versioned derivation contract.
No derived contract may alter environment, episode, public-frame, candidate,
selected-action, closure, public-gameplay, trusted-record, dataset-semantic,
or sample identity.

## 7. Privacy and independent audit

Dataset rows may contain only the accepted perspective-safe public model input
and exact public candidate descriptors. They MUST NOT contain opponent hidden
hand/deck identity, face-down Extra Deck identity, private locators, raw engine
values, internal semantic keys, response bytes, or hidden-derived diagnostics.

The model-input inspector and dataset auditor MUST consume public/admitted
values only. They MUST be able to show self/opponent perspective, phase/turn
context, zones, visible/redacted identities, public references, candidate
kinds and optional fields, exact candidate order/count, and the selected label
without querying `CoreHost` or private `PlayerObservation` state.

The paired-hidden-world gate requires that different hidden worlds with equal
accepted public observation and equal complete public domain produce equal
`LogicalModelInputV1`, `EncodedModelInputV1`, and model-input identity. No
hidden passcode or private semantic key may enter a dataset identity or
diagnostic.

## 8. Explicit non-goals

This contract does not authorize:

```text
dataset generation or Task 2 implementation
arbitrary JSON/NumPy/Arrow/Parquet membership
RandomLegal or human demonstrations as Teacher labels
multi-deck or arbitrary-deck training
RL, self-play, search, replay-buffer priorities, or rewards
framework-specific loaders or ML dependencies
checkpoint generation or training
hidden-state inference or positive-lethal label expansion
```

## 9. Related authority

- [BC contract](P6_BC_CONTRACT.md)
- [Dataset identity v1](../contracts/dataset-identity-v1.md)
- [Dataset manifest v1](../contracts/dataset-manifest-v1.md)
- [Admission receipt v1](../contracts/admission-receipt-v1.md)
- [Trusted trajectory v1](../contracts/trusted-trajectory-v1.md)
- [Phase-5 model contract](../p5/P5_MODEL_CONTRACT.md)
- [Phase-6 implementation plan](P6_IMPLEMENTATION_PLAN.md)
