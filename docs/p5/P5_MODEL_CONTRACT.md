# OCGForge Phase 5 model-facing contract v1

## Status and authority

**Status:** Accepted as the Phase 5 Task 1 model-facing contract freeze.

This document defines the normative Phase 5 representation boundary. It does
not implement a model adapter, scorer, learner, training loop, or physical
tensor backend. A future implementation MUST satisfy this document and the
acceptance gates in P5_ACCEPTANCE_PLAN.md.

The owning namespace and layer are:

~~~text
ygo::model
~~~

The authoritative source remains the existing public V2 decision boundary:

~~~text
PublicEnvironmentObservation
+ complete ordered EnvironmentActionCandidate[]
~~~

The public request kind and acting player are used only when they are already
present in the observation's public decision context, or as equality checks
against that context when the surrounding public frame provides them. They are
not a second authority. EnvironmentContinuationView and any other request-only
state are not independent Phase 5 model inputs and may not be reconstructed
from private or internal values.

Normative terms such as MUST, MUST NOT, SHOULD, and MAY have their usual
contract meaning. A representation that cannot satisfy a MUST fails closed for
the complete input; it is not repaired by reducing the legal domain.

## 1. Contract identifiers

Phase 5 owns these versioned downstream domains:

| Surface | Contract identifier | Meaning |
| --- | --- | --- |
| logical model input | ocgforge.model_logical_input.v1 | structured public model representation |
| deterministic encoded input | ocgforge.model_encoded_input.v1 | integer/categorical representation plus routing sidecar |
| card vocabulary | ocgforge.model_card_vocabulary.v1 | immutable public-passcode to dense-ID mapping |
| model-input identity | ocgforge.model_input_identity.v1 | digest over logical and encoded semantic input |
| physical batch layout | ocgforge.model_batch_layout.v1 | lossless ragged/padded execution view |
| derived supervision sample | ocgforge.model_supervision_sample.v1 | trajectory-to-label materialization |

These identifiers are independent of the existing contracts that remain
authoritative for their own meanings:

~~~text
ocgforge.public_environment_observation.v1
ocgforge.public_safe_state.v1
ocgforge.episodic_environment.v2
ocgforge.public_action_identity.v1
ocgforge.public_candidate_domain.v1
ocgforge.trusted_trajectory.v1
~~~

An incompatible change to a Phase 5 semantic field, code table, vocabulary
mapping, or identity input requires a new version. A physical layout change
may version the layout independently and does not by itself require a new
model-input identity.

## 2. Boundary and ownership

### 2.1 Allowed source values

The model layer MAY consume only values already emitted by the public
environment:

| Allowed value | Use |
| --- | --- |
| PublicEnvironmentObservation | public perspective, decision index, safe-state bytes, and safe decision context |
| public observation context | already-public request kind/player and safe referenced locators; request metadata from elsewhere is an integrity check only |
| complete ordered EnvironmentActionCandidate[] | every current legal public candidate, including its public descriptors and key |
| explicit immutable CardVocabularyV1 | deterministic encoding of already-public passcodes |
| accepted trusted_trajectory.v1 public frame/record | only for a later derived supervision sample |

The candidate vector is the complete domain for one current public request. A
caller-provided candidate count or digest is an integrity check only; the
adapter derives and validates it from the actual vector.

### 2.2 Forbidden source values

The model layer MUST NOT access, accept, derive, or persist any of the
following as model input or model identity material:

| Forbidden value | Reason |
| --- | --- |
| PlayerObservation | internal observation record; it contains internal decision metadata |
| CoreHost or raw engine queries | omniscient engine authority is outside the public policy boundary |
| ActionCandidate.semantic_key | internal identity may contain hidden card identity |
| exact engine response bytes or response hashes | internal control/audit material, not public model input |
| SubmissionToken | live freshness/control-plane value, not semantic input |
| internal decision/continuation IDs, raw message hashes, or engine-step identity | internal protocol/audit values |
| EnvironmentContinuationView or other request-only state | not part of the frozen P5 input boundary |
| private, physical, persistent, or hidden-card locators | may preserve identity across a knowledge-destroying transition |
| hidden card passcodes, hidden deck entries, inferred properties, beliefs, or archetypes | information leak or speculative reconstruction |
| candidate-vector position as replay/action identity | position is only a local derived coordinate |
| batch width, bucket, padding, device, dtype, or framework metadata | physical execution details, not semantic model input |

An ObservationLocator already present in the public V2 descriptor is a safe
current public reference only. It is not a physical identity and MUST NOT be
promoted into a persistent model identity. Private locators are never accepted.

### 2.3 No advancement authority

ygo::model produces value representations and selection-routing metadata. It
does not call step, OCG_DuelSetResponse, OCG_DuelProcess, or any Teacher or
engine advancement path. A later scorer may return one of the existing public
keys, but the public environment remains the only layer that resolves that key
and advances the engine.

## 3. Representation flow

The normative data flow is:

~~~text
PublicEnvironmentObservation
+ complete ordered EnvironmentActionCandidate[N]
        |
        | existing public-safe decoder and public V2 validation
        v
LogicalModelInputV1
        |
        | fixed category codes, immutable card vocabulary, presence masks
        v
EncodedModelInputV1
        |
        | lossless offsets, optional padding, row masks
        v
ModelBatchLayoutV1
~~~

The cardinality invariant applies at every semantic and physical boundary:

~~~text
N public candidates
    -> N logical candidate records
    -> N encoded candidate feature rows
    -> N routable model candidate slots
~~~

No stage may filter, sort, deduplicate, truncate, fabricate, default, or
auto-resolve candidates. Candidate order is the order received from the
public V2 request.

## 4. LogicalModelInputV1

### 4.1 Logical shape

The logical representation is framework-neutral and retains exact public
values. It is conceptually:

~~~text
LogicalModelInputV1 {
    schema_id: "ocgforge.model_logical_input.v1"
    public_observation_digest: lowercase SHA-256 string
    public_candidate_domain_digest: optional lowercase SHA-256 string
    perspective_player: u8
    decision_index: u64
    public_decision_context: LogicalPublicDecisionContextV1
    public_safe_state: LogicalPublicSafeStateV1
    candidate_routing: CandidateRoutingMetadata[N]
    candidate_features: LogicalCandidateV1[N]
}
~~~

candidate_routing[i].public_action_key is the exact key from the public
candidate at position i. It is routing metadata, not a learned string
feature. candidate_features[i] contains the public descriptor for that same
candidate. The two vectors MUST have equal length and equal source order.

public_observation_digest is recomputed from the accepted public observation
bytes. When the observation context contains a request kind, the
public_candidate_domain_digest is recomputed from that kind and the ordered
public-key vector under ocgforge.public_candidate_domain.v1. A mismatching
caller-supplied value fails closed. A request kind/player supplied by an
associated public frame is an equality check against the observation context;
it does not extend the model input boundary or add request state. If the
observation context does not contain a request kind, no candidate-domain
digest is fabricated or accepted as an independent input; the complete key
vector remains part of the model representation and identity.

### 4.2 Public-safe state projection

public_safe_state contains every field exposed by the existing
PublicSafeStateView, with optional presence and source ordering preserved:

| Source component | Logical treatment |
| --- | --- |
| globals | copy all typed scalar, optional, terminal, turn, phase, life-point, and chain-length values exactly |
| zones | copy every decoded zone record in the existing canonical decoder order |
| entities | copy every decoded public or redacted entity in canonical locator order; never remove redacted rows |
| relationships | copy every decoded edge in canonical relationship order |
| chain | copy length and every chain link in authoritative link order; preserve target order defined by the safe-state contract |
| visible_events | copy every decoded event in canonical event-index order |
| match_context | copy perspective, flags, knowledge bits, and only static deck passcodes actually present in the public-safe value |

There are no fixed entity, relationship, event, deck, or candidate limits in
the logical contract. Unknown values rejected by the public-safe decoder are
not assigned a model code. An unknown/redacted card remains an unknown/redacted
card.

The logical representation includes the frame-local public locator token table
defined in section 4.5. A logical reference retains the exact public locator
token. It does not claim that a token occurring in different state components
denotes one physical card or one persistent entity.

At the logical layer, a decoded VisibleEvent retains its already-public
public_passcode as the exact optional u32 value supplied by
PublicSafeStateView. The encoded layer maps that value through CardVocabularyV1;
this logical fact is not a current-entity reference and does not require one.

### 4.3 Candidate projection

For each public EnvironmentActionCandidate, the logical candidate retains all
public fields:

| Public field | Logical representation |
| --- | --- |
| action_kind | canonical lower-case public action token |
| public_action_key | exact routing metadata string; excluded from learned feature payload |
| choice | optional kind, exact public value, and optional exact response selector |
| source_reference | optional public reference kind plus current public locator |
| target_reference | optional public reference kind plus current public locator |
| phase | optional exact u32 |
| position | optional exact public position code |
| source_index | optional exact u32; public descriptor data, not a candidate-vector index |
| amount | optional exact signed i32 |
| continuation_operation | empty or the exact public token: pick, amount, finish, cancel, or bypass |
| submits_engine_response | exact public boolean |

An absent optional value is not replaced with zero, an unknown category, or a
default. Its presence remains explicit.

### 4.4 No independent continuation input

P5-v1 does not consume EnvironmentContinuationView, continuation_kind,
selected_indices, remaining_indices, assigned_amounts, or continuation
cardinality/mask fields as an independent model input. A continuation action
is represented only through the public EnvironmentActionCandidate fields
already present in the frozen input vector, including action_kind,
source_index, amount, continuation_operation, submits_engine_response, and
public_action_key. The complete ordered candidate vector remains the sole
representation of the current legal choice domain.

### 4.5 Public locator token table

The logical representation retains each already-public locator token only as a
current-frame public reference. It derives one frame-local
public_locator_ordinal table from the exact UTF-8 token values appearing in:

1. public observation decision-context references;
2. current public-safe entity locators;
3. relationship source and target locators;
4. chain source and target locators;
5. historical visible-event entity and target locators; and
6. public candidate source and target locators.

The unique non-empty tokens are sorted by unsigned lexicographic UTF-8 byte
order and assigned zero-based ordinals. Equal tokens receive the same ordinal;
the ordinal means only token equality. It never means that two occurrences
refer to the same card, physical object, or persistent entity.

Current candidate, chain, and relationship references MAY additionally carry a
current_entity_ordinal only when the public projection proves that the token
matches exactly one current PublicSafeStateView entity. If that proof is not
available, the current ordinal is absent and the public_locator_ordinal is
still retained. A source/target zone, sequence, or token shape is not proof.

Visible-event references are historical. They carry only their
public_locator_ordinal and MUST never receive a current_entity_ordinal, even
when the historical token happens to equal the locator of the entity currently
occupying that slot. This preserves event history without rebinding an old
locator to a new card.

## 5. Public-safe decoding rule

The model layer MUST use the existing public-safe decoder. The required path
is:

1. Validate/decode serialized public observation bytes with the existing
   decode_canonical_public_environment_observation when the source is bytes.
2. Obtain canonical safe-state bytes from the accepted public observation.
3. Call the existing decode_canonical_public_safe_state on those bytes.
4. Reject the complete model input if the decoder returns a diagnostic or no
   PublicSafeStateView.
5. Project only the returned PublicSafeStateView and public V2 candidate values
   into LogicalModelInputV1.

The model layer MUST NOT parse safe-state bytes itself, decode a
PlayerObservation, accept arbitrary caller-supplied state text, query the
engine, or reconstruct state from candidates. Re-encoding the decoded
PublicSafeStateView with the existing canonical safe-state encoder is allowed
only to verify canonical equality; it is not a second safe-state format.

The public observation's safe decision context is copied only from its public
kind, player, and safe current references. Internal decision IDs,
continuation IDs, engine-step metadata, and the internal observation hash do
not cross this boundary.

## 6. Deterministic integer and categorical encoding

### 6.1 Primitive rules

EncodedModelInputV1 uses no floating-point values. Its logical values are
encoded as follows:

| Logical value | Encoding |
| --- | --- |
| u8, u16, u32, u64 | exact unsigned integer of that width |
| signed i32 | exact two's-complement u32 bits |
| boolean | 0 or 1 |
| optional value | presence:u8 followed by the exact value when present |
| fixed categorical value | code from the fixed tables in this section |
| known public card | vocabulary ID from the immutable card vocabulary |
| unknown/redacted real card | card ID 1, with no passcode or identity-derived fields |
| physical padding row | card ID 0, row mask 0, and no routing key |

No float normalization, scaling, quantization, hashing of strings into
features, learned tokenizer, embedding lookup, or framework-specific dtype is
part of this contract.

### 6.2 Fixed public request and action codes

The following u16 model codes are frozen independently of source-language enum
layout. Code 0 is reserved for absent/invalid and is never a valid request or
action code.

Public request kind:

| Code | Token |
| ---: | --- |
| 1 | idle_command |
| 2 | battle_command |
| 3 | chain |
| 4 | option |
| 5 | card_selection |
| 6 | tribute |
| 7 | sum |
| 8 | place |
| 9 | counter |
| 10 | ordering |
| 11 | announcement |
| 12 | unselect_card |
| 13 | position |
| 14 | yes_no |

Public action kind:

| Code | Token |
| ---: | --- |
| 1 | idle_command |
| 2 | battle_command |
| 3 | chain |
| 4 | option |
| 5 | card_selection |
| 6 | announcement |
| 7 | place |
| 8 | position |
| 9 | yes_no |
| 10 | pick |
| 11 | finish |
| 12 | cancel |
| 13 | assign_amount |

unsupported is not a model category. An unsupported public request or action
fails closed.

### 6.3 Existing public enum codes

The following public values use the existing codes owned by
ocgforge.public_safe_state.v1 and ocgforge.public_action_identity.v1. Phase 5
does not renumber or reinterpret them:

| Value family | Code source |
| --- | --- |
| SemanticZone | fixed public_safe_state.v1 declaration codes |
| Position | fixed public_safe_state.v1 bit codes |
| LinkMarker | fixed public_safe_state.v1 declaration codes |
| RelationshipKind | fixed public_safe_state.v1 declaration codes |
| VisibleEventKind | fixed public_safe_state.v1 declaration codes |
| PublicChoiceKind | YesNo=1, EffectYesNo=2, EffectChoice=3, OptionValue=4, AnnouncementNumber=5 |
| PublicCardReferenceKind | VisibleCard=0, RedactedSlot=1 |

An unknown or invalid source code is a representation error. It is not mapped
to unknown unless the owning public contract explicitly represents the value
as unknown.

### 6.4 Candidate continuation-operation codes

The only continuation-related value encoded by P5-v1 is the public candidate
field continuation_operation. Its fixed codes are:

| Code | continuation_operation token |
| ---: | --- |
| 0 | empty/absent |
| 1 | pick |
| 2 | amount |
| 3 | finish |
| 4 | cancel |
| 5 | bypass |

An unknown non-empty token fails closed. The public spelling amount is
intentional and remains distinct from the action-kind token assign_amount.
EnvironmentContinuationView and its continuation_kind/selection-mask fields
have no P5-v1 model code.

### 6.5 Frame-local public references

The encoded representation contains the sorted frame-local public locator
token table defined in section 4.5. For every current public reference, the
encoded feature payload carries:

~~~text
reference presence:u8 = 1
public_locator_ordinal:u32
current_entity_ordinal:optional u32
~~~

For an absent optional reference, only presence:u8 = 0 is encoded. The
public_locator_ordinal means only exact equality of the already-public locator
token. It is not a physical-card ID, persistent identity, or replay identity.

current_entity_ordinal is allowed only for a candidate, chain, or relationship
reference when the public projection proves that its token matches exactly one
current PublicSafeStateView entity. A match may not be inferred from a zone,
sequence, slot shape, or token text. If exact current resolution is not proven,
the optional current ordinal is absent and the public locator ordinal remains.

Historical VisibleEvent references are different: their entity and target
references carry only public_locator_ordinal. They MUST never carry a
current_entity_ordinal, even if the historical token equals the locator of the
entity currently occupying that slot. Historical event locators are never
rebound to current entities.

The frame-local table and all derived ordinals are discarded at the frame
boundary and are never carried across a knowledge-destroying transition.

## 7. Card vocabulary

### 7.1 Ownership and mapping

ygo::model owns an immutable CardVocabularyV1. It is an explicit mapping from
already-public passcodes to dense IDs; it is not a database-backed hidden state
oracle.

The mapping is:

~~~text
0 = PAD
1 = PUBLIC_UNKNOWN_OR_REDACTED
2 + rank(passcode in strictly ascending vocabulary list)
~~~

PAD is legal only in a physical padded row with row mask 0. A real
unknown/redacted public entity uses ID 1 and row mask 1. A known public
passcode MUST map to an ID >= 2; an absent vocabulary entry is an error.

### 7.2 Vocabulary canonical bytes and identity

The vocabulary manifest has this exact canonical byte sequence, using the
contract's usual u32be byte_length followed by UTF-8 bytes for strings and
unsigned big-endian integers:

| Order | Field | Encoding |
| ---: | --- | --- |
| 0 | identity domain | string ocgforge.model_card_vocabulary.v1 |
| 1 | identity schema | string ocgforge.model_card_vocabulary.v1 |
| 2 | mapping rule | string ascending_public_passcode_rank_plus_two |
| 3 | passcode count | u32 |
| 4..n | passcode | one strictly ascending nonzero public passcode as u32 |

The vocabulary identity is:

~~~text
model_card_vocabulary.v1.<lowercase hexadecimal SHA-256 of those bytes>
~~~

The identity excludes filesystem paths, catalog traversal order, timestamps,
host/build identity, and database lookup timing. The passcode list is the
semantic mapping input; changing it or any reserved meaning changes the
vocabulary identity.

### 7.3 Catalog/database rule

A catalog or database MAY validate or map an already-public passcode to its
vocabulary ID. It MUST NOT:

- look up a hidden or redacted card identity;
- fill an absent public passcode;
- infer a hidden deck entry or card property;
- append a passcode dynamically to the immutable vocabulary; or
- turn catalog metadata into a replacement for the public-safe decoder.

If a known public passcode is not present in the selected vocabulary, the
complete model input fails closed. If a static opponent deck is unknown, its
passcode vector remains unknown/empty exactly as supplied by the public-safe
state; catalog lookup does not reveal it.

## 8. Canonical model-input bytes and identity

### 8.1 Common primitive encoding

Canonical Phase 5 bytes use:

~~~text
u8/u16/u32/u64: unsigned big-endian
string:         u32be byte_length || UTF-8 bytes
bytes:          u32be byte_length || raw bytes
bool:           u8 0 or 1
optional:       presence:u8 followed by the value when present
vector:         u32 count followed by entries in contract order
~~~

No unordered-container iteration is allowed. State ordering comes from the
existing public-safe decoder. Candidate ordering comes from the public V2
request and is never normalized.

### 8.2 Canonical logical input bytes

canonical_logical_model_input_bytes is the exact ordered sequence below:

| Order | Field | Encoding |
| ---: | --- | --- |
| 0 | identity domain | string ocgforge.model_logical_input.v1 |
| 1 | identity schema | string ocgforge.model_logical_input.v1 |
| 2 | public observation digest | lowercase SHA-256 string, recomputed from the public observation |
| 3 | perspective player | u8 |
| 4 | decision index | u64 |
| 5 | public observation context kind | optional canonical token string copied only from the public observation context |
| 6 | public observation context player | optional u8 copied only from the public observation context |
| 7 | referenced public entities | u32 count and current public locator strings in public-observation canonical order |
| 8 | public locator token table | u32 count and unique public locator strings sorted by unsigned UTF-8 byte order |
| 9 | safe-state | length-prefixed bytes produced by the existing public_safe_state.v1 canonical encoder after the existing decoder |
| 10 | candidate count | u32 |
| 11..n | candidate record | one exact logical public candidate record per candidate, in source order |
| n+1 | public candidate-domain digest | optional derived lowercase SHA-256 string; absent when no public request kind is available from the observation context |

Each candidate record at order 11..n contains, in this order:

~~~text
action_kind:canonical token string
public_action_key:string                  // routing metadata, not feature data
choice:optional {kind:u8, value:u64, response_index:optional u32}
source_reference:optional {kind:u8, public locator:string}
target_reference:optional {kind:u8, public locator:string}
phase:optional u32
position:optional u8
source_index:optional u32
amount:optional signed i32 encoded as u32 bits
continuation_operation:canonical token string, empty when absent
submits_engine_response:bool
~~~

The logical candidate record mirrors the already-public V2 candidate fields; it
does not import internal candidate fields. EnvironmentContinuationView is not
encoded. If an associated public request supplies a kind or player, it is used
only to check equality with the observation context and is not an additional
logical field. Empty candidate domains are invalid public model inputs and
fail closed before this codec is used.

### 8.3 Canonical encoded input bytes

canonical_encoded_model_input_bytes binds both the logical source and its
deterministic integer representation. Its complete top-level order is:

| Order | Field | Encoding |
| ---: | --- | --- |
| 0 | identity domain | string ocgforge.model_encoded_input.v1 |
| 1 | identity schema | string ocgforge.model_encoded_input.v1 |
| 2 | logical input schema | string ocgforge.model_logical_input.v1 |
| 3 | card vocabulary identity | string model_card_vocabulary.v1.<digest> |
| 4 | public observation digest | lowercase SHA-256 string |
| 5 | perspective player | u8 |
| 6 | decision index | u64 |
| 7 | public locator token table | u32 count followed by tokens in unsigned UTF-8 byte order |
| 8 | public observation context kind | optional encoded request-kind code |
| 9 | public observation context player | optional u8 |
| 10 | observation context references | u32 count followed by public_locator_ordinal values in public-observation order |
| 11 | globals payload | exact field order in section 8.3.1 |
| 12 | zones payload | exact field order in section 8.3.2 |
| 13 | entities payload | exact field order in section 8.3.3 |
| 14 | relationships payload | exact field order in section 8.3.4 |
| 15 | chain payload | exact field order in section 8.3.5 |
| 16 | visible-events payload | exact field order in section 8.3.6 |
| 17 | match-context/decks payload | exact field order in section 8.3.7 |
| 18 | public candidate-domain digest | optional derived lowercase SHA-256 string |
| 19 | encoded candidate count | u32 |
| 20..n | candidate feature row | one row per source candidate, in source order, as specified in section 8.3.8 |
| n+1 | routing-key count | u32, equal to candidate count |
| n+2..m | routing key | one exact public_action_key string per candidate, in source order |

The public observation context kind/player in orders 8 and 9 are copied only
from the accepted PublicEnvironmentObservation. A same-frame public request
kind/player, when available outside the observation context, is an equality
check and is not a second encoded field. EnvironmentContinuationView is not
encoded anywhere.

The encoded scalar/state payload contains only the integer, categorical, card
ID, presence-mask, and frame-local-reference values defined below. The routing-
key vector is a parallel selection sidecar and is not made available to a
learned feature extractor as a string feature. It is included in canonical
bytes so that the model-input identity cannot detach a score row from the
candidate it is allowed to select.

### 8.3.1 Globals payload

The globals payload is written in this exact order:

~~~text
duel_flags:u64
life_points:u32 count followed by ordered u32 values
player_to_act:optional u8
turn_player:optional u8
turn_count:optional u32
phase:optional u32
chain_length:u32
winner:optional u8
win_reason:optional u8
terminal:bool
~~~

These are the decoded public-safe globals. No engine-step, raw-message, or
internal observation field is added.

### 8.3.2 Zones payload

Write zone count as u32, then every decoded zone in the existing
public_safe_state.v1 canonical order. Each zone record is:

~~~text
player:u8
kind:u8
total_count:u32
public_identity_count:u32
hidden_count:u32
player_observable_order:bool
~~~

No zone record is filtered or globally capped.

### 8.3.3 Entities and CardProperties payload

Write entity count as u32, then every current decoded entity in its existing
canonical locator order. The entity feature row is:

~~~text
public_locator_ordinal:u32
identity_known:bool
card_vocabulary_id:u32
owner:optional u8
controller:optional u8
zone:u8
sequence:optional u32
overlay_sequence:optional u32
position:u8
face_up:bool
face_down:bool
printed:optional CardProperties
current:optional CardProperties
~~~

The row position is the current_entity_ordinal for this decoded entity; it is
frame-local and is not serialized as a persistent identity. A known public
entity has a vocabulary ID of at least 2. An unknown/redacted real entity has
identity_known=false, no identity-derived properties, and vocabulary ID 1.
ID 0 is reserved for masked physical padding only.

Each optional CardProperties value is written as a presence byte followed by
these fields in exactly this order:

~~~text
type:optional u32
attribute:optional u32
race:optional u64
attack:optional signed i32 as two's-complement u32 bits
defense:optional signed i32 as two's-complement u32 bits
base_attack:optional signed i32 as two's-complement u32 bits
base_defense:optional signed i32 as two's-complement u32 bits
level:optional u32
rank:optional u32
link_rating:optional u32
link_markers:u32 count followed by u8 codes in public_safe_state.v1 order
left_scale:optional u32
right_scale:optional u32
status_flags:optional u32
counters:u32 count followed by type:u32,count:u32 pairs in public_safe_state.v1 order
~~~

The existing public-safe decoder has already enforced that a redacted entity
has no passcode, printed properties, or current identity properties. P5 does
not fill them from a catalog.

### 8.3.4 Relationships payload

Write relationship count as u32, then every relationship in the existing
canonical relationship order:

~~~text
kind:u8
source:CurrentReference
target:CurrentReference
~~~

CurrentReference is written as an optional reference. When present its exact
order is:

~~~text
presence:u8 = 1
public_locator_ordinal:u32
current_entity_ordinal:optional u32
~~~

The optional current ordinal is present only when the public projection proves
an exact match to one current decoded entity. It is absent rather than guessed
when proof is unavailable. A relationship never uses a physical or persistent
identity.

### 8.3.5 Chain-links payload

Write chain length as u32, link count as u32, then every link in authoritative
chain-link order:

~~~text
index:u32
activating_player:optional u8
source:optional CurrentReference
activation_zone:optional u8
effect_description:optional u64
targets:u32 count followed by CurrentReference values
~~~

The same exact-current-resolution rule applies to source and target
references. Chain order and target order are not sorted by P5.

### 8.3.6 Visible-events payload

Write visible-event count as u32, then every event in the existing canonical
event-index order:

~~~text
event_index:u64
kind:u8
player:optional u8
entity:optional HistoricalReference
public_card_vocabulary_id:optional u32
from_zone:optional u8
to_zone:optional u8
count:optional u32
amount:optional signed i32 as two's-complement u32 bits
counter_type:optional u32
phase:optional u32
winner:optional u8
win_reason:optional u8
effect_description:optional u64
targets:u32 count followed by HistoricalReference values
~~~

HistoricalReference contains only:

~~~text
presence:u8 = 1
public_locator_ordinal:u32
~~~

It MUST NOT contain current_entity_ordinal. A historical event locator is
never rebound to the current entity occupying the same locator. The internal
engine_step_index remains omitted, as required by public_safe_state.v1.

The logical VisibleEvent public_passcode is transformed as follows in this
encoded record: absent public_passcode stays absent; a present already-public
passcode is mapped to its exact immutable CardVocabularyV1 ID, which MUST be
at least 2. A present passcode missing from that vocabulary fails closed. No
current-entity resolution is required for this historical card value, and no
database lookup may fill an absent passcode.

### 8.3.7 Match-context and deck payload

Write match context in this exact order:

~~~text
perspective_player:u8
duel_flags:u64
own_decklist_known:bool
opponent_decklist_known:bool
own_deck
opponent_deck
~~~

Each deck is written as:

~~~text
known:bool
main_deck:u32 count followed by vocabulary ID:u32 values in public-safe order
extra_deck:u32 count followed by vocabulary ID:u32 values in public-safe order
~~~

An unknown deck MUST have zero main/extra counts. A known public passcode is
mapped through the selected immutable vocabulary; a hidden or absent passcode
is never reconstructed.

### 8.3.8 Candidate feature rows

Write each candidate row in the exact source vector order. Each row is:

~~~text
action_kind:u16
choice:optional {kind:u8, value:u64, response_index:optional u32}
source_reference:optional {kind:u8, public_locator_ordinal:u32, current_entity_ordinal:optional u32}
target_reference:optional {kind:u8, public_locator_ordinal:u32, current_entity_ordinal:optional u32}
phase:optional u32
position:optional u8
source_index:optional u32
amount:optional signed i32 as two's-complement u32 bits
continuation_operation:u8
submits_engine_response:bool
~~~

The candidate row does not contain public_action_key. The key is written only
in the routing sidecar after all rows. Source/target current ordinals follow
the exact-current-resolution rule from section 6.5; the public locator ordinal
is always the token-equality reference. A candidate operation token is encoded
by the table in section 6.4. No continuation view or hidden response data is
added.

### 8.3.9 Routing-key sidecar

After all candidate feature rows, write the routing sidecar in this exact
order:

~~~text
routing_key_count:u32
public_action_key[0]:string
public_action_key[1]:string
...
public_action_key[N-1]:string
~~~

The count MUST equal the source candidate count. Each key is the exact
public_action_key at the corresponding source position; keys are not sorted,
hashed into a feature, or replaced with a candidate ordinal. Every key MUST be
valid and unique under ocgforge.public_action_identity.v1. A sidecar mismatch
rejects the complete encoded input.

### 8.4 Model-input identity

canonical_model_input_identity_bytes is:

| Order | Field | Encoding |
| ---: | --- | --- |
| 0 | identity domain | string ocgforge.model_input_identity.v1 |
| 1 | identity schema | string ocgforge.model_input_identity.v1 |
| 2 | logical input schema | string ocgforge.model_logical_input.v1 |
| 3 | encoded input schema | string ocgforge.model_encoded_input.v1 |
| 4 | card vocabulary identity | string |
| 5 | canonical logical input | length-prefixed logical bytes |
| 6 | canonical encoded input | length-prefixed encoded bytes |

The model-input identity is:

~~~text
model_input.v1.<lowercase hexadecimal SHA-256 of those bytes>
~~~

The identity binds public state, exact candidate membership/order/keys, public
descriptors, deterministic codes, and vocabulary mapping. It MUST NOT include
any ModelBatchLayoutV1 value.

The following are explicitly excluded from model-input identity:

~~~text
batch size or batch composition
sample order in a batch
candidate padding width or bucket capacity
ragged offsets
padding values
row masks used only to materialize padding
physical tensor shape, storage dtype, device, or backend
framework/library name
allocation order, worker, thread, PID, path, wall clock, or build metadata
~~~

Two physical batches may therefore contain different layouts while every
sample retains the same model-input identity.

## 9. ModelBatchLayoutV1

### 9.1 Role

ModelBatchLayoutV1 is a lossless physical execution view over one or more
already encoded model inputs. It does not redefine semantic ordering,
candidate identity, or model-input identity.

The layout MAY be ragged, padded, bucketed, or converted into a framework's
integer arrays, provided the conversion preserves the exact encoded values and
the masks below. A backend cannot make a value semantic merely by placing it
in a tensor.

### 9.2 Ragged offsets

For a batch of B model inputs, each variable collection has a u64 offset vector
of length B + 1. At minimum the layout owns:

~~~text
candidate_offsets
zone_offsets
entity_offsets
relationship_offsets
chain_link_offsets
visible_event_offsets
decision_context_reference_offsets
public_locator_token_offsets
own_deck_passcode_offsets
opponent_deck_passcode_offsets
~~~

For every collection:

~~~text
offsets[0] = 0
offsets[i] <= offsets[i + 1]
offsets[B] = flat_buffer_length
~~~

For candidates specifically:

~~~text
candidate_offsets[i + 1] - candidate_offsets[i] = N_i
~~~

The flat candidate rows and flat routing keys are concatenated in batch sample
order, while each sample's internal candidate order remains exactly the public
V2 order. Offset overflow, missing rows, or inconsistent flat-buffer lengths
rejects the layout; no value is dropped.

### 9.3 Padded views and masks

For a padded candidate view, let:

~~~text
W >= max(N_0, N_1, ..., N_(B-1))
~~~

The layout has:

~~~text
candidate_features_padded[B][W]
candidate_row_mask[B][W]
candidate_routing_keys_padded[B][W]
~~~

For each sample i:

~~~text
candidate_row_mask[i][j] = 1  when 0 <= j < N_i
candidate_row_mask[i][j] = 0  when N_i <= j < W
~~~

Real rows occupy positions 0..N_i-1 in exact source order. Padding rows have
zero/default encoded values, empty routing keys, and mask 0. card ID 0 is
legal only in such a masked padding row; a real unknown card uses ID 1.

An optional-field presence mask is distinct from the row mask: a real row may
have a present bit of 0 for one optional field while its row mask remains 1.

The same rule applies to padded variable state collections with their
corresponding row masks. A masked row is never scored, selected, or treated as
a legal candidate.

### 9.4 Capacity and batching rules

- There is no global max_options, max_candidates, entity cap, or hidden fixed
  tensor width.
- A bucket capacity smaller than any N_i is a structured layout failure.
- A caller MAY partition inputs into multiple physical batches before layout,
  but it MUST NOT split one candidate domain across batches.
- Batch composition and sample order are execution details and MUST NOT enter
  model-input identity.
- Changing a bucket, padding width, offset representation, or framework dtype
  cannot change candidate count, order, routing keys, or encoded row values.

### 9.5 Required roundtrip invariant

For every valid ragged input and every valid padded capacity W:

~~~text
unpad(pad(ragged(input), W)) == ragged(input)
~~~

Equality is exact for encoded scalar/state buffers, candidate rows, routing
keys, per-field presence masks, counts, and within-sample order. It does not
require physical padding bytes or offsets to be identical after a second layout
choice. A failed roundtrip rejects the layout and never falls back to a
truncated view.

## 10. Exact candidate and identity invariants

The following are normative:

1. The public candidate count is nonzero and fits the existing public-domain
   contract's u32 count.
2. Every public key is valid under ocgforge.public_action_identity.v1.
3. Public keys are unique within the current ordered domain.
4. The model candidate count equals the source candidate count at every layer.
5. Candidate membership and order are byte-preserved; no lexical sort is
   applied to candidate rows or routing keys.
6. A candidate's public descriptor and routing key remain paired at the same
   local position.
7. public_action_key is the selection/routing identity. It is never replaced
   by a string feature, candidate ordinal, card vocabulary ID, or tensor row
   number.
8. A candidate ordinal is the zero-based current vector position only. It may
   be derived for a supervision label and may appear as a local physical row
   coordinate, but it is never replay identity.
9. A public candidate that cannot be represented safely rejects the entire
   frame. The offending candidate is not removed.
10. No maximum-domain metric, including G28, authorizes a cap. The real
    complete witness domain is represented in full.

## 11. Trajectory-to-supervision derivation

ModelSupervisionSampleV1 is a derived value, not a change to
ocgforge.trusted_trajectory.v1:

~~~text
ModelSupervisionSampleV1 {
    schema_id: "ocgforge.model_supervision_sample.v1"
    model_input: LogicalModelInputV1 + EncodedModelInputV1 identity
    source_public_semantic_decision_id: public decision ID metadata
    selected_public_action_key: exact selected key metadata
    candidate_ordinal: u32 derived from the current ordered domain
}
~~~

The materializer MUST:

1. consume one accepted DecisionRecord and its PublicFrameSnapshot;
2. reconstruct the model input from the frame's public observation and
   complete ordered public candidate vector;
3. find selected_public_action_key by exact string equality;
4. require exactly one match;
5. record that match's zero-based current candidate ordinal as the derived
   label; and
6. retain the selected public key as routing/audit metadata.

Missing, duplicate, malformed, or inconsistent keys fail closed. The
materializer MUST NOT sort candidates, replace the key with the ordinal, read
internal semantic keys, or mutate the trusted trajectory. The ordinal is not
part of replay identity, public action identity, public candidate-domain
identity, or model-input identity.

No reward, loss, optimizer, policy sampling, recurrent state, or learner
framework is defined here.

## 12. Failure semantics

The complete model input or physical batch operation fails closed on any of the
following conditions:

~~~text
unknown or incompatible Phase 5 contract ID
public observation decoder failure
public-safe decoder failure or non-canonical safe-state bytes
missing or inconsistent public observation context or same-frame request integrity check
empty, duplicate, malformed, or reordered candidate domain
public observation/candidate reference that cannot resolve safely
candidate count mismatch at any layer
unknown public action/category/continuation code
known public passcode absent from the selected immutable vocabulary
hidden or redacted identity presented as a known passcode
integer, offset, or count overflow
padding capacity smaller than a real candidate domain
row-mask, offset, route-key, or roundtrip inconsistency
trajectory selected key missing or non-unique
any attempt to use a forbidden internal/private value
~~~

The failure result MUST identify the failed contract boundary and reason
without including hidden identity, raw response bytes, or private locators.
There is no first-match collision resolution, candidate truncation, fallback
vocabulary, automatic action, or guessed public value.

## 13. Explicit non-goals

Phase 5 Task 1 does not authorize or define:

~~~text
neural networks
Behavior Cloning
PyTorch, JAX, NumPy, Trackio, Accelerate, or another ML dependency
framework/backend selection
losses, optimizers, or float normalization
RL, self-play, policy sampling, or reward shaping
checkpoint training or model checkpoints
new gameplay semantics or legal-decision handling
Teacher behavior or Teacher DTO changes
trusted trajectory schema or identity changes
fixed global action/observation tensors
hidden-state beliefs, card inference, or catalog enrichment
~~~

The later P5 implementation may provide framework-neutral reference probes
and derived physical views, but it must remain downstream of the frozen public
contracts.
