# Decision protocol v1

## Contract ID

```text
ocgforge.decision_protocol.v1
```

This ID names the complete `DecisionRequest`/`ActionCandidate` public
semantic contract: typed request families, complete legal candidate
membership, authoritative ordering, continuation linkage, and response
submission ownership. The independent semantic-key identity rules are owned
by `ocgforge.action_identity.v1` in
`docs/contracts/action-identity-v1.md`.

## Purpose

This contract is the player-facing decision boundary over the pinned OCG
core. It exposes complete legal primitive candidates without flattening a
large combination into one action. An environment continuation step is an
OCGForge decision, but it is not a new OCG engine decision.

For one original interactive engine message:

```text
OCG message
  -> DecisionRequest 0
  -> zero or more adapter-local continuation requests
  -> one final response byte vector
  -> exactly one OCG_DuelSetResponse
  -> OCG_DuelProcess
```

The engine remains paused during every adapter-local step.

## DecisionRequest

```text
DecisionRequest
  decision_id             deterministic identity of this request
  engine_step_index       index of the original engine process step
  player                  acting player
  kind                    typed decision family
  engine_message_type     pinned numeric MSG_* value
  engine_message_name     pinned symbolic name
  raw_message_hash        SHA-256 of the complete engine frame
  candidates              complete ordered legal semantic candidates
  continuation            immutable SelectionContinuation when non-atomic
```

`decision_id` and `raw_message_hash` are data identities, not pointers or
runtime object addresses. A continuation contains only information supplied by
the message and information legitimately visible to the acting player.

## ActionCandidate

Each candidate has a unique `semantic_key` within its request. Keys are stable
across processes and do not depend on pointer addresses, unordered-map order,
wall-clock time, random UUIDs, UI labels, or compiler object layout.

The semantic action kinds are:

```text
ENGINE_RESPONSE  an atomic choice with exact final response bytes
PICK             select one primitive continuation item; no engine response
FINISH           submit the current complete legal selection
CANCEL           submit the original message's legal cancellation response
ASSIGN_AMOUNT    assign one exact amount to the current counter item
```

An intermediate candidate has `submits_engine_response=false` and no response
bytes. A terminal candidate has `submits_engine_response=true` and exact bytes.
Calling the response submission boundary with an intermediate candidate is an
error; the caller must first apply the continuation transition.

For cards, semantic identity includes code, controller, location, sequence,
and overlay sequence/position where applicable. Two physical copies with the
same passcode therefore remain distinct.

## SelectionContinuation

```text
SelectionContinuation
  continuation_id
  continuation_kind
  continuation_step
  original_message_type
  raw_message_hash
  selected_items
  remaining_items
  mandatory_items
  constraints
  can_finish
  can_cancel
```

The state is immutable from the caller's perspective. A transition creates a
new value with a new deterministic ID. The old ID is not accepted by the new
request, which makes stale actions fail closed.

Unordered set families use monotonic original candidate indices. This removes
permutation duplicates while preserving every legal final set. Ordered families
use a remaining list and append each picked item to the response order; no
canonicalization removes meaningful permutations.

## Family constraints

- Multi-card selection uses the engine min/max and cancelable flag. `FINISH`
  appears only when the selected cardinality is legal.
- Tribute selection tracks weighted `release_param` contribution and follows
  the pinned core's adjusted max and minimum tests.
- Sum selection tracks fixed mandatory items and optional items with one or
  two possible contribution values. A pick is offered only if a legal terminal
  completion remains reachable under the pinned equal or greater mode.
- Placement tracks typed controller/location/sequence zone slots and rejects
  duplicate zones.
- Counter allocation tracks remaining required amount and per-card capacity;
  all feasible amounts, including zero, are represented.
- Ordering tracks a remaining list and encodes the exact selected permutation.
- Race and attribute announcements derive their domains from the engine mask;
  no fixed vocabulary is used.

## Response construction and engine immobility

Response builders use the pinned little-endian wire formats. Intermediate
choices never call `OCG_DuelSetResponse` and never call `OCG_DuelProcess`.
Only a final `FINISH`, `CANCEL`, `ENGINE_RESPONSE`, or completed
`ASSIGN_AMOUNT` transition exposes one response for the original message.

The host may then submit that one response and resume the engine. A retry,
protocol error, crash, or silent correction is evidence that the family is not
engine-verified.

## Completeness and failure behavior

The candidate count equals the actual selectable candidate count. No global
candidate cap exists and no legal candidate is truncated. Semantic keys are
unique and ordered deterministically. An unsupported message, malformed frame,
stale key, duplicate key, impossible partial state, or incomplete candidate set
raises a structured protocol error. The adapter never silently chooses a
player decision.

## Trace relationship

Atomic M0 decisions retain `ygo.engine_trace.v1` semantics. Continuation-aware
traces use the versioned v2 fields described in
`docs/contracts/engine-trace-v2.md`; intermediate records set
`engine_advanced=false` and leave the selected response hash empty. Only the
terminal continuation event carries the final response hash.
