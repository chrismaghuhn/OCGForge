# OCGForge Glossary

## ActionCandidate

One legal semantic primitive presented for a `DecisionRequest`.

A candidate has deterministic semantic identity. A terminal candidate may carry the exact engine response bytes; an intermediate continuation candidate does not advance the engine.

## Acceptance evidence

Tests, machine-readable artifacts, hashes, fixture results, and summaries used to support a defined milestone claim.

Acceptance evidence is scope-specific.

## BabelCDB

Pinned card database input used by the repository rules bundle.

The inspected repository records its snapshot license as unresolved.

## Bundle ID

Deterministic identity for the canonical rules-bundle inputs.

Do not confuse it with a Git commit SHA or gameplay hash.

## Canonical serialization

A precisely defined byte encoding for semantic data used for persistence/hashing/reproducibility.

## Candidate completeness

Property that the presented legal candidate set contains the full supported legal domain rather than a truncated or heuristic subset.

## Continuation

Adapter-local immutable decision state used to express a complex original engine decision through multiple primitive steps while keeping the OCG engine paused.

## CoreHost

OCGForge C++ RAII wrapper around the pinned public OCG C API.

It owns the trusted engine lifecycle/query/response boundary.

## DecisionRequest

Typed player-facing request created from one original interactive engine message, with deterministic identity, metadata, candidates, and optional continuation state.

## Engine-verified

A support classification indicating that the relevant behavior was exercised through the pinned engine path with acceptance evidence.

Stronger than parser/oracle-only verification.

## Fail closed

Refuse to continue when correctness/completeness/visibility is not proven.

In OCGForge, fail-closed behavior is preferred to fabricating a player response.

## Fixed matchup

The exact deck-vs-deck scope certified by M3.

It is not a claim of global deck support.

## Gameplay hash

A semantic deterministic identity for gameplay-relevant content.

Do not assume every trace or artifact hash is a gameplay hash.

## Information safety

Property that a player-facing consumer learns only information legitimately available from that perspective and configuration.

## Knowledge-destroying transition

A transition such as hidden-zone randomization that invalidates prior physical-card identity knowledge.

## Locator

Perspective-safe reference to an observable entity/slot relationship.

A locator is not necessarily a persistent physical-card ID.

## M3

Locked fixed-deck conformance milestone for the Swordsoul Tenyi vs. Salamangreat slice.

## M3.5

Narrow ocgcore public API-hardening milestone that adds/fixes Xyz-material query and starting-player control through an ordered repository patchset.

## Observation hash

SHA-256 over the canonical perspective-safe `PlayerObservation` content defined by its contract.

It must not include omniscient hidden state.

## OCG / ocgcore

The pinned Yu-Gi-Oh! rules engine stack used as game-semantics authority.

## Ordered patchset

Repository-versioned patches applied in a declared order to an immutable pinned upstream base.

The patchset is a canonical input when included in the rules bundle.

## Paired-world test

Privacy test where two engine worlds differ only in hidden information. The selected player's observation should remain equivalent until the hidden difference becomes legitimately visible.

## PlayerObservation

Perspective-safe semantic state contract intended for agents, teachers, search adapters, and model adapters.

Current schema: `ygo.player_observation.v1`.

## Player view

Earlier/narrower perspective projection contract retained separately from `PlayerObservation`.

## Protocol-verified

A support classification indicating that parsing/response/oracle behavior is verified without the stronger required real-engine fixture evidence.

Do not relabel it as engine-verified.

## Raw message hash

SHA-256 identity of the complete engine message frame used by the decision protocol.

It is not a player observation hash.

## Rules bundle

Exact set of pinned runtime rules/data inputs and repository patchset identity needed to reproduce the environment.

## Semantic key

Deterministic action identity built from semantic content rather than memory addresses or runtime object identity.

## Trace v1 / v2

Versioned engine/protocol trace contracts.

Trace v2 supports continuation-aware records in which intermediate adapter-local decisions do not advance the engine.

## Unsupported fail closed

Explicit status indicating OCGForge refuses to invent a complete legal implementation for the relevant family.

## Visible event

Perspective-filtered semantic game-history event exposed through the supported observation event projection.

Raw engine packets are not directly forwarded as player-visible history.
