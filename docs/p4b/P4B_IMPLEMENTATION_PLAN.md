# Phase 4B TeacherCore + Immutable StrategyProfile Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement a deterministic, public-only TeacherCore with two immutable, exactly bound StrategyProfiles while preserving complete candidate domains, information safety, deterministic identity, and the accepted Phase-3/Phase-4A trajectory path.

**Architecture:** ygo::policy owns Teacher session and runner execution. ygo::teacher owns immutable profile data, public-state interpretation, bounded episode-local strategy state, complete-domain feature evaluation, goal/line control, deterministic ranking, fallback, and derived explanations. ygo::environment remains the only legality and public-candidate authority, while ygo::trajectory remains the only trusted provenance, replay, and admission owner.

**Tech Stack:** C++17, CMake/Ninja, CTest, Python 3.11 repository probes, existing ygo::trajectory::ByteWriter/ByteReader primitives, existing V2 public DTOs, and the pinned locked matchup/rules bundle.

---

## Frozen execution constraints

- Start each implementation task from the exact prior task head and a clean worktree. The next-task authorization message owns the exact Task-2 base; Task 2 must verify that its base contains all Task-1 review fixes.
- Do not modify fixtures/decks/, third_party/rules_bundle.lock.json, the pinned rules materialization, Phase-3 contract meanings, or Phase-4A public-policy meanings.
- Use only the existing ygo::policy::PolicyInput boundary:

  ~~~cpp
  struct PolicyInput final {
      const environment::PublicEnvironmentObservation& observation;
      const std::vector<environment::EnvironmentActionCandidate>& candidates;
  };
  ~~~

- DecisionFrame, SubmissionToken, PlayerObservation, CoreHost, internal semantic keys, protocol IDs, continuation IDs, raw response bytes, and private state remain runner/control-plane values. They never reach TeacherCore.
- Every valid nonempty authoritative supplied vector produces one evaluation record per candidate in supplied order. No profile or evaluator filters, truncates, deduplicates, reconstructs, or reorders the vector.
- Deterministic Teacher v1 returns an existing public_action_key and no policy RNG. Accepted records use existing PolicyArtifact, participant assignment, PolicyRngDecisionProvenance::NONE, TrajectoryRecorder, shard, semantic replay, admission, receipt, and dataset path.
- Missing public facts are BLOCKED; no private lookup compensates for them.
- The locked first profiles are ocgforge.swordsoul_tenyi.ml_v1 versus ocgforge.salamangreat.ml_v1 and the reverse, under the exact certified rules bundle and matchup identities in P4B_TEACHER_CONTRACT.md.

### CTest fail-closed and cardinality rule

Every P4B CTest command below that uses a regular expression MUST include `--no-tests=error`. The associated task/evidence validator MUST inspect the CTest result and require the exact expected test count; zero tests or an unexpected count is never PASS. The multi-target counts are fixed as follows:

| Command scope | Expected CTest count |
| --- | ---: |
| Task 2 profile codec/negative tests | 2 |
| Task 2 public/trajectory regression tests | 3 |
| Task 3 boundary/DTO tests | 2 |
| Task 4 domain-preservation/ranking tests | 2 |
| Task 5 evaluator CTest | 1 |
| Task 6 state/rejection tests | 2 |
| Task 7 goal-line/recovery tests | 2 |
| Task 8 fallback/explanation tests | 2 |
| Task 9 Swordsoul profile/scenario tests | 2 |
| Task 10 Salamangreat profile/scenario tests | 2 |
| Task 11 provenance/runner tests | 2 |
| P4B-G14 Phase-4A regression expression | 6 |

Single-target CTest expressions in the plan expect exactly 1. The intentional RED/missing-target commands still fail closed because `--no-tests=error` reports no registered target.

Task 5 additionally requires exactly one direct Python focused gate:
`python -B tests/teacher/teacher_public_fact_matrix_test.py`. That Python
execution is not included in the CTest selected-test count. The authoritative
Task-5 focused gate is therefore:

```text
PYTHON:
python -B tests/teacher/teacher_public_fact_matrix_test.py
PASS required

CTEST:
ctest --test-dir <active-build-dir> \
  --output-on-failure \
  --no-tests=error \
  --tests-regex "^teacher_evaluator_test$"

selected = 1
passed   = 1
```

## File and module map

| Surface | Responsibility |
| --- | --- |
| include/ygo/teacher/strategy_profile.hpp | Immutable StrategyProfile value types, binding values, and typed profile references. |
| include/ygo/teacher/strategy_profile_codec.hpp | Strict canonical profile and binding codec/identity declarations. |
| src/teacher/strategy_profile*.cpp | Validation, canonical bytes, content identities, and immutable publication checks. |
| include/ygo/teacher/teacher_decision.hpp | Derived per-candidate evaluation, score, and staged result values; Task 6 adds the value-owned state delta. |
| include/ygo/teacher/teacher_core.hpp | Public-only TeacherCore proposal interface. |
| include/ygo/teacher/strategy_state.hpp | Episode-local state and accepted-transition delta types. |
| src/teacher/ | Generic public evaluators, state reconciliation, goal/line controller, fallback, and explanation implementation. |
| include/ygo/policy/teacher.hpp | ygo::policy adapter that maps TeacherCore results to existing PolicySelection. |
| include/ygo/policy/teacher_runner.hpp and src/policy/teacher_runner.cpp | V2 runner integration using the existing recorder/replay/admission path. |
| fixtures/teacher_profiles/*.json | Human-reviewable profile authoring inputs; paths and source bytes are not profile identity. |
| tests/teacher/ | Focused contract, privacy, deterministic, profile, state, runner, and acceptance tests. |
| tools/p4b/ | Short-scope acceptance/evidence generation only after the implementation tasks exist. |
| docs/p4b/ | Normative contract, implementation plan, and generated Phase-4B evidence. |

The JSON authoring files are convenience inputs. The strict C++ codec and canonical profile bytes own identity. A source file moved to another path with identical validated content must produce the same profile ID.

## Task 1: Contract/specification freeze

**Status:** Task-1 review-fix finalization.

TASK1_INITIAL_HEAD =
6f46c3c7d10692356ac2d7f085c58f1fefcc88e7

TASK1_REVIEW_FIX_1_HEAD =
989707b3f867d5e1fc7a313ed07549efc3abe8fb

The final accepted Task-1 head is supplied externally by independent review after the final Task-1 commit exists.

Task 2 MUST start from the exact externally authorized Task-1 head. It MUST verify that its base contains all Task-1 review fixes.

A Git commit MUST NOT attempt to embed its own commit SHA as an authoritative self-identity, because changing that content changes the commit SHA.

The next-task authorization message owns the exact Task-2 base SHA.

**Files:**

- Create: docs/p4b/P4B_TEACHER_CONTRACT.md
- Create: docs/p4b/P4B_IMPLEMENTATION_PLAN.md

**Owning layer:** Architecture and contract documentation.

**Invariants affected:** Freezes the public-only input boundary, complete-domain exactly-once evaluation, deterministic score/tie-break/fallback, profile identity/binding, public-safe state lifecycle, provenance ownership, and gate classes.

**Semantic change vs internal implementation:** Contract meaning is introduced for Phase 4B. No runtime behavior changes.

**Focused tests:** Markdown structure, code-fence balance, invalid-marker scan, local-link resolution, and git diff --check.

**Regression tests:** None; Phase-4A G00–G29 remains frozen evidence and is not rerun for this documentation-only task.

**Privacy implications:** The specification explicitly blocks hidden facts and continuation-wide facts absent from PolicyInput.

**Determinism implications:** The specification fixes canonical profile bytes, sorted semantic arrays, checked integer ranking, and bytewise public-key equality completion.

**Replay/provenance implications:** The specification reuses existing PolicyArtifact, participant assignment, recorder, replay, and admission owners.

- [x] Verify live origin/main is 2493046c967f4718dbbf4a63098b37edb0b5a336 and the starting worktree is clean.
- [x] Read Issue #16, accepted Phase-3/Phase-4A contracts, live policy/runner APIs, required research documents, and repository testing conventions.
- [x] Write the normative contract and this implementation plan under docs/p4b/.
- [x] Run the final documentation checks, commit both files, push the dedicated branch, and stop before Task 2.

**Stop condition:** The Task-1 branch contains documentation/specification only, the exact commit is pushed, and NEXT_TASK_AUTHORIZED=NO.

## Task 2: StrategyProfile v1 identity, codec, and immutable binding

**Files:**

- Create: include/ygo/teacher/strategy_profile.hpp
- Create: include/ygo/teacher/strategy_profile_codec.hpp
- Create: src/teacher/strategy_profile.cpp
- Create: src/teacher/strategy_profile_codec.cpp
- Create: src/teacher/teacher_policy_binding.cpp
- Create: tests/teacher/strategy_profile_codec_test.cpp
- Create: tests/teacher/strategy_profile_negative_test.cpp
- Modify: CMakeLists.txt to register the focused library/test target.

**Owning layer:** ygo::teacher immutable data and codec layer.

**Invariants affected:** P4B-G04, P4B-G05, P4B-G06, P4B-G17; no profile path identity, no silent alias, no mutable publication.

**Semantic change vs internal implementation:** The ocgforge.strategy_profile.v1 and ocgforge.teacher_policy_binding.v1 meanings, field order, content IDs, reference validation, and exact deck/rules binding are versioned semantics. Choice of private lookup indexes is internal.

**Focused tests:**

- strategy_profile_codec_test: canonical bytes, recomputed ID, strict round-trip, sorted vectors, path-independent content, exact DeckRole codes, and canonical-byte-sorted unique PredicateRef vectors with no order-significant alternative list.
- strategy_profile_negative_test: unknown schema, invalid enum/token, duplicate/out-of-order entries, dangling references, line cycle, bad binding, overflow, mismatched ID, and trailing bytes.

**Regression tests:** public_action_identity_test, public_safe_state_test, and trajectory_codec_test; these prove no existing public or trajectory codec was changed.

**Privacy implications:** Profile data uses static passcodes and public predicate references only. It cannot contain a hidden card instance, hidden order, private locator, internal key, or raw engine field.

**Determinism implications:** Use the existing trajectory primitive encoding: UTF-8 length-prefixed strings, big-endian integers, explicit vector ordering, strict duplicate rejection, and SHA-256 content identities.

**Replay/provenance implications:** TeacherPolicyBindingV1 is content metadata carried through existing PolicyArtifact.artifact_metadata_identity; no Phase-3 field or codec is added.

**Binding validation:** Task 2 MUST validate teacher_core_artifact_identity as a canonical identity valid for the existing ProvenanceKind::ProducerImplementation category; strategy_profile_id as ocgforge.strategy_profile.v1.<64 lowercase hex>; score_contract_identity as ocgforge.policy.teacher_score.v1; fallback_contract_identity as ocgforge.policy.teacher_fallback.v1; tie_break_contract_identity as ocgforge.policy.public_key_tiebreak.v1; and an optional diagnostic_contract_identity as ocgforge.teacher_decision_explanation.v1. Task 2 validates canonical identity classes and exact literals, but not production registry membership; that registration belongs to Task 11.

The score-contract validation freezes the nine score dimensions/order, signed i32 contribution range, signed i64 accumulation, checked overflow/underflow behavior, and higher-is-better lexicographic comparison under ocgforge.policy.teacher_score.v1. Task 2 also rejects every own_deck_role or opponent_deck_role value other than 0=FirstLockedDeck and 1=SecondLockedDeck.

- [ ] Write failing tests that construct the smallest valid profile, compute ocgforge.strategy_profile.v1.<64 lowercase hex>, compute ocgforge.teacher_policy_binding.v1.<64 lowercase hex>, and assert canonical path independence.
- [ ] Run ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^(strategy_profile_codec_test|strategy_profile_negative_test)$" and record the expected missing-target failure.
- [ ] Implement the exact value types and canonical codec using ygo::trajectory::ByteWriter/ByteReader; reject malformed data before publication.
- [ ] Add strict profile/binding validation: exact matchup, rules bundle, format, mode, flags, own/opponent deck roles, references, DAG, ranges, and content ID.
- [ ] Validate own_deck_role and opponent_deck_role as exactly 0=FirstLockedDeck or 1=SecondLockedDeck, and validate the binding identity classes/literals and score-contract ownership rules.
- [ ] Re-run the focused CTest command and expect both targets to pass with zero failures.
- [ ] Run ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^(public_action_identity_test|public_safe_state_test|trajectory_codec_test)$" and expect all selected regression targets to pass.
- [ ] Run git diff --check, commit with feat: add phase 4b strategy profile identity, and stop for review.

**Stop condition:** A profile can be accepted only from exact canonical bytes and identity; malformed or mismatched content cannot create a Teacher session.

## Task 3: Minimal Teacher decision DTOs and public boundary adapter

**Files:**

- Create: include/ygo/teacher/teacher_decision.hpp
- Create: include/ygo/teacher/teacher_explanation.hpp
- Create: include/ygo/teacher/teacher_core.hpp
- Create: src/teacher/teacher_decision.cpp
- Create: tests/teacher/teacher_policy_boundary_compile_test.cpp
- Create: tests/teacher/teacher_decision_dto_test.cpp
- Modify: CMakeLists.txt for the new focused targets.

**Owning layer:** ygo::teacher decision values; ygo::policy remains the existing selector owner.

**Invariants affected:** P4B-G00, P4B-G01, P4B-G12; exact PolicyInput and existing PolicySelection output.

**Semantic change vs internal implementation:** The result/status/explanation field meanings are versioned. Header layout, helper names, and allocation strategy are internal.

**Focused tests:**

- Compile the TeacherCore header in a translation unit that includes ygo/policy/policy.hpp but not episodic_environment.hpp, core_host.hpp, episode_driver.hpp, or trajectory internals.
- Assert that TeacherRankingResult preserves input-order evaluation records and maps a successful result to PolicySelectionResult{key, std::nullopt}.

**Regression tests:** Phase-4A policy_boundary_compile_test, policy_boundary_test.py, and public_fact_matrix_test.py.

**Privacy implications:** The boundary test must fail if TeacherCore includes or accepts DecisionFrame, PlayerObservation, CoreHost, SubmissionToken, internal keys, raw responses, or private diagnostics.

**Determinism implications:** DTOs contain no time, process, pointer, provider, RNG, or completion-order fields. Explanation IDs and vectors are canonical.

**Replay/provenance implications:** No Teacher DTO is added to the canonical trajectory. The adapter returns the existing public action result and leaves attribution to the runner.

- [ ] Write the boundary compile test and DTO round-trip/validation tests.
- [ ] Run ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^(teacher_policy_boundary_compile_test|teacher_decision_dto_test)$" and record the expected missing-target failure.
- [ ] Implement the public-only TeacherCore::propose(const ygo::policy::PolicyInput&, const StrategyProfile&, const EpisodeLocalStrategyState&) declaration and derived result types.
- [ ] Implement the adapter that converts only a successful Teacher result to existing PolicySelection; rejected/blocked results carry no key.
- [ ] Re-run the focused tests and the three Phase-4A boundary regressions.
- [ ] Run git diff --check, commit with feat: define phase 4b teacher decision boundary, and stop for review.

**Stop condition:** TeacherCore has a compile-enforced public input boundary and no gameplay-facing output other than one existing public action key.

## Task 4: Authoritative-domain preservation records and deterministic resolver

**Files:**

- Create: include/ygo/teacher/candidate_evaluator.hpp
- Create: include/ygo/teacher/deterministic_resolver.hpp
- Create: src/teacher/candidate_evaluator.cpp
- Create: src/teacher/deterministic_resolver.cpp
- Create: tests/teacher/teacher_domain_preservation_test.cpp
- Create: tests/teacher/teacher_ranking_test.cpp
- Modify: CMakeLists.txt.

**Owning layer:** Generic TeacherCore resolver.

**Invariants affected:** P4B-G01, P4B-G02; preservation of the upstream authoritative supplied domain, nine-component signed-integer score, checked arithmetic, and public-key tie-break.

**Semantic change vs internal implementation:** The score vector order, range, arithmetic failure, exactly-once evaluation shape, and ocgforge.policy.public_key_tiebreak.v1 are versioned semantics. A one-pass loop or pre-sized vector is internal.

**Focused tests:**

- Supply a domain containing multiple legal candidates, one strategically bad candidate, and equal-score candidates.
- Assert one evaluation record per input candidate, exact input order, unchanged candidate keys, no duplicate evaluation, and selected key membership.
- Assert the test does not attempt to determine legal-domain completeness; Environment/Phase 4A owns that guarantee.
- Assert survival/lethal, goal/recovery, tactical, timing, target, resource, follow-up, battle/Main-2, and profile dimensions compare lexicographically.
- Assert equality selects the bytewise-smallest key even when vector order is reversed.
- Assert empty, duplicate-key, malformed-key, overflow, and unsupported-total cases return no selection.

**Regression tests:** public_action_identity_test, random_legal_test, and policy_determinism_test.py; RandomLegal behavior must remain unchanged.

**Privacy implications:** Feature records include only public candidate descriptors and decoded public-safe facts; no internal semantic key or candidate index is stored as identity.

**Determinism implications:** Use std::array<std::int64_t, 9>, checked i32 contribution arithmetic, no floating point, and bytewise comparison of public_action_key.

**Replay/provenance implications:** The full evaluation vector is derived diagnostic data. Only the selected public key reaches V2 and the existing trajectory record.

- [ ] Write failing authoritative-domain preservation tests with a spy evaluator that records each supplied public key exactly once.
- [ ] Run ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^(teacher_domain_preservation_test|teacher_ranking_test)$" and record the expected missing-target failure.
- [ ] Implement candidate validation, one logical evaluation pass, fixed score-vector comparison, and explicit key-only equality completion.
- [ ] Implement checked arithmetic that returns INVALID on overflow/underflow instead of wrapping, saturating, or clamping.
- [ ] Re-run focused tests and the Phase-4A policy regressions.
- [ ] Run git diff --check, commit with feat: add complete-domain teacher resolver, and stop for review.

**Stop condition:** A valid candidate cannot disappear from evaluation evidence, and no resolver path selects candidate zero or the first vector element by default.

## Task 5: Generic public fact and tactical/resource evaluators

**Files:**

- Create: include/ygo/teacher/public_fact_registry.hpp (PublicFactValue and immutable registry)
- Create: include/ygo/teacher/candidate_features.hpp
- Create: include/ygo/teacher/tactical_evaluator.hpp
- Create: include/ygo/teacher/target_evaluator.hpp
- Create: include/ygo/teacher/material_evaluator.hpp
- Create: include/ygo/teacher/interaction_evaluator.hpp
- Create: src/teacher/public_fact_registry.cpp
- Create: src/teacher/candidate_features.cpp
- Create: src/teacher/tactical_evaluator.cpp
- Create: src/teacher/target_evaluator.cpp
- Create: src/teacher/material_evaluator.cpp
- Create: src/teacher/interaction_evaluator.cpp
- Create: tests/teacher/teacher_public_fact_matrix_test.py
- Create: tests/teacher/teacher_evaluator_test.cpp
- Modify: CMakeLists.txt.

**Owning layer:** Generic TeacherCore public-fact and feature/evaluator layer.

**Invariants affected:** P4B-G00, P4B-G01, P4B-G03, P4B-G11, P4B-G18; public facts are explicit and legality remains engine-owned.

**Semantic change vs internal implementation:** PublicFactValue IDs, kinds, bounds, scopes, canonical encoding/order, feature meanings, DIRECT/SAFE_DERIVATION/BLOCKED classifications, and score contributions are versioned. Helper decomposition is internal.

**Focused tests:**

- Decode only PublicSafeStateView from the public observation's canonical bytes.
- Define the immutable fact registry before any state task may consume or commit PublicFactValue.
- Map every fact used by the evaluator to a source field or mark it BLOCKED.
- Evaluate visible target threats, material/cost preservation, interaction timing, follow-up, and public tactical safety without reconstructing legal choices.
- Assert an absent request-wide continuation fact returns BLOCKED for the dependent rule instead of consulting EnvironmentDecisionRequest or private state.
- Assert hidden hand/order/physical identity is never accepted as a fact source.

**Regression tests:** public_safe_state_test, policy_boundary_test.py, public_fact_matrix_test.py, and episodic_paired_world_test.

**Privacy implications:** Candidate source/target references are used only under their accepted visibility kind. Redacted slots are not resolved to physical cards. No opponent hidden card is inferred.

**Determinism implications:** Fact registries are immutable sorted identity lists; feature values are bounded integers; no float, map iteration, random sampling, or host-dependent arithmetic.

**Replay/provenance implications:** Feature/evaluator outputs are derived explanations and score inputs only. They do not alter public frame bytes or trajectory identity.

- [ ] Write failing public-fact matrix and evaluator tests for direct, safe-derived, and blocked facts.
- [ ] Run python -B tests/teacher/teacher_public_fact_matrix_test.py and ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^teacher_evaluator_test$" and record the expected missing-target failures.
- [ ] Implement the immutable typed public-fact registry with fact IDs, value kinds, bounds, validity scopes, canonical value ordering, and explicit blocked results.
- [ ] Implement safe-state decode and public observation/candidate-to-fact extraction without private compensation.
- [ ] Implement generic tactical, target, material, and interaction evaluators over one supplied candidate descriptor.
- [ ] Require Task-5 evaluators to reuse the already accepted `add_score_contribution()` Task-4 API for checked contribution composition; Task 5 MUST NOT reopen or modify Task-4 score, tie, or domain semantics, and Task-4 production files remain outside Task-5 scope unless independently authorized.
- [ ] Re-run focused tests and Phase-4A safe-state/privacy regressions.
- [ ] Run git diff --check, commit with feat: add public teacher evaluators, and stop for review.

**Stop condition:** Every evaluator fact has an auditable public source, and any missing source blocks or uses a declared lower fallback without private compensation.

## Task 6: Episode-local strategy state and transactional reconciliation

**Dependency:** Task 5 must complete the immutable public-fact registry and
its validation/extraction contract before Task 6 can propose or commit any
`PublicFactValue`.

**Files:**

- Create: include/ygo/teacher/strategy_state.hpp
- Create: src/teacher/strategy_state.cpp
- Create: src/teacher/strategy_state_reconciler.cpp
- Create: tests/teacher/teacher_strategy_state_test.cpp
- Create: tests/teacher/teacher_rejected_transition_test.cpp
- Modify: include/ygo/teacher/teacher_decision.hpp to add the value-owned `std::optional<TeacherStateDeltaV1> proposed_state_delta` field.
- Modify: CMakeLists.txt.

**Owning layer:** Participant-owned ygo::teacher session state with runtime-owned
participant/frame routing.

**Invariants affected:** P4B-G03, P4B-G07, P4B-G08, P4B-G09; state reset/isolation, participant-perspective routing, frame-local proposal binding, registry-validated facts, accepted-only commit, deferred observation dominance, and no future-action queue.

**Semantic change vs internal implementation:** `EpisodeLocalStrategyStateV1`, `TeacherStateDeltaV1`, value-owned result composition, participant-scoped current-observation proposal, frame-local accepted-transition binding, reset, pure proposal, deferred reconciliation, rejection behavior, invalidation, and reconciliation are versioned. Whether a lookup table or sorted vector backs a fact ledger is internal.

**Focused tests:**

- Assert every state fact is accepted only after Task-5 registry validation; unregistered, malformed, out-of-bounds, or wrong-scope values fail closed without repair or silent drop.
- Reset two profiles and two participants, interleave proposals, and compare each result with isolated execution.
- Propose a value-owned `TeacherStateDeltaV1`, reject the action, and assert byte-equivalent state before and after rejection.
- Accept an action using the exact frame-local proposal binding, verify the selected key equals `proposed_for_public_action_key`, and assert the commit does not consume a `StepAccepted.next` observation belonging to another participant.
- Require the current proposal observation to belong to the same participant as the state, to be later than the state's last accepted decision index when present, and to reconcile before proposal without mutating trusted state.
- Require every `CURRENT_RECONCILIATION` fact in a proposed replacement to match the exact current participant `PublicFactSnapshot`; registry validity alone is insufficient.
- Defer reconciliation after an accepted action until that participant receives its next actionable public observation; prove no cross-participant facts or frame state enter the participant state.
- Remove/negate a public resource and assert stale line nodes are invalidated before that same participant's next decision.
- Assert state contains no candidate key for future execution, candidate index, locator carried through a shuffle, token, internal key, or engine-step value.

**Regression tests:** episodic_rejection_test, episodic_paired_world_test, episodic_reset_after_failure_test, and policy_runner_integration_test.

**Privacy implications:** State stores only registry-validated public fact classes/scalars and accepted public key history. Runtime routing must never reconcile one participant's state with another participant's perspective. Paired hidden worlds with equal public input must produce equal state deltas and state evolution.

**Determinism implications:** Reset state is identical for equal profile IDs; value-owned deltas use exact base-state/frame bindings, accepted public decision order, and canonical fact/ID ordering, never wall time or thread order. Opponent decisions may intervene, so only strict same-participant decision-index advancement is required.

**Replay/provenance implications:** Strategy state and `TeacherStateDeltaV1` are derived policy memory, not canonical trajectory input. A rejected step creates no record and no state advancement.

- [ ] Write failing registry-validation, reset/isolation/rejection, value-owned-delta, and invalidation tests.
- [ ] Run ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^(teacher_strategy_state_test|teacher_rejected_transition_test)$" and record the expected missing-target failure.
- [ ] Implement `EpisodeLocalStrategyStateV1`, `TeacherStateDeltaV1`, the value-owned `TeacherRankingResult` field, pure proposal, and accepted-transition commit APIs.
- [ ] Implement participant-scoped proposal reconciliation against the current public observation, preserving accepted markers and deriving only registered invalidation IDs in delta evidence.
- [ ] Retain the proposal's current observation as ephemeral frame-local commit context; require its decision index to equal the accepted transition and do not consume `StepAccepted.next` for another participant's state.
- [ ] Require proposed `CURRENT_RECONCILIATION` facts to match the exact current participant public-fact snapshot; reject registry-valid mismatches without repair.
- [ ] Enforce exact base-state/public-action/profile bindings and zero mutation on rejection, interruption, mismatch, or failed commit.
- [ ] Re-run focused tests, including participant-perspective/frame-binding and current-fact-source tests, and the listed episodic regressions.
- [ ] Run git diff --check, commit with feat: add transactional teacher strategy state, and stop for review.

**Stop condition:** A rejected or interrupted run cannot advance trusted strategic state; every committed fact was validated by Task 5 and matched the owning participant's current public snapshot; exact frame-local proposal binding is enforced; `StepAccepted.next` cannot cross participant boundaries; and same-participant reconciliation occurs before the next policy decision.

## Task 7: Goal, partial-order line, and recovery controller

**Files:**

- Create: include/ygo/teacher/predicate_registry.hpp
- Create: src/teacher/predicate_registry.cpp
- Create: include/ygo/teacher/goal_line_controller.hpp
- Create: include/ygo/teacher/recovery_controller.hpp
- Create: src/teacher/goal_line_controller.cpp
- Create: src/teacher/recovery_controller.cpp
- Create: tests/teacher/teacher_goal_line_test.cpp
- Create: tests/teacher/teacher_recovery_test.cpp
- Modify: src/teacher/strategy_profile.cpp to invoke the immutable Task-7 predicate registry during profile validation.
- Modify: CMakeLists.txt.

**Owning layer:** Task-7 immutable predicate registry and generic TeacherCore
strategy controller using profile data.

**Invariants affected:** P4B-G01, P4B-G02, P4B-G04, P4B-G05, P4B-G07, P4B-G09; predicate meaning, goals/lines, recovery, and scoring remain declarative, partial-order, public-only, and current-frame driven.

**Semantic change vs internal implementation:** The `ocgforge.policy.teacher_predicate.v1` registry, exact predicate IDs/scopes/argument schemas, TRUE/FALSE/UNSUPPORTED/INVALID semantics, conjunction status precedence, goal/line/node/recovery controller rules, invalidation IDs, confidence caps, candidate-intent matching, and the exact `ActiveGoalLineOrValidatedRecoveryProgress` contributions are versioned. Graph traversal implementation, registry lookup indexes, and memoization are internal. `src/teacher/strategy_profile.cpp` remains the validation entry point but may not accept a predicate without the Task-7 registry.

**Focused tests:**

- Exercise every initial predicate registry entry with its exact scope, ordered atom schema, public/profile-static source, and status semantics.
- Reject unknown predicate IDs, wrong scopes, wrong arity, wrong atom kinds, invalid fact IDs/kinds/bounds, invalid candidate token domains, invalid profile-static references, and any `ACCEPTED_PUBLIC_HISTORY` predicate because no v1 history owner exists.
- Validate a DAG with independent nodes and assert both supplied orders remain legal policy evaluation inputs.
- Match candidate intents from public candidate metadata using conjunctions and alternative intent-ID sets without generating actions; retain all matching IDs as sorted evidence.
- Retain an active eligible goal/line before considering priority/ID selection, then verify deterministic goal `(priority descending, goal_id ascending)` and line `(line preference descending, line_id ascending)` selection.
- Verify DAG-ready nodes require all incoming predecessors to be completed, independent nodes remain ready, and empty completion predicates do not auto-complete.
- Complete a node only after an accepted action and a subsequent perspective-safe public observation for the same participant satisfies the declared completion predicate; never assume `StepAccepted.next` belongs to that participant.
- Invalidate an active line when a public body/resource/target/zone/copy budget disappears or a restriction is observed.
- Choose a declared recovery edge only when its source, all invalidation reasons, preconditions, and target are proven; verify tie order `(target goal priority descending, confidence cap ascending, recovery_edge_id ascending)` and no queued action.
- Verify exact progress contributions `+3` for a ready active-line intent, `+2` for an eligible recovery intent, `max(+3,+2)` when both apply, and zero for a proven nonmatch; unsupported/invalid proof produces no contribution.

**Regression tests:** episodic_replay_test, episodic_paired_world_test, episodic_interrupt_test, and public_action_identity_test.

**Privacy implications:** Predicates use only OBSERVATION, CANDIDATE, and PROFILE_STATIC scopes in v1; no `ACCEPTED_PUBLIC_HISTORY` predicate is registered until a public-history owner exists, and there is no private predicate scope. Redacted references remain redacted and identity-dependent semantics are unsupported.

**Determinism implications:** Evaluate registry entries, profile IDs, graph edges, ready nodes, goals, lines, intents, and recovery edges in explicit canonical order; dependencies are checked for cycles; independent nodes do not gain order from unordered storage. Controller ties use only the frozen tuples, never candidate vector position or completion order.

**Replay/provenance implications:** Plan progress is policy-local derived state. The engine/replay path receives only the selected current public key.

- [ ] Write failing predicate-registry/profile-validation, line-DAG, intent-match, invalidation, and recovery tests; `teacher_goal_line_test` owns the registry and profile-validation coverage so the Task-7 focused CTest count remains 2.
- [ ] Run ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^(teacher_goal_line_test|teacher_recovery_test)$" and record the expected missing-target failure.
- [ ] Implement the immutable `ocgforge.policy.teacher_predicate.v1` registry and integrate it into `validate_strategy_profile` through the explicit `src/teacher/strategy_profile.cpp` hook.
- [ ] Implement registered public predicate evaluation with exact status precedence and no history/private fallback.
- [ ] Implement deterministic active goal/line retention and selection, DAG-ready nodes, same-participant post-acceptance completion, candidate-intent matching, recovery eligibility/tie order, and exact progress contributions.
- [ ] Connect controller output to the Task-4 complete-domain evaluator as score contributions, never as candidate filtering.
- [ ] Re-run focused tests and the listed episodic regressions.
- [ ] Run git diff --check, commit with feat: add partial-order teacher recovery, and stop for review.

**Stop condition:** Every profile predicate is registry-validated before publication; unsupported or invalid public proof never becomes a score or selection; goals, lines, nodes, and recovery edges resolve only by the frozen deterministic rules; interruption/participant turns invalidate stale plan state before that participant's next selection; and no graph edge can enqueue an exact future engine action.

## Task 8: Deterministic fallback and derived explanation

**Files:**

- Create: include/ygo/teacher/fallback_resolver.hpp
- Create: include/ygo/teacher/teacher_explanation_codec.hpp
- Create: src/teacher/fallback_resolver.cpp
- Create: src/teacher/teacher_explanation_codec.cpp
- Create: tests/teacher/teacher_fallback_test.cpp
- Create: tests/teacher/teacher_explanation_test.cpp
- Modify: CMakeLists.txt.

**Owning layer:** Generic TeacherCore resolver and derived audit layer.

**Invariants affected:** P4B-G02, P4B-G11, P4B-G16; explicit F0–F4 fallback, no RandomLegal escape, canonical diagnostic separation.

**Semantic change vs internal implementation:** F0–F4 semantics, confidence classes, explanation fields, diagnostic schema, and key-only tie completion are versioned. Whether explanation values are assembled by a builder or returned by value is internal.

**Focused tests:**

- Exercise F0 active line, F1 recovery/replan, F2 profile utility, F3 generic public safety, and F4 public-key equality completion.
- Assert every fallback level retains N evaluation records and emits an explicit level.
- Assert unsupported arithmetic/public fact behavior descends only to a total public stage or returns BLOCKED.
- Assert explanation canonical bytes are stable and contain no forbidden field.
- Toggle optional explanation persistence under the identical diagnostic contract and assert selected action, public gameplay identity, and record identity remain equal; then change diagnostic-contract semantics/version or the TeacherCore/profile/binding artifact and assert new provenance/binding and potentially new record identity while public gameplay may remain equal.
- Assert every fallback level enriches the same single N-record evaluation vector overall; no stage appends a second record per candidate.

**Regression tests:** Phase-4A random_legal_test, policy_runner_integration_test, and trajectory privacy/codec tests.

**Privacy implications:** Explanations contain public features, profile-defined IDs, public keys, scores, confidence, and fallback only. They do not contain internal engine identifiers or hidden data.

**Determinism implications:** Explanation vectors use sorted IDs and checked integers; fallback never uses first candidate, random device, wall time, or process order.

**Replay/provenance implications:** Explanation is optional derived policy/audit data. It is not a DecisionRecord field and is not required for admission.

- [ ] Write failing fallback/explanation tests for all levels, unsupported stages, and forbidden fields.
- [ ] Run ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^(teacher_fallback_test|teacher_explanation_test)$" and record the expected missing-target failure.
- [ ] Implement the versioned fallback resolver and explanation codec without modifying public environment or trajectory codecs.
- [ ] Add exact PolicySelection mapping with rng_cursor = std::nullopt for deterministic Teacher output.
- [ ] Re-run focused tests and Phase-4A policy/trajectory regressions.
- [ ] Run git diff --check, commit with feat: add deterministic teacher fallback diagnostics, and stop for review.

**Stop condition:** Every supported nonempty valid domain gets a public deterministic outcome or an explicit fail-closed result; no hidden or stochastic escape path exists.

## Task 9: Minimal Swordsoul Tenyi StrategyProfile

**Files:**

- Create: fixtures/teacher_profiles/ocgforge.swordsoul_tenyi.v1.json
- Create: src/teacher/swordsoul_tenyi_profile.cpp
- Create: tests/teacher/swordsoul_profile_test.cpp
- Create: tests/teacher/swordsoul_teacher_scenarios_test.cpp
- Modify: CMakeLists.txt only for profile/scenario test registration.

**Owning layer:** Immutable Swordsoul profile data; generic TeacherCore remains unchanged.

**Invariants affected:** P4B-G04, P4B-G06, P4B-G09, P4B-G18; exact own/opponent deck binding and public-only strategy.

**Semantic change vs internal implementation:** The profile content, role IDs, goals, line/recovery IDs, interaction rules, and preference values are versioned profile content. JSON key ordering, loader allocation, and fixture path are not identity.

**Focused tests:**

- Validate exact Swordsoul deck/rules/matchup binding and canonical profile ID.
- Cover a minimal public-supported slice around Mo Ye/Chixiao foundation, Longyuan/Level-10 access, Tenyi/Monk access, Taia/Summit recovery, interaction preservation, and safe stop/lethal classes.
- Include at least one public interruption where the expected body/target/resource is absent and a declared recovery edge wins.
- Assert profile rules never reference absent cards or hidden hand/deck order.

**Regression tests:** existing M3 locked-deck identity tests, public_fact_matrix_test.py, and Phase-4A public candidate/policy tests. Do not modify deck fixtures.

**Privacy implications:** Static passcodes/roles are profile configuration. The profile may not assert a hidden opponent card is present or carry a physical card identity through a shuffle.

**Determinism implications:** Profile source is compiled to canonical bytes; role/goal/line/recovery/preferences are sorted by the Task-2 codec; no random profile branch is allowed.

**Replay/provenance implications:** The profile ID is embedded in TeacherPolicyBindingV1, then the existing PolicyArtifact; scenario diagnostics remain derived.

- [ ] Add the reviewable JSON source and the compiled profile factory with only exact locked-list cards, registered public predicates, and supported line/recovery facts.
- [ ] Run python -B tests/teacher/teacher_profile_binding_test.py and ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^(swordsoul_profile_test|swordsoul_teacher_scenarios_test)$" and record the expected missing-target failures.
- [ ] Add profile loading through the immutable registry and assert content ID from canonical bytes, not path.
- [ ] Add engine-reachable/public-input scenario fixtures without introducing a general hidden board-construction API.
- [ ] Re-run the focused scenario, profile, and locked-deck regressions.
- [ ] Run git diff --check, commit with feat: add swordsoul teacher profile slice, and stop for review.

**Stop condition:** The Swordsoul slice selects only supplied public candidates, marks unsupported facts explicitly, and passes its profile/binding tests without changing the locked deck.

## Task 10: Minimal Salamangreat StrategyProfile

**Files:**

- Create: fixtures/teacher_profiles/ocgforge.salamangreat.v1.json
- Create: src/teacher/salamangreat_profile.cpp
- Create: tests/teacher/salamangreat_profile_test.cpp
- Create: tests/teacher/salamangreat_teacher_scenarios_test.cpp
- Modify: CMakeLists.txt only for profile/scenario test registration.

**Owning layer:** Immutable Salamangreat profile data; generic TeacherCore remains unchanged.

**Invariants affected:** P4B-G04, P4B-G06, P4B-G09, P4B-G18; exact reverse deck binding and public-only recovery.

**Semantic change vs internal implementation:** The profile content and supported line/recovery preferences are versioned. Fixture loader and test arrangement are internal.

**Focused tests:**

- Validate exact Salamangreat own-deck/Swordsoul opponent binding and canonical profile ID.
- Cover a minimal public-supported slice around Of Fire/Gazelle, Spinny/Miragestallio, Wolf recursion, Raging/copy preservation, Princess/Weasel conversion, trap recovery, and safe stop/lethal classes.
- Include interruption/recovery scenarios for Of Fire, Miragestallio, Princess, and unavailable Extra Deck copies.
- Assert no profile rule depends on hidden top-deck order, hidden hand contents, or hidden physical identity.

**Regression tests:** M3 deck identity/locked matchup tests, public_fact_matrix_test.py, and Phase-4A policy/privacy tests.

**Privacy implications:** The profile can encode static exact deck roles and public interaction classes only. It cannot inspect a hidden Foxy top card or reconstruct an opponent hidden hand.

**Determinism implications:** Use the same canonical profile codec and fixed integer preferences as Task 9; no profile-local RNG or unordered iteration.

**Replay/provenance implications:** The reverse-deck profile binds through the same existing policy metadata/provenance fields; no Teacher-specific trajectory schema is created.

- [ ] Add the reviewable JSON source and the compiled profile factory using only validated public facts and exact locked-list cards.
- [ ] Run python -B tests/teacher/teacher_profile_binding_test.py and ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^(salamangreat_profile_test|salamangreat_teacher_scenarios_test)$" and record the expected missing-target failures.
- [ ] Add engine-reachable/public-input scenarios for recursion, interaction, recovery, and copy-safe stopping.
- [ ] Compare equal public paired worlds and assert identical selected key, evaluation evidence, explanation, and state delta.
- [ ] Re-run focused scenario, profile, privacy, and locked-deck regressions.
- [ ] Run git diff --check, commit with feat: add salamangreat teacher profile slice, and stop for review.

**Stop condition:** Both initial profile roles are exact, immutable, public-only, and independently tested without broadening deck or rules support.

## Task 11: Teacher policy, provenance, and trusted runner integration

**Files:**

- Create: include/ygo/policy/teacher.hpp
- Create: include/ygo/policy/teacher_runner.hpp
- Create: src/policy/teacher.cpp
- Create: src/policy/teacher_runner.cpp
- Create: src/policy/runner_shared.hpp
- Create: src/policy/runner_shared.cpp
- Modify: src/policy/runner.cpp only to use the shared semantic runner path without changing RandomLegal behavior.
- Modify: include/ygo/policy/production_provenance.hpp
- Modify: src/policy/production_provenance.cpp to register the Teacher producer, deterministic complete sampling contract, profile/binding metadata identities, and existing public adapters.
- Create: tests/teacher/teacher_provenance_test.cpp
- Create: tests/teacher/teacher_runner_trajectory_test.cpp
- Modify: CMakeLists.txt.

**Owning layer:** ygo::policy execution/runner; ygo::trajectory remains provenance/replay/admission owner.

**Invariants affected:** P4B-G00, P4B-G07, P4B-G08, P4B-G10, P4B-G12, P4B-G13, P4B-G14.

**Semantic change vs internal implementation:** Teacher artifact/binding registration, deterministic sampling identity, NONE RNG attribution, runner rejection semantics, and shared path ownership are collection/provenance semantics. Extracting a common loop is internal only if the RandomLegal outputs and accepted Phase-4A tests remain identical.

**Focused tests:**

- Construct one exact Teacher PolicyArtifact with DETERMINISTIC_HEURISTIC, ocgforge.no_policy_rng.v1, deterministic complete sampling, immutable core identity, and binding metadata.
- Validate two participant assignments with exact deck/seat mapping for normal and mirror assignments.
- Run a short Teacher episode through EpisodicEnvironment.step() → TrajectoryRecorder → shard → semantic replay → admission → receipt/dataset.

**Required participant-perspective handoff gate:**

- Run a participant-perspective handoff scenario: Player 0 proposes and receives StepAccepted; when StepAccepted.next belongs to Player 1, Player-0 state does not consume Player-1 observation; Player-0 accepted markers still commit; Player 1 uses isolated state; and Player 0 reconciles only when it later receives its own actionable public frame before selection. Prove no cross-participant fact, hand, or entity information enters either state.
- Prove exact frame-local correlation: the proposal observation decision index equals AcceptedActionTransition.decision_index.

- Inject a stale/nonmember action and assert no Teacher state commit, no canonical record, quarantine, and no retry.
- Spawn independent processes and compare decision keys, diagnostics, and state deltas.

**Regression tests:** all Phase-4A focused policy tests, trajectory_codec_test, trajectory_recorder_test, the new teacher_runner_trajectory_test for short Teacher replay/admission integration, and policy_runner_integration_test. The existing Phase-4A replay-admission Heavy Replay is outside this focused set and runs only under the owning-layer rule or an explicitly authorized final gate.

**Privacy implications:** The runner may use control-plane frame/token values only to submit to V2 and to route a pending proposal to its owning participant. TeacherCore receives the same public inputs as RandomLegal and no trajectory restricted evidence; StepAccepted.next is never fed into the previous participant's state merely because it is next.

**Determinism implications:** Teacher uses PolicyRngMode::None; runner output does not depend on environment root seed for policy behavior, path, PID, thread, provider, or callback completion order. Frame routing is explicit by participant perspective and proposal decision-index correlation, not by completion order.

**Replay/provenance implications:** This task is the first Teacher integration through the exact trusted path. No direct receipt issuance, special Teacher shard, or explanation-required admission branch is permitted.

- [ ] Write failing provenance and runner integration tests, including policy-origin rejection/quarantine.
- [ ] Run ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^(teacher_provenance_test|teacher_runner_trajectory_test)$" and record the expected missing-target failure.
- [ ] Implement DeterministicTeacherPolicy as a thin adapter over TeacherCore and state, returning existing PolicySelection.
- [ ] Extract or share only the runner mechanics needed to preserve the existing RandomLegal path; keep V2 action submission and recorder callbacks unchanged.
- [ ] Register exact production identities and validate the profile/binding registry before session creation.
- [ ] Re-run the Teacher tests and the Phase-4A regression set from P4B-G14.
- [ ] Run git diff --check, commit with feat: integrate teacher with trusted trajectory runner, and stop for review.

**Stop condition:** An accepted Teacher action is indistinguishable from another trusted policy at V2/trajectory/admission boundaries, while rejected Teacher actions remain zero-record quarantines.

## Task 12: Phase-4B focused acceptance and evidence

**Files:**

- Create: tools/p4b/phase4b_acceptance.py
- Create: tools/p4b/phase4b_clean_checkout_acceptance.ps1
- Create: tests/teacher/phase4b_acceptance_test.py
- Create: docs/p4b/P4B_ACCEPTANCE.md
- Create: docs/p4b/p4b_acceptance.json
- Modify: CMakeLists.txt only for any final probe target.

**Owning layer:** Phase-4B acceptance/evidence orchestration.

**Invariants affected:** P4B-G00–P4B-G18 and the final Task Gate/Integration Gate/Phase-4B Final Acceptance definitions.

**Semantic change vs internal implementation:** Acceptance schema, gate IDs, report fields, frozen scenario identity, and evidence provenance are semantic evidence contracts. Process orchestration and report formatting are internal when they preserve the schema and source identities.

**Focused tests:**

- Run all P4B focused CTest/Python gates in one short-scope acceptance command.
- Run both profile roles, normal/mirror seat assignment, starting player 0/1, deterministic independent-process replay, recovery scenarios, and a short admitted Teacher trajectory.
- Generate machine-readable gate results from executable output; do not hand-edit JSON or Markdown.

**Regression tests:** P4B-G14 Phase-4A focused policy set and any Phase-3 trajectory tests whose owning layer was changed. The historical Phase-4A Heavy Replay/lifecycle artifacts remain frozen baseline evidence unless the owning-layer rule in the contract requires a fresh run.

**Privacy implications:** Acceptance compares paired public-equivalent worlds and scans all emitted diagnostics/state artifacts for forbidden hidden/internal fields.

**Determinism implications:** The report binds exact source head, rules bundle, profile IDs, policy artifact IDs, scenario IDs, and independent-process outputs; physical paths are evidence locators only.

**Replay/provenance implications:** Final acceptance requires the existing trajectory/admission output and exact artifact bindings; no Teacher-only acceptance shortcut is allowed.

- [ ] Write failing acceptance validator tests for required gate IDs, statuses, source/head identity, and no unrun PASS claims.
- [ ] Run python -B tests/teacher/phase4b_acceptance_test.py and record the expected missing-gate failure.
- [ ] Implement the short acceptance runner and generated evidence writer with explicit PASS, FAIL, NOT_RUN, SKIPPED, and BLOCKED statuses.
- [ ] Run the focused acceptance at a clean checkout and compare semantic outputs across independent processes.
- [ ] Run any conditional long gate only when the owning-layer rule requires it; record unrun heavy gates as NOT_RUN, never as PASS.
- [ ] Validate generated JSON/Markdown from the source evidence and run git diff --check.
- [ ] Commit with test: accept phase 4b teacher path, obtain independent review, and stop. Do not begin Phase 4C.

**Stop condition:** P4B-G00–G18 and the final fixed-matchup Teacher/provenance matrix have fresh evidence, unresolved gates remain explicitly reported, and no Phase-4C or ML implementation is included.

## Cross-task review checklist

Before any future task is considered complete, the implementer must answer all of the following from fresh command output:

- Does the changed code consume only PolicyInput, immutable profile data, and same-participant public-derived state?
- Does every valid supplied candidate produce exactly one evaluation record in supplied order?
- Are invalid/unsupported arithmetic, public facts, profile references, and domains fail-closed?
- Is every authoritative order explicit and independent of unordered storage, PID, path, wall time, scheduling, provider, and pointer identity?
- Does rejected step() produce zero state advancement, zero canonical record, quarantine, and no retry?
- Does the action use the existing public key and the same V2 → recorder → shard → replay → admission chain?
- Did the task change only its declared semantic surface?
- Were only the narrow focused tests and relevant short regressions run, with heavy gates reported separately?
- Are all claimed statuses tied to commands actually executed at the exact source head?

Any unanswered question is a stop condition and must be reported as BLOCKED, not inferred closed.
