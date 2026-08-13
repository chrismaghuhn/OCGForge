# `ygo.engine_trace.v1`

## Purpose

This is the canonical JSON Lines contract for the M0 controlled duel. The
first line is a manifest. Every later line is one engine decision step or one
terminal result. The serialized bytes, including line endings, are hashed with
SHA-256; key order and array order are therefore part of the contract.

The trace is a player-view artifact. It may contain public state hashes and
action metadata, but it must not contain hidden opponent card passcodes,
identities, or deck order. An omniscient debugging artifact, if introduced
later, must use a separate name and schema and cannot be accepted as a
training observation.

## Manifest record

The manifest contains these fields:

```json
{
  "trace_schema_version": "ygo.engine_trace.v1",
  "rules_bundle_id": "...",
  "core_repository": "...",
  "core_commit": "...",
  "cardscripts_repository": "...",
  "cardscripts_commit": "...",
  "database_repository": "...",
  "database_commit": "...",
  "core_api_version": "11.0",
  "compiler_identity": "...",
  "build_type": "Debug",
  "platform_identity": "...",
  "duel_flags": 0,
  "fixture_deck_hashes": ["...", "..."],
  "seed_bundle": [0, 0, 0, 0],
  "policy_identifier": "m0.deterministic_priority.seeded_tie.v1"
}
```

`seed_bundle` is the complete four-word `SeedBundle`, not a shortened seed
display value. Deck hashes are SHA-256 hashes of the committed fixture files.
The rules-bundle identifier is the machine-verified identifier from
`third_party/rules_bundle.lock.json`.

## Decision record

Each supported interactive engine message produces one decision record with
at least:

```json
{
  "step_index": 0,
  "player_to_act": 0,
  "engine_message_type": 11,
  "raw_message_length": 123,
  "raw_message_sha256": "...",
  "decision_request_kind": "idle_command",
  "complete_candidate_count": 12,
  "ordered_candidate_semantic_keys": ["...", "..."],
  "selected_semantic_key": "...",
  "selected_response_sha256": "...",
  "public_state_hash": "...",
  "terminal": false,
  "winner": null,
  "win_reason": null
}
```

The candidate count must equal the number of serialized semantic keys and the
number of candidates accepted by the selector. Candidate order is retained
for compatibility with the engine, while semantic keys provide deterministic
policy identity. The exact response bytes are not stored in the public trace;
their SHA-256 hash proves which response was submitted without making a
second action protocol.

The M0 policy uses the first seed word only when several normal-summon
candidates share the same priority: it sorts their semantic keys and selects
`seed_bundle[0] % candidate_count`. This is part of the versioned policy
contract and is not a substitute for future policy semantics.

## Terminal record

A terminal record has the same step shape. It sets `terminal` to `true`,
provides `winner` and `win_reason`, and leaves the decision-specific fields
empty or zero. The M0 fixture currently reaches a terminal LP result.

## Canonicalization rules

- UTF-8 JSON, one object per line, no insignificant whitespace.
- Object keys use the implementation's fixed lexicographic contract order.
- Arrays preserve engine candidate order or the specified seed/deck order.
- SHA-256 values are lowercase hexadecimal.
- Missing winner or reason values serialize as JSON `null`.
- No wall-clock time, process id, pointer value, hash-map order, or thread
  scheduling result is serialized.

## Public state hash

The M0 implementation hashes the public field query bytes together with the
player perspective. It is a transition-integrity value, not a replacement for
the player-view contract. Future state fields must be added only after
proving that hidden identities and hidden deck order cannot enter the public
projection.
