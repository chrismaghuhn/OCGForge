# OCGForge Episodic Environment Contract v3

**Contract ID:** `ocgforge.episodic_environment.v3`

**Status:** Normative Stage-A successor contract; implemented by the V3 public
projection slice and pending independent documentation review.

V3 is the incompatible public-environment successor required by the typed
native Select/Unselect operation. It preserves the historical V2 runtime and
its V1 public identity codecs. V3 does not change engine legality, internal
protocol identities, observation privacy rules, continuation semantics, or
response bytes.

## 1. Versioned entry points

The two certified configuration entry points are deliberately explicit:

```text
CertifiedEnvironmentConfig::canonical()
    -> ocgforge.episodic_environment.v2
    -> ocgforge.environment_identity.v2
    -> public_action/domain/decision identity V1

CertifiedEnvironmentConfig::canonical_v3()
    -> ocgforge.episodic_environment.v3
    -> ocgforge.environment_identity.v3
    -> public_action/domain/decision identity V2
```

`canonical_v3()` derives the certified rules, decks, scripts, database,
format, duel-mode, and internal contract values from the existing canonical
configuration. It changes only these public successor bindings before
recomputing `environment_semantic_id`:

```text
contract_id                         = ocgforge.episodic_environment.v3
public_action_identity_schema_id    = ocgforge.public_action_identity.v2
public_candidate_digest_schema_id   = ocgforge.public_candidate_domain.v2
public_decision_identity_schema_id  = ocgforge.public_semantic_decision_identity.v2
environment identity schema         = ocgforge.environment_identity.v3
```

The historical `canonical()` V2 identity and its canonical bytes remain
unchanged.

## 2. Environment identity V3

The V3 environment identity uses the existing primitive encoding: strings are
`u32be byte_length || UTF-8 bytes`, vectors carry a `u32be` count, and integer
widths are explicit. Its exact field order is:

| Order | Field | Value or meaning |
| ---: | --- | --- |
| 0 | hash domain | string `ocgforge.environment_identity.v3` |
| 1 | identity schema | string `ocgforge.environment_identity.v3` |
| 2 | episodic contract | string `ocgforge.episodic_environment.v3` |
| 3 | Decision Protocol contract | string `ocgforge.decision_protocol.v1` |
| 4 | observation contract | string `ygo.player_observation.v1` |
| 5 | internal action identity | string `ocgforge.action_identity.v1` |
| 6 | public action identity | string `ocgforge.public_action_identity.v2` |
| 7 | internal candidate-domain identity | string `ocgforge.candidate_domain.v1` |
| 8 | public candidate-domain identity | string `ocgforge.public_candidate_domain.v2` |
| 9 | episode identity | string `ocgforge.episode_identity.v1` |
| 10 | internal semantic-decision identity | string `ocgforge.semantic_decision_identity.v1` |
| 11 | public semantic-decision identity | string `ocgforge.public_semantic_decision_identity.v2` |
| 12 | public observation | string `ocgforge.public_environment_observation.v1` |
| 13 | public safe state | string `ocgforge.public_safe_state.v1` |
| 14 | seed derivation | unchanged V2 value |
| 15 | rules bundle | unchanged certified rules-bundle identity |
| 16 | Core API version | unchanged certified value |
| 17 | ocgcore commit | unchanged certified value |
| 18 | resolved ocgcore checkout | unchanged certified value |
| 19 | Core patchset identity | unchanged certified value |
| 20 | Core patchset digest | unchanged certified value |
| 21 | CardScripts commit | unchanged certified value |
| 22 | resolved CardScripts checkout | unchanged certified value |
| 23 | database commit | unchanged certified value |
| 24 | resolved database checkout | unchanged certified value |
| 25 | database artifact digest | unchanged certified value |
| 26 | format | unchanged certified value |
| 27 | duel mode | unchanged certified value |
| 28 | duel flags | `u64be`, unchanged certified value |
| 29 | locked-deck vector | unchanged ordered identity pairs |
| 30 | required-script closure | unchanged certified identity |

No action key, raw message hash, continuation ID, hidden identity, pointer,
host path, or runtime timing enters this identity.

`ocgforge.episode_identity.v1` remains the episode codec. Its canonical bytes
and field order are unchanged; its parent `environment_semantic_id` is the V3
environment identity when a V3 config is used.

## 3. Public V3 DTO

V3 uses the existing value-owned public frame and request types with one new
public candidate field:

```text
EnvironmentActionCandidate {
    action_kind
    public_action_key
    optional typed choice
    optional safe source/target references
    optional safe phase/position/source-index/amount values
    optional safe continuation operation
    submits_engine_response
    card_selection_operation: None | Select | Unselect
}
```

The in-memory field is value-owned and defaults to `None`. Its placement at
the end of the C++ DTO preserves existing positional aggregate initializers;
the canonical V2 action identity still encodes it immediately after
`action_kind` as defined by `public-action-identity-v2.md`.

V3 retains the existing public observation projection:

```text
PublicEnvironmentObservation
    -> public_environment_observation.v1
    -> public_safe_state.v1
```

No internal `PlayerObservation` decision identity, continuation ID, raw
response, internal semantic key, or hidden card identity is published.

## 4. Projection semantics

The internal decoder metadata is authoritative for the public operation:

```text
request.kind == UnselectCard
AND candidate.action_kind == CardSelection

    internal Select
        -> public Select
        -> public_action_identity.v2 operation code 1

    internal Unselect
        -> public Unselect
        -> public_action_identity.v2 operation code 2

    internal None or unknown
        -> fail closed
```

For all other request/candidate combinations:

```text
internal operation MUST be None
public operation = None
```

The complete candidate count, membership, and protocol-provided order are
preserved. A failed safe projection fails the complete frame; it never drops
an offending candidate or selects a replacement.

V3 candidate keys, domain digests, and public decision identities are built
only with:

```text
public_action_key_v2
public_candidate_domain_digest_v2
public_semantic_decision_id_v2
```

The private frame-local resolver is the V2 resolver. It maps exactly one
current V2 public key to exactly one internal semantic key and fails closed on
unknown, duplicate, malformed, or ambiguous bindings.

## 5. Active-config facade identity

Every output contract uses the active configuration's contract ID. In a V3
session, the following all carry
`ocgforge.episodic_environment.v3`:

```text
DecisionFrame
StepRejected
EpisodeFailure
EpisodeTerminal
EpisodeInterrupted
```

Reset, step, and interrupt validation compare the submitted contract ID to
the active config contract. A V2 contract submitted to a V3 session, or a V3
contract submitted to a V2 session, is rejected before authoritative
mutation.

V3 reset uses an `EpisodeSpec` whose `contract_id` is V3. The engine process,
semantic-action, administrative-cancellation, lifecycle, token, terminal,
interruption, and fail-closed semantics remain those already accepted for the
episodic facade.

## 6. V2/V3 coexistence and rejection matrix

```text
canonical V2 config + V2 EpisodeSpec
    -> V2 frame, V1 public action/domain/decision codecs

canonical V3 config + V3 EpisodeSpec
    -> V3 frame, V2 public action/domain/decision codecs

mixed contract/config/public identity fields
    -> factory rejection before a session is created

V1 public key/domain/decision value in a V2 frame
    -> historical accepted version

V2 public key/domain/decision value in a V3 frame
    -> accepted successor version

V1 public key/domain/decision value in a V3 frame
    -> reject

V2 public key/domain/decision value in a V2 frame
    -> reject
```

The V2 environment does not reinterpret its historical public DTO or identity
bytes. It emits the new candidate field as its default `None` and keeps using
the V1 public identity functions; V3 is the only path that exposes the
Select/Unselect operation. The `key/domain/decision` wording above describes
the frame's version binding; domain and decision identities are not separate
submit-able action inputs.

## 7. Retained contracts and deferred migrations

Unchanged and retained:

```text
ocgforge.episode_identity.v1
ygo.player_observation.v1
ocgforge.public_environment_observation.v1
ocgforge.public_safe_state.v1
ocgforge.decision_protocol.v1
ocgforge.action_identity.v1
ocgforge.candidate_domain.v1
ocgforge.semantic_decision_identity.v1
ygo.engine_trace.v2
```

This V3 implementation does not migrate or relabel:

```text
trusted trajectory
restricted replay evidence
collection evidence bundles
admission receipts
dataset manifests/identities
model logical/encoded/batch inputs
Task7 materialization
Teacher continuation remediation
RUN_A
RUN_B
training
```

Those successors require their own versioned implementation and acceptance
closure. Existing V2 trajectory/model artifacts remain historical and are not
silently reinterpreted as V3.

## 8. Verification

The focused V3 gate is:

```text
tests/episodic/episodic_environment_v3_public_operation_test.cpp
```

It proves canonical V3 factory acceptance, V3 frame projection, selected and
unselected operation mapping, Cancel=`None`, V2 public action/domain/decision
bindings, fail-closed inconsistent-operation cases, active-config frame/
reject/interruption IDs, and fresh-process deterministic output.

Historical V2 and public-safe regression gates remain required. A known
pre-existing `trajectory_replay_admission_test` baseline issue is outside
this Stage-A contract and must not be repaired here.
