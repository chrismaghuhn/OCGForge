# Perspective-safe public action identity v1

## Contract IDs

This contract owns the three policy-facing action/decision identity domains
below:

```text
ocgforge.public_action_identity.v1
ocgforge.public_candidate_domain.v1
ocgforge.public_semantic_decision_identity.v1
```

The public semantic decision identity consumes the separately owned
`ocgforge.public_environment_observation.v1` digest defined by
`docs/contracts/public-environment-observation-v1.md`.

The successor public environment contract that consumes them is:

```text
ocgforge.episodic_environment.v2
```

The internal protocol identities remain unchanged:

```text
ocgforge.action_identity.v1
ocgforge.candidate_domain.v1
ocgforge.semantic_decision_identity.v1
```

## 1. Purpose and ownership

`ActionCandidate.semantic_key` is the trusted internal identity used by the
Decision Protocol, `EpisodeDriver`, exact response path, and `EngineTrace v2`.
It is not automatically safe for policy publication. In particular, a card
key or continuation key can contain an identity that is hidden from the
acting player.

This contract defines the independent projection boundary used by
`EpisodicEnvironment` v2:

```text
Decision Protocol internal candidate
    + internal semantic_key
              |
              | privacy projection audited against PlayerObservation and
              | emitted through PublicEnvironmentObservation
              v
EnvironmentActionCandidate
    + public_action_key
              |
              v
Agent / Teacher / Model
```

The projection is owned by the environment boundary. The rules engine,
Decision Protocol, `PlayerObservation`, and `EngineTrace v2` retain their
existing ownership and semantics.

## 2. Non-equivalence and versioning

The accepted `ocgforge.episodic_environment.v1` contract exposes
`semantic_key` and binds its candidate digest to the internal semantic-key
vector. Replacing those values in place would change public field meaning,
hash input, decision identity, and replay interpretation. V1 is therefore
frozen; it is not an alias for the new surface.

`ocgforge.episodic_environment.v2` and
`ocgforge.environment_identity.v2` are the smallest public-contract and
environment-identity migrations required for the new public fields. The
episode identity codec remains `ocgforge.episode_identity.v1` because its
canonical field sequence is unchanged; it consumes the v2 environment
semantic ID as its parent value.

The `public_action_identity.v1`, `public_candidate_domain.v1`, and
`public_semantic_decision_identity.v1` layouts are finalized by this
pre-acceptance prerequisite. Their corrected choice and public-observation
fields are not a compatibility alias for an already accepted schema. No
additional public-action version is needed for this PR.

`ygo.player_observation.v1`, `ocgforge.action_identity.v1`,
`ocgforge.candidate_domain.v1`, `ocgforge.semantic_decision_identity.v1`,
and `ygo.engine_trace.v2` are not reinterpreted or modified by this contract.
The public observation boundary is separately owned by
`docs/contracts/public-environment-observation-v1.md`.

## 3. Public action key

### 3.1 Safety rule

`public_action_key` is constructed only from a projection descriptor whose
values are visible to the acting player's current `PlayerObservation`.
Internal `semantic_key`, raw engine bytes, `raw_message_hash`, internal
continuation IDs, internal candidate digests, pointers, and persistent hidden
card identities are never inputs to this key.

A public card reference contains only:

```text
kind: VisibleCard | RedactedSlot
observation_locator: current ObservationLocator value
```

The locator must be copied from the current perspective-safe observation and
is an ephemeral current zone/slot reference. It is not a physical-card ID.
A `RedactedSlot` reference identifies the visible slot, not the hidden card
occupying it. A visible card may use its current observation locator as well;
any additional visible card fields belong to the public candidate DTO and
must pass the same observation audit.

The projection must not put a hidden passcode in a redacted reference. The
codec does not infer visibility from an internal candidate; the projection
must prove visibility before calling it.

### 3.2 Canonical action-key descriptor

The canonical byte sequence is the following ordered sequence. Strings use
the accepted `u32be byte_length || UTF-8 bytes` encoding; optional values use
`presence:u8` followed by the value when present.

| Order | Field | Encoding |
| ---: | --- | --- |
| 0 | identity domain | string `ocgforge.public_action_identity.v1` |
| 1 | identity schema | string `ocgforge.public_action_identity.v1` |
| 2 | action kind | canonical lower-case token string |
| 3 | typed choice | optional `{kind:u8, value:u64, response_index:optional u32}` |
| 4 | source reference | optional `{kind:u8, observation_locator:string}` |
| 5 | target reference | optional `{kind:u8, observation_locator:string}` |
| 6 | phase | optional `u32` |
| 7 | position | optional `u8` |
| 8 | source index | optional `u32` |
| 9 | amount | optional signed `i32`, encoded as its two's-complement `u32` bits |
| 10 | continuation operation | canonical lower-case token string, empty when absent |

The typed choice kinds are:

| Kind | Meaning | Value |
| --- | --- | --- |
| `YesNo` | `MSG_SELECT_YESNO` | `0 = no`, `1 = yes` |
| `EffectYesNo` | `MSG_SELECT_EFFECTYN` | `0 = no`, `1 = yes` |
| `EffectChoice` | one command/effect entry from an engine list | exact engine/list selector; not a candidate-vector index |
| `OptionValue` | `MSG_SELECT_OPTION` | engine-provided `u64` option value plus exact response selector |
| `AnnouncementNumber` | `MSG_ANNOUNCE_NUMBER` | engine-provided `u64` number plus exact response selector |

`response_index` is required for `OptionValue` and `AnnouncementNumber` because
the public value and the exact response selector are both semantic. It is
never a synthetic environment candidate index. Boolean choices reject a
response selector; `EffectChoice` carries the engine/list selector in its
typed value.

The public key is the exact, unambiguous value encoding:

```text
public_action.v1.<lowercase hexadecimal canonical descriptor bytes>
```

It is deliberately a full encoding rather than a digest alias. This keeps
the safe descriptor one-to-one and avoids making a cryptographic collision
the action-selection mapping. The prefix is part of the lexical identity.

The descriptor contains no card passcode field. A public key may differ when
the visible locator or another visible typed choice differs, but two
internal candidates in one current domain must not be assigned the same
public descriptor.

### 3.3 Projection and completeness

For every complete ordered internal candidate vector:

1. validate the internal vector with the existing protocol validator;
2. audit every policy-required field against the acting player's
   `PlayerObservation` and public observation projection;
3. build exactly one `EnvironmentActionCandidate` per internal candidate, in
   the protocol-provided order;
4. carry every typed semantic choice required by the family, including the
   option/announcement value and response selector;
5. construct exactly one public action key from the audited safe descriptor;
6. preserve the full candidate count and membership; and
7. reject the whole frame with a structured privacy/projection failure if any
   candidate cannot be safely represented.

Privacy is never achieved by dropping, truncating, fabricating, sorting, or
defaulting candidates. An unsupported safe representation is a failed frame,
not a smaller public domain.

## 4. Deterministic internal mapping

The environment retains an internal, frame-local binding table:

```text
public_action_key -> exactly one current internal candidate
                     -> its existing semantic_key
                     -> exact existing response path
```

The table is not part of the public DTO, public digest, public decision ID,
observation, or public replay input. The selected internal semantic key is
resolved only after public validation and is then passed to the existing
`EpisodeDriver` path.

The following are fail-closed errors before authoritative advancement:

- an unknown public key;
- a malformed public key or binding;
- two current internal candidates with the same public key;
- a public key whose safe descriptor is not uniquely tied to one current
  candidate; or
- a projection that cannot prove the required fields are visible.

No collision is resolved by candidate order, first-match behavior, hidden
identity, a candidate index, or an inferred controller choice.

## 5. Public candidate-domain digest

`ocgforge.public_candidate_domain.v1` is SHA-256 over these canonical bytes:

| Order | Field | Encoding |
| ---: | --- | --- |
| 0 | identity domain | string `ocgforge.public_candidate_domain.v1` |
| 1 | request kind | canonical lower-case token string |
| 2 | candidate count | `u32` |
| 3..n | public action key | one string for each key in authoritative protocol order |

The public digest binds the ordered public key vector. It does not hash the
internal semantic-key vector and does not replace the complete public
candidate collection. Empty, malformed, or duplicate public domains fail
closed before a public frame is published.

The internal `ocgforge.candidate_domain.v1` digest remains available to
internal protocol/trace/replay evidence and is never substituted into this
public field.

## 6. Public semantic decision identity

`ocgforge.public_semantic_decision_identity.v1` is SHA-256 over:

| Order | Field | Encoding |
| ---: | --- | --- |
| 0 | identity domain | string `ocgforge.public_semantic_decision_identity.v1` |
| 1 | identity schema | string `ocgforge.public_semantic_decision_identity.v1` |
| 2 | episode semantic ID | existing public episode ID string |
| 3 | environment decision index | `u64` |
| 4 | acting player | `u8` |
| 5 | public request kind | canonical lower-case token string |
| 6 | public observation digest | lowercase SHA-256 string from `ocgforge.public_environment_observation.v1` |
| 7 | public candidate-domain digest | lowercase SHA-256 string |

The public decision ID contains public decision position, acting-player
coupling, request family, the safe public observation identity, and the public
domain identity. It deliberately does not contain or commit to:

- protocol `DecisionRequest.decision_id`;
- `engine_step_index`;
- `PlayerObservation.observation_hash`;
- internal `semantic_key` values;
- internal candidate-domain digests;
- `raw_message_hash`;
- internal continuation IDs;
- exact response bytes, pointers, or hidden card identities.

The public frame carries `PublicEnvironmentObservation v1`, not the attached
`PlayerObservation v1`. Its own digest binds the safe public state without
transitively carrying internal decision or continuation identity.

## 7. Public v2 replay

Public episodic replay records:

```text
EnvironmentConfig using ocgforge.episodic_environment.v2
EpisodeSpec
ordered public_action_key values
```

At each regenerated frame, replay must:

1. regenerate the complete internal protocol candidate domain;
2. regenerate the same `PublicEnvironmentObservation v1` projection;
3. perform the same perspective-safe candidate projection in the same order;
4. recompute the public observation digest, public candidate-domain digest,
   and public semantic decision ID;
5. resolve the recorded public key against the regenerated frame-local
   binding table; and
6. submit the resolved existing internal semantic key through `EpisodeDriver`.

Public replay never submits a candidate index or internal key. Internal
protocol/trace replay may continue to use the unchanged internal semantic
keys and `EngineTrace v2` identities.

## 8. Required paired-world proof

The normative fixture has two worlds that differ only in hidden opponent-card
identity:

```text
World A internal key: card.0.3.14821890.0.8.0
World B internal key: card.0.3.7654321.0.8.0
Visible slot in both: p0:SPELL_TRAP_ZONE:0
Public reference in both: RedactedSlot(p0:SPELL_TRAP_ZONE:0)
Public descriptor in both: action_kind=card_selection,
                           source_index=3,
                           source=RedactedSlot(p0:SPELL_TRAP_ZONE:0)
```

The fixture must prove byte equality for:

```text
PublicEnvironmentObservation
public candidate descriptor
public_action_key
public_candidate_domain_digest
public_semantic_decision_id
```

The attached internal `PlayerObservation v1` values may differ in private
decision/continuation metadata. The internal semantic keys may differ. The
public key resolves to the one current internal candidate separately in each
world; it must not be used to claim that the hidden physical cards are the
same.

The focused golden test in
`tests/episodic/public_action_identity_test.cpp` is the codec-level proof.
The future v2 facade acceptance suite must repeat the same property through
the real projection boundary.

## 9. Explicit non-goals

This contract does not:

- change Decision Protocol legality or candidate ordering;
- change `PlayerObservation` visibility or canonical serialization;
- change `EngineTrace v2`;
- implement `EpisodicEnvironment` reset/step;
- expose raw response bytes or internal binding tables;
- define model tensors, trajectories, rewards, or ML behavior;
- change rules, decks, or the canonical rules bundle.
