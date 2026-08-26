# Episodic Environment V1 — Phase 1: Shared EpisodeDriver Extraction

**Status:** implementation specification
**Normative architecture:** ADR-0002
**Normative public contract:** ocgforge.episodic_environment.v1
**Phase:** EpisodeDriver extraction only
**Semantic change:** NONE
**Production implementation:** NOT INCLUDED IN THIS PR
**Architecture review:** GO — 2026-08-26
**Inspected live base:** main at 72c29009f107a2ebb172d85de1c70b38d2f007d8

This document is an implementation plan and code map. It does not replace
ADR-0002, the accepted episodic contract, the existing decision, observation,
or trace contracts, or M3/M4 acceptance evidence.

## 1. Decision, scope, and non-goals

Phase 1 extracts one internal authoritative advancement implementation from
the canonical simulation path. The extracted driver becomes the sole writer of
existing trace transitions and the sole caller of the active duel's process and
response APIs. The canonical evaluator becomes a client that supplies a
semantic key chosen by its existing replay or deterministic conformance policy.

~~~text
Before

run_canonical_simulation
  CoreHost lifecycle + process + decode + observation + continuation
  + response + trace + policy + result/worker concerns

After Phase 1

run_canonical_simulation
  canonical identity, seat resolution, replay/policy key choice,
  result/hash/persistence, worker-compatible result formation
             |
             v
EpisodeDriver
  fresh duel, process/decode, complete decision boundary, observation,
  continuation, exact response, existing trace transitions, closure
~~~

The refactor is semantic-neutral. For the locked existing corpus, the same
job, rules bundle, decks, seed, seat assignment, starting player, and policy
behavior must produce the same ordered decisions, observations, continuations,
responses, terminal result, trace semantics, and semantic gameplay hash.

Phase 1 does not implement or expose any of the following:

- a public EpisodicEnvironment;
- reset, step, or a submission freshness token;
- public EnvironmentConfig, EpisodeSpec, RunControl, StepRejected, or
  EpisodeInterrupted;
- a semantic-action budget;
- rewards, trajectory schemas, writers, teachers, model adapters, tensors, or
  ML algorithms;
- new mechanics, arbitrary decks, worker scheduling, a new library target, or
  a target rename;
- any generated acceptance evidence in this documentation PR.

The current process budget remains exactly the canonical M4 max_steps behavior.
The future public semantic-action budget is Phase 2 work because it defines a
new public interruption semantic and is not required to preserve the current
canonical loop.

## 2. Live implementation basis

The inspected repository places CoreHost, protocol, continuations, trace, and
observation in ygo_m0. The ygo_m4 static library currently contains only
src/simulation/canonical_simulation.cpp and links ygo_m0. Both ygo_core_probe
and ygo_m4_worker link ygo_m4. Therefore the smallest correct Phase-1 location
is:

~~~text
include/ygo/environment/episode_driver.hpp       proposed internal header
src/environment/episode_driver.cpp               proposed implementation
CMakeLists.txt: ygo_m4 source list               proposed compilation owner
~~~

The new files are not created by this PR. They remain in ygo_m4 for Phase 1.
Adding a ygo_environment target, renaming ygo_m4, or moving lower layers is
out of scope. The name ygo_m4 is historical, but its present dependency edge
is the narrowest integration seam.

EpisodeDriver must not depend on SimulationJob, CanonicalSimulationConfig, or
SimulationResult. The canonical evaluator translates those values into a
small internal driver input and maps driver results/metrics back. This keeps
the logical driver below simulation policy/evaluation even though both sources
compile into ygo_m4 during Phase 1.

The current authoritative loop is
src/simulation/canonical_simulation.cpp, especially
run_canonical_simulation:

- seed_bundle derives the four-word deterministic seed;
- the function builds CoreHostConfig, constructs CoreHost, loads decks, starts
  the duel, and optionally loads a test-only setup script;
- one process loop obtains raw messages, ingests two ObservationSessions,
  decodes protocol frames, recognizes retry and terminal messages, and rejects
  multiple interactive decisions;
- the inner loop validates each complete candidate set, lets replay or
  DeterministicConformancePolicy choose a candidate, builds an acting-player
  observation, verifies visible candidate locators, creates a trace step, and
  applies atomic or continuation actions;
- terminal continuation and atomic actions submit an exact response; an
  intermediate continuation changes only adapter-local request state;
- the function aggregates metrics, hashes and optionally persists the trace,
  then formats SimulationResult for the worker.

The core and lower-layer behavior that must be reused rather than copied is:

| Layer | Existing authority | Phase-1 rule |
| --- | --- | --- |
| Core | CoreHost construction/destruction, load_deck, start_duel, process, submit_response, and query methods | Driver calls the existing methods; it does not wrap or reinterpret ocgcore. |
| Protocol | decode_messages, DecisionRequest, ActionCandidate, validate_candidate_set, select_candidate, semantic keys | Driver consumes the protocol-owned complete order element-for-element. |
| Continuation | make_continuation_request and apply_continuation_action | Driver owns the active request but delegates every state transition to the existing continuation layer. |
| Observation | ObservationSession, build_player_observation, attach_decision_context, candidate_observation_consistent | Driver orchestrates the existing perspective-safe flow; it does not reimplement visibility or locators. |
| Trace | make_decision_step, attach_observation_metadata, canonical trace serialization, semantic gameplay hashing | Driver writes all transition steps; canonical simulation only reads the completed trace and derives existing result fields. |
| M4 | run_canonical_simulation and worker JSON protocol | The worker remains unchanged and calls the refactored canonical evaluator exactly as it does today. |

## 3. Target architecture and exact authority boundary

EpisodeDriver sits above CoreHost, protocol, continuations, observation, and
trace primitives, and below the canonical evaluator and the future episodic
facade. It contains no policy strategy and no worker transport logic.

~~~text
Canonical evaluator                         Future facade, Phase 2 only
  validates canonical identity                 reset / step
  selects existing semantic key                    |
  derives result/hash/persistence                  |
                    \                              /
                     \                            /
                       internal EpisodeDriver
                 process -> complete boundary -> apply key
                 trace transition writer and duel owner
                               |
               CoreHost + protocol + observation + trace primitives
~~~

The driver does not choose a strategic action. For every published decision,
the client obtains the existing request and acting-player observation, chooses
one existing semantic_key, and calls the driver to apply that key. The driver
performs membership validation again before any mutation, owns response
submission, and then either publishes the next boundary or closes the duel.

The canonical evaluator must never reconstruct a trace step, call
CoreHost::process, call CoreHost::submit_response, decode raw engine bytes,
build an observation, apply a continuation, or append a trace transition after
migration. A read of the completed EngineTrace for hashing, serialization, and
result formation is permitted and required.

## 4. Responsibility map

Risk values describe the risk if the responsibility is moved incorrectly:
semantic, privacy, and determinism. “Exact equivalence test” names the
required Phase-1 proof, not a test claimed to have run in this documentation
PR.

| Responsibility | Current file/function | Current owner | Proposed Phase-1 owner | MOVE / WRAP / LEAVE | Mutable state involved | Semantic risk | Privacy risk | Determinism risk | Exact equivalence test |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Canonical identity validation | canonical_simulation.cpp, is_canonical_identity | Canonical evaluator | Canonical evaluator | LEAVE | SimulationResult failure state | High | None | High | P1-G11 canonical identity negative case |
| Seat assignment and fixed-deck selection | run_canonical_simulation | Canonical evaluator | Canonical evaluator, then immutable driver input | LEAVE / pass by value | resolved deck order | High | Medium | High | P1-G01/P1-G09 manifest deck order |
| Seed derivation | seed_bundle and CoreHostConfig setup | Canonical evaluator | EpisodeDriver initialization | MOVE | seed words | High | None | High | P1-G01 seed bundle and trace equality |
| Fresh CoreHost construction/destruction | run_canonical_simulation scope | Canonical evaluator | EpisodeDriver | MOVE | unique duel lifetime | High | High | Medium | P1-G14 fresh-duel isolation |
| Deck loading | CoreHost::load_deck calls | Canonical evaluator | EpisodeDriver | MOVE | active duel deck state | High | High | High | P1-G01 manifests and P1-G09 results |
| Duel start and optional fixture script | start_duel and load_fixture_script | Canonical evaluator | EpisodeDriver | MOVE | active duel/script state | High | High | High | P1-G01 plus P1-G11 fixture failure paths |
| Engine process loop | CoreHost::process loop | Canonical evaluator | EpisodeDriver | MOVE | process counter, raw frame | High | Medium | High | P1-G03/P1-G08/P1-G14 |
| Raw message acquisition | ProcessResult.message | Canonical evaluator | EpisodeDriver | MOVE | current raw message value | High | High | High | P1-G05/P1-G08 |
| Observation-session ingestion | ObservationSession::ingest twice per process result | Canonical evaluator | EpisodeDriver | MOVE | sessions for player 0 and 1 | High | High | High | P1-G04/P1-G12 |
| Protocol decoding | decode_messages | Canonical evaluator | EpisodeDriver | MOVE | decoded message and request | High | High | High | P1-G03/P1-G05/P1-G11 |
| Retry handling | decoded.retry branch | Canonical evaluator | Driver detects; canonical preserves SimulationResult mapping | WRAP | retry counter and failed state | High | None | High | P1-G11 retry probe |
| Terminal detection and winner/reason | decoded.terminal branch | Canonical evaluator | EpisodeDriver | MOVE | terminal state, winner, reason | High | Medium | High | P1-G08/P1-G09 |
| Candidate validation | validate_candidate_set | Protocol called by canonical | Protocol, orchestrated by EpisodeDriver | WRAP | current request only | High | None | High | P1-G03/P1-G11 |
| Candidate membership and order | decoder-produced request.candidates | Protocol | Protocol | LEAVE | protocol vector | Blocker | Medium | Blocker | P1-G03 exact ordered transcript |
| Candidate counters and maxima | run_canonical_simulation counters | Canonical evaluator | EpisodeDriver metrics | MOVE | candidate sets, total, max | Medium | None | High | P1-G01/P1-G03 maximum witness |
| Acting-player identification | DecisionRequest.player | Protocol/canonical | Protocol; driver consumes it | LEAVE / WRAP | current request player | High | Blocker | High | P1-G04/P1-G05/P1-G12 |
| PlayerObservation construction | build_player_observation | Canonical evaluator calls observation layer | Observation layer, orchestrated by EpisodeDriver | WRAP | current active observation | High | Blocker | High | P1-G04/P1-G12 |
| Observation finalization and hash | attach_decision_context | Canonical evaluator calls observation layer | Observation layer, orchestrated by EpisodeDriver | WRAP | observation hash/context | High | Blocker | High | P1-G04/P1-G05 |
| Candidate/observation consistency | candidate_observation_consistent loop | Canonical evaluator | Observation layer, orchestrated by EpisodeDriver | WRAP | no new identity state | High | Blocker | High | P1-G04/P1-G11/P1-G12 |
| Canonical policy selection | replay action lookup and DeterministicConformancePolicy::choose | Canonical evaluator | Canonical evaluator | LEAVE | replay index and policy-local state | High | High | High | P1-G01 ordered selected keys |
| Semantic-key resolution | select_candidate or policy return | Canonical evaluator/protocol | Protocol, invoked by EpisodeDriver before mutation | WRAP | current request remains valid on rejection | High | Medium | High | P1-G03/P1-G11 stale key |
| Continuation creation | decoder and make_continuation_request | Protocol | Protocol | LEAVE / WRAP | continuation value within request | Blocker | High | Blocker | P1-G06 continuation transcript |
| Intermediate continuation application | apply_continuation_action and inner loop | Canonical evaluator | EpisodeDriver | MOVE | replacement current request, step count | Blocker | High | Blocker | P1-G06 and P1-G17 |
| Terminal continuation application | apply_continuation_action and submit | Canonical evaluator | EpisodeDriver | MOVE | final response, state closure | Blocker | High | Blocker | P1-G06/P1-G07 |
| Atomic exact response submission | CoreHost::submit_response | Canonical evaluator | EpisodeDriver | MOVE | response count and paused core | Blocker | High | Blocker | P1-G07 |
| Semantic-action counting | inner-loop increment | Canonical evaluator | EpisodeDriver metrics | MOVE | accepted semantic action count | High | None | High | P1-G01/P1-G10 |
| Existing trace manifest and steps | manifest, make_decision_step, terminal record | Canonical evaluator | EpisodeDriver only | MOVE | EngineTrace and decision index | Blocker | Medium | Blocker | P1-G08/P1-G10 |
| Existing gameplay hash | semantic_gameplay_hash(trace) | Canonical evaluator | Canonical evaluator reading driver trace | LEAVE | SimulationResult gameplay_hash | High | None | High | P1-G10 |
| Trace serialization and persistence | canonical_trace_jsonl_v2 and file write | Canonical evaluator | Canonical evaluator reading driver trace | LEAVE | result trace fields, file output | High | Medium | High | P1-G08/P1-G16 |
| Engine-process budget | for index < job.max_steps | Canonical evaluator | EpisodeDriver | MOVE | process index/state | High | None | High | P1-G11 nonterminal max-steps probe |
| Protocol diagnostic context | emit_unsupported_diagnostic | Canonical evaluator | EpisodeDriver, using its live trace and raw message | MOVE | failure context only | High | Medium | Medium | P1-G11 unsupported/malformed probes |
| Result aggregation and error-code formatting | SimulationResult setup and catches | Canonical evaluator | Canonical evaluator | LEAVE | result, error counters, timings | High | None | Medium | P1-G09/P1-G11 |
| Worker-facing result formation | ygo_m4_worker main and JSON protocol | M4 worker | M4 worker | LEAVE | value-only worker result | High | None | High | P1-G13/P1-G14 |

## 5. Driver-owned state and lifetime

EpisodeDriver owns the mutable advancement state. It is non-copyable and
non-movable because CoreHost is non-copyable and non-movable. The driver is
created once per canonical job and is destroyed before that job's
SimulationResult leaves run_canonical_simulation.

| Object | Form and construction | Destruction | Boundary survival | Authority and exposure |
| --- | --- | --- | --- | --- |
| Resolved driver configuration | Value. It contains the already selected seat-order decks, rules paths, seed, starting player, process budget, observation mode, required scripts, and test-only setup path. | Driver destruction. | Never borrowed by a returned boundary. | Authoritative input snapshot. It is not a public V1 config. |
| CoreHost | Unique ownership through std::unique_ptr because the existing type is non-movable. Construct after driver input is accepted; load both decks, start the duel, then load the existing optional fixture script in the current order. | Immediately on failure teardown or driver destruction. | Never crosses a boundary. | The only mutable core owner. No reference, pointer, query buffer, or engine address is exposed. |
| ObservationSession[2] | Values constructed with perspectives 0 and 1 after duel setup. | Driver destruction. | Never crosses a boundary. | Authoritative visible-event accumulators. Neither session is exposed. |
| Current raw message | Value vector retained while its decision and any adapter-local continuation are active. | Replaced only after a final response allows another process result, or destroyed. | Never crosses a boundary. | Trace/diagnostic input only; never returned to policy/client. |
| Current DecisionRequest | Optional value. The driver adopts the decoded request and replaces it with the protocol-created continuation request after an intermediate action. | Cleared before an accepted final response or on closure. | Address borrowed by DriverDecisionBoundary only until the next mutating call or destruction. | Authoritative candidate vector and continuation state. The driver does not clone or sort candidates. |
| Current PlayerObservation | Optional value built only in the existing Full observation mode; Deferred finalization is completed once by attach_decision_context. | Replaced when a new request is published or cleared on closure. | Address borrowed by DriverDecisionBoundary only until the next mutating call or destruction. | Perspective-safe active observation only. No opponent observation is returned. |
| EngineTrace | Value, initialized with the existing v2 manifest. | Driver destruction after canonical simulation has read it. | Read-only reference may be used only while driver lives; it is not a decision boundary. | EpisodeDriver is the sole transition writer. Canonical simulation has read-only access after or during closure. |
| Driver metrics | Value: existing process-call and response-submission counts plus candidate, observation, continuation, response-time, and semantic-action counters. | Driver destruction after a snapshot is copied to SimulationResult. | Read-only reference may be used only while driver lives; it is not a decision boundary. | Accounting source for moved advancement work. It is not a policy input. |
| Lifecycle state | Private enum and counters: setup, advancing, awaiting decision, terminal, budget-exhausted, failed; existing trace decision index and current engine process index. | Driver destruction. | Never crosses a boundary. | Prevents skipped decisions and post-terminal mutation. It is not public V1 lifecycle data. |
| Performance-audit collector, when compiled | Borrowed pointer supplied by the canonical evaluator's existing stack-local audit collector. | The collector outlives the driver. | Never crosses a boundary. | Existing instrumentation only. The driver does not retain it outside the job. |

### 5.1 Boundary lifetime rule

DriverDecisionBoundary contains non-owning pointers to the driver's current
DecisionRequest and, in Full observation mode, PlayerObservation. The pointer
representation is a borrowed view, not an ownership or nullability loophole:
`request != nullptr` is an invariant of every returned
DriverDecisionBoundary; `observation != nullptr` is an invariant in Full
observation mode, and `observation == nullptr` is permitted only in the
existing `ObservationMode::None`. The rule is hard and must be documented in
the actual internal header:

~~~text
DriverBoundary references remain valid only until the next mutating
EpisodeDriver call or driver destruction.
~~~

The evaluator must not retain a boundary or dereference either borrowed
pointer across apply_semantic_key, advance_until_boundary, or driver
destruction. A successful mutating call invalidates all previous boundary
views. `ProtocolErrorCode::InvalidSemanticKey` is explicitly pre-mutation:
the driver remains `AWAITING_DECISION` and the current values remain intact,
but the canonical client handles that exception as the existing job error and
does not reuse the old boundary as a retry or rejection protocol.

This lifetime rule prevents an internal view from becoming an accidental
long-lived public API. It also prevents a policy/client from holding a
continuation's old candidate domain after a PICK or ASSIGN_AMOUNT has created a
new domain.

## 6. Minimal internal C++ surface

This is a documentation-only sketch. It is not a public contract and must not
be copied into a Phase-1 public header without a separate accepted decision.

~~~cpp
namespace ygo::environment {

struct EpisodeDriverConfig final {
    core::RulesBundlePaths rules;
    core::FixtureDeck player_zero_deck;
    core::FixtureDeck player_one_deck;
    std::uint64_t seed = 0;
    std::uint64_t duel_flags = 0;
    std::uint8_t starting_player = 0;
    std::uint32_t engine_process_budget = 0;
    bool build_full_observation = true;
    std::vector<std::uint32_t> required_script_codes;
    std::filesystem::path fixture_setup_script;  // existing test-only hook
    bool instrumentation = false;
    bool force_unsupported_for_test = false;     // existing job probe only
};

struct DriverDecisionBoundary final {
    const protocol::DecisionRequest* request = nullptr;  // invariant: non-null
    const observation::PlayerObservation* observation = nullptr;
        // non-null in Full mode; null only for existing ObservationMode::None
};

struct DriverGameTerminal final {
    std::uint8_t winner;
    std::uint8_t win_reason;
};

struct DriverProcessBudgetExceeded final {
    std::uint32_t process_calls;
};

struct DriverErrorCounters final {
    std::uint64_t retries = 0;
    std::uint64_t unsupported = 0;
    std::uint64_t automatic = 0;
    std::uint64_t truncated = 0;
    std::uint64_t core_errors = 0;
};

struct DriverFailure final {
    DriverErrorCounters errors;
    std::string failure_code;
    std::string error_message;
};

using DriverBoundary =
    std::variant<DriverDecisionBoundary, DriverGameTerminal,
                 DriverProcessBudgetExceeded, DriverFailure>;

class EpisodeDriver final {
public:
    explicit EpisodeDriver(EpisodeDriverConfig config);

    DriverBoundary advance_until_boundary();
    DriverBoundary apply_semantic_key(const std::string& semantic_key);

    const trace::EngineTrace& trace() const noexcept;
    const DriverMetrics& metrics() const noexcept;

private:
    // CoreHost, sessions, current raw message/request/observation,
    // trace, counters, and lifecycle state.
};

}  // namespace ygo::environment
~~~

The pointer members keep `DriverDecisionBoundary`, and therefore
`DriverBoundary`, normally copy/move assignable for the canonical client loop
(`boundary = driver.apply_semantic_key(...)`). The returned-boundary invariants
above, rather than reference-member assignment behavior, enforce validity.

The constructor is needed because current canonical execution constructs a
fresh duel before it enters the process loop. It owns that setup rather than
accepting a CoreHost reference. advance_until_boundary is needed for initial
automatic processing and for the next engine result after a final response.
apply_semantic_key is needed so the client cannot submit response bytes or
mutate continuation state directly. trace and metrics are read-only result
inputs for canonical aggregation.

DriverMetrics includes read-only mirrors of CoreHost process_call_count and
response_submission_count so an integration test can prove continuation
immobility without receiving a CoreHost reference.

| Surface element | Why Phase 1 needs it | Consumer | Authority and visibility | Phase-2 disposition |
| --- | --- | --- | --- | --- |
| EpisodeDriverConfig | Transfers already resolved job inputs without CoreHost ownership escaping. | Canonical evaluator and test integration. | Internal value-only configuration; contains no public V1 identity or control schema. | Replace or adapt only when a public facade has accepted inputs. |
| DriverDecisionBoundary.request | Lets the current canonical replay/conformance policy inspect the existing complete semantic domain. | Canonical evaluator only. | Non-null borrowed authoritative protocol pointer; response-byte implementation detail is not widened into a public API. | Replaced by a versioned public DecisionFrame/view. |
| DriverDecisionBoundary.observation | Makes the current perspective-safe active observation available when current Full mode already builds one. | Canonical evaluator and future internal facade client. | Borrowed and perspective-safe; non-null in Full mode and null only for the pre-existing None mode. | Replaced by public DecisionFrame observation field. |
| DriverGameTerminal | Lets canonical result formation obtain the existing engine-defined winner/reason without inspecting CoreHost. | Canonical evaluator. | Internal, authoritative only for a true decoded terminal. | Maps to public EpisodeTerminal later. |
| DriverProcessBudgetExceeded | Keeps current max_steps closure distinct from a game terminal. | Canonical evaluator. | Internal, non-gameplay control result. | Maps to public Interrupted only after Phase-2 RunControl work. |
| DriverFailure | Preserves fail-closed execution information while keeping CoreHost private. | Canonical evaluator. | Internal result mapped to current SimulationResult; never a public failure contract. | Replaced by a versioned public EpisodeFailure later. |
| advance_until_boundary | Owns all automatic processing up to exactly one next decision, terminal, budget closure, or failure. | Canonical evaluator. | Authoritative mutating operation; never callable while a decision is pending. | Future facade uses the same internal operation, not a public method. |
| apply_semantic_key | Enforces protocol membership validation, continuation application, exact response submission, and post-submit advancement in one authority. | Canonical evaluator. | Authoritative mutating operation; accepts a semantic key, not bytes, index, or CoreHost handle. An unknown/stale key propagates the existing `ProtocolErrorCode::InvalidSemanticKey` before mutation. | Future facade adapts it behind versioned action/freshness validation. |
| trace and metrics | Preserve current result hash, serialization, persistence, and counter work without duplicate transition construction. | Canonical evaluator only. | Read-only internal views; driver is the only trace writer. | Future audit/telemetry contracts must be separately versioned. |

DriverBoundary is internal now. The accepted Phase-2 public concepts listed
below remain deferred:

| Internal in Phase 1 | Public only in Phase 2 |
| --- | --- |
| DriverDecisionBoundary with borrowed current values | EnvironmentConfig, EpisodeSpec, DecisionFrame, ActionSelection, StepResult |
| DriverGameTerminal, DriverProcessBudgetExceeded, and DriverFailure as internal control results | public EpisodeTerminal, EpisodeInterrupted, EpisodeFailure, StepRejected |
| Existing trace decision index and response/hash fields | public policy decision_index, semantic decision ID wrapper, candidate-domain contract digest, core_response_submitted |
| Existing process limit | RunControl semantic-action budget and administrative cancellation |
| Per-driver lifetime discipline | submission_token and public freshness rules |

After successful construction, protocol, core, and observation execution
errors return the private DriverFailure result after the driver marks itself
failed and destroys its mutable CoreHost. Canonical simulation copies that
result into the current SimulationResult mapping. Errors before a driver
exists, such as CoreHost construction failure, remain on the current canonical
exception path. The deliberate exception is an unknown or stale semantic key:
`apply_semantic_key` propagates the existing
`ProtocolErrorCode::InvalidSemanticKey` before mutation, while the driver
remains `AWAITING_DECISION`; canonical maps it through the current job-level
ProtocolError path. No Phase-1 public failure DTO or `StepRejected` is
introduced.

## 7. Exact advancement algorithm

### 7.1 Initialization

~~~text
construct EpisodeDriver(config):
  retain a value copy of resolved input
  derive the current four-word seed from config.seed exactly as seed_bundle does
  create CoreHostConfig with current rules, flags, starting player,
    seed words, required script codes, and existing audit pointer when enabled
  uniquely construct CoreHost
  load player_zero_deck into team 0 and player_one_deck into team 1
  start_duel
  if existing fixture_setup_script is non-empty:
    call the existing test-only load_fixture_script after start_duel
  construct ObservationSession(0, duel_flags) and ObservationSession(1, duel_flags)
  construct the current ygo.engine_trace.v2 manifest from CoreHost and decks
  set state = advancing, process index = 0, trace decision index = 0
~~~

The input has already been checked by canonical identity and seat-resolution
logic. The driver must not silently substitute a rules bundle, deck, seed,
starting player, script, or observation mode.

### 7.2 Advance until a boundary

~~~text
advance_until_boundary():
  require state == advancing

  if force_unsupported_for_test:
    run the existing synthetic malformed/unsupported decode only through the
    private driver test hook; emit the current diagnostic shape, mark failed,
    tear down, and return DriverFailure with the current forced_unsupported
    counter/code mapping without processing the duel

  while process calls < engine_process_budget:
    engine_step = next engine process index
    raw = CoreHost.process()
    increment next engine process index exactly once
    retain raw as current raw message
    ingest raw once into each existing ObservationSession at engine_step
    decoded = protocol.decode_messages(raw, engine_step)

    if decoded.retry:
      record the existing retry condition and return DriverFailure; never retry
      or choose

    if decoded.terminal:
      allocate the current v2 terminal TraceStep with
        decision_index = current trace index, then increment that index
      build only the current player-0 terminal observation when Full mode,
        using the existing observation layer with the post-incremented current
        trace index exactly as the existing terminal path does
      attach its existing metadata to the terminal step
      append the terminal step exactly once
      set state = terminal and return DriverGameTerminal(winner, win_reason)

    if decoded is noninteractive:
      continue

    if decoded has not exactly one interactive DecisionRequest:
      return DriverFailure through the current unsupported protocol mapping

    current_request = decoded request by value
    publish_current_request()
    return DriverDecisionBoundary(address of current_request,
                                  address of current_observation if Full, else null)

  set state = budget-exhausted
  return DriverProcessBudgetExceeded(CoreHost.process_call_count())
~~~

publish_current_request performs the same existing work for an initial decoded
request and for a continuation-produced request:

~~~text
validate_candidate_set(current_request)
increment candidate-set, total-candidate, and maximum-candidate counters
if Full observation mode:
  build PlayerObservation for current_request.player with:
    decision_index = current trace decision index
    engine_step_index = current_request.engine_step_index
    that player's existing visible-event session
    the same own-deck knowledge and resolved seat deck
    Deferred finalization
  attach_decision_context once
  verify each visible source/target candidate locator with
    candidate_observation_consistent
  retain this one observation by value
otherwise:
  retain no observation; do not fabricate an empty observation
set state = awaiting decision
~~~

No policy executes inside this algorithm. The driver neither fabricates a
response nor advances past an interactive request.

Every “return DriverFailure” branch follows Section 7.6: it records the
existing failure classification/counters, preserves diagnostic context while
available, tears down the mutable duel, and leaves no live boundary.

### 7.3 Apply an atomic candidate

For either atomic or continuation application, `select_candidate` is the
first operation after the `AWAITING_DECISION` state check. If it reports the
existing `ProtocolErrorCode::InvalidSemanticKey` for an unknown or stale key,
the driver propagates that `ProtocolError` immediately. It does not create a
TraceStep, change counters, replace request/observation state, submit a
response, call `process`, return `DriverFailure`, or leave
`AWAITING_DECISION`. The canonical client maps the exception exactly as it
does today for a job-level ProtocolError; it must not retry, retain/reuse the
old boundary, or expose a Phase-2-style `StepRejected` result.

~~~text
apply_semantic_key(key), when current_request has no continuation:
  require state == awaiting decision
  selected = protocol.select_candidate(current_request, key)
    // InvalidSemanticKey propagates before any mutation.
    // Membership and complete-domain validation occur before mutation.
  create the existing decision TraceStep from retained raw message,
    current request, and existing internal public-state hash query
  attach current observation metadata when present
  set existing trace decision index and selected semantic key
  set engine_advanced = true
  set selected_response_sha256 and final_engine_response_hash from the
    selected existing exact response bytes
  append the step to driver trace
  increment semantic-action count
  clear current boundary values and set state = advancing
  CoreHost.submit_response(selected.exact_response_bytes)
  increment no additional policy or continuation counter
  return advance_until_boundary()
~~~

The driver owns both candidate lookup and response submission. The canonical
evaluator supplies only the selected semantic_key. It may keep the existing
conformance policy's choice mechanism, but it must pass only that chosen key
back to the driver. No selected ActionCandidate reference survives the call.

### 7.4 Apply an intermediate continuation action

~~~text
apply_semantic_key(key), when current_request has a continuation:
  require state == awaiting decision
  selected = protocol.select_candidate(current_request, key)
    // InvalidSemanticKey propagates before any mutation.
  transition = protocol.apply_continuation_action(current_request, selected.semantic_key)

  if transition is intermediate:
    create and append the existing decision TraceStep:
      same retained raw message and same engine_step_index
      selected semantic key
      engine_advanced = false
      no selected/final engine response bytes or final response hash
    increment semantic-action and continuation-intermediate counters
    replace current_request with transition.request
    do not call CoreHost.submit_response
    do not call CoreHost.process
    publish_current_request using the new continuation identity/state and
      a new perspective-safe acting-player observation
    return DriverDecisionBoundary for the new request
~~~

This covers PICK, ASSIGN_AMOUNT, and every intermediate state of unordered
selection, tribute, sum, zone placement, counter allocation, ordering, and
announcement masks. The new request is produced by the protocol layer and
therefore retains its existing continuation ID, state hash, candidate order,
and same engine_step_index semantics.

### 7.5 Apply a terminal continuation action

~~~text
  if transition is terminal:
    create and append the existing decision TraceStep:
      selected semantic key
      engine_advanced = true
      selected_response_sha256 = SHA-256(transition.engine_response)
      final_engine_response_hash = same SHA-256
    increment semantic-action count
    clear current boundary values and set state = advancing
    CoreHost.submit_response(transition.engine_response) exactly once
    return advance_until_boundary()
~~~

FINISH, CANCEL when it is terminal, ordering completion or bypass, and every
terminal continuation use the protocol-produced exact response bytes. The
driver does not create macro-actions and does not submit a response for any
intermediate action.

### 7.6 Failure and teardown

~~~text
on retry, unsupported/malformed decoding, incomplete candidates,
candidate/observation inconsistency, observation failure, core failure,
or response failure:
  preserve the current diagnostic inputs while they are still valid
  mark the private lifecycle state failed
  do not choose, cancel, retry, draw, lose, or fabricate a response
  snapshot the current error category, counters, failure code, and message
  destroy the unique CoreHost before returning DriverFailure
~~~

The canonical client maps DriverFailure into the existing SimulationResult
counter/code/string fields. It must not resume or repair a failed driver.

## 8. Existing trace semantics remain exact

EpisodeDriver is the sole writer of existing EngineTrace transitions. It owns
the EngineTrace value, manifest, and TraceStep vector. Canonical simulation may
read the completed trace, calculate semantic_gameplay_hash, serialize
canonical_trace_jsonl_v2, derive the existing trace hash, and persist the
existing output. It must not parallel-reconstruct transitions from callbacks,
response events, or policy decisions.

| Existing or future field/meaning | Phase-1 classification | Required behavior |
| --- | --- | --- |
| Trace manifest and ygo.engine_trace.v2 schema | KEEP EXACTLY | Driver creates the same manifest fields, deck order, seed bundle, policy identifier, and schema version. |
| TraceStep.decision_index | KEEP EXACTLY | Driver increments it for every existing trace step, including continuation steps and the terminal record. It is not the future policy-only V1 index. |
| TraceStep.engine_step_index | KEEP EXACTLY | An intermediate continuation retains the original engine index; a process result advances it only through the process loop. |
| TraceStep.engine_advanced | KEEP EXACTLY | Atomic and terminal-continuation steps are true; intermediate continuation steps are false; terminal record remains true. |
| selected_response_sha256 | KEEP EXACTLY internally | Preserve current in-memory population; do not invent a new serialized meaning. |
| final_engine_response_hash | KEEP EXACTLY | It is absent for intermediates and hashes the exact submitted final response for atomic/terminal actions. |
| Terminal TraceStep | KEEP EXACTLY | One record with the existing winner/reason, raw-message hash, public-state hash, and player-0 terminal observation metadata behavior. |
| Continuation identity/state fields | KEEP EXACTLY | Copy only from the existing protocol request into make_decision_step. |
| canonical_trace_jsonl_v2 and canonical trace hash | KEEP EXACTLY | Same-build equivalence is byte and hash exact. Provenance differences remain distinct from gameplay semantics. |
| semantic_gameplay_hash | KEEP EXACTLY | Canonical evaluator derives it from the driver-owned finished trace using the existing function. |
| Future policy decision_index | PHASE 2 NEW FIELD | Do not add or alias it to TraceStep.decision_index. |
| Future core_response_submitted | PHASE 2 NEW FIELD | Do not reinterpret engine_advanced to mean it. |
| Future candidate-domain digest | INTERNAL TEST SIDECAR NOW; public V1 later | A Phase-1 characterization digest has a separate test-only schema and never changes trace bytes or gameplay hash. |

The internal public-state hash query remains a trace-only operation inside the
driver. It is not a policy observation and must not be returned through
DriverDecisionBoundary.

## 9. Locked characterization corpus and comparison artifact

Phase 1 does not invent a new gameplay workload. It adds characterization
resolution over the existing accepted and understood workload only.

| Corpus member | Existing source | Role in Phase 1 | New work allowed |
| --- | --- | --- | --- |
| Canonical 16-game matrix | tests/m3/full_game/full_fixed_deck_test.py; seeds 1 through 4, normal/mirror seats, starting players 0 and 1, 2200 process steps | Primary end-to-end semantic-equivalence corpus. It proves complete games, both seat and start partitions, terminal results, hashes, traces, and observation history. | Capture a normalized sidecar transcript from its existing trace/result outputs. |
| Shared-simulation full game | tests/m4/test_shared_simulation_compatibility.py, seed 2/start player 0/max steps 1800 | Existing compatibility characterization for semantic hash, trace hash, zero error counters, and candidate mean. | Compare the same invocation through the refactored evaluator. |
| Existing nonterminal probe | same test, seed 2/start player 0/max steps 1 | Proves current max_steps exhaustion remains nonterminal with the existing successful probe exit and no invented interruption type. | Capture/compare its partial trace, result, and counters. |
| Existing forced unsupported probe | same test with force-unsupported | Proves malformed/unsupported protocol remains diagnostic and fail-closed. | Capture/compare failure class, counters, diagnostic category, and no trace output. |
| Existing protocol continuation families | continuation_core_test, continuation_oracle_test, decision_family_test, decision_fail_closed_test | Proves current PICK, FINISH, CANCEL, ASSIGN_AMOUNT, ordering, zone, announcement, tribute, sum, counter, stale-key, malformed, and no-truncation semantics. | Retain unchanged as lower-layer regression. |
| Existing M1 real-engine continuation fixture | m1_engine_fixture_test using the controlled tribute decks | Existing lower-layer CoreHost/protocol proof of an adapter-local tribute continuation. | Keep unchanged. Do not replace it. |
| New Driver tribute integration | New test over the same controlled tribute fixture semantics | Directly proves the new driver does not call process or submit a response for an intermediate tribute choice and submits exactly one final response. | Add only this integration coverage; do not add a different gameplay workload. |
| Existing privacy/observation regression | observation and privacy CTest tests, including continuation privacy coverage | Normal privacy regression, not a new Phase-1 workload. | Run unchanged and compare existing observation hashes in the characterization corpus. |
| Existing M4 worker integration | tests/m4/test_worker_integration.py and worker protocol tests | Preserves worker count/process equivalence and one fresh duel per job. | Run unchanged; do not modify worker JSON semantics. |

If implementation-time inspection finds an authoritative advancement branch
that none of these cases exercises, the implementation must stop and identify
the branch. It may add the smallest existing-style deterministic fixture for
that precise coverage gap only. The PR must document the branch, why the
locked corpus missed it, and why the fixture does not expand gameplay support.

### 9.1 Pre-refactor raw-artifact capture and pure normalization

P1-G01 is a one-time baseline capture, not a command that may be replayed
against the implementation. It has two strictly separated stages:

~~~text
Worktree A — exact 72c29009f107a2ebb172d85de1c70b38d2f007d8
  build the unchanged pre-refactor binary
  run the locked corpus using only the existing commands/binaries
  emit immutable raw trace/result artifacts and a raw-artifact manifest

Worktree B — implementation branch
  run a versioned pure collector over Worktree-A raw artifacts
  write the checked-in characterization fixture and detached provenance manifest
~~~

The Worktree-A capture does not contain, invoke, or depend on the new
collector. The Worktree-B collector is a pure normalizer: it reads the
already emitted raw artifacts and their manifest, and it does not build a
probe, invoke a worker, or run any simulation. It must not modify raw
artifacts, invent actions, or use a post-refactor binary as a baseline source.

The implementation PR should add the following test-only assets:

~~~text
tools/episodic/capture_phase1_characterization.py
tests/episodic/fixtures/phase1-pre-extraction-characterization.json
tests/episodic/fixtures/phase1-pre-extraction-characterization.provenance.json
tests/episodic/test_phase1_equivalence.py
~~~

The fixture is generated and source-controlled for exact comparison. Its
generated detached provenance manifest binds the base source SHA, base
binary/build identity, rules/deck identities, normalized collector arguments,
raw-artifact names and SHA-256 values, collector source SHA/version, and the
fixture SHA-256. A detached manifest is required because embedding a file's
own digest in that file would be self-referential. Paths in the recorded
arguments are repository-relative or normalized identifiers, never
host-specific absolute paths.

The fixture and provenance manifest are never hand-edited to match a refactor.
The generated runtime raw artifacts belong under an ignored artifacts path;
this documentation PR creates neither capture nor generated copy.

The sidecar uses a test-only schema identifier such as
ocgforge.episodic.phase1.characterization.v1. It is not the accepted public
candidate-domain digest schema and it does not alter a trace, an observation,
or the semantic gameplay hash.

Each transcript record is emitted in current execution order and contains:

~~~text
job_id
source_base_commit
seed
seat_assignment
starting_player
ordered_record_index
ordered semantic_key sequence, including selected key
DecisionRequest.decision_id
DecisionRequest.kind
engine_step_index
candidate count
ordered candidate semantic keys
test-only ordered candidate-domain SHA-256 digest
observation schema and observation_hash
continuation identity, derived continuation kind, step, and state hash
submitted response SHA-256, absent for intermediates
existing trace record identity and trace hash
winner and win_reason where terminal
semantic gameplay hash
existing failure_code, error message category, and all failure counters
~~~

The canonical trace already supplies ordered candidate keys, request identity,
request kind, engine index, continuation identity/state, observation hash,
final response hash, terminal fields, and trace identity for the normal
corpus. The collector derives continuation kind from the existing
DecisionRequest-kind-to-continuation mapping, and derives the test-only digest
from the retained ordered key vector. It keeps the vector as the primary
evidence, so a digest collision can never hide a mismatch.

For each job, the collector records a final summary object rather than
reconstructing one from trace steps. That summary preserves current
SimulationResult-facing engine steps, interactive decisions, semantic actions,
candidate counters, visible-event/observation counters, error counters,
winner/reason, gameplay hash, and optional trace hash. The negative probes
record their current diagnostic category and output-presence behavior.

### 9.2 Candidate-domain maximum witness

This witness is **REQUIRED PHASE 1 as test-side characterization**, not as a
new public API or gameplay hash input.

| Classification | Decision |
| --- | --- |
| REQUIRED PHASE 1 | The collector emits the deterministic maximum-domain witness and the comparator checks it against the pre-refactor fixture. |
| OPTIONAL PHASE 1 | Additional semantically inert driver audit counters only if an existing trace/result field cannot produce a required record. They require explicit review. |
| DEFER PHASE 2 | Any public candidate-domain digest, policy-visible witness, gameplay-hash field, or public audit schema. |

The collector derives the maximum candidate-domain witness from every
transcript record. It selects the first maximum in canonical execution order;
there is no unordered reduction or arbitrary tie selection. The witness
contains:

~~~text
semantic job identity and seed
engine_step_index
DecisionRequest.decision_id and kind
raw-message hash already present in the trace
continuation identity, derived kind, and step
candidate count
ordered semantic-key vector and test-only digest
observation_hash
chosen semantic_key
~~~

The expected fixture contains the witness. The post-refactor comparator
requires the same witness and every underlying record to match. This proves
that a matching maximum cannot conceal loss from another candidate domain.

No additional production instrumentation is necessary if the existing trace
and result fields provide the record. If a field cannot be obtained without a
hook, the hook must be test-only, opt-in, semantically inert, excluded from
trace/gameplay hash input, and explicitly reviewed before use.

## 10. Failure model and exact preservation

Phase 1 preserves current outward SimulationResult and worker behavior. It
does not map a failure to a draw, loss, win, pass, cancel, default action, or
automatic response.

| Failure or closure path | Current outward behavior | Driver behavior after extraction | Mutation/teardown rule | Regression evidence |
| --- | --- | --- | --- | --- |
| Canonical identity mismatch before driver construction | pass false; failure_code canonical_identity_mismatch | Remains in canonical evaluator. | No CoreHost exists. | P1-G11. |
| Test-only forced unsupported probe | unsupported counter increments; failure_code forced_unsupported; probe retains existing diagnostic/exit behavior | Driver owns the synthetic decoder call and diagnostic context through a private test-only config flag. | No normal process loop begins; no fabricated trace or response. | P1-G11. |
| MSG_RETRY | retry counter increments; failure_code retry; current runtime error path fails job | Driver detects the decoded retry at the same process boundary. | No retry and no action selection; driver becomes failed and tears down. | P1-G11. |
| Unsupported engine message, multiple interactive messages, malformed protocol message | ProtocolError maps to unsupported_decision unless it is incomplete candidates | Driver calls the same decoder and emits the existing bounded diagnostic using retained raw message and trace context. | No response after failure; teardown immediately. | P1-G11 and P1-G06. |
| Empty, duplicate, incomplete, or response-inconsistent candidate domain | ProtocolErrorCode::IncompleteCandidates maps to candidate_truncated and truncated counter | Driver invokes the same validation before publication and before selected-key mutation. | No candidate repair, filtering, or selection; teardown. | P1-G03/P1-G11. |
| Unknown or stale semantic key | Current select_candidate/apply continuation rejects it with `ProtocolErrorCode::InvalidSemanticKey` | Driver propagates the existing `ProtocolError` before replacing request/observation, appending a trace step, changing a counter, submitting, or processing; it does not produce `DriverFailure`. | Driver remains `AWAITING_DECISION`; canonical maps the error through its current job-level ProtocolError path, with no retry, auto-selection, or Phase-2 `StepRejected`. | P1-G06, P1-G11, and direct stale-key-through-driver test. |
| Candidate/observation inconsistency | Unsupported ProtocolError and fail-closed result | Driver uses the existing observation helper exactly once per published boundary. | No synthesized visible locator or response; teardown. | P1-G04/P1-G11/P1-G12. |
| Observation/query/finalization failure | Existing core or general failure result, depending on source exception | Driver snapshots the current lower-layer category as private DriverFailure for canonical's existing result mapping. | No policy fallback; no continued process loop. | P1-G04/P1-G11/P1-G12. |
| Core process or response failure | core_errors increments; failure_code core_error | Driver is the only caller, so it captures context, produces private DriverFailure, and tears down the unique host. Canonical maps the existing result fields. | No reprocess or resubmit. | P1-G07/P1-G11. |
| Process budget exhausted | terminal false; failure_code nonterminal; existing probe may return successful process exit; no public interrupted type | Driver returns internal DriverProcessBudgetExceeded. Canonical maps it back to the exact current nonterminal result. | Current duel is destroyed at job completion; no winner/reason fabricated. | P1-G11. |
| Unused replay action stream or other canonical policy/client failure | Existing general simulation error behavior | Canonical evaluator retains this client concern after the driver stops. | Driver is destroyed; no extra engine mutation. | P1-G01 replay characterization if applicable. |
| Worker parse, exception, crash, or restart error | Worker owns worker_errors and JSON result behavior | Worker code remains unchanged; it receives the same SimulationResult shape. | A failed job remains isolated; other worker jobs remain governed by existing worker code. | P1-G13/P1-G14. |

DriverFailure and the private driver lifecycle state are not the Phase-2 public
EpisodeFailure representation. They only prevent accidental re-entry while
preserving current canonical error mapping.

DriverMetrics must retain the current distinction between live internal
counters and SimulationResult publication points. In particular, canonical
simulation must not accidentally publish a partially accepted semantic action
or continuation count on a failure path where the current function exits
before copying its local totals. The characterization fixture records negative
case result fields, and P1-G11 compares them exactly.

## 11. Privacy, determinism, replay, failure, and performance implications

### Privacy implications

The driver owns the omniscient CoreHost so that canonical policy/client code
does not need a core reference. It publishes at most the current acting
player's existing PlayerObservation plus the existing DecisionRequest and
complete candidate vector. It never exposes:

- CoreHost, engine pointers, raw query payloads, script/Lua state, or locator
  caches;
- the other player's ObservationSession or PlayerObservation;
- opponent hidden hand/deck/face-down identity;
- a raw trace buffer or raw message as a policy feature;
- a persistent hidden physical-card identity.

The existing ActionCandidate type structurally contains exact_response_bytes
for trusted internal conformance code. Phase 1 must not widen that existing
implementation detail into a public or untrusted policy API. The only
authoritative response path is driver.apply_semantic_key. A sanitized public
DecisionFrame is deferred to Phase 2 before any external policy surface is
introduced.

### Determinism implications

The driver must preserve the existing seed derivation, resolved deck order,
process order, protocol-produced candidate order, semantic keys, decision IDs,
continuation IDs/state hashes, observation canonical hash, response bytes,
trace field order, and semantic gameplay hash. It must not introduce pointer
identity, wall time, worker identity, random IDs, unordered iteration, or a
new sort. DriverBoundary lifetime and all state replacement occur in one
deterministic call order.

### Replay implications

Canonical replay remains a client loop. It reads its next existing semantic key
and passes that exact string to the driver. The driver validates it against the
unchanged current request and uses the existing response or continuation
builder. The resulting trace preserves every current selected key and final
response hash, so replay equivalence is checked at both action and final
gameplay-hash levels.

### Failure implications

Unsupported and malformed inputs remain fail-closed. The driver may not
auto-select, trim a domain, turn budget exhaustion into a terminal outcome, or
retry a rejected response. Its private failed state is terminal for that live
duel and its CoreHost is destroyed before the job result returns.

### Performance implications

Phase 1 is not a performance milestone. The implementation must not add
candidate bucketing, caches, serialization changes, reserve-backed
experiments, tensorization, worker parallelism, new scheduling, or benchmark
thresholds. Timing counters may be moved with their existing work so that
result output remains comparable. A measurable regression is a separate
investigation after semantic equivalence, not a reason to weaken any gate.

## 12. Phase-1 acceptance matrix

Every gate below is PENDING until executed on the final implementation SHA.
Historical M3/M4 evidence is context for the corpus, not a fresh PASS claim.

| Gate | Purpose | Exact setup | Exact PASS condition | Failure severity | Evidence artifact |
| --- | --- | --- | --- | --- | --- |
| P1-G01 Current behavior characterization | Freeze the actual pre-refactor semantic baseline exactly once. | **Worktree A:** clean worktree at 72c29009f107a2ebb172d85de1c70b38d2f007d8; build the unchanged existing ygo_core_probe; run tests/m3/full_game/full_fixed_deck_test.py with 16 games and max steps 2200 plus existing shared-simulation probes; emit immutable raw trace/result artifacts and their manifest. **Worktree B:** on the implementation branch, run the versioned pure collector over those raw artifacts only. | Raw-artifact manifest binds exact base source and binary/build identity; generated fixture and detached provenance manifest bind the raw SHA-256 values, collector source SHA/version and arguments, and contain all 16 terminal records plus specified nonterminal/unsupported summaries. | BLOCKER | immutable Worktree-A raw-artifact manifest/capture log; tests/episodic/fixtures/phase1-pre-extraction-characterization.json and .provenance.json. |
| P1-G02 Driver ownership compile boundary | Prove one active advancement owner. | Build ygo_m4 and new driver integration target; run a source-ownership guard that checks canonical_simulation no longer calls CoreHost process/submit, decoder, observation builder, continuation apply, or trace append APIs. | Build succeeds and the guard identifies EpisodeDriver as the only runtime owner of those calls. | BLOCKER | CTest log and ownership-guard output. |
| P1-G03 Candidate-domain equivalence | Detect membership, order, or key drift. | Run post-refactor collector over the locked corpus and compare every ordered record to the fixture. | Same count, ordered semantic-key vector, test-only digest, and selected key at every boundary. | BLOCKER | normalized comparison JSON and diff-free report. |
| P1-G04 Observation equivalence | Preserve perspective-safe observation construction. | Same collector comparison with Full observation mode; focused observation tests. | Same observation schema/hash for every recorded boundary and terminal record; existing focused tests pass. | BLOCKER | comparison report and CTest log. |
| P1-G05 Decision identity equivalence | Preserve protocol decision identity and engine step pairing. | Same collector comparison. | Same DecisionRequest.decision_id, kind, player, raw hash, and engine_step_index sequence. | BLOCKER | comparison report. |
| P1-G06 Continuation equivalence | Preserve every adapter-local transition. | Existing continuation core/oracle/family/fail-closed tests plus transcript comparison. | Same IDs/state hashes/domain transitions; intermediates have no response/process; terminal actions produce current exact response hash. | BLOCKER | CTest log and comparison report. |
| P1-G07 Response equivalence | Preserve submitted bytes. | Full corpus transcript plus direct driver continuation integration. | Every submitted response SHA-256 equals baseline; no intermediate has one; terminal/atomic action submits exactly once. | BLOCKER | comparison report and driver integration output. |
| P1-G08 Trace equivalence | Preserve existing trace contract. | Full 16-game Conformance run in the same build/configuration and compare JSONL bytes and trace hashes. | Exact v2 JSONL and trace hash equality for each comparable job; no field reinterpretation. | BLOCKER | per-job trace byte/hash comparison. |
| P1-G09 Terminal equivalence | Preserve game outcomes. | Full 16-game corpus and shared full-game probe. | Same terminal flag, winner, win reason, engine steps, and current M3 summary values. | BLOCKER | comparison report. |
| P1-G10 Gameplay-hash equivalence | Preserve semantic execution. | Full 16-game corpus, shared probe, replay cases where present. | Same semantic gameplay hash for each same semantic job. | BLOCKER | comparison report. |
| P1-G11 Failure equivalence | Preserve fail-closed behavior. | Existing max-steps, forced unsupported, malformed/unsupported, retry, stale-key, and invalid-domain probes. | Same result class/failure code/error counter behavior, no fabricated output, and current probe exit behavior. The stale-key-through-driver check must observe the existing `ProtocolErrorCode::InvalidSemanticKey` with unchanged `AWAITING_DECISION` state, request/observation identity, trace length, counters, process count, and response count. | BLOCKER | focused test logs and negative-case transcript. |
| P1-G12 Privacy regression | Prevent information disclosure. | Existing privacy, observation, paired-world, and continuation-privacy CTest coverage. | All current privacy tests pass unchanged; P1-G04 shows no hash drift. | BLOCKER | CTest log. |
| P1-G13 Worker-count/process equivalence | Preserve M4 worker semantics. | Build ygo_m4_worker; run tests/m4 worker integration and protocol suites with the refactored evaluator. | Existing semantic projection is equal across worker-count modes; worker JSON contract is unchanged. | BLOCKER | M4 unittest log and worker outputs. |
| P1-G14 Fresh-duel isolation | Preserve one private duel/session per job. | Existing lifecycle and worker valid-invalid-valid scenarios plus a repeated canonical-job test. | Each job receives a fresh host/session; no state leaks across jobs and existing worker recovery behavior remains. | BLOCKER | worker integration/lifecycle logs. |
| P1-G15 Full M0–M4 regression | Check all accepted lower-layer regressions. | Final native CTest and repository, M3, and M4 Python suites on the final implementation build. | Every configured applicable suite exits zero. New test counts may differ from historic evidence. | BLOCKER | final verification logs. |
| P1-G16 Clean-checkout verification | Prove reproducibility from final source without replacing the pre-refactor baseline. | New clean worktree at final implementation SHA; verify immutable P1-G01 provenance/integrity, including fixture/provenance hashes and every archived raw-artifact hash; configure/build required targets; run P1-G02 through P1-G15 and P1-G17. | The P1-G01 baseline is verified but never regenerated; all final-source gates reproduce without untracked source or sibling-repository dependency. Missing immutable baseline evidence fails verification rather than causing a new baseline capture. | BLOCKER | clean-checkout command log, baseline-integrity report, and final artifact manifest. |
| P1-G17 Driver tribute integration | Close the direct Driver/CoreHost intermediate-step gap without replacing lower-layer tests. | New driver integration test uses the same controlled tribute fixture semantics as m1_engine_fixture_test. | At least one intermediate tribute step leaves DriverMetrics mirrors of process_call_count and response_submission_count unchanged; the terminal action raises the latter by exactly one and next processing occurs only afterward. | BLOCKER | named CTest result and test output. |

The planned Worktree-A baseline command for the primary corpus is:

~~~text
python -B tests/m3/full_game/full_fixed_deck_test.py ^
  --probe build/release-windows-zig/ygo_core_probe.exe ^
  --games 16 --max-steps 2200 --timeout 240 ^
  --output artifacts/episodic/phase1/pre-refactor/full-games
~~~

The exact build directory and executable suffix must be selected from the
final clean Windows configuration. The implementation evidence records those
resolved paths and does not substitute a native fallback for a claimed
canonical gate.

## 13. Staged implementation sequence

The Phase-1 implementation belongs in one implementation PR with disciplined,
buildable commits. The present documentation PR is separate. The implementation
PR must never merge a selectable second advancement path: before migration the
driver is only a compile-time shell; the migration commit removes the canonical
loop in the same commit that makes the driver active.

### Step 0 — Freeze the pre-refactor baseline and normalize it without replay

- **Owning layer:** test and characterization tooling.
- **Files to inspect/change:** **Worktree A** runs the existing probe/full-game/
  shared-simulation tests unchanged and emits only ignored raw artifacts plus
  their manifest. **Worktree B** adds
  tools/episodic/capture_phase1_characterization.py, generated
  tests/episodic/fixtures/phase1-pre-extraction-characterization.json, and its
  detached .provenance.json manifest.
- **Exact responsibility moved:** none.
- **Existing helper reused:** Worktree-A existing ygo_core_probe outputs,
  full_fixed_deck_test.py, shared-simulation probes, and trace/result
  serialization.
- **New type/function:** a Worktree-B pure artifact collector and exact
  normalized comparator schema. The collector never invokes a simulation.
- **Invariants touched:** none; the primary vector remains the existing
  ordered trace domain.
- **Tests added/updated:** collector self-test, fixture-integrity test, and
  provenance-integrity test including source/build/artifact/collector hashes.
- **Acceptance gate:** P1-G01 and the input fixture for P1-G03 through
  P1-G11.
- **Expected semantic delta:** NONE.
- **Rollback point:** delete only Worktree-B tooling/fixture; preserve the
  immutable Worktree-A evidence and make no runtime change.

### Step 1 — Declare the internal driver seam in the existing ygo_m4 target

- **Owning layer:** environment orchestration compiled by ygo_m4.
- **Files to inspect/change:** CMakeLists.txt;
  include/ygo/environment/episode_driver.hpp;
  src/environment/episode_driver.cpp; add a focused compile/interface test as
  needed.
- **Exact responsibility moved:** ownership declarations, immutable input
  value object, private lifecycle enum, DriverMetrics, DriverBoundary, and
  the explicit non-owning boundary lifetime contract.
- **Existing helper reused:** CoreHost, ObservationSession, DecisionRequest,
  PlayerObservation, EngineTrace.
- **New type/function:** EpisodeDriverConfig, EpisodeDriver,
  DriverDecisionBoundary, DriverGameTerminal, and
  DriverProcessBudgetExceeded.
- **Invariants touched:** no live caller moves yet; no public V1 type,
  environment target, or policy callback is added.
- **Tests added/updated:** build ygo_m4 and a static interface test that
  verifies the driver exposes no CoreHost accessor.
- **Acceptance gate:** preliminary P1-G02.
- **Expected semantic delta:** NONE.
- **Rollback point:** remove only new uncalled internal declarations/source
  entry.

### Step 2 — Implement driver setup and one authoritative boundary state machine

- **Owning layer:** EpisodeDriver.
- **Files to inspect/change:** episode_driver header/implementation and the
  source-owned helper functions currently at the top of
  canonical_simulation.cpp.
- **Exact responsibility moved:** seed derivation, CoreHostConfig construction,
  fresh host setup, deck load/start/script order, both observation sessions,
  raw message ownership, process counting, decode, retry/terminal detection,
  current request publication, observation orchestration, candidate metrics,
  and terminal trace construction.
- **Existing helper reused:** seed derivation formula, CoreHost methods,
  decode_messages, validate_candidate_set, build_player_observation,
  attach_decision_context, candidate_observation_consistent,
  make_decision_step, and attach_observation_metadata.
- **New type/function:** private publish_current_request,
  build_current_observation, append_terminal_trace_step, and
  advance_until_boundary.
- **Invariants touched:** current raw message is retained through all
  continuation substeps; observation is deferred-finalized exactly once; no
  policy executes in the driver.
- **Tests added/updated:** focused test for no advancing while a boundary is
  active, terminal trace fixture comparison, and process-budget mapping.
- **Acceptance gate:** P1-G02, P1-G04, P1-G05, P1-G08, P1-G09, P1-G11.
- **Expected semantic delta:** NONE.
- **Rollback point:** before canonical adopts the driver, this work remains
  uncalled and must not be merged as a second runtime path.

### Step 3 — ATOMIC AUTHORITY TRANSFER

- **Owning layer:** EpisodeDriver for mechanics; canonical simulation for
  policy/replay/result concerns.
- **Files to inspect/change:** episode_driver implementation;
  canonical_simulation.cpp; only the ygo_m4 source list already changed in
  Step 1.
- **Exact responsibility moved, in one commit:** EpisodeDriver becomes active;
  the canonical client loop replaces the old loop; semantic-key resolution,
  atomic application, continuation application, intermediate boundary
  publication, final response submission, trace decision-step append,
  semantic-action/continuation counters, and driver closure move to the
  driver; **all** direct canonical advancement calls are removed.
- **Existing helper reused:** select_candidate,
  DeterministicConformancePolicy::choose, apply_continuation_action,
  sha256_bytes, and existing trace helper functions.
- **New type/function:** apply_semantic_key plus a small canonical client loop:

  ~~~text
  boundary = driver.advance_until_boundary()
  while boundary is DriverDecisionBoundary:
    require boundary.request != nullptr
    key = replay next key or m3_policy.choose(*boundary.request).semantic_key
    try:
      boundary = driver.apply_semantic_key(key)
    catch ProtocolError with code ProtocolErrorCode::InvalidSemanticKey:
      map through the existing canonical job-level ProtocolError path; do not retry
  map terminal, process-budget, or DriverFailure into the current SimulationResult
  ~~~

- **Invariants touched:** the driver revalidates the chosen key before
  mutation; policy does not get CoreHost; an intermediate continuation neither
  submits nor processes; driver is sole trace writer; the source-ownership
  guard passes in this commit and confirms that canonical_simulation.cpp has
  no direct CoreHost lifecycle/process/submit/query, raw decode, observation
  construction, continuation application, or trace-append authority.
- **Tests added/updated:** transcript comparator, stale-key test through the
  driver, and source-ownership guard.
- **Acceptance gate:** P1-G02 through P1-G11.
- **Expected semantic delta:** NONE.
- **Rollback point:** revert this one atomic migration commit to restore the
  old canonical loop; do not retain both active paths.

### Step 4 — Post-transfer cleanup only (no authority changes)

- **Owning layer:** canonical simulation remains a client.
- **Files to inspect/change:** canonical_simulation.cpp and its includes;
  comments and result-mapping names; source-ownership guard remains a
  regression check.
- **Exact responsibility moved:** none. Step 3 has already removed every
  direct advancement authority. This step may remove dead includes, normalize
  names, simplify result mapping, and clarify comments only.
- **Existing helper reused:** driver trace/metrics read-only access; existing
  semantic_gameplay_hash, canonical trace serialization, m3_summary_json, and
  result persistence.
- **New type/function:** a narrow driver-to-SimulationResult metrics snapshot
  mapping only.
- **Invariants touched:** canonical simulation may read but never append to
  driver trace; worker result fields stay value-only and unchanged. This step
  must not remove, relocate, or introduce an advancement call.
- **Tests added/updated:** retain the already-passing source-ownership guard;
  no new ownership transition is permitted.
- **Acceptance gate:** preserve P1-G02 and P1-G08 evidence established by
  Step 3.
- **Expected semantic delta:** NONE.
- **Rollback point:** revert this cleanup separately if needed; revert Step 3
  as one unit to restore the old loop, never manually restore a partial loop.

### Step 5 — Add additive driver integration coverage

- **Owning layer:** ygo_m4 integration test.
- **Files to inspect/change:** new
  tests/episodic/episode_driver_tribute_integration_test.cpp and CMake test
  registration; existing m1_engine_fixture_test remains untouched.
- **Exact responsibility moved:** none; this is a new direct assertion over
  the already controlled tribute semantics.
- **Existing helper reused:** the same M1 tribute deck setup, DriverMetrics
  mirrors of existing CoreHost counts, protocol semantic keys, and existing
  fixture policy choices.
- **New type/function:** test-local choose-first-valid-key helper only; no
  general policy type.
- **Invariants touched:** intermediate response count and process count stay
  unchanged; terminal response count rises by exactly one.
- **Tests added/updated:** P1-G17.
- **Acceptance gate:** P1-G06, P1-G07, and P1-G17.
- **Expected semantic delta:** NONE.
- **Rollback point:** remove this additive test only; it is not a replacement
  for lower-layer coverage.

### Step 6 — Run the full equivalence and clean-checkout gates

- **Owning layer:** verification and generated evidence, never handwritten
  source edits.
- **Files to inspect/change:** generated artifact locations only when a gate
  explicitly requires checked-in evidence; no summary document is updated to
  claim a milestone beyond the actual evidence.
- **Exact responsibility moved:** none.
- **Existing helper reused:** all current CTest, repository Python, M3 Python,
  M4 Python, full-game, and worker test commands.
- **New type/function:** none beyond the collector/comparator from Step 0.
- **Invariants touched:** every semantic equivalence dimension.
- **Tests added/updated:** verify P1-G01 provenance/integrity, then execute
  P1-G02 through P1-G15 and P1-G17 from a clean final checkout. P1-G01 is
  never rerun from the final implementation SHA.
- **Acceptance gate:** P1-G01 integrity plus P1-G02 through P1-G17 execution.
- **Expected semantic delta:** NONE.
- **Rollback point:** reject or revert the implementation PR if any blocker
  gate differs. Do not rebless a baseline in the same change.

## 14. Recommended implementation PR structure

Use **one implementation PR with multiple disciplined commits**, not stacked
parallel branches. The core loop is sensitive enough that independently
changing its clusters in separate PRs would make equivalence review and
rollback ambiguous.

Recommended commit order:

1. test: normalize immutable pre-refactor raw artifacts into Phase-1 characterization
2. build: declare internal EpisodeDriver seam in ygo_m4
3. refactor: atomically transfer canonical advancement authority to EpisodeDriver
4. test: add driver equivalence and tribute integration coverage
5. docs/evidence: record only generated final evidence that an accepted gate
   requires

Commit 3 must activate the driver, replace the canonical loop, remove every
old direct advancement call, and make the source-ownership guard pass in the
same commit. Its diff can be large, but its authority transfer is atomic and
reviewable against the committed baseline. Commit 4 and later may clean dead
includes, names, result mapping, or comments, but may not remove or relocate
additional advancement authority. No commit may introduce a runtime feature
flag that chooses between two advancement implementations.

The implementation PR remains separate from this docs-only PR. It must not
merge until P1-G01 baseline integrity is verified and all final-source blocker
gates have fresh evidence. It must not update
CURRENT_PROJECT_STATE.md to claim a public episodic implementation merely
because this internal extraction exists.

## 15. Direct answers to the required questions

| Question | Direct answer |
| --- | --- |
| 1. What makes run_canonical_simulation the current advancement owner? | It constructs the duel, processes raw messages, decodes and validates decisions, builds observations, applies continuations, submits responses, creates trace transitions, detects terminal/failure/budget closure, and counts advancement metrics. |
| 2. What moves to EpisodeDriver? | All authoritative duel advancement mechanics listed in the MOVE/WRAP rows of Section 4, including trace transition writing. |
| 3. What remains in canonical evaluation/policy code? | Canonical identity/seat validation, replay index and policy key choice, unused replay validation, SimulationResult formatting, gameplay hash derivation, trace serialization/persistence, and worker-compatible result consumption. |
| 4. What is the minimal Phase-1 API? | Construction from resolved internal input, advance_until_boundary, apply_semantic_key, and read-only trace/metrics access. |
| 5. What mutable state does the driver own? | Unique CoreHost, both sessions, active raw message/request/continuation/observation, trace, metrics, process/trace indices, and lifecycle state. |
| 6. Who owns current DecisionRequest and candidates? | Driver owns the current request value; protocol owns its candidate semantics and order. Boundary only borrows it. |
| 7. Who owns candidate validation? | Protocol owns validation; driver invokes it before publication and selected-key mutation. |
| 8. Who owns candidate ordering? | Protocol alone. Driver must preserve the supplied vector element-for-element. |
| 9. Who owns observation construction? | The existing observation layer owns it; driver orchestrates the existing build/finalize/consistency calls. |
| 10. Who owns continuation state? | Protocol owns its representation and transition logic; driver owns the active request that contains the current state. |
| 11. Who owns response submission? | EpisodeDriver only. |
| 12. Who owns trace generation? | EpisodeDriver only appends existing transition records. |
| 13. Does trace generation live fully in the driver? | Transition generation does. Canonical reads the finished trace for existing hash, serialization, and persistence only. |
| 14. Who owns semantic gameplay hashing? | Canonical result aggregation calls the existing hash function over the driver-owned completed trace. |
| 15. How is an intermediate continuation represented? | A new protocol-created DecisionRequest with the same engine step, a new continuation state/domain, a false engine_advanced trace step, and a new driver boundary. |
| 16. How is a true engine terminal represented? | DriverGameTerminal internally, derived only from decoded MSG_WIN, with the current terminal trace record and winner/reason. |
| 17. How are fail-closed errors represented? | A private DriverFailure plus failed driver state, mapped to the existing SimulationResult fields; an unknown/stale key alone preserves the existing pre-mutation `ProtocolErrorCode::InvalidSemanticKey` path. No public V1 failure type or `StepRejected` exists in Phase 1. |
| 18. How is process budget preserved? | Driver counts the same CoreHost process calls up to current job.max_steps, returns internal budget exhaustion, and canonical maps it to current nonterminal behavior. |
| 19. Is semantic_action_budget needed now? | No. It is accepted Phase-2 RunControl semantics and must not alter Phase-1 canonical behavior. |
| 20. Is submission_token needed now? | No. It is Phase-2 public freshness control and has no Phase-1 internal behavior. |
| 21. What code is deleted after migration? | In atomic Step 3: direct CoreHost lifecycle/process/submit/query, raw decoding, sessions, observation construction, continuation application, trace append, and moved counters from canonical_simulation.cpp. Later cleanup removes no advancement authority. |
| 22. How is no semantic drift proven? | Immutable pre-refactor raw artifacts and provenance-normalized transcript, plus exact per-boundary, trace, result, privacy, worker, and clean-checkout gates. |
| 23. How do M4 workers consume it? | Unchanged: the worker continues to call run_canonical_simulation and serialize the same SimulationResult. |
| 24. Which current tests are sufficient? | The 16-game matrix, shared probes, continuation protocol/core tests, privacy/observation tests, and M4 worker tests cover their existing layers. |
| 25. Which new tests are needed? | A generated transcript collector/comparator, ownership guard, stale-key-through-driver check, and additive same-fixture tribute Driver integration test. |
| 26. What is the safest PR structure? | One docs-only spec PR now; one later implementation PR with disciplined commits and an atomic authority-transfer commit. |
| 27. When must implementation stop? | Any semantic transcript/trace/hash/privacy/failure mismatch, a required public V1 type/budget/token, an unexercised advancement branch without approved narrow fixture, or unresolved ownership ambiguity. |

## 16. Findings and stop conditions

### BLOCKER

None identified during the live-code inspection and architecture review.

### MAJOR

1. **DriverBoundary borrowed-view lifetime must be explicit.** The
   implementation must state and test that boundary pointers remain valid only
   until the next mutating driver call or driver destruction.
2. **EpisodeDriver must be the sole writer of existing EngineTrace
   transitions.** Canonical simulation may derive results from the trace but
   must not reconstruct or append any parallel transition.

### MINOR

1. **Additive Driver tribute integration coverage is required.** Keep the
   current M1 protocol/core fixture at its lower layer and add a new Driver
   integration test over the same controlled tribute semantics.

### NOTE

1. ygo_m4 is the appropriate Phase-1 compilation owner. A target split or
   rename is deferred to Phase 2 when real public facade dependencies exist.
2. Existing trusted ActionCandidate response bytes are not a new Phase-1
   policy surface. They must not be widened; a sanitized external view remains
   Phase 2 work.
3. Building the observation before a policy selection is necessary to publish
   the future-compatible boundary. The existing conformance policy is pure
   with respect to CoreHost, and P1-G04/P1-G11 must prove no observable
   success or failure drift in the locked corpus.

Implementation must stop rather than compensate if any blocker-equivalent
condition occurs:

- an ordered candidate, decision ID, continuation state, response hash,
  observation hash, terminal result, trace record, or gameplay hash differs;
- candidate/observation consistency cannot be proven without leaking or
  inventing identity;
- an intermediate continuation calls process or submits a response;
- the driver requires public reset/step, a semantic-action budget, or a
  submission token to preserve current behavior;
- canonical simulation still contains a second active advancement
  implementation after migration;
- clean-checkout evidence requires untracked files, altered rules inputs, or a
  sibling repository.

## 17. Documentation PR plan

This PR contains only this implementation specification. It must use a
docs-only branch and PR body that states:

~~~text
Suggested branch: chris/spec/episodic-phase1-episode-driver
Suggested title: docs: specify shared EpisodeDriver extraction
~~~

~~~text
Base: main at 72c29009f107a2ebb172d85de1c70b38d2f007d8
Scope: implementation specification only
Production code changes: none
Gameplay semantic changes: none
Contract changes: none
Phase: internal EpisodeDriver extraction design only
Non-goals: public reset/step, trajectory/ML, new mechanics, target split
Implementation may begin only after this specification is reviewed.
~~~

The PR must not claim episodic implementation acceptance, create generated
acceptance artifacts, or update current project state. It must remain
unmerged until human review.

**SPEC READY FOR REVIEW**
