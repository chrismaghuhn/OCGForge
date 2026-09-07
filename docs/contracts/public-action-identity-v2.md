# Perspective-safe public action identity v2

**Status:** Normative Stage-A successor contract; implemented by the public
identity V2 slice and consumed by `episodic_environment.v3`.

This contract is the incompatible successor to the frozen public action,
candidate-domain, and public semantic-decision identity V1 codecs. It exposes
the already public direction of native `MSG_SELECT_UNSELECT_CARD` candidates.
It does not change engine legality, candidate order, response bytes, or the
internal Decision Protocol identities.

## Contract IDs

```text
ocgforge.public_action_identity.v2
ocgforge.public_candidate_domain.v2
ocgforge.public_semantic_decision_identity.v2
public_action.v2.
```

The public observation and safe-state inputs remain:

```text
ocgforge.public_environment_observation.v1
ocgforge.public_safe_state.v1
```

The internal identities remain:

```text
ocgforge.action_identity.v1
ocgforge.candidate_domain.v1
ocgforge.semantic_decision_identity.v1
```

## 1. Typed operation metadata

The public auxiliary enum is:

```cpp
enum class PublicCardSelectionOperation : std::uint8_t {
    None = 0,
    Select = 1,
    Unselect = 2,
};
```

The names describe what choosing the candidate does:

```text
MSG_SELECT_UNSELECT_CARD selected list
    -> CardSelectionOperation::Unselect

MSG_SELECT_UNSELECT_CARD unselected/selectable list
    -> CardSelectionOperation::Select

Finish, Cancel, and unrelated card-selection requests
    -> CardSelectionOperation::None
```

`ContinuationOperation` is a different auxiliary type and is unchanged. The
operation direction is not inferred from `semantic_key`, candidate order,
`source_index`, public-key spelling, or any private engine value.

## 2. Layered validation

The isolated public action codec does not receive `DecisionRequest.kind`, so it
enforces only the structural rule:

```text
action_kind != "card_selection"
    -> operation MUST be None

action_kind == "card_selection"
    -> None, Select, and Unselect are valid operation codes

unknown operation code
    -> reject
```

The Environment projection adds request-family semantics:

```text
request.kind == UnselectCard
AND candidate.action_kind == CardSelection
    -> operation MUST be Select or Unselect

request.kind != UnselectCard
AND candidate.action_kind == CardSelection
    -> operation MUST be None

Finish or Cancel
    -> operation MUST be None
```

Any violation rejects the complete public frame. It does not remove, rewrite,
sort, truncate, or default an individual legal candidate.

## 3. Canonical public action key V2

Strings use `u32be byte_length || UTF-8 bytes`. Optional values use a
`presence:u8` followed by the value when present. The exact V2 action bytes
are:

| Order | Field | Encoding |
| ---: | --- | --- |
| 0 | identity domain | string `ocgforge.public_action_identity.v2` |
| 1 | identity schema | string `ocgforge.public_action_identity.v2` |
| 2 | action kind | canonical lower-case token string |
| 3 | card-selection operation | `u8`: `0=None`, `1=Select`, `2=Unselect` |
| 4 | typed choice | optional `{kind:u8, value:u64be, response_index:optional u32be}` |
| 5 | source reference | optional `{kind:u8, observation_locator:string}` |
| 6 | target reference | optional `{kind:u8, observation_locator:string}` |
| 7 | phase | optional `u32be` |
| 8 | position | optional `u8` |
| 9 | source index | optional `u32be` |
| 10 | amount | optional signed `i32`, encoded as two's-complement `u32be` bits |
| 11 | continuation operation | canonical lower-case token string; empty means absent |

The lexical key is:

```text
public_action.v2.<lowercase hexadecimal canonical descriptor bytes>
```

`Select` and `Unselect` therefore have different public action identities
when every other descriptor field is equal. The key contains no passcode. A
visible or redacted card reference contains only its current
perspective-safe observation locator and reference kind.

The V1 action codec remains byte-for-byte historical. Its input type has a
default `card_selection_operation = None`; passing `Select`, `Unselect`, or
an unknown value to the V1 encoder rejects instead of discarding new
semantics. A `public_action.v1.` key is never accepted by the V2 validator,
and a V2 key is never accepted by the V1 validator.

## 4. Canonical public candidate-domain identity V2

`ocgforge.public_candidate_domain.v2` is SHA-256 over:

| Order | Field | Encoding |
| ---: | --- | --- |
| 0 | identity domain | string `ocgforge.public_candidate_domain.v2` |
| 1 | request kind | canonical lower-case token string |
| 2 | candidate count | `u32be` |
| 3..n | public action key | one V2 key string per candidate in authoritative source order |

The V2 domain codec accepts only V2 public action keys, preserves complete
candidate membership and supplied ordering, and rejects empty or duplicate
domains. It never sorts or substitutes candidate indices.

## 5. Canonical public semantic-decision identity V2

`ocgforge.public_semantic_decision_identity.v2` is SHA-256 over:

| Order | Field | Encoding |
| ---: | --- | --- |
| 0 | identity domain | string `ocgforge.public_semantic_decision_identity.v2` |
| 1 | identity schema | string `ocgforge.public_semantic_decision_identity.v2` |
| 2 | episode semantic ID | public episode identity string |
| 3 | environment decision index | `u64be` |
| 4 | acting player | `u8` (`0` or `1`) |
| 5 | public request kind | canonical lower-case token string |
| 6 | public observation digest | lowercase SHA-256 string from `public_environment_observation.v1` |
| 7 | public candidate-domain digest | lowercase SHA-256 string from `public_candidate_domain.v2` |

The identity contains no internal protocol decision ID, engine step index,
internal candidate digest, internal continuation ID, response bytes, pointer,
raw message hash, or hidden card identity.

## 6. Frame-local resolution and privacy

The environment maintains a private, current-frame binding:

```text
V2 public_action_key
    -> exactly one current internal ActionCandidate
    -> existing internal semantic_key and response path
```

The binding is not part of a public DTO, public digest, public identity, or
public replay input. Unknown keys, invalid keys, duplicate bindings, or
ambiguous mappings fail closed before authoritative advancement. Candidate
order is never used as a collision resolver.

The projection consumes only the acting player's public observation and the
complete internal candidate vector. It may publish `VisibleCard` only when
the current observation proves identity visibility; otherwise it publishes a
`RedactedSlot` with its current public locator. Hidden passcodes, raw
protocol bytes, internal semantic keys, CoreHost state, pointers, and private
locators never enter the V2 identity.

Paired hidden worlds with equal public observation and public candidate
semantics must produce equal public action keys, V2 candidate-domain digests,
and V2 public decision identities even when their internal keys differ.

## 7. Compatibility boundary

```text
public_action_identity.v1 + public_action.v1.
    -> historical V1 only

public_action_identity.v2 + public_action.v2.
    -> V2 only

V1/V2 mixed identity or key/domain validation
    -> reject before mutation
```

This contract does not change `PlayerObservation v1`,
`public_environment_observation.v1`, `public_safe_state.v1`, internal action
identity, internal candidate-domain identity, internal semantic-decision
identity, Decision Protocol legality, candidate ordering, exact response
bytes, or continuation semantics.

Trajectory, restricted replay, model-input, Task7 materialization, Teacher,
dataset, RUN_A, RUN_B, and training successors are separate migrations and
remain deferred until explicitly authorized.

## 8. Verification

The focused implementation gate is:

```text
tests/episodic/public_action_identity_test.cpp
```

It must retain the historical V1 golden vectors and prove V2 operation
distinctness, V1/V2 rejection, structural operation validation, duplicate and
empty-domain rejection, version-pure resolvers, canonical ordering, paired
world privacy, and fresh-process determinism.
