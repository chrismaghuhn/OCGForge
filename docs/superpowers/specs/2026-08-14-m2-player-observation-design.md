# OCGForge M2 Player Observation and Information-State Design

## Status

Approved for implementation by the user in this task. This document is
intentionally uncommitted because the task explicitly forbids commits.

## Goal and boundaries

M2 adds a deterministic, model-independent `PlayerObservation` boundary over
the pinned OCG C API. The engine remains omniscient internally; every consumer
on the observation side receives only a perspective-filtered snapshot, a
perspective-filtered visible-event slice, static match knowledge permitted by
an explicit configuration, and the current typed decision context.

The existing `player-view-v1` API and its tests remain supported unchanged.
M2 uses a new `ygo.player_observation.v1` contract because the old view uses a
redaction sentinel and has no typed semantic state, relationships, history, or
canonical hash. M2 does not add tensor padding, model code, belief tracking,
search, or full mechanics certification.

## Architecture

The implementation is split into five pure/read-only boundaries:

```text
CoreHost raw query bytes and process frames
        |
        v
Raw snapshot/event decoders
        |
        v
Perspective privacy filter and safe observation locators
        |
        v
PlayerObservation semantic contract
        |
        v
Canonical JSON serialization and SHA-256 observation hash
```

`CoreHost` continues to own lifecycle, response submission, and raw public
query calls. `PlayerObservationBuilder` owns projection and never mutates the
duel. `ObservationSession` owns adapter-side event history and turn/phase
metadata supplied by processed engine frames; it never queries or stores
hidden card identities. A separate `CardCatalog` projection exposes printed
metadata only for known passcodes and is never used to fill redacted cards.

## Semantic contract

The top-level observation contains:

```text
schema_version
perspective_player
decision_index
engine_step_index
globals
zones
entities
relationships
chain
visible_events
decision_context
match_context
observation_hash
```

The authoritative representation uses variable-length vectors and explicit
optionals; it has no maximum card, entity, action, or history constants.
`observation_hash` is SHA-256 over the canonical serialized observation with
the hash field itself omitted. Canonical serialization uses UTF-8 JSON with
fixed object-key order, deterministic array ordering, explicit `null` for
absent values, decimal integer values, and stable enum strings.

`ObservedCard` separates:

* observation locator, which is derived from the currently public owner,
  controller, semantic zone, sequence, and overlay slot;
* identity-known/passcode;
* printed/static catalog metadata;
* current/effective engine state;
* position/face state, status flags, counters, scales, and link markers.

An observation locator is not a physical engine identity. Cards omitted from a
hidden deck or hidden hand have no observation entity. Cards entering a hidden
or randomized collection lose their prior public locator. Hidden field and
hidden Extra Deck entities retain only safe public facts such as controller,
zone, sequence where publicly meaningful, position, and counts; all identity
dependent features are absent. Unknown passcodes are represented as `null`,
never as passcode zero.

## Visibility policy

`MatchKnowledgeConfig` defaults to `own_decklist_known=true` and
`opponent_decklist_known=false`. It records the policy in match context and
trace metadata. Current Main Deck order and identities are never projected as
zone entities. Own deck and Extra Deck composition can appear only as static
multisets in match context. Opponent deck composition is included only when
explicitly enabled.

For a perspective player, own cards remain identifiable in the hand and own
Extra Deck; public face-up cards and engine-public cards are identifiable to
both players. Opponent hidden hand and Main Deck entries are represented by
counts only. Opponent hidden face-down field cards are represented with
redacted features. Opponent face-down Extra Deck entries are represented by
count and no arbitrary ordering; face-up Extra Deck entries are public
entities. If a pinned-core visibility rule cannot be proved, the builder
redacts the questionable field and the coverage inventory marks it pending.

## Zones and relationships

The public zone enum is semantic: `MAIN_DECK`, `HAND`, `MONSTER_ZONE`,
`SPELL_TRAP_ZONE`, `GRAVEYARD`, `BANISHED`, `EXTRA_DECK`, `FIELD_ZONE`,
`PENDULUM_RELEVANT_STATE`, and `OVERLAY`. Pinned-core PZONE/FZONE/STZONE/
MMZONE/EMZONE values are mapped to semantic helpers using the actual core
slot layout; no separate physical Pendulum Zone is invented.

Relationships are explicit and use safe observation locators:

* `XYZ_MATERIAL` links a visible Xyz host to each material slot;
* `EQUIP` links an equip card to its visible target;
* `TARGET` links visible card targets.

Unknown Xyz materials retain relationship slots and count but not passcodes or
static features unless the engine proves that identity visible to the
perspective. Relationship endpoints are omitted rather than leaked when the
relationship itself is not visible.

## Engine state and history

The pinned `OCG_DuelQueryField` provides duel flags, LP, zone counts,
occupancy, Xyz material counts, and current chain source/activation metadata.
Turn player, turn count, and phase are maintained by parsing the public
`MSG_NEW_TURN` and `MSG_NEW_PHASE` frames in `ObservationSession`; player-to-act
comes from the typed `DecisionRequest` when present. Winner and win reason are
maintained from `MSG_WIN`. Missing metadata is explicit `null`, never guessed
from UI text.

`VisibleGameEvent` is a structured, filtered subset of public engine frames.
M2 covers movement, summon/set/position changes, draw counts, shuffle and
randomization boundaries, LP changes, chain activation/resolution, targets,
equip, counters, destruction/banish/return effects as exposed by the pinned
message family, and terminal win events. Raw packets, private card codes, and
localized effect text are not serialized. A machine-readable event coverage
inventory records supported, fixture-not-needed, deferred, and private event
families.

## Decisions and traces

The decision boundary remains:

```text
PlayerObservation + DecisionRequest + complete ActionCandidate[]
```

Decision context stores request identity, kind, continuation identity/state,
and an optional safe locator reference for observable candidates; it never
duplicates the legal candidate list into card state. Candidate consistency
tests resolve every visible locator against the observation and fail closed for
unresolvable hidden references.

`ygo.engine_trace.v2` gains the perspective player, observation schema, and
observation hash on decision records. Existing raw/provenance and semantic
gameplay hash guarantees remain unchanged except that the visible observation
hash is included as a semantic state input. Omniscient diagnostics, when
needed by a fixture, are kept outside training-safe trace records.

## Verification strategy

Implementation proceeds in internal reviewable phases. Each phase follows
test-first red/green/refactor cycles and runs its focused Windows tests before
the next phase. Required adversarial tests construct paired omniscient worlds
that differ only in opponent hidden hand, deck order, face-down field, or
hidden Extra Deck contents and assert identical observation bytes and hashes.
Positive reveal tests assert divergence only after a public reveal. Additional
fixtures exercise Fusion, Synchro, Xyz/overlay, Link/markers, Pendulum/scales,
counters, chain state, knowledge-destroying transitions, candidate references,
serialization determinism, process determinism, and observation side-effect
freedom.

The fixture setup may use a repository-owned script loaded through the pinned
public `OCG_LoadScript` API to create deterministic public Xyz/material,
relationship, counter, equip, or Pendulum states. It must not modify the
pinned rules bundle or upstream sources.

## Known conservative choices

* Hidden Main Deck and hand identities are never retained in the observation
  session, even if a raw engine packet carries their code; a separately
  supported reveal event can expose only the code and public event payload.
* Face-down Xyz materials are countable relationship slots with redacted
  identities unless a fixture proves player visibility.
* The static catalog is limited to fields available in the generated pinned
  card-data projection. Card text and localized names remain outside M2.
* Any event family whose player-relative visibility cannot be proven is
  emitted only as a generic public boundary event or classified as pending in
  the event inventory.
