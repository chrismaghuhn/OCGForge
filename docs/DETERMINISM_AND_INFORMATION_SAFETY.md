# Determinism and Information Safety

Determinism and hidden-information safety are cross-cutting OCGForge requirements.

They must be designed together because identity, ordering, history, hashing, and diagnostics can leak information even when obvious card fields are redacted.

## 1. Deterministic input identity

A reproducible duel depends on more than one integer seed.

Relevant canonical inputs include:

- rules-bundle identity;
- ordered repository patchset;
- deck manifests;
- rules/duel flags;
- starting-player configuration;
- seed bundle;
- semantic action sequence;
- contract/schema versions where persisted artifacts are compared.

Do not let ambient latest upstream state enter canonical execution.

## 2. Stable semantic identities

Do not build authoritative IDs from:

- pointer values;
- heap addresses;
- object addresses;
- unordered-map iteration;
- runtime-generated UUIDs;
- wall clock;
- thread IDs;
- process IDs;
- machine paths.

Use semantic message/card/selection data.

## 3. Candidate ordering

A complete candidate set must also have deterministic ordering.

For unordered semantic sets, define canonical ordering.

For decisions where order changes the response, preserve meaningful engine semantics rather than sorting away permutations.

Deterministic ordering is part of reproducible policy input.

## 4. Continuations

Adapter-local continuation state is authoritative decision state but not new OCG engine state.

Intermediate continuation steps must not advance the engine.

This makes a multi-step model/player interface possible without changing the underlying OCG decision.

## 5. Perspective boundary

The engine is omniscient.

The agent is not.

`PlayerObservation` is built for an explicit `perspective_player` and remains
the perspective-safe observation-layer source. The episodic public boundary
emits the separately versioned
`ocgforge.public_environment_observation.v1` projection; it must not publish
an attached `PlayerObservation v1` directly when that record contains internal
decision or continuation identity.

Raw omniscient queries are not an acceptable shortcut for a model adapter.

## 6. Hidden identity

Examples of hidden identity that must remain protected:

- opponent Main Deck composition/order unless static match configuration explicitly makes composition known;
- current opponent Main Deck entries/order;
- opponent hidden Hand cards;
- opponent face-down Extra Deck cards;
- identity-derived properties of an opponent face-down field card when not legitimately public;
- identity continuity across knowledge-destroying randomization.

Own legitimate private information may be visible to the owning perspective.

## 7. Locators are not persistent physical IDs

A locator such as a visible field slot identifies an observable location/state relationship.

It does not grant permission to track the same hidden physical card forever.

When a card enters a knowledge-destroying hidden zone, the old identity relationship must be discarded unless the rules/public information legitimately preserves it.

## 8. Randomization boundaries

Shuffle, hidden-zone randomization, and similar transitions can destroy knowledge.

The visible event model should represent that fact explicitly where supported.

Never reconstruct hidden identity after randomization from engine internals.

## 9. Hashes and privacy

A hash can leak hidden information if hidden data is included in its input.

Therefore:

- player observation hashes must be computed only over perspective-safe canonical fields;
- raw omniscient query bytes must not enter the player observation hash;
- debug-only state must not enter the player observation hash;
- semantic hash domains should be documented/versioned.

A digest being one-way does not make hidden input safe to expose.

## 10. Canonical serialization

For deterministic hashes:

- define exact field set;
- define exact key order when the contract requires it;
- define array ordering;
- define null representation;
- define integer/boolean/enum encoding;
- define final newline behavior where specified;
- exclude incidental runtime metadata.

A “same JSON object” claim is insufficient if byte-level hashes are persisted.

## 11. Trace identity vs. gameplay identity

A trace can contain provenance that differs between toolchains or artifact paths.

Cross-process gameplay determinism should compare semantic gameplay identity.

Do not accidentally promote a provenance hash into the semantic game-state identity.

## 12. Diagnostics

Diagnostics should be rich enough to debug:

- message type/name;
- raw-message hash;
- step;
- player;
- bundle;
- deck identity;
- seed;
- recent trace context.

But debug output should not be routed into player observations or training features.

When publishing diagnostics from hidden-information failures, review whether raw payloads contain private card data.

## 13. Paired-world testing

A strong privacy test constructs two worlds that differ only in information hidden from one perspective.

The player-facing observation should remain equivalent for that perspective until the difference becomes legitimately visible.

This is stronger than checking that one obvious `code` field is null.

## 14. Parallelism

Future parallel/vectorized execution must preserve per-environment canonical semantics.

If internal parallel scheduling can change:

- candidate ordering;
- event ordering;
- RNG consumption;
- semantic hashes;
- response timing visible to logic;

then it is not a safe optimization.

## 15. Information-safety review checklist

For any new field/API/event/hash:

- Who is allowed to know this?
- From which public engine evidence is it derived?
- Is it identity-derived?
- Does it survive a shuffle when it should not?
- Can ordering leak hidden state?
- Can cardinality leak more than zone counts already reveal?
- Does a hash include hidden input?
- Does a debug/provenance field cross into the player-facing schema?
- Is absence represented explicitly rather than fabricated?
- Do paired-world tests cover it?
