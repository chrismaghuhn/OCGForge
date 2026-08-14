# OCGForge M1 Decision Protocol Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a pinned-core-correct, continuation-based decision protocol for the major combinatorial OCG player-choice families while preserving M0 fail-closed and privacy guarantees.

**Architecture:** Keep parsing, constraint evaluation, continuation transitions, and response encoding as focused protocol modules. Continuation values contain no engine pointers; intermediate transitions produce no response bytes, and only terminal transitions produce one final response for the original OCG message. Extend traces with a semantic gameplay projection without changing M0 v1 meaning.

**Tech Stack:** C++17, CMake/Ninja on Windows, pinned OCG core 11.0, Python unittest/property/oracle helpers, CTest, GitHub Actions Windows runner.

---

## Baseline checkpoint

The native `dev-windows` preset cannot configure in this environment because
MSVC/LLVM and PATH Ninja are unavailable. The repository's existing
Windows-only `dev-windows-zig` fallback was configured and used for the M0
baseline. Its exact checkpoint is recorded in the final report before M1
changes. The rules lock is checked after every verification batch.

### Task 1: Preserve the authority checkpoint

**Files:** no tracked files

- [x] Run `cmake --preset dev-windows` and record the toolchain failure.
- [x] Run `cmake --preset dev-windows-zig`.
- [x] Run `cmake --build --preset dev-windows-zig --parallel`.
- [x] Run `ctest --preset dev-windows-zig --output-on-failure`.
- [x] Run `python -m unittest discover -s tests/python -v`.
- [x] Run the controlled duel probe, `git diff --check`, and the lock-file diff check.

## Slice 1: Coverage and protocol contract

### Task 2: Inventory pinned interactive messages

**Files:**
- Create: `docs/protocol/DECISION_COVERAGE.md`
- Create: `docs/protocol/decision_coverage.json`
- Test: `tests/protocol/decision_coverage_test.py`

- [ ] Record every interactive message ID from `ocgapi_constants.h`, including
  the current M0 status, target status, wire structure, response structure,
  order/combinatorial properties, fixture evidence, and exact source paths.
- [ ] Classify `ANNOUNCE_CARD` as fail-closed with the predicate-domain
  blocker and `ROCK_PAPER_SCISSORS`/`REQUEST_DECK` as out of scope unless a
  fixture proves they are required.
- [ ] Make the Python test load the JSON and assert unique IDs, complete
  statuses, and the pinned bundle/core commits.
- [ ] Run `python tests/protocol/decision_coverage_test.py`; expected result is
  a passing schema/content check before protocol code changes.

### Task 3: Publish the v1 contract

**Files:**
- Create: `docs/contracts/decision-protocol-v1.md`

- [ ] Document request identity, candidate uniqueness, semantic-key rules,
  continuation state, action kinds, stale-action rejection, engine
  immobility, final-response construction, ordered versus unordered behavior,
  privacy, and fail-closed handling.
- [ ] State explicitly that continuation steps are environment decisions, not
  separate OCG engine decisions, and exactly one final response is submitted
  for one original engine message.

## Slice 2: Continuation core

### Task 4: Add immutable protocol value types

**Files:**
- Modify: `include/ygo/protocol/action_candidate.hpp`
- Modify: `include/ygo/protocol/decision_request.hpp`
- Create: `include/ygo/protocol/continuation.hpp`
- Create: `include/ygo/protocol/selection_constraints.hpp`
- Test: `tests/protocol/continuation_core_test.cpp`

- [ ] Add typed semantic action kinds and terminal/intermediate response
  metadata without removing existing M0 fields.
- [ ] Add stable card/zone/item locators and value continuation state; do not
  expose raw `OCG_Duel` or card pointers.
- [ ] Define `apply_continuation_action` so an intermediate `PICK` returns a
  new request with no response bytes, while a terminal action returns one
  response for the original message.
- [ ] Add deterministic IDs based only on raw message hash, family, step, and
  canonical state; reject keys from older states.
- [ ] Write a failing test for intermediate engine immobility and stale-key
  rejection, run it to observe failure, then implement the smallest passing
  value model.

### Task 5: Add response builders and constraint evaluators

**Files:**
- Create: `include/ygo/protocol/response_builder.hpp`
- Create: `src/protocol/response_builder.cpp`
- Create: `src/protocol/selection_constraints.cpp`
- Test: `tests/protocol/response_builder_test.cpp`

- [ ] Encode canonical card index responses, zone tuples, counter int16
  allocations, ordered int8 permutations, option indexes, bit masks, and
  announcement card codes in the exact pinned little-endian formats.
- [ ] Implement completion predicates for min/max subsets, weighted tribute,
  exact/multi-value sums, greater-sum mode, distinct zones, counter capacity,
  and permutations.
- [ ] Ensure no builder silently truncates counts or candidates.
- [ ] Add malformed/overflow tests and run the focused CTest target.

## Slice 3: Decision-family implementations

### Task 6: Decode option, card, tribute, and unselect messages

**Files:**
- Modify: `src/protocol/message_decoder.cpp`
- Modify: `include/ygo/protocol/message_decoder.hpp`
- Test: `tests/protocol/decision_family_test.cpp`

- [ ] Preserve all `MSG_SELECT_OPTION` options and duplicate payloads with
  ordinal-based semantic keys.
- [ ] Replace the M0 single-card restriction with a canonical continuation for
  `MSG_SELECT_CARD`, including min zero, max greater than min, finish, and
  cancel behavior.
- [ ] Add weighted `MSG_SELECT_TRIBUTE` continuation using parsed
  `release_param` and the pinned `SelectTributeP` terminal rules.
- [ ] Add explicit engine-driven iterative `MSG_SELECT_UNSELECT_CARD`
  candidates and response encoding; do not merge its semantics into ordinary
  unordered selection.
- [ ] Add frame fixtures and round-trip response assertions.

### Task 7: Decode sum, placement, counters, and ordering

**Files:**
- Modify: `src/protocol/message_decoder.cpp`
- Test: `tests/protocol/decision_family_test.cpp`

- [ ] Parse both mandatory and optional sum lists, the mode byte, target,
  min/max, and two-value contribution fields.
- [ ] Generate only sum picks with a proven completion and encode the final
  optional-card index set.
- [ ] Extend place/disfield to N distinct typed zones with exact 3-byte
  responses and controller ownership.
- [ ] Add progressive counter allocation with every feasible amount and exact
  final int16 response bytes.
- [ ] Add card/chain ordering continuations and exact permutation response.

### Task 8: Decode announcements and preserve fail-closed families

**Files:**
- Modify: `src/protocol/message_decoder.cpp`
- Test: `tests/protocol/announcement_test.cpp`
- Modify: `docs/protocol/DECISION_COVERAGE.md`
- Modify: `docs/protocol/decision_coverage.json`

- [ ] Implement option-preserving number, single/multi race masks, and
  single/multi attribute masks from engine-provided domains.
- [ ] Keep announce-card predicate bytecode fail-closed with structured error
  context; add a test proving no guessed card domain is exposed.
- [ ] Add the explicit unselect-card classification to the inventory.

## Slice 4: Exhaustive and engine tests

### Task 9: Add small-domain oracle tests

**Files:**
- Create: `tests/protocol/continuation_oracle_test.py`
- Create: `tests/protocol/continuation_oracle.cpp`
- Modify: `CMakeLists.txt`

- [ ] Enumerate all subsets for card/tribute domains and compare terminal
  semantic responses with continuation reachability.
- [ ] Enumerate exact and greater sum cases with one/two contribution values
  and mandatory cards; assert equality with the pinned-core oracle.
- [ ] Enumerate all bounded counter allocations and all permutations for small
  ordering domains.
- [ ] Assert no missing, extra, or duplicate unordered terminal semantics.
- [ ] Register the tests in CTest and run them before fixture work.

### Task 10: Add controlled engine fixture coverage

**Files:**
- Create: `fixtures/protocol/README.md`
- Create: `tests/fixtures/protocol_fixture_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] Add focused deterministic fixture metadata with deck hashes, seed,
  bundle ID, target message, and bounded stopping condition.
- [ ] Use only pinned CardScripts/BabelCDB data and prove engine acceptance of
  final responses without `MSG_RETRY`, crash, or silent correction.
- [ ] Mark any family without engine evidence protocol-verified or fail-closed;
  do not upgrade status from parser tests alone.

### Task 11: Add privacy and invariant tests

**Files:**
- Modify: `tests/privacy/player_view_test.cpp`
- Create: `tests/protocol/continuation_privacy_test.cpp`

- [ ] Verify continuations contain only acting-player-visible locators and
  message-authorized data for card, tribute, sum, and ordering cases.
- [ ] Assert candidate count equals serialized candidate count, keys are unique,
  state IDs change deterministically, intermediate steps have no response, and
  unsupported messages fail closed.

## Slice 5: Traces and semantic gameplay hash

### Task 12: Add continuation-capable trace records

**Files:**
- Modify: `include/ygo/trace/engine_trace.hpp`
- Modify: `src/trace/engine_trace.cpp`
- Create: `docs/contracts/engine-trace-v2.md`
- Test: `tests/determinism/semantic_trace_test.py`

- [ ] Preserve v1 canonical serialization for atomic M0 traces.
- [ ] Add v2 fields for decision index, engine step, message type,
  continuation ID/step, engine advancement, candidate keys, state hash, and
  final response hash; intermediate records serialize `engine_advanced=false`
  and a null final response hash.
- [ ] Implement the canonical semantic gameplay projection and
  `semantic_gameplay_hash` excluding compiler, path, timestamp, machine, and
  CI provenance.
- [ ] Verify same-seed repeated and independent-process semantic hashes.

## Slice 6: Windows CI and final verification

### Task 13: Extend the existing Windows workflow

**Files:**
- Modify: `.github/workflows/m0.yml`

- [ ] Keep the workflow Windows-only and retain every M0 step.
- [ ] Add coverage-schema, protocol, oracle, fixture, privacy, semantic-hash,
  generated-lock-integrity, and focused engine tests.
- [ ] Do not add Linux jobs, floating dependencies, or candidate caps.

### Task 14: Run the complete final matrix

**Files:** no tracked files

- [ ] Run `python -m unittest discover -s tests/python -v`.
- [ ] Run `cmake --preset dev-windows-zig` and
  `cmake --build --preset dev-windows-zig --parallel`.
- [ ] Run `ctest --preset dev-windows-zig --output-on-failure`.
- [ ] Run the controlled smoke and same-seed/independent-process determinism
  tests, recording artifact and semantic gameplay hashes.
- [ ] Run `git diff --check`, verify the rules lock has no diff, and report
  `git status --short --branch` without committing.
- [ ] Produce the requested PASS/PENDING/NOT APPLICABLE acceptance matrix and
  list every remaining fail-closed family.
