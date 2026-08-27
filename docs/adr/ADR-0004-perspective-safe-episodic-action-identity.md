# ADR-0004: Define a perspective-safe public episodic action identity

## Status

Accepted

## Context

The accepted internal action identity is useful and correct for the trusted
protocol/driver path, but it is not automatically a policy-safe identity. A
legal candidate observed after a long accepted action prefix had the internal
key:

```text
card.0.3.14821890.0.8.0
```

The card was hidden from the acting player. `PlayerObservation` correctly
redacted its identity, while forwarding the internal key would expose the
hidden passcode. The existing Phase-2 public facade therefore correctly
failed closed at the privacy projection boundary.

Dropping that candidate would violate complete-domain semantics. Publishing
the passcode would violate information safety. Rewriting the internal key
would change the accepted Action Identity and EngineTrace/replay semantics.

## Decision

Introduce a separate policy-facing identity contract:

```text
internal ActionCandidate.semantic_key
        -> EpisodeDriver

internal candidate
        -> privacy projection
        -> EnvironmentActionCandidate.public_action_key
        -> Agent / Teacher / Model
```

The exact versioned domains are:

```text
ocgforge.public_action_identity.v1
ocgforge.public_candidate_domain.v1
ocgforge.public_semantic_decision_identity.v1
ocgforge.public_environment_observation.v1
ocgforge.public_safe_state.v1
ocgforge.episodic_environment.v2
ocgforge.environment_identity.v2
```

The normative details are owned by
`docs/contracts/public-action-identity-v1.md` and
`docs/contracts/episodic-environment-v2.md`; the public observation and its
nested safe-state codec are owned by
`docs/contracts/public-environment-observation-v1.md`.

The following rules are binding:

1. Internal `semantic_key` is not automatically public/policy-safe.
2. Episodic v2 exposes `public_action_key`, never the internal key as the
   policy selection field.
3. A public key contains only data visible to the acting player's current
   `PlayerObservation`, and the public frame emits only the separate
   `PublicEnvironmentObservation` projection.
4. A hidden card is represented only by a perspective-safe current locator,
   such as its visible zone/slot; its passcode and physical identity are not
   included.
5. Every public key maps deterministically to exactly one current internal
   candidate. Unknown, colliding, ambiguous, or unproven mappings fail
   closed.
6. The complete legal candidate domain is preserved exactly. Privacy does not
   filter, truncate, fabricate, sort, or default candidates.
7. The public candidate-domain digest hashes the ordered public-key vector.
8. Public semantic decision identity binds the safe
   `public_observation_digest` and does not transitively include raw message
   hashes, internal continuation IDs, internal candidate digests, protocol
   decision identity, or hidden card identities.
9. Public replay records public keys and resolves them against the regenerated
   public domain. Internal protocol/trace replay remains internal.
10. The internal Action Identity, Decision Protocol legality, and
    `PlayerObservation v1` field/byte semantics remain unchanged. `EngineTrace
    v2`, rules, and decks remain unchanged; the public environment uses the
    separate sanitized observation contract.

### Policy-facing role correction

`PlayerObservation v1` remains the authoritative observation-layer source
record and retains its existing fields, canonical bytes, and attached
`DecisionContext` semantics for internal integration. It is no longer a
policy-facing episodic wire value: its attached internal decision identities
cannot safely cross that boundary. `PublicEnvironmentObservation v1`
supersedes direct policy use through an explicit security-boundary projection,
and `PublicEnvironmentObservation v1` owns the policy-facing safe-state bytes
and digest. This is a correction to cross-boundary usage, not a silent change
to the v1 observation schema or serializer.

## Alternatives considered

### Keep `semantic_key` as the public key

Rejected. The observed hidden opponent-card key proves that the internal
grammar can carry information outside the acting player's observation.

### Remove only hidden-card candidates

Rejected. Candidate completeness is an invariant. A legal candidate cannot be
made to disappear to satisfy a privacy check.

### Replace the passcode with a candidate index or opaque hash alias

Rejected. An index is not stable across regenerated domains, and an alias
without an independently specified safe descriptor does not prove a unique
mapping. A public key must be a deterministic encoding of audited visible
semantics and must resolve through the current domain.

### Change the internal Action Identity or EngineTrace v2

Rejected. The trusted internal path is already accepted and is needed for
exact response/replay/evidence semantics. The privacy boundary belongs above
it.

### Reuse v1 with a changed field meaning

Rejected by the repository versioning policy. The public candidate field,
candidate digest input, decision identity, and replay interpretation all
change. V1 is frozen and v2 is explicit.

## Consequences

- Policy callers receive a complete, perspective-safe candidate domain.
- The internal driver keeps the exact existing semantic key and response path.
- Public candidate/domain/decision identities can be equal across paired
  worlds even when internal semantic keys differ.
- A collision or insufficiently safe projection closes the frame rather than
  guessing or reducing the legal domain.
- Public replay needs a regenerated public-domain projection and frame-local
  binding, while internal trace/replay remains unchanged.
- The environment semantic identity must bind both the retained internal
  schemas and the new public schemas, requiring `environment_identity.v2`.
- `PlayerObservation v1` remains the source of perspective-safe state facts,
  while `PublicEnvironmentObservation v1` is the only policy-facing
  serialization at the episodic boundary. Its nested `public_safe_state.v1`
  bytes have an explicit field order, null encoding, enum mapping, and
  container ordering.

## Compatibility / migration

`ocgforge.episodic_environment.v1` remains frozen for existing callers and
artifacts. V2 callers must use the new public fields and schemas; an unknown
or mixed v1/v2 identity set is rejected before mutation. No compatibility
alias is defined.

The public action, public candidate-domain, and public semantic-decision
schemas are first accepted by this prerequisite, so their v1 definitions are
being finalized before any caller can rely on the earlier draft layout. The
public observation schema is independently v1 and separately owned.

The episode identity schema stays v1 because its codec is unchanged. Its
parent `environment_semantic_id` is supplied by the explicitly versioned v2
environment identity. Internal protocol, action, candidate-domain,
semantic-decision, observation, and trace IDs remain at their accepted v1/v2
versions with unchanged meanings. The public observation projection is
separately versioned as `ocgforge.public_environment_observation.v1`, and its
nested canonical state codec is separately named
`ocgforge.public_safe_state.v1`.

## Verification

`tests/episodic/public_action_identity_test.cpp` is the focused pure-codec
proof. It includes typed-choice coverage, exact action-key, public-safe-state,
public-observation, public-domain, and public-decision golden vectors; a paired
hidden-card fixture through attached decision context and the real
`PlayerObservation` serializer; hidden-identity rejection; ordered-domain
mutation coverage; and unknown/collision rejection checks.

The paired fixture uses:

```text
World A internal key: card.0.3.14821890.0.8.0
World B internal key: card.0.3.7654321.0.8.0
Both public references: RedactedSlot(p0:SPELL_TRAP_ZONE:0)
```

The future Phase-2 implementation must repeat this property through the real
`EpisodicEnvironment` projection and close the applicable G01-G32 gates. This
ADR does not claim those implementation gates pass.
