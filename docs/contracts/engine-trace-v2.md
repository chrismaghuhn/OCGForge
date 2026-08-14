# Engine trace v2

## Scope

`ygo.engine_trace.v2` preserves the M0 raw engine trace fields and adds the
state needed to audit adapter-local continuations. It remains Windows-only
and is authoritative only when the manifest pins the rules bundle, core
commit, card-script commit, database commit, decks, seeds, and policy.

## Continuation events

Each adapter-local choice is a trace step with the same original raw-message
hash and engine process step. `continuation_id` and `continuation_step` identify
the immutable continuation state. `engine_advanced=false` means the action was
applied only to the adapter continuation; `final_engine_response_hash` must be
`null`, and the adapter must not call either engine response submission or
engine process.

The v2 JSON field `complete_candidate_count` is the trace contract's complete
`candidate_count`: it must equal the length of
`ordered_candidate_semantic_keys` for every decision record. The legal domain
is never truncated to the immediate candidate count or to a fixed cap.

The final `FINISH`, `CANCEL`, atomic action, or completed amount allocation has
`engine_advanced=true` and exactly one `final_engine_response_hash`. The host
then submits that response once and resumes the pinned core. `decision_index`
counts adapter decisions, while `engine_step_index` identifies the unchanged
engine process step across continuation choices. Continuation diagnostics also
record steps, peak immediate candidate count, and terminal solutions exposed;
the probe reports response-build time as a non-hashed diagnostic. These metrics
never cap or truncate the legal domain. Timing is deliberately excluded from
the canonical artifact and semantic gameplay hashes so repeated runs remain
deterministic.

## Canonical trace hash

`canonical_trace_jsonl_v2` uses deterministic key ordering and UTF-8 JSONL.
`canonical_trace_hash_v2` is SHA-256 over those exact bytes. The v1 serializer
and hash remain available for compatibility with M0 artifacts.

## Semantic gameplay hash

`semantic_gameplay_hash` hashes a separate deterministic projection containing:

- pinned rules/core/database authority, duel flags, seeds, fixture deck hashes,
  and policy identifier;
- player, decision family, ordered semantic candidates, selected semantic key,
  public state hash, continuation identity/step, engine-advanced state, and
  final engine response hash, and terminal outcome.

It intentionally excludes raw message length/bytes, raw-message hashes, engine
message numeric IDs, decision IDs derived from transport bytes, and diagnostic
timings. Thus a transport-only representation change does not change the
semantic gameplay hash, while a changed legal domain, public state, selected
semantic action, or final engine response does.
