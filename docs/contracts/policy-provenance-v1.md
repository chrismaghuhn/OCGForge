# Policy provenance v1

## Status and purpose

**Contract ID:** `ocgforge.policy_provenance.v1`
**Status:** Accepted Phase-3A contract.
**Owning layer:** future `ygo::trajectory` collection provenance.

This contract records exact producer attribution for accepted public actions:
which immutable policy artifact, which participant assignment, which selection
contract, and which policy-local RNG stream/state produced the selection. It
does not make policy provenance learner-visible or gameplay-semantic.

It supports `RANDOM_LEGAL`, `DETERMINISTIC_HEURISTIC`,
`NEURAL_CHECKPOINT`, `SEARCH_ASSISTED`, and `IMPORTED_DEMONSTRATION`
artifacts without requiring any implementation of them.

The words **MUST**, **MUST NOT**, and **FAIL CLOSED** are normative.

## 1. Domains and identifiers

| Domain | Role | Lexical identity |
| --- | --- | --- |
| `ocgforge.policy_provenance.v1` | provenance envelope and per-record attribution schema | no separate envelope ID |
| `ocgforge.policy_artifact_identity.v1` | immutable policy artifact | `policy_artifact.v1.<lowercase SHA-256>` |
| `ocgforge.participant_policy_assignment_identity.v1` | one policy assignment to one V2 participant | `participant_policy_assignment.v1.<lowercase SHA-256>` |
| `ocgforge.policy_rng_initialization_identity.v1` | concrete policy-local RNG initialization | `policy_rng_initialization.v1.<lowercase SHA-256>` |
| `ocgforge.policy_rng_stream_identity.v1` | one concrete policy-local RNG stream | `policy_rng.v1.<lowercase SHA-256>` |
| `ocgforge.policy_rng_decision_provenance.v1` | pre/post RNG provenance for one accepted action | exact canonical bytes; no standalone ID |
| `ocgforge.no_policy_rng.v1` | fixed exact versioned contract identifier for deterministic policies without a RNG | exact literal; not a content-addressed artifact |

All strings, optionals, vectors, integers, and SHA-256 values use the
primitive rules in
[trusted-trajectory-v1.md](trusted-trajectory-v1.md#101-primitive-rules).

No byte sequence or identifier in this contract may contain or derive from an
environment root seed, raw engine state, hidden card, opponent-private
observation, V2 submission token, internal semantic key, protocol decision ID,
raw response, host, process, thread, wall clock, scheduling order, provider,
hardware serial, compiler path, or physical storage location.

Policy-owned RNG initialization material may contain a policy-owned seed, key,
or initial state. It is never an environment/Core root seed or hidden game
state. Its raw bytes are restricted policy-collection material, never learner
input. A `CURSOR` record encodes only the initialization identity; `STATE` may
carry canonical policy state only under the stricter rules in section 4.4.

### 1.1 Identity classes

An **artifact/content identity** MUST name immutable content by a documented
content address. An exact immutable **contract identity** is a fully versioned
contract literal and need not itself be content-addressed. A field explicitly
permitted to use either category must resolve to one of them without a mutable
alias. `ocgforge.no_policy_rng.v1` is an exact contract identity, not an
artifact/content identity.

## 2. PolicyArtifact

A `PolicyArtifact` is the immutable description of a concrete policy producer.
Its environment/game-information inputs may arise only from the acting
participant's perspective-safe V2 public boundary, including the complete
ordered candidate domain. It may additionally use policy-local recurrent or
strategic state, policy RNG, and state derived solely from prior permitted
public observations and actions for that same participant. It must not use
CoreHost, raw engine state, opponent-private observations, internal action
keys, internal IDs, or hidden-derived caches.

Its action adapter returns one existing `public_action_key`; it must not infer
or submit an internal key, candidate-vector index, response selector as a
vector coordinate, reconstructed legal domain, or hidden value. An artifact
that cannot operate on the full published public domain is not a trusted
policy producer.

It contains:

| Field | Requirement |
| --- | --- |
| `policy_kind` | one `u8` code from section 2.1 |
| `producer_implementation_identity` | nonempty artifact/content identity or exact versioned implementation contract identity |
| `inference_adapter_identity` | nonempty artifact/content identity or exact versioned inference/no-op-adapter contract identity |
| `observation_adapter_identity` | nonempty artifact/content identity or exact versioned observation-adapter contract identity |
| `action_adapter_identity` | nonempty artifact/content identity or exact versioned public-action-adapter contract identity |
| `sampling_contract_identity` | nonempty exact versioned complete selection/sampling contract identity |
| `policy_rng_contract_identity` | nonempty exact versioned policy-local RNG algorithm/state-codec contract identity, or `ocgforge.no_policy_rng.v1` |
| `model_checkpoint_identity` | optional immutable content-addressed model/checkpoint artifact identity |
| `search_contract_identity` | optional immutable content-addressed search artifact or exact versioned search contract identity |
| `demonstration_source_identity` | optional immutable content-addressed imported-demonstration artifact identity |
| `artifact_metadata_identity` | optional privacy-reviewed immutable content-addressed provenance-metadata artifact identity |

Each field MUST meet the identity category stated above. A required
artifact/content identity that is not content-addressed, or a required contract
identity that is missing, mutable, unversioned, or aliased, fails closed. A
mutable URI, branch name without an immutable commit, version range, `latest`
alias, host-local path, provider alias, wall clock, or environment-dependent
name fails closed.

### 2.1 Policy-kind codes

`policy_kind` uses one `u8`:

| Code | Value | Mandatory constraints |
| ---: | --- | --- |
| 0 | `RANDOM_LEGAL` | non-`no_policy_rng` RNG contract and complete sampling contract; model, search, and demonstration source absent |
| 1 | `DETERMINISTIC_HEURISTIC` | `ocgforge.no_policy_rng.v1` and deterministic sampling contract; model, search, and demonstration source absent |
| 2 | `NEURAL_CHECKPOINT` | model/checkpoint and inference adapter present; sampling and RNG mode declared explicitly |
| 3 | `SEARCH_ASSISTED` | search contract present; checkpoint optional; sampling and RNG mode declared explicitly |
| 4 | `IMPORTED_DEMONSTRATION` | demonstration source and action adapter present; stochastic import resolution declares its RNG contract |

A future search-assisted Teacher is merely representable. This contract does
not implement, require, or validate a Teacher, model, search, or framework.

### 2.2 Policy-artifact identity codec

`policy_artifact_id` is the lowercase SHA-256 digest of these exact fields:

| Order | Field | Encoding |
| ---: | --- | --- |
| 0 | identity domain | string `ocgforge.policy_artifact_identity.v1` |
| 1 | identity schema | string `ocgforge.policy_artifact_identity.v1` |
| 2 | provenance contract | string `ocgforge.policy_provenance.v1` |
| 3 | policy kind | `u8` |
| 4 | producer implementation identity | string |
| 5 | inference adapter identity | string |
| 6 | observation adapter identity | string |
| 7 | action adapter identity | string |
| 8 | sampling contract identity | string |
| 9 | policy RNG contract identity | string |
| 10 | model checkpoint identity | optional string |
| 11 | search contract identity | optional string |
| 12 | demonstration source identity | optional string |
| 13 | artifact metadata identity | optional string |

The lexical form is:

```text
policy_artifact.v1.<lowercase hexadecimal SHA-256 digest>
```

The stored ID MUST recompute exactly. The artifact identity is provenance
only: it must never change an environment, episode, frame, candidate domain,
selected public action, closure, or public gameplay identity.

## 3. ParticipantPolicyAssignment

A `ParticipantPolicyAssignment` binds one policy artifact to one V2
participant. It replaces a single episode-global `behavior_policy_id` because
alternating-player self-play requires every accepted record to resolve to the
acting participant's concrete assignment.

| Field | Requirement |
| --- | --- |
| `player` | `u8` V2 player `0` or `1` |
| `seat_role` | `STARTING_PLAYER` or `NON_STARTING_PLAYER` |
| `deck_role` | `FIRST_LOCKED_DECK` or `SECOND_LOCKED_DECK` in certified lock order |
| `resolved_locked_deck_id` | exact certified deck ID used after V2 seat assignment |
| `resolved_locked_deck_sha256` | exact certified deck digest |
| `policy_role` | `u8` code from section 3.1 |
| `policy_artifact_id` | a declared immutable artifact |
| `assignment_epoch` | `u32`, starting at zero per player |
| `effective_from_decision_index` | `u64` global index at which this assignment becomes active |
| `league_context` | optional all-or-absent tuple in section 3.2 |

The provenance envelope MUST declare an epoch-zero assignment effective at
global decision index zero for each V2 player, even for a zero-decision
terminal. Later assignments for one player have strictly increasing epochs
and strictly increasing effective indices.

For a record, the acting assignment is the declared assignment for the same
player with the greatest `effective_from_decision_index` that does not exceed
the record's global index. It must resolve to exactly one assignment. A record
whose explicit `acting_policy_assignment_id` differs from this result fails
closed. This permits explicit future reassignment while rejecting an
unattributed fallback policy.

### 3.1 Policy-role codes

| Code | `policy_role` |
| ---: | --- |
| 0 | `BEHAVIOR` |
| 1 | `OPPONENT` |
| 2 | `EVALUATION` |
| 3 | `DEMONSTRATOR` |
| 4 | `SELF_PLAY` |

Roles are collection provenance, not a legal-action or reward setting.

### 3.2 League context

`league_context` is one optional tuple:

```text
league_generation:u64
league_member_id:string
league_role:string
```

It is encoded as a `presence:u8` followed, when present, by the fields above
in that exact order. `league_role` is a nonempty canonical lower-case token.
A partial tuple fails closed. This is future-capable metadata only; it does
not implement or ratify a league.

### 3.3 Participant-assignment identity codec

`participant_policy_assignment_id` is the lowercase SHA-256 digest of:

| Order | Field | Encoding |
| ---: | --- | --- |
| 0 | identity domain | string `ocgforge.participant_policy_assignment_identity.v1` |
| 1 | identity schema | string `ocgforge.participant_policy_assignment_identity.v1` |
| 2 | provenance contract | string `ocgforge.policy_provenance.v1` |
| 3 | player | `u8` |
| 4 | seat role | `u8`: `0=STARTING_PLAYER`, `1=NON_STARTING_PLAYER` |
| 5 | deck role | `u8`: `0=FIRST_LOCKED_DECK`, `1=SECOND_LOCKED_DECK` |
| 6 | resolved locked deck ID | string |
| 7 | resolved locked deck SHA-256 | string |
| 8 | policy role | `u8` |
| 9 | policy artifact ID | string |
| 10 | assignment epoch | `u32be` |
| 11 | effective from decision index | `u64be` |
| 12 | league context | exact optional tuple bytes |

The lexical form is:

```text
participant_policy_assignment.v1.<lowercase hexadecimal SHA-256 digest>
```

The declared artifact must exist in the same provenance envelope. The player,
seat role, deck role, and deck values must agree with the accepted V2 reset
input. A stored assignment ID must recompute before it can be used to resolve
a decision record.

## 4. Policy RNG provenance

Policy RNG is separate from engine RNG and V2 root-seed semantics. It is never
a public gameplay identity input. A policy cannot identify or seed its stream
from wall time, PID, thread, scheduling, hardware, provider, mutable global
state, engine root seed, hidden observation, or response bytes.

### 4.1 Policy RNG initialization identity

A concrete stochastic stream needs an initialization identity in addition to
its algorithm, assignment, and local stream name. A cursor alone is not a
stream state: two differently initialized streams can have the same cursor.

For `CURSOR` or `STATE`, `policy_rng_initialization_identity` is the lowercase
SHA-256 digest of these exact fields:

| Order | Field | Encoding |
| ---: | --- | --- |
| 0 | identity domain | string `ocgforge.policy_rng_initialization_identity.v1` |
| 1 | identity schema | string `ocgforge.policy_rng_initialization_identity.v1` |
| 2 | policy RNG contract identity | string |
| 3 | policy RNG stream ID | string |
| 4 | initialization material | bytes canonical under the declared policy RNG contract |

Its lexical form is:

```text
policy_rng_initialization.v1.<lowercase hexadecimal SHA-256 digest>
```

The initialization material may contain a policy-owned RNG seed, key, or
initial state, but MUST NOT contain or derive from environment/Core root-seed
material, hidden game state, or another forbidden value from section 1. The
raw initialization bytes are restricted policy-collection material; a
`CURSOR` record encodes the identity, not those bytes. A collector or future
admission verifier that claims exact producer attribution MUST verify the
identity against canonical initialization material under the declared RNG
contract. This identity is collection provenance only and never a gameplay or
learner input.

For `NONE`, `policy_rng_contract_identity`, `policy_rng_stream_id`, and
`policy_rng_initialization_identity` are all the exact literal
`ocgforge.no_policy_rng.v1`; no initialization material exists.

### 4.2 Policy RNG stream identity

`policy_rng_identity` is the lowercase SHA-256 digest of:

| Order | Field | Encoding |
| ---: | --- | --- |
| 0 | identity domain | string `ocgforge.policy_rng_stream_identity.v1` |
| 1 | identity schema | string `ocgforge.policy_rng_stream_identity.v1` |
| 2 | policy artifact ID | string |
| 3 | participant policy assignment ID | string |
| 4 | policy RNG contract identity | string |
| 5 | policy RNG stream ID | string |
| 6 | policy RNG initialization identity | string |

Its lexical form is:

```text
policy_rng.v1.<lowercase hexadecimal SHA-256 digest>
```

The stream ID is a nonempty immutable/versioned UTF-8 token. Assignments for
different participants MUST use distinct `policy_rng_identity` values even if
they share a checkpoint or policy artifact. The initialization identity binds
the concrete initialized stream without making policy RNG a gameplay input.

### 4.3 Per-decision RNG provenance codec

Every accepted `DecisionRecord` owns one
`PolicyRngDecisionProvenance`. It encodes, in order:

| Order | Field | Encoding |
| ---: | --- | --- |
| 0 | provenance domain | string `ocgforge.policy_rng_decision_provenance.v1` |
| 1 | provenance schema | string `ocgforge.policy_rng_decision_provenance.v1` |
| 2 | decision index | `u64be` |
| 3 | acting policy assignment ID | string |
| 4 | policy RNG identity | string |
| 5 | policy RNG contract identity | string |
| 6 | policy RNG stream ID | string |
| 7 | policy RNG initialization identity | string |
| 8 | RNG mode | `u8` from section 4.4 |
| 9 | mode payload | exact bytes from section 4.4 |

The index and assignment ID must equal the surrounding record. The referenced
assignment's artifact must declare the same RNG contract; the initialization
identity and stream ID must recompute the stored policy RNG identity. The
policy actor, not V2, supplies this provenance at its accepted selection
boundary.

### 4.4 RNG modes

| Code | Mode | Exact payload |
| ---: | --- | --- |
| 0 | `NONE` | no bytes; RNG contract, stream ID, and initialization identity are all `ocgforge.no_policy_rng.v1` |
| 1 | `CURSOR` | pre-cursor `u64be`, then post-cursor `u64be` |
| 2 | `STATE` | pre-state bytes, then post-state bytes; each uses `u32be length || bytes` and the declared RNG contract's canonical state codec |

`NONE` attributes deterministic policies without inventing a random seed.
`CURSOR` is valid only when the declared RNG contract plus the concrete
initialization identity makes every cursor a unique stream state. If that
proof is unavailable, the actor MUST use `STATE`. `STATE` and `CURSOR`
identify exactly which declared initialized stream acted and its pre/post
provenance. A future verifier may check that a declared RNG contract permits a
transition, but environment replay never depends on reproducing it.

State bytes are restricted collection provenance only. They must be canonical
under the declared policy RNG contract and may contain policy-owned RNG state,
including policy-owned seed/key material, but MUST NOT contain a root seed,
hidden data, submission token, internal action key, host/process/provider
identity, or raw response. If this cannot be proven, a trusted policy actor
cannot emit state mode.

## 5. PolicyProvenanceEnvelope codec

One `PolicyProvenanceEnvelope` is nested in every `EpisodeManifest`. It is an
immutable catalog, not a policy input:

```text
policy_artifacts: PolicyArtifact[]
participant_assignments: ParticipantPolicyAssignment[]
```

Artifacts are ordered by ascending `policy_artifact_id`. Assignments are
ordered by ascending `participant_policy_assignment_id`. The codec rejects
duplicates, missing artifact references, invalid assignment epoch sequences,
or assignments that contradict accepted V2 environment/episode identity input.

`canonical_policy_artifact_bytes` is the ordered data in section 2.2 followed
by its lexical `policy_artifact_id` string.
`canonical_participant_policy_assignment_bytes` is the ordered data in section
3.3 followed by its lexical
`participant_policy_assignment_id` string.

`canonical_policy_provenance_envelope_bytes` encodes:

| Order | Field | Encoding |
| ---: | --- | --- |
| 0 | envelope domain | string `ocgforge.policy_provenance.v1` |
| 1 | envelope schema | string `ocgforge.policy_provenance.v1` |
| 2 | artifact count | `u32be` |
| 3..n | artifact | exact artifact bytes in ascending artifact-ID order |
| n+1 | assignment count | `u32be` |
| n+2..m | assignment | exact assignment bytes in ascending assignment-ID order |

Per-decision assignment/RNG fields remain owned by their `DecisionRecord`.
They are bound into `trajectory_record_id` in the order of global decision
index; see [trusted-trajectory-v1.md](trusted-trajectory-v1.md#92-trusted-trajectory-record-identity-codec).

## 6. Attribution versus re-execution

This contract proves exact producer attribution, not bit-identical future
inference re-execution:

- `policy_artifact_id` identifies the immutable policy/checkpoint/config;
- `participant_policy_assignment_id` identifies the participant, seat, deck,
  role, and assignment epoch;
- `policy_rng_identity`, its initialization identity, and per-decision pre/post
  provenance identify the declared concrete selection stream;
- V2 semantic replay regenerates public frames and applies stored public
  action keys.

It does not require a verifier to rerun neural inference, reproduce floating
point, obtain a provider service, schedule identical search, or reproduce
wall-clock behavior. A stronger model-reproducibility claim needs its own
future versioned contract.

## 7. Privacy, determinism, and versioning

All policy provenance is collection provenance only. It is excluded from
`public_gameplay_trajectory_id` and every learner observation/action
projection. It distinguishes `trajectory_record_id` values for otherwise
identical public gameplay.

A decoder MUST fail closed on:

- a required artifact/content identity that is missing, mutable, unversioned,
  or not content-addressed;
- a required contract identity that is missing, mutable, unversioned, aliased,
  or not the exact declared literal;
- an unknown kind, role, seat role, deck role, RNG mode, or schema;
- unsorted or duplicate artifact/assignment IDs;
- an assignment that cannot resolve uniquely for the acting player;
- a shared policy RNG identity across different participant assignments;
- a `CURSOR` whose declared contract and initialization identity do not prove
  unique cursor-to-state meaning;
- an invalid, absent, or unverifiable initialization identity or pre/post data
  for the declared RNG mode;
- prohibited data in an artifact, assignment, RNG stream, initialization, or
  state; or
- an attempt to make policy provenance alter public gameplay identity.

Changing a field order, enum code, byte encoding, identity prefix, visibility
rule, or identity input requires a new versioned domain. A v1 reader must
reject unknown versions and cannot silently alias an older or future policy
artifact.

## 8. Non-goals

This contract does not implement a policy, Teacher, random legal actor,
heuristic, neural checkpoint, inference adapter, search, league, model
runtime, policy RNG service, storage writer, admission receipt, dataset, or
framework integration.
