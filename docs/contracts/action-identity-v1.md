# Action identity v1

## Contract ID

```text
ocgforge.action_identity.v1
```

## Purpose and ownership

This contract owns the semantic identity rules for
`ActionCandidate.semantic_key`, including the keys of adapter-local
continuation candidates. `docs/contracts/decision-protocol-v1.md` remains the
owner of `DecisionRequest`, `ActionCandidate` shape, complete candidate
membership, authoritative candidate ordering, and exact response ownership.

The two IDs are deliberately distinct. `ocgforge.decision_protocol.v1`
identifies the complete player-facing request/candidate semantic contract.
`ocgforge.action_identity.v1` identifies how one candidate is named and how
that name remains stable across processes and replay. An incompatible change
to semantic-key construction requires a new action-identity ID even when the
request type remains compatible.

## Semantic-key rules

`semantic_key` is a deterministic UTF-8 string. Its value is produced by the
protocol decoder or continuation state machine and is the authority for
candidate identity. A caller never supplies response bytes, a candidate
index, a pointer, or an engine object identity.

The existing v1 key families bind the semantic fields needed by their action:

- card and target keys include the family/index, card code, controller,
  location, and sequence where the action exposes a card locator;
- command keys include their decision family, command, and source index;
- position, amount, announcement, mask, place, yes/no, finish, cancel, and
  pass keys include their exact typed choice value;
- continuation pick, amount, finish, cancel, and bypass keys include the
  deterministic continuation identity and source/amount choice required to
  distinguish the transition.

The current lexical forms are fixed by the v1 implementation:

```text
card locator:       family.command.index.code.controller.location.sequence
command:            family.command.index
position:           position.code.position
yes/no:             yes_no.no | yes_no.yes
chain pass:         chain.pass
option:             option.index.u64_value
place:              place.controller.location.sequence
card cancel:        card.cancel
unselect card:      unselect.selected|unselected.source_index.code.controller.location.sequence
unselect end:       unselect.finish | unselect.cancel
announce number:    announce_number.index.u64_value
announce mask:      announce_mask.source_index
continuation pick:  continuation_id.pick.source_index
continuation amount: continuation_id.amount.source_index.amount
continuation end:   continuation_id.finish | continuation_id.cancel | continuation_id.bypass
```

Numeric components use their non-negative decimal `std::to_string` spelling;
the dot separators and family literals are part of the identity. A
continuation ID is the existing deterministic continuation-state identity,
whose protocol-owned form is:

```text
cont.raw_message_hash.continuation_kind.engine.engine_step
    .step.continuation_step.selected.indices
    .amounts.amounts.mask.selected_mask
```

The line wrapping above is presentation only; the encoded ID contains no
whitespace. This existing raw-message hash is not raw engine bytes, but it is
the SHA-256 identity of the complete engine frame. It is not copied into the
script-closure payload by this prerequisite.

For a given `DecisionRequest`, each key is unique. The protocol-provided
candidate vector order is authoritative and is not part of key construction;
the environment preserves that order. Key construction never uses pointer
addresses, unordered-container iteration, wall-clock time, random UUIDs,
compiler object layout, UI labels, or host filesystem paths.

The semantic key identifies the candidate choice, not the final response
bytes. Response bytes remain private to the protocol/driver execution path.
Stale-action rejection is owned by the request/continuation identity and the
environment's non-semantic freshness token; a key alone is not a freshness
token.

## Privacy and replay boundary

Action Identity v1 describes the existing internal semantic-key grammar.
Continuation semantic keys may derive from `raw_message_hash` through their
`continuation_id`; therefore a `semantic_key` is not automatically
policy-safe. The candidate-domain digest hashes the supplied semantic keys
unchanged and inherits their safety classification. Neither a semantic key
nor a candidate-domain digest is automatically safe for public publication.

Any public `EpisodicEnvironment` projection MUST independently prove that the
complete key and domain are perspective-safe for the acting player using the
current `PlayerObservation`/public-projection audit. If a key or domain cannot
be proven perspective-safe, publication fails closed. The public observation
boundary remains `PlayerObservation` v1.

Replay compares the ordered semantic keys and their owning decision/domain
identities. It does not persist exact response bytes as caller-facing action
identity and does not use this ID as a replacement for EngineTrace v2 or the
semantic gameplay hash.

## Compatibility

Changing the key grammar, locator fields, continuation-key identity, choice
encoding, or normalization rules incompatibly requires a new
`action_identity_schema_id`. Adding a new legal decision family without
changing existing key meanings may remain within v1 only when the existing
protocol coverage and determinism contracts are preserved.
