# `ygo.player_observation.v1`

## Boundary and compatibility

This is a new semantic contract. It does not redefine `player-view-v1`.
`EngineState` remains omniscient inside the pinned OCG core; a
`PlayerObservation` is always built for one explicit `perspective_player` and
is the only state projection intended for an agent, teacher, search adapter,
or model adapter.

The contract has three deliberately separate parts:

1. the current perspective-safe snapshot (`globals`, `zones`, `entities`,
   `relationships`, and `chain`);
2. static match knowledge (`match_context`), which is configured independently
   from current ordered engine state; and
3. perspective-filtered visible history (`visible_events`).

The environment does not add opponent beliefs, probabilities, inferred hands,
archetypes, or other speculative state.

## Top-level fields

```text
schema_version       "ygo.player_observation.v1"
perspective_player   0 or 1
decision_index       adapter observation index
engine_step_index    pinned engine process-step index
globals              LP, duel flags, optional turn/phase/terminal facts
zones                variable-length typed zone counts
entities             variable-length perspective-safe card entities
relationships        variable-length Xyz/equip/target edges
chain                visible structured chain links
visible_events       perspective-filtered event history
decision_context     decision reference, never the candidate array
match_context        configured static knowledge and duel flags
observation_hash     SHA-256 of the canonical contents without this field
```

There are no fixed tensor dimensions, maximum entity counts, maximum action
counts, or model vocabulary requirements in this authoritative schema.

## Privacy rules

- Main Deck current entries and order are never emitted as entities.
- Opponent hidden Hand and face-down Extra Deck identities are omitted; their
  counts remain in `zones`.
- An opponent face-down field card may remain as a slot-level redacted entity,
  but its passcode and identity-derived properties are absent (`null`).
- Own legitimate private cards remain identifiable, including own Hand and
  own face-down field/Extra Deck cards.
- Public face-up field cards, public graveyard/banished cards, and public
  face-up Extra Deck cards are projected when the pinned query proves them
  visible.
- A hidden card never receives a pointer, object address, persistent engine
  ID, or identity-derived feature set. Location locators such as
  `p1:MONSTER_ZONE:2` are safe references to the currently observable slot,
  not physical-card identities; a hidden Deck transition has no entity
  locator.
- Opponent decklist knowledge defaults to hidden. Own and opponent static
  deck contexts are configuration, not current Deck order.
- Xyz material count and attachment edges are exposed from the pinned query.
  M2.1 confirmed that the pinned API exposes only aggregate count plus an
  ordered raw-code vector in the parent query. Its apparent per-material
  `LOCATION_OVERLAY` query returns no record, and the location query returns
  only a null marker. M2 therefore emits redacted overlay entities and
  explicit edges; material identity is an API limitation, not an inferred
  OCGForge identity.

## Card properties

`ObservedCard.printed` is static catalog data. `ObservedCard.current` is
current pinned query state. Absence is represented by JSON `null`, not a
sentinel number. Link cards expose `link_rating` and typed `link_markers`;
Link `defense` is absent rather than fabricated. Xyz cards expose `rank` and
do not synthesize `level`. Pendulum scales are optional properties and
Pendulum-related zones are mapped from the configured duel flags.

## Decision reference

`decision_context` contains `decision_id`, decision kind, engine step,
acting player, engine message metadata, continuation ID when present, and
the safe observation locators referenced by candidates. The complete ordered
`ActionCandidate[]` remains in the authoritative `DecisionRequest`; it is not
copied into card-state structures. `candidate_observation_consistent` is the
fail-closed consistency check used by tests and integration callers.

## Canonical serialization

`canonical_serialize` produces UTF-8 compact JSON followed by one LF. The
implementation's fixed key order is part of the contract. Arrays are ordered
as follows:

- zones: `(player, enum kind, total count, public count, hidden count)`;
- entities: locator lexicographic order;
- relationships: `(enum kind, source locator, target locator)`;
- counters: `(type, count)`;
- Link markers: enum order after sorting;
- chain links: engine chain index; link targets lexicographic order;
- visible events: `(event_index, engine_step_index)`; event targets
  lexicographic order;
- static deck contexts: sorted passcodes, so a configured deck is a
  composition/multiset rather than an ordered current Deck;
- life-point vectors and chain-link order retain their semantic order.

Optional scalar, enum, string, and locator values serialize as JSON `null`
when absent. Booleans serialize as `true`/`false`; integers use JSON decimal
numbers; enum values use stable names. Query and event wire packets are
decoded little-endian before projection, but wire byte order is not exposed in
the semantic JSON.

The hash input is the exact canonical JSON object produced before
`observation_hash` is appended. It includes only the perspective-safe fields
above and excludes wall clock, machine path, compiler, pointer values, raw
omniscient query bytes, and debug-only state. The stored field is the
lowercase SHA-256 digest of that byte sequence. Re-serializing the same
observation must reproduce the same bytes and digest.

## History and knowledge boundaries

The event projector supports the M2 event subset listed in
`docs/observation/event_coverage.json`. It never forwards raw engine packets.
Shuffle, hand/Extra randomization, and reverse-deck messages produce an
explicit `RandomizationBoundary`; hidden destinations have no persistent
entity locator. Unsupported message families are intentionally documented as
deferred rather than guessed.

## Authority and limitations

The pinned rules bundle remains the authority for legality and current card
state. This contract certifies the observation boundary and focused fixtures;
it does not certify all Yu-Gi-Oh! cards, all summoning lines, competitive deck
support, complete historical belief reconstruction, or full mechanics
compatibility.
