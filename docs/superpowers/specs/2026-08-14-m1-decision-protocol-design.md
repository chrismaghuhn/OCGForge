# OCGForge M1 Decision Protocol Design

## Scope and authority

This design implements the supplied M1 specification on top of the clean M0
authority commit `945a43aec34fc5cc53d403acd8c2f0f47eff81cc`. The pinned rules
bundle remains authoritative and unchanged:

- bundle `6fbbd212ae4be2df36170dcbfcdf5c46aaaa0e3091cf815c2d0261fd01640ea4`
- OCG core `9a0c558c2d686542f7914a6d529fd7aa57746aed`
- CardScripts `f337c87018ca723c1aded5143e616bb649555273`
- BabelCDB `89ad6837b0766a52984d8c715a7d5d4f8447946b`

The pinned OCG core's `playerop.cpp`, `processor_unit.h`, `field.cpp`, and
`ocgapi_constants.h` are the protocol authority. The pinned ygo-agent source
is reference-only and is not a runtime dependency.

## Architecture

The protocol is split into four pure responsibilities:

1. Wire decoders parse one complete OCG message into typed immutable input
   records.
2. Constraint evaluators determine legal terminal selections and whether a
   partial state has at least one completion.
3. Continuation transitions turn one semantic action into either another
   adapter-local `DecisionRequest` or one final response byte vector.
4. Response builders encode the exact little-endian response formats accepted
   by the pinned core.

`CoreHost` continues to own the real duel. A continuation contains no engine
pointer and its intermediate transitions return no response bytes. Therefore
the caller cannot advance the engine for a `PICK` or `ASSIGN_AMOUNT`; only a
terminal action exposes a response for the original engine message.

## Value model

`DecisionRequest` gains `decision_id`, `engine_step_index`, and
`raw_message_hash`, plus an optional value `SelectionContinuation`.
`ActionCandidate` gains semantic action kinds `PICK`, `FINISH`, `CANCEL`, and
`ASSIGN_AMOUNT`, stable card position data, an amount field, and an explicit
`submits_engine_response` bit. Intermediate candidates have an empty response
and `submits_engine_response=false`; terminal candidates have the exact final
response and `true`.

The continuation stores only parsed message data and public decision context:

- continuation kind and deterministic continuation ID;
- original message type and raw-message hash;
- canonical original item order;
- selected and remaining item indices;
- count, contribution, zone, announcement, or counter constraints;
- mandatory sum items where present;
- current step, finishability, and cancellation permission.

Card identity uses code plus controller, location, sequence, and overlay
sequence/position where applicable. Passcode alone is never a physical-card
identity. The continuation ID is derived from the raw-message hash, family,
step, and canonical selected state, so a candidate from an older state is
rejected as an invalid semantic key.

## Canonical progression

Unordered subsets use monotonic original item indices. A `PICK` may only select
an item after the current last selected index. This proves each unordered
terminal set has exactly one path while retaining every legal set. The rule is
used for card selection, tribute, sum, multi-zone placement, and multi-value
race/attribute announcements because the pinned core validates those responses
as sets or masks.

Ordering decisions use the remaining-item list in canonical original order.
Each `PICK` removes one item and appends it to the ordered result. No
canonicalization is applied to `MSG_SORT_CARD` or `MSG_SORT_CHAIN`, so every
valid permutation remains reachable exactly once.

Counter allocation processes the sorted engine card list in order. Each step
offers every amount from zero through the largest amount that can still leave
a feasible completion. The terminal response contains one signed 16-bit
little-endian amount per original card.

## Pinned message semantics

- `MSG_SELECT_OPTION`: atomic candidates preserve every option ordinal,
  including duplicate option payloads; response is the option index.
- `MSG_SELECT_CARD`: continuation over indexed card locations with wire min,
  max, optional cancel, and optional finish when the current set is legal.
- `MSG_SELECT_TRIBUTE`: continuation over cards with `release_param`; the
  evaluator follows the pinned `SelectTributeP` checks exactly, including the
  core's adjusted max and weighted minimum contribution.
- `MSG_SELECT_SUM`: continuation over optional cards and fixed mandatory cards.
  Mode 0 uses exact target reachability with one or two values per card. Mode 1
  follows the pinned greater-sum terminal predicate. Candidate generation uses
  completion search and never exposes a proven dead partial state.
- `MSG_SELECT_PLACE` and `MSG_SELECT_DISFIELD`: typed zone candidates from the
  pinned 32-bit flag, with continuation for more than one zone and duplicate
  prevention.
- `MSG_SELECT_COUNTER`: progressive exact amount allocation; no automatic
  distribution.
- `MSG_SORT_CARD` and `MSG_SORT_CHAIN`: ordered continuation and exact index
  permutation response.
- `MSG_ANNOUNCE_NUMBER`: atomic engine option candidates because the pinned
  wire count is the number of options and the core accepts one option index.
- `MSG_ANNOUNCE_RACE` and `MSG_ANNOUNCE_ATTRIB`: atomic for one bit and
  continuation masks for multiple requested bits.
- `MSG_ANNOUNCE_CARD`: fail-closed because the core sends declarability
  predicate opcodes, not a complete card domain; no guessed subset is exposed.
- `MSG_SELECT_UNSELECT_CARD`: engine-driven iterative selection remains
  atomic per engine message and is analyzed separately from `MSG_SELECT_CARD`.
- `MSG_ROCK_PAPER_SCISSORS` and `MSG_REQUEST_DECK`: remain out of scope unless
  a controlled M1 fixture makes them necessary.

## Engine boundary and errors

Applying an intermediate action returns a new request and no engine response.
Applying a terminal action returns the one response for the original message;
the caller may then call `CoreHost::submit_response` exactly once and resume
`OCG_DuelProcess`. Stale semantic keys, duplicate candidates, malformed
messages, incomplete candidate sets, unsupported domains, and impossible
continuation states fail explicitly with structured `ProtocolError` values.

## Testing and traces

Every continuation family has a brute-force small-domain oracle. Tests compare
the set of terminal semantic responses, not only candidate counts. Wire
round-trip tests build exact response bytes and controlled engine fixtures are
the only basis for `SUPPORTED_ENGINE_VERIFIED`.

M0 trace v1 remains semantically unchanged for atomic decisions. Continuation
records use `ygo.engine_trace.v2` with engine step index, decision index,
continuation ID/step, engine advancement, candidate keys, state hash, and
final response hash. A canonical semantic gameplay projection excludes
compiler, path, timestamp, machine, and CI provenance before hashing.

## Deliberate limitations

No Linux build or CI is added. No fixed global candidate cap exists. No engine
message is auto-answered by the adapter. Families whose complete legal domain
or engine acceptance cannot be demonstrated remain explicitly fail-closed.
