# Perspective-safe public environment observation v1

## Contract ID

```text
ocgforge.public_environment_observation.v1
```

This is the policy-facing observation consumed by
`ocgforge.episodic_environment.v2`. It is not an alias for
`ygo.player_observation.v1`.

## 1. Boundary and ownership

The observation layer may retain an attached `PlayerObservation v1` record for
internal diagnostics, protocol integration, and trace evidence. The public
environment must perform a separate projection before exposing state:

```text
PlayerObservation v1
    -> explicit safe-state serializer and context projection
    -> PublicEnvironmentObservation v1
    -> Agent / Teacher / Model
```

The public serializer owns the public bytes and
`public_observation_digest`. It must never serialize the canonical v1
`PlayerObservation` object directly.

## 2. Public shape

The public value contains:

```text
PublicEnvironmentObservation {
    schema_version = ocgforge.public_environment_observation.v1
    perspective_player
    decision_index
    safe_state
    public_decision_context
    public_observation_digest
}

public_decision_context {
    kind                       optional public request-family token
    player                     optional acting player
    referenced_entities        sorted current safe observation locators
}
```

`safe_state` contains the perspective-safe state fields owned by
`PlayerObservation v1`: globals, zones, entities, relationships, chain,
visible events, and match context. Its canonical bytes are produced by the
public serializer for this contract, not by
`canonical_serialize(PlayerObservation)`.

The public shape does not contain:

- `decision_id`;
- `continuation_id`;
- `raw_message_hash`;
- internal candidate/domain digests;
- internal engine-step identity;
- the v1 `observation_hash`.

The public projection may retain safe request-family, acting-player, and
current observation-locator metadata. It must fail closed if any state or
context field cannot be proven perspective-safe.

## 3. Canonical public observation bytes

The canonical byte sequence uses the repository `u32be byte_length || UTF-8`
string encoding and is ordered as follows:

| Order | Field | Encoding |
| ---: | --- | --- |
| 0 | identity domain | string `ocgforge.public_environment_observation.v1` |
| 1 | identity schema | string `ocgforge.public_environment_observation.v1` |
| 2 | perspective player | `u8` |
| 3 | decision index | `u64be` |
| 4 | canonical safe-state bytes | length-prefixed bytes |
| 5 | context kind | optional canonical lower-case token string |
| 6 | context player | optional `u8` |
| 7 | referenced entities | `u32` count followed by sorted locator strings |

The `public_observation_digest` is the lowercase SHA-256 of these bytes. The
digest is the only observation identity admitted to the public semantic
decision identity. The public action/domain identities remain separate.

The public state serializer must bind every field that the public DTO exposes,
including explicit null/absence markers and authoritative ordering. It must
not bind or copy fields that are omitted from the public shape.

## 4. Attached decision-context rule

`attach_decision_context(PlayerObservation&, DecisionRequest&)` may continue to
populate the internal v1 record. The public projection copies only
`kind`, `player`, and safe referenced locators. It drops the attached v1
`decision_id`, `continuation_id`, engine-step metadata, and v1 observation
hash. The public projection must not derive a replacement by hashing the full
internal observation.

Thus two internal observations may have different v1 canonical bytes and
different v1 hashes because their private request identities differ while
their `PublicEnvironmentObservation` values and public digests remain equal.

## 5. Paired-world acceptance

The required fixture attaches distinct internal decision and continuation IDs
to the two worlds before projection. It must prove:

```text
World A hidden identity != World B hidden identity
PlayerObservation v1 bytes may differ
PublicEnvironmentObservation bytes equal
public_observation_digest equal
public_semantic_decision_id equal when public candidate domain is equal
```

It must also prove that changing a visible safe-state byte changes the public
observation digest and public semantic decision identity.

## 6. Compatibility and non-goals

`ygo.player_observation.v1` remains unchanged for its owning observation and
internal integration consumers. This contract does not change observation
visibility, Decision Protocol legality, `EngineTrace v2`, rules, decks, or
the production `EpisodicEnvironment` implementation.
