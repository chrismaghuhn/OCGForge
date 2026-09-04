# Phase 6 Task7 non-smoke input materialization contract v1

## Status and scope

**Status:** **PROPOSED / pending independent review**.

This is the P6-T6R1 documentation-only contract freeze for the first Task7
production gap identified by the accepted Task6 readiness audit. It defines a
reviewable, lossless physical materialization boundary for a future PyTorch
candidate scorer. It does not implement that boundary.

This document does not authorize production code, a neural model, an optimizer
step, Behavior Cloning, trajectory or dataset generation, checkpoint creation,
Task5C meaningful evaluation, Meta-8, recurrent models, RL, self-play, or JAX
work. It does not resolve the separate Task5C meaningful-profile gap.

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

If a future implementation cannot preserve a rule in this document, it MUST
reject the complete input. It MUST NOT remove rows, round values, rebuild a
candidate domain, select a fallback action, or downgrade a non-smoke request to
the Task4 smoke path.

## 1. Live audited baseline

This proposal was audited at the following immutable source baseline:

```text
AUDIT_DATE=2026-09-04
AUTHORIZED_BASE=d10bbba299982bfcb30e44e451a1f170c6daa746
PHASE6_PRIMARY_BACKEND_DIRECTION=PYTORCH
JAX_STATUS=DEFERRED_CANDIDATE
JAX_REJECTED=NO
TASK6_FINAL_PASS=YES
TASK7_READINESS=BLOCKED
TASK7_AUTHORIZED=NO
TASK7_STARTED=NO
```

The audit examined the current Phase-5 contract and evidence, ADR-0007,
ADR-0008, the Task6 readiness record, the Phase-6 checkpoint and Task4A
numeric contracts, the `ygo::model` logical/encoded/batch/vocabulary headers
and sources, the Task4 projection and PyTorch model sources, and their
relevant model and Task4A tests. No newer Task7 materialization contract was
present at this baseline.

The following existing regressions were executed against this base. They are
understanding/regression evidence only; they are not Task7 acceptance evidence.

| Command | Result | Observed scope |
| --- | --- | --- |
| `ctest --test-dir build/dev-windows --output-on-failure -j 1 -R "^(logical_model_input_test\|card_vocabulary_test\|encoded_model_input_test\|model_batch_layout_test\|model_supervision_sample_test)$"` | `PASS` | 5/5 tests |
| `python -m unittest -v tests.phase6.phase6_task4a_codec_test tests.phase6.phase6_task4a_model_test tests.phase6.phase6_task4a_inference_test tests.phase6.phase6_task4a_cuda_preflight_test` | `PASS` | 24 tests |

The local audited runtime exposes `torch.int64` and `torch.bool` under the
accepted Task6 PyTorch version. That observation establishes no framework
semantic authority and does not itself authorize a PyTorch implementation.

## 2. Owning layer and authority boundary

The authoritative semantic source remains `ygo::model`:

```text
PublicEnvironmentObservation
+ complete ordered EnvironmentActionCandidate[N]
        ↓
LogicalModelInputV1
        ↓
EncodedModelInputV1
        ↓
ModelBatchLayoutV1
        ↓
proposed Task7 exact physical materialization
        ↓
future PyTorch tensors
        ↓
future candidate scorer
```

The proposed materializer is downstream of accepted Phase-5 surfaces. It is
not a gameplay, legality, observation, candidate-domain, dataset, replay, or
checkpoint authority. `ygo::model` remains the source of semantic values and
the Environment remains the sole authority for legal candidates, action-key
resolution, continuation lifecycle, response construction, and engine
advancement.

A future materializer MUST receive an already validated association of:

```text
LogicalModelInputV1
+ EncodedModelInputV1
+ exact model_input_identity
+ RaggedModelBatchV1 built from those encoded samples
```

For supervised data, that association additionally comes from the accepted
`ModelSupervisionSampleV1`/admission path. The materializer validates that each
ragged sample reconstructs to its associated encoded sample and that the
associated Phase-5 model-input identity validates over the associated logical
and encoded inputs. A ragged layout alone is a lossless execution view; it is
not sufficient source authority for a Phase-5 semantic identity.

The materializer MUST NOT receive or query:

```text
CoreHost
PlayerObservation
raw engine queries or debug state
ActionCandidate.semantic_key
private locators or persistent engine identity
response bytes or response hashes
SubmissionToken
internal decision or continuation identifiers
hidden passcodes, hidden deck order, beliefs, or inferred archetypes
```

## 3. Existing Phase-5 authority preserved

This proposal does not redefine, alias, or upgrade any of these accepted
contracts:

```text
ocgforge.model_logical_input.v1
ocgforge.model_encoded_input.v1
ocgforge.model_card_vocabulary.v1
ocgforge.model_input_identity.v1
ocgforge.model_batch_layout.v1
```

In particular, Phase 5 already owns exact public values, optional presence,
integer widths, source order, redacted/unknown semantics, complete candidate
cardinality, routing sidecars, immutable vocabulary identity, and lossless
ragged/padded layout. The new bridge materializes those values; it does not
create a parallel semantic representation.

The following remain true without reinterpretation:

```text
CardVocabulary ID 0 = PAD only
CardVocabulary ID 1 = PUBLIC_UNKNOWN_OR_REDACTED
CardVocabulary ID >= 2 = known already-public passcode mapping

N public candidates
→ N logical candidates
→ N encoded candidates
→ N materialized candidate records
→ N future score slots
```

## 4. Relationship to the Task4A smoke projection

`ocgforge.phase6.task4.numeric_projection.v1` remains an immutable historical
Task4A smoke identity. Its `state_rows[*,8]`, `candidate_rows[*,28]`,
binary32 normalization, and provisional architecture are not a Task7 input
contract.

The audited Task4 projection normalizes each `u32` into binary32. For example,
its effective binary32 arithmetic produces the same learned scalar for these
two distinct valid `source_index` values:

```text
u32 0xFFFFFFFE → Task4 normalized value 1.0f
u32 0xFFFFFFFF → Task4 normalized value 1.0f
```

Both values are valid `u32` public-action descriptor values. Their routing
sidecars remain different, but the Task4 learned candidate row collapses them.
This is precisely the permitted Task4 smoke limitation, not a defect to repair
inside the Task4 contract. Under the selected materialization below they are:

```text
0xFFFFFFFE → [65535, 65534]
0xFFFFFFFF → [65535, 65535]
```

The selected Task7 contract has a new identity and does not modify Task4A
source, tests, artifacts, provenance, architecture, or checkpoint meaning.

```text
TASK4_NUMERIC_PROJECTION_CHANGED=NO
TASK4_ARCHITECTURE_IDENTITY_REUSED=NO
```

## 5. Options evaluated

| Option | Losslessness | Determinism and transparency | PyTorch execution fit | Cost and future reuse | Decision |
| --- | --- | --- | --- | --- | --- |
| A. Fixed-width limbs | Exact when limb order and reconstruction are frozen | Clear positional reconstruction; easy known-answer tests | Values can use signed `int64` cells without needing unsigned wide tensor operators | Four cells for `u64`; compact compared with bit planes; reusable by a later stateless or recurrent architecture | Required component |
| B. Bit decomposition | Exact | Very explicit, but wide and less transparent for ordinary scalar fields | Boolean/integer storage is possible | Up to 64 features per scalar; larger masks and more expensive table surfaces without a semantic benefit over limbs | Rejected for V1 |
| C. Raw typed integer tensors plus learned embeddings/projections | Raw storage can be exact, but a bare proposal leaves unsigned `u64`, signed-bit, nullable, and canonical conversion rules unresolved | A learned transform is architecture work, not a complete materialization specification | `int64` is available, but not every model operation should be assumed for every integer dtype/device combination | Could become part of a later architecture, but does not by itself prove a portable lossless source codec | Not sufficient alone |
| D. Hybrid typed tables plus exact limbs | Exact for every represented Phase-5 primitive | Field type, presence, row mask, table order, and limb reconstruction are explicit | Canonical source values use `torch.int64` limbs and `torch.bool` masks; a later scorer chooses its own embeddings/projections | More tables than Task4, but no type-flattening and reusable by later approved architectures | **Selected** |

V1 selects Option D: structured typed table families with fixed base-2^16 limb
decomposition. This is not chosen because it is the smallest code change. It
is selected because it exposes every semantic primitive separately, preserves
the accepted order, avoids direct wide-integer `float32` casts, and permits a
future architecture to decide how to embed or project the exact fields.

## 6. New identity and configuration

The versioned materialization contract identity is:

```text
ocgforge.phase6.task7.input_materialization.v1
```

The contract also requires a separate configuration identity:

```text
schema:  ocgforge.phase6.task7.input_materialization_config.v1
prefix:  phase6_task7_input_materialization_config.v1.
value:   lowercase SHA-256 of canonical configuration bytes
```

The configuration identity binds, in this exact conceptual order:

1. this materialization schema and configuration schema;
2. the five preserved Phase-5 contract IDs listed in section 3;
3. the `u16_most_significant_first` limb rule in section 7;
4. the `torch.int64` limb and `torch.bool` mask physical type declarations;
5. every table identity and the table order in section 9;
6. every column name, source primitive, limb width, presence rule, row order,
   child-offset rule, and padding value in section 9;
7. the distinct `R`, `OR`, `CR`, and `HR` source-reference forms and their
   owner-specific uses in section 9;
8. the separate `globals.chain_length` and `chain_state.length` source fields;
9. candidate N-to-N/source-order and routing-sidecar rules;
10. raw-locator and routing-key control-sidecar exclusion rules; and
11. the ragged-first/padded-equivalence rules in section 11.

It MUST NOT bind GPU model, CUDA version, device index, PID, wall time,
filesystem path, worker scheduling, batch composition, batch capacity, bucket
width, physical padding width, allocation order, or process topology.

There is deliberately no new dataset-membership, replay, public-action,
candidate-domain, checkpoint, or source-sample identity. A future materialized
sample carries the accepted source identities named in section 13. Canonical
materialization bytes are reproducibility/integrity data; their digest, if an
implementation exposes one, MUST NOT become an independent membership or
semantic authority.

## 7. Exact primitive materialization and losslessness proof

### 7.1 Canonical physical primitive form

Every numeric source primitive is represented by most-significant-first
base-2^16 limbs. A limb is stored in one `torch.int64` cell whose permitted
value is `0..65535`. Boolean, presence, and row-mask values are stored in
`torch.bool` cells and are exactly `false` or `true`.

For an unsigned `w`-bit source integer, where `w` is 8, 16, 32, or 64 and
`k = ceil(w / 16)`, its limbs are:

```text
limb[i] = (value >> (16 * (k - 1 - i))) & 0xFFFF
for i = 0 .. k - 1
```

The reconstruction rule is:

```text
value = Σ(limb[i] << (16 * (k - 1 - i)))
```

The following physical column forms are frozen:

| Source primitive | Tensor cells | Reconstruction | Collision argument |
| --- | --- | --- | --- |
| `u8` | one `int64` limb in `0..255` | limb value | one-to-one range embedding |
| `u16` | one `int64` limb in `0..65535` | limb value | one-to-one range embedding |
| `u32` | two `int64` limbs | base-2^16 reconstruction | unique positional representation |
| `u64` | four `int64` limbs | base-2^16 reconstruction into `u64` | unique positional representation |
| `i32` | two limbs of the exact Phase-5 two's-complement `u32` bit pattern | reconstruct `u32`, reinterpret as two's-complement `i32` | bit pattern is preserved exactly, including `INT32_MIN` |
| `bool` | one `bool` cell | `false` or `true` | the source domain has exactly two values |
| `optional<T>` | `present:bool` plus the `T` limbs | `ABSENT` when false; otherwise decode limbs | presence distinguishes `ABSENT` from `PRESENT(0)` |
| fixed categorical code | source-width limbs; code remains in its schema column | exact source code | table/column identity prevents a code from changing semantic family |
| CardVocabulary ID | `u32` limbs | exact `u32` ID | `0`, `1`, and each `>=2` ID remain distinct |
| frame-local public locator ordinal | `u32` limbs | exact `u32` ordinal | exact equality value survives; it gains no persistent meaning |

All limb values are below `2^16`, and therefore also inside binary32's exact
integer range should a future, separately reviewed architecture convert an
individual limb cell to binary32. That optional architecture-local conversion
MUST NOT recompose a wide source value into one float, discard limbs, or
replace the canonical `int64`/mask materialization.

The complete canonical materialization packet is injective over every encoded
Phase-5 primitive it represents. Numeric values use the unique reconstruction
above; optionality and padding use independent masks; exact public locator
tokens and routing strings remain byte-exact control sidecars rather than being
discarded or hashed into learned features. Thus an excluded learned-string
feature is not a collapsed source value: it remains attached to, and validated
with, its exact source packet.

### 7.2 Optional and padding canonical values

An absent optional field has `present=false` and all of its value limbs equal
zero. A present zero has `present=true` and all value limbs equal zero. The
two forms are different and decode differently.

A physical padding row has `row_mask=false`, all presence cells false, and all
limb cells zero. A real row has `row_mask=true`, including a real row whose
known card identity is unknown/redacted. A real unknown/redacted entity has
CardVocabulary ID 1; it MUST NOT be represented as the ID-0 padding value.

### 7.3 Offset safety

Phase-5 ragged offsets are `u64` execution layout values. The materializer
validates every source offset before creating any tensor. A physical PyTorch
index/shape offset is emitted as `torch.int64` only when it is non-negative,
monotonic, consistent with the actual flat buffer, and at most `INT64_MAX`.
An offset outside that executable range fails closed before allocation; it is
never wrapped, clamped, or decomposed into a fake tensor shape. This cannot
drop a real row because a physical tensor with that many elements could not be
formed under the declared execution representation.

## 8. Source-validation rules

Before emitting any Task7 materialized table, a future implementation MUST:

1. require the exact Phase-5 schema IDs from section 3;
2. reconstruct each `EncodedModelInputV1` from the supplied
   `RaggedModelBatchV1` and compare its canonical encoded bytes to the
   associated encoded source;
3. verify the associated `LogicalModelInputV1`, `EncodedModelInputV1`, and
   `model_input_identity` together through the existing Phase-5 identity path;
4. verify the concrete immutable CardVocabulary identity for each sample;
5. verify every ragged offset, flat-buffer length, row count, candidate count,
   routing sidecar count, Phase-5 optional-presence invariant, and the
   independent `globals.chain_length` and `chain_state.length` source values;
6. require nonempty, valid, unique routing keys in exact source order;
7. validate all limb ranges, presence bits, row masks, and canonical-zero
   rules before exposing a tensor; and
8. reject the entire source batch on the first inconsistency without yielding a
   partial batch or a substitute value.

The materializer consumes Phase-5 encoded values directly. It MUST NOT use the
Task4 `Phase6BcStateInputV1`, Task4 candidate-only locator namespace, Task4
normalization rows, Task4 numeric model-input unit, or Task4 smoke corpus as a
source representation.

## 9. Structured state table families

### 9.1 Table notation and batch rules

Each named table is a family of named tensor columns, not one untyped dense
row matrix. `U8`, `U16`, `U32`, `U64`, and `I32` use the exact limbs specified
in section 7. `Bool` means a `torch.bool` column. `P<T>` means a separate
`<field>_present:Bool` column followed by a `<field>_value:T` limb column.
`R` is the exact Phase-5 `EncodedCurrentReference` form:

```text
R = public_locator_ordinal:U32,
    current_entity_ordinal:P<U32>
```

`OR` is an optional `EncodedCurrentReference`, used only where Phase 5 makes
the `R` value optional:

```text
OR = present:Bool, R
```

`CR` is an optional Phase-5 `EncodedCardReference`. It is the only reference
form that carries a card-reference `kind_code`:

```text
CR = present:Bool, kind_code:U8, R
```

`HR` is the historical public-reference form:

```text
HR = present:Bool, public_locator_ordinal:U32
```

Relationships use `R`; a chain source uses `OR`; chain targets use `R`; and
candidate source/target references use `CR`. A relationship or chain reference
MUST NOT receive an invented card-reference `kind_code`. A candidate `CR` MUST
retain the exact accepted `kind_code`.

For a flat table with `R` rows, a `T` column with `k` limbs is an
`torch.int64[R, k]` tensor and a `Bool` column is a `torch.bool[R]` tensor. Its
padded derivative is `torch.int64[B, W, k]` or `torch.bool[B, W]`, where `B`
is physical batch size and `W` is that table's physical width,
respectively. The declaration applies to every table below; an implementation
MUST NOT replace a typed column with a single generic `float32` row.

Each variable table has a source-order flat ragged form, a `row_mask` of true
for every flat real row, and a `u64`-validated `sample_offsets[B+1]` vector.
Child tables have an analogous parent-offset vector. A padded derivative has
the same named columns with shape `[B, W, ...]` and an explicit row mask; it
is governed by section 11.

The table sequence below is canonical. Rows are never sorted for tensor
convenience. An implementation MUST not infer a row relationship from a
physical address, allocation order, or a candidate row number.

### 9.2 Singleton-per-sample tables

`sample_header` has one real row per sample in batch order:

```text
perspective_player:U8
decision_index:U64
public_observation_context_kind_code:P<U16>
public_observation_context_player:P<U8>
public_locator_count:U32
candidate_count:U32
```

`globals` has one real row per sample in the same order:

```text
duel_flags:U64
player_to_act:P<U8>
turn_player:P<U8>
turn_count:P<U32>
phase:P<U32>
chain_length:U32
winner:P<U8>
win_reason:P<U8>
terminal:Bool
```

`chain_state` has one real row per sample in the same order:

```text
length:U32
```

`globals.chain_length` and `chain_state.length` are distinct accepted Phase-5
source fields. The materializer MUST preserve each in its named table and MUST
NOT infer, replace, or merge either value from the other.

`match_context` has one real row per sample in the same order:

```text
perspective_player:U8
duel_flags:U64
own_decklist_known:Bool
opponent_decklist_known:Bool
own_deck_known:Bool
opponent_deck_known:Bool
```

All four singleton tables have a sample row mask of all true. Their source
order is batch sample order, not a learned identity.

### 9.3 State collection tables

| Table identity | Row order | Exact columns |
| --- | --- | --- |
| `life_points` | Phase-5 life-point vector order within each sample | `value:U32` |
| `decision_context_references` | public-observation context reference order | `public_locator_ordinal:U32` |
| `zones` | accepted `public_safe_state.v1` decoder order | `player:U8`, `kind_code:U8`, `total_count:U32`, `public_identity_count:U32`, `hidden_count:U32`, `player_observable_order:Bool` |
| `entities` | current decoded canonical locator order | `public_locator_ordinal:U32`, `identity_known:Bool`, `card_vocabulary_id:U32`, `owner:P<U8>`, `controller:P<U8>`, `zone_code:U8`, `sequence:P<U32>`, `overlay_sequence:P<U32>`, `position_code:U8`, `face_up:Bool`, `face_down:Bool` |
| `entity_properties` | exactly two rows per entity: `printed`, then `current`; no row is omitted | `property_role:U8` (`printed=1`, `current=2`), `property_present:Bool`, `type:P<U32>`, `attribute:P<U32>`, `race:P<U64>`, `attack:P<I32>`, `defense:P<I32>`, `base_attack:P<I32>`, `base_defense:P<I32>`, `level:P<U32>`, `rank:P<U32>`, `link_rating:P<U32>`, `left_scale:P<U32>`, `right_scale:P<U32>`, `status_flags:P<U32>` |
| `property_link_markers` | per-property accepted link-marker order | `link_marker_code:U8` |
| `property_counters` | per-property accepted counter-pair order | `type:U32`, `count:U32` |
| `relationships` | accepted canonical relationship order | `kind_code:U8`, `source:R`, `target:R` |
| `chain_links` | authoritative Phase-5 chain-link order | `index:U32`, `activating_player:P<U8>`, `source:OR`, `activation_zone_code:P<U8>`, `effect_description:P<U64>` |
| `chain_targets` | authoritative target order inside each chain link | `target:R` |
| `visible_events` | canonical visible-event-index order | `event_index:U64`, `kind_code:U8`, `player:P<U8>`, `entity:HR`, `public_card_vocabulary_id:P<U32>`, `from_zone_code:P<U8>`, `to_zone_code:P<U8>`, `count:P<U32>`, `amount:P<I32>`, `counter_type:P<U32>`, `phase:P<U32>`, `winner:P<U8>`, `win_reason:P<U8>`, `effect_description:P<U64>` |
| `visible_event_targets` | authoritative target order inside each visible event | `public_locator_ordinal:U32` |
| `own_main_deck_ids` | public-safe own-main-deck order | `card_vocabulary_id:U32` |
| `opponent_main_deck_ids` | public-safe opponent-main-deck order | `card_vocabulary_id:U32` |
| `own_extra_deck_ids` | public-safe own-extra-deck order | `card_vocabulary_id:U32` |
| `opponent_extra_deck_ids` | public-safe opponent-extra-deck order | `card_vocabulary_id:U32` |

`entity_properties` has an `entity_property_offsets` vector with exactly two
rows per parent entity. `property_link_markers` and `property_counters` use
`property_link_marker_offsets` and `property_counter_offsets`, respectively.
`chain_targets` uses `chain_target_offsets`; `visible_event_targets` uses
`visible_event_target_offsets`. These offsets are part of the physical
structure, are validated exactly, and do not become persistent card identity.

For entities, IDs have these mandatory semantics:

```text
row_mask=true and card_vocabulary_id=1  → real PUBLIC_UNKNOWN_OR_REDACTED entity
row_mask=false and card_vocabulary_id=0 → physical padding only
card_vocabulary_id>=2                  → known already-public passcode mapping
```

The `entities` row coordinate is the already accepted frame-local
`current_entity_ordinal`. It is available only through that table order and
the Phase-5-proven reference fields; it is not an added persistent or learned
card identity.

No property, counter, deck vector, relationship, chain link, event, or entity
collection has a global cap in this proposal.

### 9.4 Locator control sidecar

The accepted Phase-5 public locator token table is retained exactly in a
source-verification control sidecar in its unsigned UTF-8 source order, with
the corresponding sample offsets. It is not a learned text feature, a
tokenizer input, a string hash, or a persistent identity.

The learner-facing materialization represents the accepted meaning of a
locator only through `public_locator_ordinal` and, where Phase 5 already
proved it, `current_entity_ordinal`. A raw locator string is never converted
to a learned numeric feature. The sidecar permits exact source validation and
canonical-materialization comparison without granting the learner a new
identity surface.

In particular:

```text
public_locator_ordinal = frame-local equality feature only
current_entity_ordinal = present only on Phase-5-proven exact current resolution
historical visible-event references = historical only; never rebound to current entities
```

## 10. Candidate table and routing boundary

`candidates` is a ragged table with one real row for each `EncodedCandidate`
in exact source-vector order. Its columns are:

```text
action_kind_code:U16
choice_present:Bool
choice_kind_code:U8
choice_value:U64
choice_response_index:P<U32>
source_reference:CR
target_reference:CR
phase:P<U32>
position:P<U8>
source_index:P<U32>
amount:P<I32>
continuation_operation_code:U8
submits_engine_response:Bool
```

The `choice_present` bit is separate from the optional response-index bit.
When the choice is absent, all choice value limbs are canonical zero. The
source/target reference presence bits are likewise distinct from their
current-entity-ordinal presence bits. These rules preserve every accepted
candidate distinction, including choice kind/value/response-index, source and
target references, phase, position, source index, amount, continuation
operation, and engine-response submission flag.

The materialization MUST preserve this invariant for every valid source
sample:

```text
candidate_offsets[i + 1] - candidate_offsets[i] = N_i
candidate row count for sample i                   = N_i
future score slot count for sample i               = N_i
routing sidecar count for sample i                 = N_i
```

It MUST NOT filter, sort, deduplicate, truncate, fabricate, auto-resolve, or
split a candidate domain. A candidate ordinal is not a candidate-table
feature. The physical candidate row coordinate only records source position
for alignment and is not replay identity, action identity, or a learned
semantic feature.

### 10.1 `public_action_key` is control metadata, not a learned feature

The exact `public_action_key` vector remains in a non-learner routing sidecar,
aligned one-to-one with the candidate table in source order. It participates
in accepted Phase-5 model-input identity and inference routing validation, but
the learner tensors MUST NOT contain:

```text
public_action_key bytes
routing-key hashes
internal semantic keys
response bytes
pointer/object identity
filesystem paths
PIDs
wall-clock values
```

A future scorer returns a score vector in candidate-table source order. The
existing routing sidecar resolves the selected score slot to the exact supplied
public key. The scorer neither receives nor reconstructs that key.

Changing only an otherwise-valid routing key while holding candidate feature
values controlled MUST leave learner feature tensors free of that text or a
hash of it. It still changes the accepted source identity/control sidecar, and
any attempt to attach a row tensor from one sidecar to another MUST fail
closed.

## 11. Ragged-first execution and padding semantics

V1 consumes `RaggedModelBatchV1` directly. This avoids treating a chosen
capacity as source data and retains the accepted Phase-5 flat ordering and
offsets. The source `PaddedModelBatchV1`, when present, is only an equivalent
execution view:

```text
unpad(PaddedModelBatchV1) == RaggedModelBatchV1
```

must hold exactly before the materializer may accept it as evidence of the
same source batch. A materializer MUST NOT treat padded values as an
alternative semantic source or use padding width as an input feature.

A future PyTorch adapter may derive padded table tensors from the Task7
ragged tables. For every variable table:

```text
W >= maximum real row count for that table in the physical batch
row_mask[b, j] = true   for real source row j in sample b
row_mask[b, j] = false  for physical padding row j in sample b
```

Real rows occupy `0..count-1` in exact source order. Padding rows have all
limbs zero, all optional-presence cells false, all child counts/offsets empty
as applicable, empty routing-sidecar entries when the table is candidates, and
`row_mask=false`. A capacity smaller than a real collection fails closed before
any model call. A materializer or scorer MUST NOT split one candidate domain
over physical batches.

Changing batch width, bucket, padding width, device, worker count, batch
composition, or storage layout MUST NOT change a sample's accepted Phase-5
model-input identity, its decoded Task7 real rows, candidate order, routing
sidecar, or eventual selected legal candidate. Padding is physical execution
metadata, not semantic input.

```text
PADDING_SEMANTIC=NO
```

## 12. Canonical materialization bytes and determinism

For cross-process verification, V1 defines canonical materialization bytes for
one unpadded sample. The bytes use Phase-5 canonical primitives:

```text
string        = u32be byte length || UTF-8 bytes
bool          = u8 0 or 1
limb          = u16be
offset/count  = u64be
vector        = u32be count || entries in declared order
optional      = presence:u8 || limb values when present
```

The canonical sample order is:

1. materialization schema and configuration identity;
2. source `model_input_identity`, Phase-5 contract IDs, concrete
   CardVocabulary identity, public-observation digest, and optional
   public-candidate-domain digest;
3. singleton tables in the order `sample_header`, `globals`, `chain_state`,
   `match_context`;
4. all ragged state tables in the exact order in section 9.3, including their
   offsets and real-row masks;
5. exact public locator token control sidecar;
6. candidate table, candidate offsets, and candidate real-row mask; and
7. exact routing-key control sidecar.

Each table serializes its identity, row count, parent offsets where applicable,
column names in the order listed in this document, column values in source row
order, and its row mask. Tensor byte order, tensor stride, device, allocation,
framework version, GPU, and padding capacity are not canonical-materialization
inputs. Batch-global `sample_offsets` select a sample from a physical batch and
are not part of that sample's canonical bytes; each included variable-table
offset is rebased to zero within the sample. A padded derivative is compared
only after unpadding to this canonical form.

Canonical reference bytes preserve the same type separation: `R` serializes
only `public_locator_ordinal` and optional `current_entity_ordinal`; `OR` adds
only its outer presence bit; `CR` adds `kind_code`; and `HR` has no current
entity ordinal. No canonical materialization bytes invent a `kind_code` for an
`R` or `OR` value.

Canonical singleton bytes serialize `globals.chain_length` and
`chain_state.length` under their separate table and column names. They are not
aliased, replaced, or omitted even when their source values happen to be equal.

The materialization is deterministic only when identical accepted Phase-5
inputs and the same configuration identity yield byte-identical canonical
materialization bytes. Unordered iteration, pointer values, allocation order,
PID, wall time, filesystem traversal, thread scheduling, and device scheduling
MUST NOT affect them.

## 13. Replay and provenance implications

The Task7 materialization is derived execution data. It carries or references,
for each sample:

```text
source model_input_identity
concrete CardVocabulary identity
public_observation_digest
public_candidate_domain_digest when present
ordered routing sidecar in exact source order
Task7 materialization contract ID
Task7 materialization configuration identity
ModelSupervisionSampleV1 / admitted source-sample identity when training labels are in scope
```

It does not replace or redefine:

```text
trajectory identity
BC sample identity
DatasetManifest
split identity
model_input_identity
public candidate-domain identity
checkpoint identity
```

Any future cache is rebuildable derived data. It must be validated against the
exact admitted source and source `model_input_identity`; successfully loading a
tensor cache never grants dataset membership. Materialization must not mutate
the trusted trajectory, encoded input, routing sidecar, source labels, or
accepted Phase-5 canonical bytes.

## 14. Privacy implications

The materializer may consume only the accepted Phase-5 values described above.
It has no permission to augment them from the engine, a catalog, a private
observation, or a learned belief. In particular, it must not turn a redacted
entity into a known passcode, infer a deck list, preserve a physical card across
a knowledge-destroying transition, or turn a historical event locator into a
current entity reference.

For paired hidden worlds that have the same public observation and exact public
candidate domain, all of the following MUST be equal before model execution:

```text
LogicalModelInputV1
EncodedModelInputV1
model_input_identity
Task7 canonical materialization bytes
Task7 learner-facing tensor values and masks
routing/control sidecars
```

The exact public locator token sidecar is retained only to verify its already
public source input and is never a learned string, hash, persistent identity,
or hidden-information recovery channel.

## 15. PyTorch and architecture boundaries

The authority split is:

```text
semantic/source authority:        ygo::model Phase-5 values
materialization authority:        this reviewed Task7 contract
physical execution:               PyTorch tensors/modules
```

PyTorch is not a semantic authority. Framework version, CUDA version, GPU
name, device index, process topology, worker scheduling, and tensor storage
details remain execution provenance unless a separately accepted contract says
otherwise.

The Task4 architecture cannot be reused:

```text
TASK4_ARCHITECTURE_IDENTITY_REUSED=NO
```

Its fixed `[*,8]`/`[*,28]` float rows are not this structured exact input
surface. A future Task7 architecture configuration MUST declare a new identity
that references this materialization configuration identity. This proposal does
not freeze hidden widths, layers, pooling, embeddings, optimizer, learning
rate, steps, batch size, loss, checkpoint-selection rule, or training budget.

## 16. Failure semantics

The complete materialization fails closed on any of the following:

```text
unknown or incompatible Phase-5 or Task7 schema/configuration identity
logical/encoded/model-input-identity mismatch
ragged reconstruction or canonical encoded-byte mismatch
invalid, nonmonotonic, overflowing, or unexecutable offsets
candidate, routing, row-mask, presence-mask, or child-offset mismatch
loss of candidate `CR.kind_code` or an invented `kind_code` on `R` or `OR`
loss, aliasing, or source mismatch of `globals.chain_length` or `chain_state.length`
limb outside 0..65535
invalid boolean/presence value
nonzero value limbs for an absent optional field
nonzero values or presence bits in a padded row
CardVocabulary PAD ID on a real row
unknown/redacted entity represented as a known ID
physical capacity below a real collection length
candidate count differing at any materialization boundary
candidate reorder, drop, deduplication, fabrication, or domain split
invalid or detached routing sidecar
attempted raw locator/routing-key feature injection
any forbidden private, internal, or hidden source value
```

The diagnostic identifies the failed public/contract boundary without exposing
hidden identity, response bytes, private locators, or an internal semantic key.
There is no truncation, default value, automatic action, fallback vocabulary,
fallback policy, retry, or partial-batch result.

## 17. Required future implementation acceptance matrix

No row below is evidence that an implementation exists. Every row is a future
implementation acceptance requirement.

| Area | Required future proof |
| --- | --- |
| Primitive exactness | Round-trip `u8` and `u16` boundaries; `u32` values `0`, `1`, `2^24-1`, `2^24`, and `2^32-1`; `u64` values `0`, `1`, values above `2^24`, both `2^32` boundaries, and `2^64-1`; `i32` minimum, `-1`, `0`, `1`, and maximum. Compare decoded source integers, not approximate floats. |
| Optional exactness | Prove `ABSENT` differs from `PRESENT(0)` for every optional primitive family, including optional references and optional `i32`. Verify absent limbs are canonical zero without treating that as a present zero. |
| Task4 collision regression | Construct valid otherwise-equivalent public candidate descriptors with `source_index=0xFFFFFFFE` and `source_index=0xFFFFFFFF`. Prove the old Task4 learned float row collides while the new candidate `source_index` limbs differ and round-trip. Do not modify Task4 to make this pass. |
| Full state coverage | Cover sample/header fields, globals including `globals.chain_length`, separate `chain_state.length`, life points, zones, entities, printed/current properties, link markers, counters, relationships, chain links and targets, visible events and historical targets, match context, all four public deck passcode-ID vectors, context references, and locator ordinals. |
| Reference-type fidelity | Prove Candidate `EncodedCardReference.kind_code` is retained exactly. Prove Relationship `EncodedCurrentReference`, optional Chain source, and Chain targets never receive an invented `kind_code`; verify their `R`/`OR` round-trips separately from Candidate `CR`. |
| Chain-state fidelity | Prove `globals.chain_length` and `chain_state.length` each materialize and round-trip through their separate named tables. The test must compare each result to its own Phase-5 source field and must not derive one from the other. |
| Candidate completeness | For arbitrary `N`, including `24`, `25`, and `129`, prove public count = logical count = encoded count = materialized real rows = score slots = routing keys. `W<N` must fail closed. |
| Ordering | Use distinct and duplicate-looking public descriptors with distinct valid keys. Prove exact source order is retained in candidate rows, score slots, and routing sidecars; no sort or deduplication occurs. |
| Ragged/padded equivalence | Materialize ragged input, pad at `W=max` and `W>max`, unpad, and compare every real table row, field, presence bit, routing key, offset, and canonical sample byte. Change batch composition and capacity without changing per-sample semantic values or selection. |
| Unknown/PAD separation | Prove a real redacted entity has `row_mask=true` and CardVocabulary ID 1. Prove physical padding has `row_mask=false` and ID 0. Ensure optional absence never changes a real row into padding. |
| Routing-key isolation | Change an otherwise-valid routing string while controlled learner feature values remain equal. Prove no learner tensor contains its bytes or hash, while the source identity/control-sidecar mismatch prevents detached row/key use. |
| Locator boundary | Prove equal raw locator tokens receive equal frame-local ordinals, historical references never gain current-entity ordinals, and a knowledge-destroying transition never yields persistent identity. |
| Privacy paired worlds | Use the established paired hidden worlds. Require equal Phase-5 inputs, canonical Task7 bytes, learner tensors, masks, source-order routing sidecars, score vector cardinality, and selected public key. |
| Deterministic repeat | In fresh processes, materialize identical accepted sources and compare the configuration identity, canonical materialization bytes, table values, masks, offsets, and source bindings byte-for-byte. |
| Failure behavior | Exercise malformed schemas, detached logical/encoded inputs, invalid vocabulary IDs, malformed limbs, absent/presence ambiguity, offset/count overflow, malformed padding, insufficient capacity, candidate/key mismatch, illegal reordering, and forbidden-source injection. Each must reject without row dropping or fallback. |

## 18. Explicit non-goals

This proposal does not define or authorize:

```text
Task7 tensorizer implementation
PyTorch model implementation
new ArchitectureConfig implementation
optimizer, loss, training schedule, batch size, or stopping rule
meaningful BC training or trajectory/dataset generation
new checkpoint, corpus, dataset, or evaluation job
Task5C meaningful fixed-matchup profile/job implementation
Task4 contract migration or smoke-artifact rewrite
JAX implementation or backend bake-off
recurrent memory, value heads, RL, self-play, Meta-8, search, or world models
new gameplay, legality, observation, candidate, replay, or dataset authority
```

## 19. Remaining Task7 blockers

Independent review must accept this proposal before an implementation slice can
begin. Even then, a separately authorized and independently accepted
implementation is required before this first gap is resolved. The second gap
remains intentionally separate: the current Task5C evaluator has no meaningful
fixed-matchup context/job path for a Task7 checkpoint.

```text
TASK7_INPUT_MATERIALIZATION_CONTRACT=PROPOSED
TASK7_INPUT_MATERIALIZATION_IMPLEMENTED=NO

TASK5C_MEANINGFUL_PROFILE_IMPLEMENTED=NO

TASK7_READINESS=BLOCKED
TASK7_AUTHORIZED=NO
TASK7_STARTED=NO
```
