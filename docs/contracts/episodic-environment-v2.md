# OCGForge Episodic Environment Contract v2

**Contract ID:** `ocgforge.episodic_environment.v2`
**Status:** Accepted public-identity successor under ADR-0004; production implementation is not included in this prerequisite.
**Owning layer:** `environment` / shared `EpisodeDriver` orchestration above CoreHost, protocol, observation, and trace layers.

This contract is the public reset/step successor required by the
perspective-safe action-identity boundary. It defines the v2 delta from
`ocgforge.episodic_environment.v1`; it does not redefine the rules engine,
Decision Protocol legality, `PlayerObservation`, or `EngineTrace v2`.

## 1. Why v2 exists

V1 names the public selected action `semantic_key` and hashes the internal
semantic-key vector as `ocgforge.candidate_domain.v1`. The observed hidden-card
case proves that those internal identities are not automatically policy-safe.
Changing their meaning in v1 would silently change a public contract and
replay format. V2 therefore makes the public/private boundary explicit:

```text
internal ActionCandidate.semantic_key
        -> EpisodeDriver

internal candidate
        -> perspective-safe projection
        -> EnvironmentActionCandidate.public_action_key
        -> policy caller
```

The v1 contract remains valid for its existing internal/public meaning and is
not an alias for v2.

## 2. Versioned identity set

| Surface | V2 value | Role |
| --- | --- | --- |
| episodic public contract | `ocgforge.episodic_environment.v2` | public reset/step DTO and replay contract |
| environment semantic identity | `ocgforge.environment_identity.v2` | binds the v2 public and retained internal identity domains |
| episode semantic identity | `ocgforge.episode_identity.v1` | unchanged codec, consumes the v2 environment ID |
| internal action identity | `ocgforge.action_identity.v1` | unchanged `semantic_key` ownership |
| public action identity | `ocgforge.public_action_identity.v1` | safe per-candidate key |
| internal candidate domain | `ocgforge.candidate_domain.v1` | unchanged internal ordered-key digest |
| public candidate domain | `ocgforge.public_candidate_domain.v1` | ordered public-key digest |
| internal semantic decision identity | `ocgforge.semantic_decision_identity.v1` | unchanged internal identity |
| public semantic decision identity | `ocgforge.public_semantic_decision_identity.v1` | safe public frame identity |
| observation | `ygo.player_observation.v1` | unchanged perspective-safe state |
| trace | `ygo.engine_trace.v2` | unchanged internal/audit trace |

Unknown or incompatible v2 IDs fail closed before authoritative mutation.
V1 callers and v1 persisted replay inputs are not silently accepted as v2.

## 3. Environment identity v2 delta

`environment_semantic_id` uses the accepted primitive encoding and SHA-256.
Its v2 field order is:

| Order | Field |
| ---: | --- |
| 0 | hash domain `ocgforge.environment_identity.v2` |
| 1 | identity schema `ocgforge.environment_identity.v2` |
| 2 | episodic contract `ocgforge.episodic_environment.v2` |
| 3 | Decision Protocol contract `ocgforge.decision_protocol.v1` |
| 4 | observation contract `ygo.player_observation.v1` |
| 5 | internal action identity `ocgforge.action_identity.v1` |
| 6 | public action identity `ocgforge.public_action_identity.v1` |
| 7 | internal candidate domain `ocgforge.candidate_domain.v1` |
| 8 | public candidate domain `ocgforge.public_candidate_domain.v1` |
| 9 | episode identity `ocgforge.episode_identity.v1` |
| 10 | internal decision identity `ocgforge.semantic_decision_identity.v1` |
| 11 | public decision identity `ocgforge.public_semantic_decision_identity.v1` |
| 12..28 | the unchanged seed, rules-bundle, Core API, patchset, CardScripts, database, format, duel-mode, flags, locked-deck, and required-script-closure fields owned by episodic v1 |

The retained internal schema IDs pin the trusted internal path; they do not
make any per-frame internal key public. No candidate key, raw message hash,
continuation ID, or hidden card identity is an environment-identity input.

## 4. Public frame and candidate shape

V2 uses value-owned public DTOs distinct from the internal protocol structs:

```text
EnvironmentActionCandidate {
    action_kind
    public_action_key
    optional safe source/target references
    optional safe phase/position/source-index/amount values
    optional safe continuation operation
    submits_engine_response
}

EnvironmentDecisionRequest {
    kind
    player
    complete ordered EnvironmentActionCandidate vector
    safe continuation view when required
}

DecisionFrame {
    contract_id = ocgforge.episodic_environment.v2
    episode_semantic_id
    public_semantic_decision_id
    submission_token                 // control-plane only
    decision_index
    engine_step_index                // public only if independently safe in the v2 DTO
    acting_player
    PlayerObservation
    EnvironmentDecisionRequest
    public_candidate_domain_digest
}
```

The `engine_step_index` line is retained only where its public use is
explicitly proven safe; it is never an input to the public semantic decision
ID. V2 must not expose `semantic_key`, raw response bytes, raw message hash,
internal continuation ID, internal candidate digest, pointer/cache identity,
or hidden card identity in a public DTO.

The complete public candidate vector has exactly the same count, membership,
and authoritative order as the internal protocol vector. A failed safe
projection fails the frame; it never removes an offending candidate.

## 5. Public selection and internal advancement

V2 `ActionSelection` is:

```text
ActionSelection {
    contract_id = ocgforge.episodic_environment.v2
    episode_semantic_id
    public_semantic_decision_id
    submission_token
    public_action_key
}
```

The fixed rejection order remains contract/version, lifecycle, episode,
submission token, public semantic decision ID, then membership in the
complete current public domain. After those checks, the frame-local internal
binding resolves the public key to exactly one current internal candidate and
passes only that candidate's existing `semantic_key` to `EpisodeDriver`.

Collision, unknown key, or public/internal-domain divergence is a structured
fail-closed result before normal public advancement. The internal driver
continues to own exact response construction, continuation transitions, core
processing, and all EngineTrace v2 semantics.

## 6. Identity and replay rules

The public frame uses `public_candidate_domain_digest` and
`public_semantic_decision_id` from
`docs/contracts/public-action-identity-v1.md`. The internal domain digest,
protocol decision ID, raw message hash, and continuation identity remain
private/internal values.

Public replay records ordered public action keys and resolves them against the
regenerated current public domain. It never records or submits an internal
semantic key. Internal protocol/trace replay remains allowed to use internal
semantic keys under their unchanged contracts.

## 7. Acceptance boundary for this prerequisite

This contract is a normative prerequisite, not an implementation claim. The
focused codec test must prove the paired-world property, canonical golden
vectors, ordered public-domain hashing, and fail-closed collision behavior.
The later Phase-2 implementation must additionally prove complete projection,
frame-local unique mapping, replay, lifecycle, zero-mutation rejection, and
the existing G01-G32 regression/privacy gates.

## 8. Explicit non-goals

V2 does not implement the environment, change legality or ordering, change
observation visibility, change EngineTrace v2, change rules/decks, or add
trajectory/ML/model behavior. Production Phase 2 remains blocked until this
prerequisite is merged.
