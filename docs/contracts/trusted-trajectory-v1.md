# Trusted trajectory core v1

## Status and authority

**Contract ID:** `ocgforge.trusted_trajectory.v1`
**Status:** Accepted Phase-3A contract.
**Owning layer:** future `ygo::trajectory`, above `EpisodicEnvironment V2`.

This contract defines the logical, immutable trajectory values that a future
recorder and admission path must preserve. It does not define a recorder,
file format, shard, object, transport, framework adapter, reward function, or
model.

The authority order is unchanged: the pinned rules bundle owns game legality;
the accepted Decision Protocol and EpisodicEnvironment V2 contracts own the
public decision boundary; this contract owns only the values recorded above
that boundary. If this document conflicts with an accepted lower-layer
contract, the lower-layer contract wins and a v1 recorder must fail closed.

The words **MUST**, **MUST NOT**, and **FAIL CLOSED** are normative.

## 1. Contract domains

| Domain or fixed contract identifier | Purpose |
| --- | --- |
| `ocgforge.trusted_trajectory.v1` | logical episode schema and canonical episode/value codec |
| `ocgforge.policy_provenance.v1` | exact producer, participant assignment, and policy-RNG provenance; defined in [policy-provenance-v1.md](policy-provenance-v1.md) |
| `ocgforge.policy_artifact_identity.v1` | immutable policy-artifact identity codec |
| `ocgforge.participant_policy_assignment_identity.v1` | participant-to-policy assignment identity codec |
| `ocgforge.policy_rng_initialization_identity.v1` | concrete policy RNG initialization identity codec |
| `ocgforge.policy_rng_stream_identity.v1` | policy-local RNG stream identity codec |
| `ocgforge.policy_rng_decision_provenance.v1` | per-accepted-decision policy RNG provenance codec |
| `ocgforge.no_policy_rng.v1` | fixed exact versioned contract identifier for a deterministic policy with no RNG |
| `ocgforge.public_gameplay_trajectory_identity.v1` | global public-gameplay replay identity; never a participant learner feature |
| `ocgforge.trajectory_record_identity.v1` | trusted record identity that additionally binds collection provenance |
| `ocgforge.restricted_replay_evidence.v1` | restricted Phase-3B admission-replay companion for an `INTERRUPTED` closure |

Existing `environment_semantic_id`, `episode_semantic_id`, and
`public_semantic_decision_id` keep their existing meanings and codecs. This
contract does not rename, widen, or alias them.

## 2. Scope and ownership boundary

The future layer has this position:

```text
ocgcore
  -> Decision Protocol
  -> EpisodeDriver
  -> EpisodicEnvironment V2
  -> ygo::trajectory
  -> Phase 3B persistence / admission
  -> Phase 4 Teacher / evaluation
  -> Phase 5 model adapter
  -> Phase 6+ ML
```

`ygo::trajectory` MAY consume immutable accepted V2 public frames, accepted
V2 reset/replay identity inputs, and accepted closure values. It MUST NOT:

- query `CoreHost`, raw engine state, query buffers, pointers, or caches;
- access `ActionCandidate.semantic_key`, protocol `DecisionRequest.decision_id`,
  an internal candidate digest, raw-message hash, internal continuation ID,
  raw response bytes, or an EngineTrace v2 step;
- build, trim, sort, deduplicate, filter, default, or otherwise reconstruct a
  legal domain;
- advance the engine, submit a response, or mutate V2 continuation state;
- read an opponent-private `PlayerObservation`; or
- turn restricted diagnostics or verification evidence into learner data.

An immutable V2 value that cannot be proven complete, perspective-safe, or
internally consistent with its accepted V2 identities is not a trajectory
frame. The recorder rejects the candidate episode; it does not repair the
value or substitute a smaller domain.

## 3. Logical episode model

The logical shape is:

```text
EpisodeEnvelope
├── EpisodeManifest
│   └── PolicyProvenanceEnvelope
├── DecisionRecord[]
└── EpisodeClosure
```

This is a logical value, not a physical storage object. A Phase-3B shard may
contain zero, one, or many envelopes, but cannot change an envelope's
canonical bytes or identities.

### 3.1 EpisodeManifest

`EpisodeManifest` contains these values:

| Field | Visibility and meaning |
| --- | --- |
| trusted trajectory contract ID | fixed to `ocgforge.trusted_trajectory.v1` |
| V2 contract ID | fixed to `ocgforge.episodic_environment.v2` |
| `environment_semantic_id` | existing semantic identity |
| exact V2 environment-identity input bytes | restricted replay/admission input; must be `canonical_environment_identity_bytes(config)` |
| episode identity schema ID | fixed to `ocgforge.episode_identity.v1` |
| `episode_semantic_id` | existing semantic identity |
| exact V2 episode-identity input bytes | restricted replay/admission input; must be `canonical_episode_identity_bytes(config, spec)` |
| `PolicyProvenanceEnvelope` | collection provenance only, defined by `ocgforge.policy_provenance.v1` |
| collection disposition | `CLEAN` or irreversible policy-rejection quarantine |

The two V2 identity-input byte strings are retained so a future verifier can
reconstruct the exact accepted reset configuration and `EpisodeSpec`. They
are not learner-visible features. In particular, the episode input contains
the root seed and must never be exposed through a learner projection, an
unredacted error, or a public hash chosen by this contract.

The semantic resource identities inside V2 environment identity input remain
part of the environment's existing semantic identity. They are not a claim
about the collector's compiler, host, provider, path, time, worker, or
physical storage.

### 3.2 PublicFrameSnapshot

A `PublicFrameSnapshot` is the immutable evidence for one actionable V2
`DecisionFrame`, excluding its live submission token and engine progress
counter. It contains, at minimum:

```text
decision_index
acting_player
PublicEnvironmentObservation
public_observation_digest
EnvironmentDecisionRequest
complete ordered EnvironmentActionCandidate[]
public_candidate_domain_digest
public_semantic_decision_id
```

The snapshot copies the complete ordered V2 request and candidate vector
without changing a byte meaning or order. Its `request.player` MUST equal
`acting_player`; its observation perspective and decision index MUST agree
with the frame; and recomputing the V2 public observation digest,
candidate-domain digest, and public semantic decision ID MUST yield the
stored values.

`submission_token` is live control-plane freshness only. `engine_step_index`
is not part of this snapshot, its digest, or its identity. Neither field is a
canonical trajectory value.

### 3.3 DecisionRecord

One `DecisionRecord` represents exactly one **accepted public semantic
action** at one actionable V2 frame. It is not a framework `(s, a, r, s')`
row. It contains:

| Field | Rule |
| --- | --- |
| `frame` | one `PublicFrameSnapshot` |
| `selected_public_action_key` | occurs exactly once in `frame.request.candidates`, by exact string equality |
| `transition_class` | one of the three codes in section 5 |
| `successor` | one successor reference in section 5 |
| `acting_policy_assignment_id` | resolves to exactly one manifest participant assignment |
| `policy_rng_decision_provenance` | exact pre/post selection provenance under `ocgforge.policy_provenance.v1` |

Candidate vector position is never stored as or accepted as action identity.
The selected key is the V2 `public_action_key`; a vector position may only be
recomputed later as a local derived coordinate after validating the complete
ordered domain.

### 3.4 EpisodeClosure

Every envelope ends in exactly one closure kind:

```text
TERMINAL
INTERRUPTED
FAILED
```

`EpisodeClosure` owns terminal views and any unacted interruption boundary.
It never creates a fake action to represent either value. Section 6 defines
the fields and learner eligibility for each closure.

## 4. Temporal convention

Let `F_t` be one `PublicFrameSnapshot` at global `decision_index = t`. The
only action value recorded is the accepted key `a_t`:

```text
F_t --a_t--> F_(t+1)
```

or:

```text
F_t --a_t--> EpisodeClosure
```

The successor observation is owned once, by `F_(t+1)`. It MUST NOT be copied
into record `t` as `obs_t+1`.

### 4.1 First actionable frame

After an accepted V2 reset, the first actionable `DecisionFrame` is `F_0`.
It becomes the first `DecisionRecord` only if V2 accepts its selected action.
A reset that immediately returns `EpisodeTerminal` has a zero-decision
terminal envelope. A reset that returns `EpisodeInterrupted` or
`EpisodeFailure` has no decision record; sections 6 and 11 govern its
collection disposition.

An environment-factory rejection or `ResetRejected` is not an accepted
episode and creates no `EpisodeEnvelope`. A reset-path `EpisodeFailure` can
form a zero-decision `FAILED` envelope only when V2 has already established
the manifest's environment and episode identities. If V2 cannot establish
those identities, the event is an operational quarantine event outside this
trajectory contract, not a fabricated episode.

### 4.2 Accepted action versus rejection

A V2 `StepRejected` is a non-transition:

```text
no DecisionRecord
no decision-index increment
no successor reference
no authoritative-state mutation
```

The same current actionable frame remains V2-owned. The recorder MUST NOT
record the rejected selection, its submission token, a candidate position, or
a synthetic retry action as canonical gameplay.

If a collector knows that the rejected call came from its policy or actor, it
MUST change the manifest disposition from `CLEAN` to
`QUARANTINED_AFTER_POLICY_REJECTION`. This transition is irreversible for the
episode, including after a later successful retry. The quarantine contains
only the count and public rejection-code classification defined in section
11; it contains no submitted token, raw submitted identity,
or restricted diagnostic. A quarantined episode is not a clean behavior
trajectory and is not normal learner input.

### 4.3 Administrative cancellation while awaiting an action

An administrative V2 interrupt while `F_t` is awaiting an action emits no
`DecisionRecord`. `EpisodeClosure.pending_unacted_frame`, when present, is a
copy of that public frame snapshot without a selected action, token, or
engine-step field. It documents the boundary at which collection stopped; it
is not an action, reward transition, or next `DecisionRecord`. When `N > 0`,
the immediately preceding accepted record may refer to this boundary only
through the `NEXT_FRAME` pending-frame target defined in section 5; that
reference does not turn the frame into an accepted action.

### 4.4 Budget interruption

An engine-process or semantic-action budget can produce `INTERRUPTED` after
an accepted action or before a new actionable frame is published. The record
for an already accepted action remains valid and points to `INTERRUPTED`; the
closure holds no winner, loss, draw, or implicit reward. The exact reason,
run-control budgets, observed counts, and final engine-step evidence are
restricted verification evidence under section 13.1. They are not learner
data or public-gameplay identity inputs.

### 4.5 Envelope sequence invariants

For an envelope with `N` decision records, record `i` MUST have
`decision_index = i` for every `0 <= i < N`. The records are encoded in that
order with no gap, duplicate, reorder, or hidden action. Every nonfinal record
has a `NEXT_FRAME` successor with target kind `NEXT_DECISION_RECORD` to record
`i + 1`. The final record has exactly one successor: either the matching
closure tag, or a `NEXT_FRAME` successor with target kind
`INTERRUPTION_PENDING_UNACTED_FRAME`. The closure count field, where present,
equals `N`.

For an administrative interruption with a pending unacted frame, that frame's
decision index MUST equal `N`. If `N > 0`, record `N - 1` MUST point to it with
the pending-frame `NEXT_FRAME` target; the envelope closure MUST be
`INTERRUPTED`, and no `DecisionRecord` follows. If `N = 0`, the pending frame
is the first actionable frame after reset and has no predecessor record. An
interruption reached directly from an accepted final record has no pending
frame and that record points directly to `INTERRUPTED`.

## 5. Accepted transition and successor codes

The following codes are part of `ocgforge.trusted_trajectory.v1` and use one
`u8`:

| Code | `transition_class` | Required V2 proof |
| ---: | --- | --- |
| 0 | `ATOMIC_ENGINE_RESPONSE` | no V2 continuation view and selected candidate `submits_engine_response = true` |
| 1 | `INTERMEDIATE_CONTINUATION` | V2 continuation view present and selected candidate `submits_engine_response = false` |
| 2 | `FINAL_CONTINUATION_RESPONSE` | V2 continuation view present and selected candidate `submits_engine_response = true` |

Any other combination fails closed. An intermediate continuation is a real
policy decision: it emits one record, does not submit a core response, and
advances to a next public frame with the next global decision index. A final
continuation emits one record; V2 alone submits the one final core response
and publishes the normal successor boundary. The trajectory stores neither
the response bytes nor its hash.

`successor` is a tagged union encoded with one `u8`:

| Code | Tag | Payload |
| ---: | --- | --- |
| 0 | `NEXT_FRAME` | `next_frame_target_kind:u8`, `next_decision_index:u64`, `next_public_semantic_decision_id:string` |
| 1 | `TERMINAL` | no payload |
| 2 | `INTERRUPTED` | no payload |
| 3 | `FAILED` | no payload |

`next_frame_target_kind` uses one `u8`: `0=NEXT_DECISION_RECORD` and
`1=INTERRUPTION_PENDING_UNACTED_FRAME`. For either target kind, the index MUST
equal current `decision_index + 1`.

For `NEXT_DECISION_RECORD`, the ID MUST exactly equal the next record's
`frame.public_semantic_decision_id`; a final record cannot use this target
kind. For `INTERRUPTION_PENDING_UNACTED_FRAME`, the ID MUST exactly equal
`EpisodeClosure.pending_unacted_frame.public_semantic_decision_id`; it is
valid only on the final record of an `INTERRUPTED` envelope with that pending
frame present. No `DecisionRecord` may follow the pending-frame target.

For a closure tag, the one envelope closure MUST have the same kind. No record
may follow a closure successor.

## 6. Closure semantics

### 6.1 TERMINAL

`TERMINAL` means a true V2 engine terminal only. Its public closure projection
contains:

```text
kind = TERMINAL
winner:u8
win_reason:u8
semantic_action_count:u64
last_decision_index: optional u64
terminal_view_player_0: PublicEnvironmentObservation
terminal_view_player_1: PublicEnvironmentObservation
```

Both terminal views are perspective-safe V2 terminal views, serialized with
the existing public-observation codec and its digest. They are present only
when V2 has proved and published them; a missing or invalid required view
fails closed. `semantic_action_count` MUST equal the record count, and
`last_decision_index` is absent exactly when that count is zero.

There is no final action invented to carry a terminal view. A zero-decision
terminal has an empty record vector and this closure.

### 6.2 INTERRUPTED

`INTERRUPTED` means no true engine terminal was claimed. Its public closure
projection contains:

```text
kind = INTERRUPTED
record_count:u64
pending_unacted_frame: optional PublicFrameSnapshot
```

It has no winner, loss, draw, win reason, terminal view, or implicit numeric
reward. The optional pending frame is allowed only for an administrative
cancellation while V2 awaited that exact action; it cannot be present after a
record with a **direct** closure successor. When it has a preceding record,
that record uses the pending-frame `NEXT_FRAME` target from section 5. The V2
interruption reason, run-control values, engine process count, semantic action
count, engine step, and audit prefix are restricted replay/collection evidence,
not learner features or public-gameplay identity inputs.

### 6.3 FAILED

`FAILED` records that correctness, privacy, protocol, or environment
operation was not proven. It contains only:

```text
kind = FAILED
failure_code:u8
failure_stage:u8
mutation_may_have_occurred:u8
record_count:u64
```

It has no winner, draw, loss, reward, terminal view, replay claim, or normal
learner eligibility. Restricted diagnostic text, audit evidence, raw engine
values, and untrusted response material are excluded. A failed candidate
does not receive a `public_gameplay_trajectory_id` or a trusted
`trajectory_record_id`.

## 7. Complete public candidate-domain persistence

Every decision record MUST persist the exact V2 public frame values, not only
their digests. In particular it MUST preserve the complete ordered
`EnvironmentActionCandidate[]` collection and the V2 `EnvironmentDecisionRequest`
that owns it.

Before accepting a record, a Phase-3B implementation MUST verify all of the
following without consulting an internal candidate:

1. `request.kind`, `request.player`, and `acting_player` are valid and agree;
2. the candidate count is nonzero and fits `u32`;
3. every candidate has a valid V2 public action-key descriptor;
4. every candidate's exposed descriptor fields reproduce its
   `public_action_key` exactly;
5. public keys are unique within the one ordered vector;
6. the public candidate-domain digest recomputes from the request kind and
   exact vector order;
7. the public observation digest recomputes from the exact V2 public
   observation bytes;
8. the public semantic decision ID recomputes from its accepted v2 inputs;
9. the selected public key occurs exactly once; and
10. the selected transition and successor satisfy sections 4 through 6.

The implementation MUST fail closed rather than sorting, compacting,
deduplicating, defaulting, or substituting candidates. `source_index` inside
a V2 public candidate is an existing public descriptor field; it is not a
candidate-vector index and does not authorize candidate-position selection.

## 8. Privacy classification

The following table is normative. “Public canonical” means that a value may
appear in the **global** public-gameplay projection. It does not make global
metadata or another participant's values safe learner/policy input.
“Collection provenance only” means it may be retained in the trusted envelope
but never supplied as learner features. “Restricted verification evidence” is
admission/replay evidence outside the public trajectory projection. “Forbidden”
means it is not retained in the canonical learner-visible record or either new
identity input.

| Value | Classification | Rule |
| --- | --- | --- |
| `PublicEnvironmentObservation`, public digest | public canonical | exact accepted V2 value and digest; a participant projection receives only its own records |
| public request, ordered public candidates, public-domain digest | public canonical | complete V2 domain, never a summary substitute |
| `public_semantic_decision_id`, selected `public_action_key` | public canonical | existing V2 public identity and exact selected key; IDs are verifier metadata, not automatic learner features |
| true terminal winner/win reason and perspective-safe terminal views | global public canonical | only under `TERMINAL`; a participant projection may receive only its own terminal view |
| raw `EpisodeEnvelope` | collection/replay container only | never direct learner or policy input; it contains both participants' records and restricted manifest inputs |
| `public_gameplay_trajectory_id` and `trajectory_record_id` | global collection/replay identity | never learner/policy input, feature, action field, reward input, or participant-stream metadata |
| policy artifact, assignment, policy RNG, clean/quarantine disposition | collection provenance only | exact producer attribution, never learner feature |
| V2 reset identity inputs and root seed | restricted replay/admission input | needed for semantic replay, not learner-visible |
| `semantic_gameplay_hash` | restricted verification evidence | EngineTrace v2 includes internal semantics; never an identity input here |
| `final_audit_prefix_hash` / `last_valid_audit_prefix_hash` | restricted verification evidence | audit-chain value, never learner-visible or an identity input |
| `final_response_sha256` | restricted verification evidence | raw-response-derived hash; never public canonical |
| `engine_step_index` | restricted verification evidence | only the final engine-step value is required by `RestrictedReplayEvidence` for an `INTERRUPTED` admission replay; never a v1 trajectory identity input |
| V2 interruption reason, `RunControlEvidence`, and final engine-step evidence | restricted verification evidence | required only by `RestrictedReplayEvidence` for an `INTERRUPTED` admission replay; excluded from the envelope and both new identities |
| cancellation source and arbitrary run-control metadata | forbidden | excluded from every v1 envelope and identity; a future admission contract must classify it independently before any retention |
| hidden card passcode and hidden-card-derived field | forbidden | a hash of a secret is also forbidden unless independently proved safe |
| opponent-private `PlayerObservation` | forbidden | including any attached decision context not in public projection |
| `ActionCandidate.semantic_key`, internal `ocgforge.candidate_domain.v1` digest | forbidden | never projected or substituted for public values |
| protocol `DecisionRequest.decision_id`, `raw_message_hash`, internal `continuation_id` | forbidden | no direct or hashed form |
| raw response bytes, CoreHost state, query buffers, pointers, caches | forbidden | no direct or derived persistence |
| submission token | forbidden | live freshness only |
| restricted diagnostic string/reference | forbidden | failure classification is the only v1 closure diagnostic |
| candidate vector index as action identity | forbidden | may exist only as a regenerated local derived coordinate |

No hash is presumed safe because a source value is not printed. If a proposed
digest has not been independently proven perspective-safe, it is restricted
or forbidden, never silently promoted to public canonical data.

## 9. Identity hierarchy

| Identity | Owner and meaning | Does not include |
| --- | --- | --- |
| `environment_semantic_id` | existing V2 certified environment semantics | collection, storage, model, host, wall time |
| `episode_semantic_id` | existing reset semantics within that environment | selected actions, policy provenance, storage |
| `public_semantic_decision_id` | existing public frame semantics | token, engine step, internal decision/key/digest |
| `public_gameplay_trajectory_id` | ordered **global** public gameplay and global public closure for collection/replay | policy, policy RNG, rejection disposition, storage/build/provider, learner/policy input |
| `trajectory_record_id` | one trusted collection record | physical shard/dataset/object/compression/build/provider/hardware/time, learner/policy input |

`public_gameplay_trajectory_id` is global, not participant-perspective-safe
metadata. It commits the global sequence of records and, on a terminal, both
perspective terminal views. Therefore neither it nor `trajectory_record_id` may
be supplied to a learner, policy, reward adapter, or participant stream.

Required consequences are:

```text
same reset + different accepted public actions
  => same episode_semantic_id
  => different public_gameplay_trajectory_id

same public gameplay + different behavior policy/checkpoint/provenance
  => same public_gameplay_trajectory_id
  => different trajectory_record_id
```

Neither new identity includes an artifact path, storage location, object hash,
compression choice, collector build, compiler, processor, provider, worker,
PID, thread, scheduling order, host name, wall time, or framework.

The raw `EpisodeEnvelope`, `public_gameplay_trajectory_id`, and
`trajectory_record_id` are global collection/replay values. They MUST NOT be
emitted as a learner observation, policy input, candidate field, action key,
reward input, participant-stream metadata, or public-gameplay identity alias.

### 9.1 Public gameplay trajectory identity codec

`public_gameplay_trajectory_id` is the lowercase SHA-256 digest of the exact
following bytes. Strings use `u32be byte_length || UTF-8 bytes`; nested byte
values use `u32be byte_length || bytes`; counts use `u32be`; integer values
use the widths stated below.

| Order | Field | Encoding |
| ---: | --- | --- |
| 0 | identity domain | string `ocgforge.public_gameplay_trajectory_identity.v1` |
| 1 | identity schema | string `ocgforge.public_gameplay_trajectory_identity.v1` |
| 2 | trajectory contract | string `ocgforge.trusted_trajectory.v1` |
| 3 | V2 contract | string `ocgforge.episodic_environment.v2` |
| 4 | environment semantic ID | string |
| 5 | episode identity schema | string `ocgforge.episode_identity.v1` |
| 6 | episode semantic ID | string |
| 7 | public record count | `u32be` |
| 8..n | public decision record projection | one exact `canonical_public_decision_record_bytes` per ascending decision index |
| n+1 | public closure projection | exact `canonical_public_episode_closure_bytes` |

The lexical identity is:

```text
public_gameplay_trajectory.v1.<lowercase hexadecimal SHA-256 digest>
```

Only `TERMINAL` and `INTERRUPTED` envelopes can receive this identity.
`FAILED` cannot. The identity excludes the manifest reset-input bytes, policy
provenance, policy RNG, collection disposition, rejection evidence,
run-control evidence, audit values, response values, and physical provenance.
It nevertheless commits every global public record and, for a terminal, both
terminal perspective views. It is thus a global replay/collection identity,
not a perspective-safe participant learner/policy feature.

### 9.2 Trusted trajectory-record identity codec

`trajectory_record_id` is the lowercase SHA-256 digest of:

| Order | Field | Encoding |
| ---: | --- | --- |
| 0 | identity domain | string `ocgforge.trajectory_record_identity.v1` |
| 1 | identity schema | string `ocgforge.trajectory_record_identity.v1` |
| 2 | trajectory contract | string `ocgforge.trusted_trajectory.v1` |
| 3 | public gameplay trajectory ID | string |
| 4 | canonical policy-provenance envelope | exact nested bytes from `ocgforge.policy_provenance.v1` |
| 5 | policy decision-attribution count | `u32be` |
| 6..n | policy decision attribution | one exact `canonical_policy_decision_attribution_bytes` per ascending global decision index |
| n+1 | collection disposition | exact `u8` plus its canonical quarantine payload from section 11 |

The lexical identity is:

```text
trajectory_record.v1.<lowercase hexadecimal SHA-256 digest>
```

It is issued only for a non-failed, `CLEAN` trusted collection profile. A
quarantined candidate remains auditable but has no trusted record ID until a
future separately versioned quarantine/admission contract decides otherwise.

## 10. Canonical frame and record codec

All values in this section are new exact codec rules. A decoder MUST reject
unknown enum values, invalid presence bytes, malformed UTF-8, lengths beyond
`u32`, unexpected trailing bytes, and any nested value that fails its owning
accepted contract.

### 10.1 Primitive rules

- `u8`, `u16`, `u32`, and `u64` are fixed-width big-endian unsigned values.
- Signed `i32` is its two’s-complement `u32` bit pattern in big-endian order.
- A string is `u32be byte_length || UTF-8 bytes`.
- A bytes value is `u32be byte_length || bytes`.
- A vector is `u32be count || element_0 || ... || element_(count-1)`.
- An optional is `presence:u8`, where `0` means absent and `1` means present
  followed by the value. Other presence values are invalid.
- A boolean is `u8`, where `0` is false and `1` is true. Other values are
  invalid.
- SHA-256 output is the lowercase 64-character hexadecimal digest string.

### 10.2 V2 public candidate codec

`canonical_public_environment_action_candidate_bytes` encodes one already
validated V2 public candidate in this exact order:

| Order | Field | Encoding |
| ---: | --- | --- |
| 0 | candidate schema | string `ocgforge.trusted_trajectory.v1` |
| 1 | action kind | canonical lower-case V2 action-kind token string |
| 2 | public action key | string |
| 3 | typed choice | optional `{kind:u8, value:u64be, response_index:optional u32be}` |
| 4 | source reference | optional `{kind:u8, observation_locator:string}` |
| 5 | target reference | optional `{kind:u8, observation_locator:string}` |
| 6 | phase | optional `u32be` |
| 7 | position | optional `u8` |
| 8 | source index | optional `u32be` |
| 9 | amount | optional signed `i32` as `u32be` bits |
| 10 | continuation operation | string, empty only when V2 permits no continuation operation |
| 11 | submits engine response | boolean |

`PublicChoiceKind` codes are the accepted V2 values: `1=YesNo`,
`2=EffectYesNo`, `3=EffectChoice`, `4=OptionValue`, and
`5=AnnouncementNumber`. `PublicCardReferenceKind` codes are
`0=VisibleCard` and `1=RedactedSlot`. The canonical descriptor represented by
fields 1 and 3 through 10 MUST reproduce field 2 using
`ocgforge.public_action_identity.v1` exactly.

The only valid action-kind tokens in v1 are, in this fixed lexical spelling:
`idle_command`, `battle_command`, `chain`, `option`, `card_selection`,
`announcement`, `place`, `position`, `yes_no`, `pick`, `finish`, `cancel`,
and `assign_amount`. `unsupported` and any future token fail closed.

### 10.3 V2 public continuation codec

When `EnvironmentDecisionRequest.continuation` is present,
`canonical_public_environment_continuation_bytes` encodes, in order:

| Order | Field | Encoding |
| ---: | --- | --- |
| 0 | continuation schema | string `ocgforge.trusted_trajectory.v1` |
| 1 | continuation kind | canonical lower-case token string |
| 2 | continuation step | `u32be` |
| 3 | selected indices | vector of `u32be`, in V2 order |
| 4 | remaining indices | vector of `u32be`, in V2 order |
| 5 | assigned amounts | vector of `u16be`, in V2 order |
| 6 | minimum count | `u32be` |
| 7 | maximum count | `u32be` |
| 8 | target sum | `u32be` |
| 9 | required amount | `u32be` |
| 10 | available mask | `u64be` |
| 11 | selected mask | `u64be` |
| 12 | continuation steps | `u32be` |
| 13 | exact sum | boolean |
| 14 | greater sum | boolean |
| 15 | can finish | boolean |
| 16 | can cancel | boolean |

These are existing V2 public continuation values. They are not an internal
continuation ID, and none of their local indices becomes a global candidate
action identity.

The valid continuation-kind tokens are `unordered`, `tribute`, `sum`, `zone`,
`counter`, `ordering`, and `announce_mask`. The only valid candidate
continuation-operation tokens are empty for an atomic request, or `pick`,
`amount`, `finish`, `cancel`, or `bypass` for a request with this continuation
view. Any other token or combination fails closed.

### 10.4 V2 public decision-request codec

`canonical_public_environment_decision_request_bytes` encodes:

| Order | Field | Encoding |
| ---: | --- | --- |
| 0 | request schema | string `ocgforge.trusted_trajectory.v1` |
| 1 | request kind | canonical lower-case V2 decision-kind token string |
| 2 | player | `u8` (`0` or `1`) |
| 3 | candidates | vector of exact candidate bytes in authoritative V2 order |
| 4 | continuation | optional exact continuation bytes |

The candidate vector is not sorted or normalized. Its action-key vector MUST
produce the recorded `ocgforge.public_candidate_domain.v1` digest.

The only valid decision-kind tokens in v1 are: `idle_command`,
`battle_command`, `chain`, `option`, `card_selection`, `tribute`, `sum`,
`place`, `counter`, `ordering`, `announcement`, `unselect_card`, `position`,
and `yes_no`. `unsupported` and any future token fail closed.

### 10.5 Public frame snapshot codec

`canonical_public_frame_snapshot_bytes` encodes:

| Order | Field | Encoding |
| ---: | --- | --- |
| 0 | frame schema | string `ocgforge.trusted_trajectory.v1` |
| 1 | V2 contract ID | string `ocgforge.episodic_environment.v2` |
| 2 | episode semantic ID | string |
| 3 | public semantic decision ID | string |
| 4 | decision index | `u64be` |
| 5 | acting player | `u8` (`0` or `1`) |
| 6 | public observation | exact nested `canonical_public_environment_observation_bytes` |
| 7 | public observation digest | string |
| 8 | public decision request | exact nested request bytes from 10.4 |
| 9 | public candidate-domain digest | string |

The token, internal semantic decision ID, internal candidate digest,
engine-step index, raw-message hash, continuation ID, and response material
are absent by construction.

### 10.6 Public decision-record projection codec

`canonical_public_decision_record_bytes` encodes:

| Order | Field | Encoding |
| ---: | --- | --- |
| 0 | record schema | string `ocgforge.trusted_trajectory.v1` |
| 1 | public frame snapshot | exact nested frame bytes from 10.5 |
| 2 | selected public action key | string |
| 3 | transition class | `u8` from section 5 |
| 4 | successor | exact tagged successor bytes from section 5 |

This is the entirety of a record's public gameplay projection. Its canonical
collection-provenance projection is
`canonical_policy_decision_attribution_bytes`, encoded as:

| Order | Field | Encoding |
| ---: | --- | --- |
| 0 | attribution schema | string `ocgforge.policy_provenance.v1` |
| 1 | acting policy assignment ID | string |
| 2 | policy RNG decision provenance | exact nested `ocgforge.policy_rng_decision_provenance.v1` bytes |

One attribution is required for each public record, in global decision-index
order. The record identity includes this ordered attribution vector, not the
public gameplay identity.

### 10.7 Episode-closure codec

`canonical_episode_closure_bytes` begins with:

| Order | Field | Encoding |
| ---: | --- | --- |
| 0 | closure schema | string `ocgforge.trusted_trajectory.v1` |
| 1 | closure kind | `u8`: `0=TERMINAL`, `1=INTERRUPTED`, `2=FAILED` |

The `TERMINAL` payload is, in order: winner `u8`, win reason `u8`, semantic
action count `u64be`, optional last decision index, player-0 terminal public
observation bytes and digest, then player-1 terminal public observation bytes
and digest. Both views are global collection/replay data; section 12.2 limits
a participant learner projection to its own view. The `INTERRUPTED` payload is,
in order: record count `u64be`, then optional pending unacted public frame
bytes. Its exact interruption reason/run-control evidence is not nested here;
it is the restricted Phase-3B companion in section 13.1. The `FAILED` payload
is, in order: failure code `u8`, failure stage `u8`, mutation-may-have-occurred
boolean, then record count `u64be`.

`FailureCode` uses the exact V2 codes: `0=RETRY_FAILURE`, `1=CORE_ERROR`,
`2=UNSUPPORTED_PROTOCOL`, `3=MALFORMED_PROTOCOL`,
`4=INCOMPLETE_CANDIDATES`, `5=DUPLICATE_CANDIDATES`,
`6=RESPONSE_INCONSISTENCY`, `7=CANDIDATE_OBSERVATION_INCONSISTENCY`,
`8=PRIVACY_INVARIANT`, `9=PUBLIC_FRAME_INVARIANT`,
`10=INVALID_AUTHORITATIVE_STATE`, `11=RESPONSE_SUBMISSION_FAILURE`,
`12=OBSERVATION_FAILURE`, `13=INTERNAL_DOMAIN_DIVERGENCE`,
`14=TOKEN_NAMESPACE_EXHAUSTED`, and `15=RESOURCE_IDENTITY_MISMATCH`.
`FailureStage` uses `0=VALIDATION`, `1=CONSTRUCTION`, `2=ADVANCE`,
`3=PROJECTION`, `4=ACTION`, `5=INTERRUPTION`, and `6=TEARDOWN`.

`canonical_public_episode_closure_bytes` accepts only the `TERMINAL` and
`INTERRUPTED` encodings above and is byte-for-byte identical to their
`canonical_episode_closure_bytes` form. It rejects `FAILED`; a failed
envelope cannot produce a public gameplay or trusted record identity.

### 10.8 Canonical episode-envelope codec

`canonical_episode_envelope_bytes` is the complete logical codec for Phase
3B. Its exact order is:

| Order | Field | Encoding |
| ---: | --- | --- |
| 0 | envelope schema | string `ocgforge.trusted_trajectory.v1` |
| 1 | episode manifest | exact nested manifest bytes from section 11 |
| 2 | record count | `u32be` |
| 3..n | decision record | exact collection-provenance record bytes, ascending decision index |
| n+1 | closure | exact closure bytes |

It has no physical-storage header, compression marker, object URI, build
provenance, or admission receipt. Those belong to a later contract.

## 11. Manifest, disposition, and collection-record codec

`collection_disposition` uses one `u8`:

| Code | Value | Payload |
| ---: | --- | --- |
| 0 | `CLEAN` | none |
| 1 | `QUARANTINED_AFTER_POLICY_REJECTION` | rejection count `u32be`, then that many public V2 rejection-code tokens in observed order |

Only V2 rejection-code tokens may appear in the quarantine payload. A token,
submitted key, submitted episode ID, submitted public decision ID, and
diagnostic string are explicitly excluded. Any duplicate or impossible
disposition transition fails closed.

The only valid quarantine tokens are `INCOMPATIBLE_CONTRACT`,
`INVALID_LIFECYCLE`, `WRONG_EPISODE`, `STALE_SUBMISSION_TOKEN`,
`WRONG_PUBLIC_SEMANTIC_DECISION`, `UNKNOWN_PUBLIC_ACTION_KEY`,
`PUBLIC_ACTION_DOMAIN_DIVERGENCE`, and `UNSUPPORTED_INTERRUPTION_REASON`,
spelled exactly as shown. Their ordering records only the collector-observed
rejection classifications; it never records the rejected selection or token.

`canonical_episode_manifest_bytes` encodes:

| Order | Field | Encoding |
| ---: | --- | --- |
| 0 | manifest schema | string `ocgforge.trusted_trajectory.v1` |
| 1 | V2 contract ID | string `ocgforge.episodic_environment.v2` |
| 2 | environment semantic ID | string |
| 3 | exact environment identity input | bytes from existing `canonical_environment_identity_bytes` |
| 4 | episode identity schema | string `ocgforge.episode_identity.v1` |
| 5 | episode semantic ID | string |
| 6 | exact episode identity input | bytes from existing `canonical_episode_identity_bytes` |
| 7 | policy provenance envelope | exact nested `ocgforge.policy_provenance.v1` bytes |
| 8 | collection disposition | exact bytes above |

Before retaining a manifest, a recorder MUST verify that the SHA-256 of the
stored environment identity input equals `environment_semantic_id`, that the
stored episode identity input equals the accepted V2 codec for that exact
environment input, and that its SHA-256 equals `episode_semantic_id`. A
mismatch is not a repairable metadata error.

`canonical_collection_decision_record_bytes` encodes:

| Order | Field | Encoding |
| ---: | --- | --- |
| 0 | collection-record schema | string `ocgforge.trusted_trajectory.v1` |
| 1 | public decision record | exact nested bytes from section 10.6 |
| 2 | policy decision attribution | exact nested bytes from section 10.6 |

The assignment must resolve to the record's acting player and to a declared
manifest assignment.

## 12. Canonical versus derived boundary

Canonical trusted trajectory values are limited to public episodic frames,
complete ordered public domains, selected public action keys, accepted
transition semantics, closure, and exact policy/participant provenance.

All of the following are derived and MUST be regenerable from canonical
trusted data plus an explicit versioned derivation configuration:

- numeric rewards, shaped rewards, discounts, returns, GAE, advantages,
  V-trace targets, importance weights, and normalization statistics;
- tensors, tokenization, entity embeddings, candidate embeddings, candidate
  local indexes, padding, masks, and train/validation split materializations;
- recurrent chunks, burn-in windows, TBPTT windows, overlap, hidden-state
  caches, replay priorities, replay-buffer indexes, and sampling weights;
- Parquet, Arrow, RLDS, Minari, RLlib, Reverb, TorchRL, PyTorch, Hugging Face,
  Weights & Biases, Kaggle, and other framework/export projections.

Derived data MUST NOT alter environment, episode, public-frame, candidate,
selected-action, closure, public-gameplay, or trusted-record identity.

### 12.1 Reward boundary

Environment truth is limited to winner, win reason, and terminal/interrupted/
failed status. It has no authoritative numeric reward. A future versioned
`RewardAdapter` or `RewardView` may derive `+1/-1/0`, discount, shaping, or
returns only from perspective-safe canonical data. It MUST NOT consume hidden
engine state.

Changing reward policy MUST NOT change the environment ID, episode ID, public
frame ID, candidate domain, selected action, outcome, or public gameplay
trajectory ID. `INTERRUPTED` and `FAILED` have no implicit numeric reward.

### 12.2 Recurrent POMDP streams and participant learner projection

The raw global `EpisodeEnvelope` MUST NEVER be direct learner or policy input.
A future learner may derive:

```text
AgentDecisionStream(episode, participant_assignment)
```

It is a participant-scoped learner projection, not a view of the raw envelope.
For records made by that assignment, it may contain only the acting public
observation, complete ordered request/candidate domain, selected public action
key, and accepted transition class, in global `decision_index` order. It must
exclude successor references, verifier IDs/digests, the record's collection
provenance, and every other participant's record.

When the global closure is `TERMINAL`, this projection may contain only the
terminal public observation for that assignment's player. It MUST NOT contain
the opponent terminal view. When an `INTERRUPTED` closure has a pending unacted
frame, the projection may contain it only when the frame's acting player
resolves to that participant assignment; it remains a non-action boundary, not
a training transition.

The projection MUST exclude V2 reset/replay identity bytes and root seed, raw
`EpisodeEnvelope`, all global trajectory/record identities, and verifier
identity/digest metadata unless a future perspective-safety contract proves a
specific value safe for that use. It MUST NOT use a global identity as a
feature or indirect join key.

Player 0 and player 1 streams MUST NOT be concatenated merely because their
global frames are adjacent. Two participant assignments using the same
checkpoint still own distinct recurrent state. Burn-in, sequence length,
overlap, padding, and hidden-state data are derived values, not canonical
fields.

## 13. Replay and Phase-3B handoff

A future semantic replay verifies, from restricted V2 reset inputs and public
evidence, the environment/episode identities, each regenerated ordered public
frame, its public observation digest, complete ordered public domain, selected
public action-key sequence, transition class, successor, and closure. Stored
public observations are evidence, not engine input. A `TERMINAL` closure is
verified from that replay. An `INTERRUPTED` envelope proves its public action
prefix but cannot alone prove why V2 stopped; it requires the exact restricted
companion in section 13.1 before Phase 3B may claim interruption replay
verification. Replay never requires a submission token, internal semantic key,
candidate vector position, machine identity, wall clock, worker order, or
re-execution of neural inference.

### 13.1 Restricted interruption replay evidence

`RestrictedReplayEvidence` is a Phase-3B admission-replay companion, not an
`EpisodeEnvelope` field, learner value, reward input, or public/trusted-record
identity input. It is required exactly when the envelope closure is
`INTERRUPTED`; it is invalid for `TERMINAL` and `FAILED` envelopes.

`canonical_restricted_replay_evidence_bytes` encodes these exact fields:

| Order | Field | Encoding |
| ---: | --- | --- |
| 0 | evidence schema | string `ocgforge.restricted_replay_evidence.v1` |
| 1 | V2 contract ID | string `ocgforge.episodic_environment.v2` |
| 2 | episode semantic ID | string |
| 3 | closure kind | `u8`, exactly `1=INTERRUPTED` |
| 4 | interruption reason | `u8`: `0=ENGINE_PROCESS_BUDGET`, `1=SEMANTIC_ACTION_BUDGET`, `2=ADMINISTRATIVE_CANCEL` |
| 5 | engine-process budget | `u64be` |
| 6 | semantic-action budget | `u64be` |
| 7 | observed engine-process count | `u64be` |
| 8 | observed semantic-action count | `u64be` |
| 9 | final engine-step index | `u64be` |

Fields 4 through 9 MUST exactly equal the accepted V2 `EpisodeInterrupted`
reason, `RunControlEvidence`, and final engine-step value. `cancellation.source`,
arbitrary run-control metadata, audit hashes, response hashes, raw response
bytes, and raw engine state are not fields of this evidence.

For an interrupted candidate, Phase 3B semantic admission replay MUST apply
the exact budgets, reproduce the selected public action prefix, and compare the
reason, observed process/action counts, final engine-step index, closure, and
pending-frame semantics. Any mismatch or absent evidence rejects admission.
Before an immutable `AdmissionReceipt` is issued, Phase 3B MUST
cryptographically bind the exact restricted-evidence bytes to the verified
candidate shard. This contract fixes that semantic binding requirement but not
its physical object, hashing, packaging, or publication mechanism.

Phase 3B owns and is explicitly deferred from this contract:

```text
TrajectoryShard
CandidateShardManifest
RestrictedReplayEvidence storage and candidate-shard binding
AdmissionReceipt
DatasetManifest
physical storage layout and compression
content-addressed object packaging and atomic publication
remote upload, Kaggle, Colab, and cloud transport
Parquet, Arrow, and other export files
```

The handoff is fixed only at this semantic level:

```text
canonical episode
+ RestrictedReplayEvidence when closure == INTERRUPTED
  -> candidate shard
  -> structural / schema / privacy validation
  -> semantic replay verification
  -> immutable AdmissionReceipt
  -> DatasetManifest
```

Phase 3B must preserve this contract's bytes and meanings; it must not widen
the public projection, repurpose either identity, or admit an interrupted
candidate without its exact restricted replay evidence.

## 14. Versioning and failure rules

Any change to an enum value, field order, nested byte encoding, visibility
classification, digest input, identity prefix, ordering rule, or semantic
meaning requires a new contract or identity-domain version. A v1 decoder MUST
reject unknown versions and invalid enum values. It MUST NOT use a compatibility
alias, a default missing field, an inferred old value, or a silent migration.

This contract does not change gameplay semantics, rules, decks, the rules
bundle, Decision Protocol legality or candidate ordering, PlayerObservation
v1, PublicEnvironmentObservation v1, public action identity, EpisodicEnvironment
V2, or EngineTrace v2.

## 15. Non-goals

This contract does not implement or authorize a trajectory recorder, shard
reader/writer, admission receipt, object store, remote actor, Kaggle job,
Teacher, reward implementation, tensorization, embeddings, behavior cloning,
PPO, IMPALA/V-trace, R2D2, self-play, league training, neural network, or
framework dependency.
