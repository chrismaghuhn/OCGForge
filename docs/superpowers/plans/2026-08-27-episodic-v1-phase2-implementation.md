# Episodic Environment V1 Phase 2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the accepted `ocgforge.episodic_environment.v1` reset/step facade above the existing `EpisodeDriver`, preserve canonical-simulation semantics, and produce independently verifiable Phase-2 acceptance evidence without merging the resulting PR.

**Architecture:** Keep `EpisodeDriver` as the sole owner of CoreHost, protocol, continuation, response-submission, process, observation, trace, and closure advancement. Add value-owned certified environment, identity, lifecycle, token, public DTO, rejection, run-control, and terminal-view layers in `ygo::environment::EpisodicEnvironment`; adapt `CanonicalSimulation` only through a value-returning internal driver result. Keep response bytes, raw messages, paths, and hidden engine state below the public facade.

**Tech Stack:** C++17, CMake/Ninja, pinned ocgcore/CardScripts/BabelCDB rules bundle, SHA-256 canonical codecs, existing PlayerObservation/EngineTrace v2, CTest, Python unittest/evidence harnesses.

---

## File map and ownership

Create the production public boundary in:

- `include/ygo/environment/episodic_environment.hpp` — value-owned public config, spec, run-control, token, request/candidate/frame, result/closure variants, and `EpisodicEnvironment` API. No CoreHost or protocol response bytes appear here.
- `src/environment/episodic_environment.cpp` — certified-config validation, canonical identity codecs, safe projection, lifecycle, fixed rejection precedence, token namespace, terminal cache, and translation of Driver boundaries.

Modify existing production seams only in:

- `include/ygo/environment/episode_driver.hpp` — typed Driver run-control, accepted-action metadata, interruption, terminal observation values, checked counters, and internal boundary/failure data.
- `src/environment/episode_driver.cpp` — preserve existing advancement and trace bytes while returning accepted metadata, enforcing Driver-owned budgets, closing administratively, and materializing both perspective-safe terminal observations before teardown.
- `src/simulation/canonical_simulation.cpp` — consume `DriverApplyResult` and preserve historical `SimulationResult` process-budget mapping and semantic outputs.
- `CMakeLists.txt` — compile the facade into `ygo_m4`; add focused facade/probe tests without creating a second production library.

Add focused tests and harnesses in:

- `tests/episodic/episodic_identity_test.cpp`
- `tests/episodic/episodic_environment_test.cpp`
- `tests/episodic/episodic_lifecycle_test.cpp`
- `tests/episodic/episodic_rejection_test.cpp`
- `tests/episodic/episodic_interrupt_test.cpp`
- `tests/episodic/episodic_budget_test.cpp`
- `tests/episodic/episodic_terminal_privacy_test.cpp`
- `tests/episodic/episodic_replay_test.cpp`
- `tests/episodic/episodic_fault_injection_test.cpp`
- `tests/episodic/episodic_acceptance.py`
- `tests/episodic/episodic_worker_determinism.py`
- `tools/ygo_episodic_probe/main.cpp`
- `tools/episodic/acceptance.py` and narrowly scoped evidence helpers where the existing tooling pattern requires them.

Generate, rather than hand-edit, derived evidence under:

- `artifacts/episodic/v1/`

Do not modify `docs/contracts/*`, `docs/adr/*`, `docs/m3*`, `docs/m4/*`, EngineTrace v2, PlayerObservation v1, the rules lock, locked decks, or historical baseline artifacts.

## Task 1: Lock the implementation baseline and independent identity fixtures

**Files:**

- Create: `tests/episodic/episodic_identity_test.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/episodic/normative_prerequisites_test.cpp` remains unchanged and is rerun.

- [ ] **Step 1: Record the exact source and dependency identity in the test fixture.**

  Assert the live `origin/main` base `e2557f7e059f60d7d43161f543494a0c00cc1f96` in the generated acceptance manifest, and assert the lock values from `third_party/rules_bundle.lock.json`: bundle `3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f`, format `TCG_ADVANCED_2026_05_18`, mode `DUEL_MODE_MR5`, flags `190464`, core API `11.0`, core commit `9a0c558c2d686542f7914a6d529fd7aa57746aed`, patchset `ocgforge.ocgcore.api_hardening.v1`, patchset SHA `6b5421b3a852085f48fa161a5ba1540f902aa00784a337694b21c9efc34f69bd`, CardScripts commit `f337c87018ca723c1aded5143e616bb649555273`, BabelCDB commit `89ad6837b0766a52984d8c715a7d5d4f8447946b`, and locked deck hashes `8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7` and `6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188`.

- [ ] **Step 2: Write failing C++ tests for seed vectors, closure identity, candidate digest, and independent identity byte layouts.**

  Add tests that construct expected byte vectors directly with local `append_u8`, `append_u32be`, `append_u64be`, `append_string`, and `append_vector` helpers. Cover seed roots `0`, `1`, `UINT64_MAX`, `0x8000000000000000`, and `0x0123456789abcdef`; the accepted closure golden input; candidate kind `idle_command` with two ordered keys; and the environment, episode, and semantic-decision field orders from sections 7.1–7.4 of `docs/superpowers/specs/2026-08-26-episodic-phase2-public-facade.md`. Assert the expected digest strings and that key order/count mutation changes the digest.

- [ ] **Step 3: Run the focused test to verify it fails for the absent public codec/API.**

  Run:

  ```powershell
  cmake --build build/dev-windows --target episodic_identity_test --parallel 4
  ctest --test-dir build/dev-windows -R episodic_identity_test --output-on-failure
  ```

  Expected: compile failure naming the not-yet-defined facade identity types/functions, not a test typo or an unrelated baseline failure.

- [ ] **Step 4: Add only the minimal public identity/config declarations and implementation needed by the failing test.**

  Reuse `identity_contract.hpp`, `candidate_domain_evidence.hpp`, `core::derive_seed_bundle`, `core::canonical_required_script_codes`, `simulation::CanonicalSimulationConfig`, and the lock-derived compile definitions. Add checked canonical byte helpers, `CertifiedEnvironmentConfig::canonical()`, environment/episode/decision ID functions, and explicit string tables. Keep paths in an internal resource structure and exclude them from every semantic codec.

- [ ] **Step 5: Build and run the focused identity test.**

  Run the commands from Step 3. Expected: the identity executable passes all vectors and mutation assertions.

- [ ] **Step 6: Commit the identity fixtures and minimal codec.**

  ```powershell
  git diff --check
  git add tests/episodic/episodic_identity_test.cpp include/ygo/environment/episodic_environment.hpp src/environment/episodic_environment.cpp CMakeLists.txt
  git commit -m "test: lock Episodic V1 identity fixtures"
  ```

## Task 2: Add the value-owned public DTOs and fail-closed candidate projection

**Files:**

- Modify: `include/ygo/environment/episodic_environment.hpp`
- Modify: `src/environment/episodic_environment.cpp`
- Create: `tests/episodic/episodic_environment_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing projection tests.**

  Create protocol fixtures with one complete atomic candidate, one intermediate continuation candidate, a visible source/target candidate, a duplicate-key domain, an empty domain, and a candidate whose semantic key or locator cannot be proven perspective-safe. Assert that public request/candidate values are owned copies, preserve authoritative vector order and semantic key spelling, omit exact response bytes/raw hashes/private locators, and return typed `EpisodeFailure` for duplicate, empty, malformed, incomplete, or privacy-unsafe publication.

- [ ] **Step 2: Run the projection tests and verify the expected red failures.**

  Run:

  ```powershell
  cmake --build build/dev-windows --target episodic_environment_test --parallel 4
  ctest --test-dir build/dev-windows -R episodic_environment_test --output-on-failure
  ```

  Expected: compile failure until the public DTOs and projection entry points exist.

- [ ] **Step 3: Define independent public DTOs.**

  Define `EnvironmentDecisionRequest`, `EnvironmentActionCandidate`, `EnvironmentContinuationView`, `DecisionFrame`, `SubmissionToken`, and `ActionSelection` as owning values. Public candidates contain the safe action kind, unchanged semantic key, safe optional visible references, typed values, continuation ID/step where safe, and `submits_engine_response`; they never contain `exact_response_bytes`, raw-message hash, protocol object references, CoreHost locators, or caches.

- [ ] **Step 4: Implement projection in authoritative order.**

  Validate using `protocol::validate_candidate_set`, reject empty/duplicate/malformed domains, copy exactly one public candidate per internal candidate, run `observation::candidate_observation_consistent` for required card-bearing fields, validate safe continuation fields, compare candidate count and key vector, compute `candidate_domain_digest` over the unchanged ordered keys, compute `semantic_decision_id`, and only then return a complete frame. Do not sort, filter, deduplicate, truncate, cap, or fabricate.

- [ ] **Step 5: Run the projection tests green and inspect the public header.**

  Run the focused test command, then:

  ```powershell
  rg -n "exact_response|raw_message|CoreHost|protocol::|unique_ptr|pointer|cache" include/ygo/environment/episodic_environment.hpp
  ```

  Expected: forbidden privileged fields occur only in private implementation code, not in public DTOs.

- [ ] **Step 6: Commit the public DTO/projection boundary.**

  ```powershell
  git diff --check
  git add include/ygo/environment/episodic_environment.hpp src/environment/episodic_environment.cpp tests/episodic/episodic_environment_test.cpp CMakeLists.txt
  git commit -m "feat: add owned Episodic V1 public DTOs"
  ```

## Task 3: Add the minimal Driver accepted-action and run-control seam

**Files:**

- Modify: `include/ygo/environment/episode_driver.hpp`
- Modify: `src/environment/episode_driver.cpp`
- Modify: `src/simulation/canonical_simulation.cpp`
- Modify: `include/ygo/simulation/simulation_contract.hpp` only if a checked internal bridge is required; preserve the public legacy field widths.
- Create: `tests/episodic/episode_driver_run_control_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing Driver tests for accepted metadata and budgets.**

  Assert that an accepted intermediate key returns `accepted.selected_semantic_key` unchanged, `core_response_submitted == false`, no final response hash, unchanged engine-step index, no response/process call caused by that action, and a new continuation boundary. Assert that an atomic/final key returns `core_response_submitted == true` and a Driver-produced final response hash. Add process-budget and semantic-action-budget fixtures that prove checked positive limits, no wraparound, and typed interruption reasons.

- [ ] **Step 2: Run the Driver tests red.**

  ```powershell
  cmake --build build/dev-windows --target episode_driver_run_control_test --parallel 4
  ctest --test-dir build/dev-windows -R episode_driver_run_control_test --output-on-failure
  ```

  Expected: compile failures for missing `DriverApplyResult`/run-control APIs.

- [ ] **Step 3: Add typed internal Driver values.**

  Add `DriverAcceptedAction`, `DriverApplyResult`, `DriverRunControl`, `DriverInterrupted`, and owned terminal observation values. Use `uint64_t` checked process/action counters internally. Keep `DriverBoundary` as the single advancement result family and preserve existing `DriverDecisionBoundary` borrowing rules.

- [ ] **Step 4: Refactor `apply_semantic_key` without moving authority.**

  Copy the selected semantic key before any mutating call, apply the existing continuation/response path, and return accepted metadata produced at the point the Driver knows the exact response submission/hash. Intermediate transitions never call `submit_response` or `process`. Final/atomic transitions submit exactly once and then use existing `advance_until_boundary` logic.

- [ ] **Step 5: Add Driver-owned run-control checks.**

  Validate positive finite budgets at construction. Check administrative cancellation first and process budget before every CoreHost process call. Count each accepted semantic key once at the Driver boundary. Check semantic budget before publishing another actionable frame, while allowing a true terminal reached by the accepted action to win. Preserve canonical process-budget behavior for `CanonicalSimulation`.

- [ ] **Step 6: Adapt CanonicalSimulation and run the full pre-facade regression.**

  Consume `DriverApplyResult`, ignore its added metadata in legacy aggregation, keep `DriverProcessBudgetExceeded` mapped to `failure_code = "nonterminal"` and `pass = false`, then run:

  ```powershell
  cmake --build build/dev-windows --parallel 4
  ctest --test-dir build/dev-windows -R "episode_driver_|m4_simulation_contract_test|normative_prerequisites_test" --output-on-failure
  ```

- [ ] **Step 7: Commit the minimal Driver seam.**

  ```powershell
  git diff --check
  git add include/ygo/environment/episode_driver.hpp src/environment/episode_driver.cpp src/simulation/canonical_simulation.cpp tests/episodic/episode_driver_run_control_test.cpp CMakeLists.txt
  git commit -m "refactor: return accepted EpisodeDriver metadata"
  ```

## Task 4: Implement certified construction, reset, lifecycle, and token freshness

**Files:**

- Modify: `include/ygo/environment/episodic_environment.hpp`
- Modify: `src/environment/episodic_environment.cpp`
- Create: `tests/episodic/episodic_lifecycle_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing reset/lifecycle/token tests.**

  Cover `EMPTY`, `AWAITING_ACTION`, `GAME_TERMINAL`, `INTERRUPTED`, and `FAILED`; reject reset while awaiting; reject step outside awaiting; repeat identical semantic reset after closure; assert equal environment/episode IDs but unequal nonzero tokens; assert monotonic episode/frame counters and no token change on rejected calls; assert `UINT64_MAX` exhaustion fails closed before new Driver construction or publication.

- [ ] **Step 2: Run the lifecycle tests red.**

  ```powershell
  cmake --build build/dev-windows --target episodic_lifecycle_test --parallel 4
  ctest --test-dir build/dev-windows -R episodic_lifecycle_test --output-on-failure
  ```

- [ ] **Step 3: Implement `EpisodicEnvironment::create`.**

  Load only the canonical certified config. Verify the compiled lock, resolved CardScripts/database/core identities, locked deck hashes, patchset identity, required-script closure identity, format/mode/flags, and all schema IDs before retaining an immutable semantic config. Keep runtime paths in private `EnvironmentResources` and retain no CoreHost.

- [ ] **Step 4: Implement reset validation and construction.**

  Apply lifecycle/version/config/spec/start-player/run-control/resource checks before mutating counters or creating a Driver. Map seat assignment to the two locked deck values, derive `core::derive_seed_bundle(root_seed)`, compute the accepted episode ID, increment the episode counter with overflow checking, create a fresh Driver, and advance it to the first frame/terminal/interruption/failure. Materialize and own a full public frame only after projection and coupling checks succeed.

- [ ] **Step 5: Implement the token namespace.**

  Reserve zero; increment episode incarnation once per successful reset and frame generation once per published actionable frame; never reset counters per episode; invalidate by clearing the current frame at closure. Ensure neither token component is supplied to environment/episode/decision IDs, candidate digests, trace/gameplay hashes, observations, or replay equality.

- [ ] **Step 6: Run lifecycle tests green and verify exact public transitions.**

  Run the focused test and the existing `episode_driver_*` tests. Expected: all lifecycle/token assertions pass and old Driver tests remain green.

- [ ] **Step 7: Commit reset/lifecycle/token behavior.**

  ```powershell
  git diff --check
  git add include/ygo/environment/episodic_environment.hpp src/environment/episodic_environment.cpp tests/episodic/episodic_lifecycle_test.cpp CMakeLists.txt
  git commit -m "feat: add Episodic V1 reset lifecycle"
  ```

## Task 5: Implement step validation, rejection precedence, accepted transitions, and interruption

**Files:**

- Modify: `include/ygo/environment/episodic_environment.hpp`
- Modify: `src/environment/episodic_environment.cpp`
- Create: `tests/episodic/episodic_rejection_test.cpp`
- Create: `tests/episodic/episodic_interrupt_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing rejection and interrupt tests.**

  For each rejection class mutate only one input and assert the exact precedence: incompatible contract, invalid lifecycle, wrong episode, stale token, wrong semantic decision, unknown key. Snapshot frame bytes, episode/decision IDs, token, domain digest, Driver continuation hash/counters, response/process/semantic-action counts, trace length/prefix hash, observation session state, public index, and lifecycle before and after. Assert the current valid selection still succeeds after every rejection. Add live atomic and continuation administrative-cancel tests with no candidate selection, response submission, or core process.

- [ ] **Step 2: Run both tests red.**

  ```powershell
  cmake --build build/dev-windows --target episodic_rejection_test episodic_interrupt_test --parallel 4
  ctest --test-dir build/dev-windows -R "episodic_(rejection|interrupt)_test" --output-on-failure
  ```

- [ ] **Step 3: Implement fixed caller-side validation.**

  Validate contract, lifecycle, episode ID, token, semantic decision ID, and key membership in that order. Perform key membership against the complete current owned domain before invoking the Driver. Return safe typed `StepRejected` values without changing any authoritative state. If the Driver unexpectedly reports `InvalidSemanticKey` after membership succeeded, close as `EpisodeFailure(INTERNAL_DOMAIN_DIVERGENCE)` and record mutation uncertainty.

- [ ] **Step 4: Implement accepted action result translation.**

  Treat the call as accepted after all six checks. Build `AcceptedActionTransition` exclusively from `DriverApplyResult` metadata, count the action from Driver evidence, translate next actionable boundaries with a new public decision index/token, and translate Driver interruption/failure/terminal boundaries without fabricating a next index. A post-acceptance failure must return `StepAccepted` with `next = EpisodeFailure`.

- [ ] **Step 5: Implement administrative interruption.**

  Accept only `ADMINISTRATIVE_CANCEL` while awaiting; call the Driver close seam, preserve the last valid audit prefix, invalidate the token, clear frame/Driver state, and return `Interrupted`. Reject unsupported reason or closed/empty lifecycle without mutation.

- [ ] **Step 6: Run focused tests and existing regressions.**

  ```powershell
  ctest --test-dir build/dev-windows -R "episodic_(rejection|interrupt)_test|episode_driver_" --output-on-failure
  git diff --check
  ```

- [ ] **Step 7: Commit step/rejection/interruption behavior.**

  ```powershell
  git add include/ygo/environment/episodic_environment.hpp src/environment/episodic_environment.cpp tests/episodic/episodic_rejection_test.cpp tests/episodic/episodic_interrupt_test.cpp CMakeLists.txt
  git commit -m "feat: add Episodic V1 step validation"
  ```

## Task 6: Implement typed closures, budgets, terminal views, teardown, and reset-after-failure

**Files:**

- Modify: `include/ygo/environment/episodic_environment.hpp`
- Modify: `src/environment/episodic_environment.cpp`
- Modify: `include/ygo/environment/episode_driver.hpp`
- Modify: `src/environment/episode_driver.cpp`
- Create: `tests/episodic/episodic_budget_test.cpp`
- Create: `tests/episodic/episodic_terminal_privacy_test.cpp`
- Create: `tests/episodic/episodic_fault_injection_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing closure/budget/failure tests.**

  Cover true terminal, engine-process budget, semantic-action budget on an intermediate continuation, semantic budget where the Nth action reaches true terminal, administrative cancel, retry, unsupported/malformed protocol, incomplete/duplicate candidate, privacy mismatch, response inconsistency, CoreHost error, resource identity mismatch, and token exhaustion. Assert that only true engine terminal has winner/win reason; interruptions have typed reasons/no outcome; failures have typed code/stage/mutation flag/no outcome and immediately reject later steps.

- [ ] **Step 2: Run focused tests red.**

  ```powershell
  cmake --build build/dev-windows --target episodic_budget_test episodic_terminal_privacy_test episodic_fault_injection_test --parallel 4
  ctest --test-dir build/dev-windows -R "episodic_(budget|terminal_privacy|fault_injection)_test" --output-on-failure
  ```

- [ ] **Step 3: Add public closure types and restricted diagnostics.**

  Define `EpisodeTerminal`, `EpisodeInterrupted`, `EpisodeFailure`, `ResetResult`, `StepResult`, `InterruptResult`, and their rejection codes using only safe typed values, bounded counts, accepted hashes, optional semantic IDs, and restricted diagnostic references. Do not expose raw messages, response bytes, paths, pointers, private observations, or unbounded exception text.

- [ ] **Step 4: Materialize terminal observations before Driver teardown.**

  Extend the Driver to build safe `PlayerObservation` values for perspectives 0 and 1 while CoreHost and both sessions are alive, verify schema/perspective/hash, and return owned copies. Cache only those values in the facade; `perspective_terminal_view` returns a copy and returns no value for invalid player/interruption/failure.

- [ ] **Step 5: Close and destroy mutable state on every closure.**

  Clear current frame, invalidate token, destroy Driver/CoreHost/duel/Lua/session/continuation/raw request/mutable trace state, and retain only certified config, token counters, immutable closure evidence, and true-terminal safe views. Ensure reset after failure constructs an entirely new Driver and reproduces a fresh-process semantic reference.

- [ ] **Step 6: Run focused closure and privacy tests green.**

  Run the target command from Step 2, then run:

  ```powershell
  ctest --test-dir build/dev-windows -R "episode_driver_|continuation_privacy_test|m3_real_deck_privacy_test" --output-on-failure
  ```

- [ ] **Step 7: Commit closures, budgets, and teardown.**

  ```powershell
  git diff --check
  git add include/ygo/environment/episodic_environment.hpp src/environment/episodic_environment.cpp include/ygo/environment/episode_driver.hpp src/environment/episode_driver.cpp tests/episodic/episodic_budget_test.cpp tests/episodic/episodic_terminal_privacy_test.cpp tests/episodic/episodic_fault_injection_test.cpp CMakeLists.txt
  git commit -m "feat: add Episodic V1 typed closures"
  ```

## Task 7: Add replay, G28 witness discovery, process determinism, and paired-world harnesses

**Files:**

- Create: `tests/episodic/episodic_replay_test.cpp`
- Create: `tests/episodic/episodic_worker_determinism.py`
- Create: `tests/episodic/episodic_witness_discovery.py`
- Create or modify: `tools/ygo_episodic_probe/main.cpp`
- Modify: `CMakeLists.txt`
- Create generated: `artifacts/episodic/v1/g28_max_candidate_witness.json`

- [ ] **Step 1: Write a failing semantic replay test.**

  Record only canonical environment identity, `EpisodeSpec`, and accepted semantic-key vectors for terminal, interrupted, and failed executions. Replay in a fresh environment/process, regenerate tokens locally, and compare environment/episode/decision IDs, decision/engine indices, protocol request IDs/kinds, complete ordered domains/digests, safe observation hashes, continuation state hashes, response-submission booleans, final response hashes, closure class/reason, terminal outcome, and semantic gameplay hash. Assert candidate indices, response bytes, tokens, PIDs, timing, and hidden state are absent from replay input.

- [ ] **Step 2: Run replay red and add the value-only probe.**

  ```powershell
  cmake --build build/dev-windows --target ygo_episodic_probe episodic_replay_test --parallel 4
  ctest --test-dir build/dev-windows -R episodic_replay_test --output-on-failure
  ```

  The probe must emit safe canonical JSON on stdout and keep diagnostics on stderr. It links `ygo_m4`, owns one environment, and has no production RPC/worker role.

- [ ] **Step 3: Implement replay comparison and closure replay.**

  Revalidate every current domain before applying a recorded key. Reconstruct trusted response bytes only inside the Driver. Preserve interruption/failure as semantic prefix plus typed closure; never turn them into terminal outcomes.

- [ ] **Step 4: Add independent process/worker comparison.**

  Launch the same probe job list in one and sixteen independent processes, compare only semantic JSON fields, and exclude worker count, slot, PID, scheduling, timing, and path provenance. Reuse existing M4 process-management patterns without changing the M4 worker protocol.

- [ ] **Step 5: Add paired-world live and terminal privacy tests.**

  Create two test-only worlds differing only in hidden opponent information. Compare acting-perspective observations, public candidate DTOs, semantic keys, domain digest, semantic decision ID, continuation fields, and cached terminal views. Unsafe semantic-key/domain publication must fail closed rather than being redacted by substitution.

- [ ] **Step 6: Discover and independently replay G28.**

  Persist one row per complete published domain, including continuations, with candidate count, request kind, episode ID, environment decision index, engine index, protocol decision ID, digest, and ordered keys. Compute `candidate_domain_max` as `MAX` over individual domains; retain `candidate_max_total` only as separate aggregate accounting. Select the exact tie-break from the accepted evidence contract and replay the selected witness independently. Do not use historical aggregate `1344` as a domain maximum.

- [ ] **Step 7: Run replay/privacy/determinism tests and commit harnesses/evidence sources.**

  ```powershell
  python tests/episodic/episodic_worker_determinism.py --probe build/dev-windows/ygo_episodic_probe.exe
  python tests/episodic/episodic_witness_discovery.py --probe build/dev-windows/ygo_episodic_probe.exe --output artifacts/episodic/v1/g28_max_candidate_witness.json
  ctest --test-dir build/dev-windows -R "episodic_(replay|terminal_privacy)_test" --output-on-failure
  git diff --check
  git add tests/episodic/episodic_replay_test.cpp tests/episodic/episodic_worker_determinism.py tests/episodic/episodic_witness_discovery.py tools/ygo_episodic_probe/main.cpp artifacts/episodic/v1/g28_max_candidate_witness.json CMakeLists.txt
  git commit -m "test: add Episodic V1 replay and witness harnesses"
  ```

## Task 8: Generate the G01–G30 acceptance root and evidence

**Files:**

- Create: `tests/episodic/episodic_acceptance.py`
- Create: `tools/episodic/acceptance.py`
- Generate: `artifacts/episodic/v1/*.json`
- Modify: `CMakeLists.txt` only for executable/script registration.

- [ ] **Step 1: Define one machine-readable evidence schema.**

  Each gate record must include exact implementation HEAD, `ocgforge.episodic_environment.v1`, environment/schema IDs, rules bundle, patchset, ordered locked deck identities, gate ID, result, command/tool identity, and safe diagnostics. The manifest must distinguish `PASS`, `FAIL`, `NOT_RUN`, `SKIPPED`, and `BLOCKED`; it must never infer PASS from a missing result.

- [ ] **Step 2: Add executable gate runners for G01–G30.**

  Wire the public tests/harnesses to the exact gate conditions in `docs/episodic/EPISODIC_V1_ACCEPTANCE.md`: independent reset identity, seed separation, repeated-reset freshness, isolation/soak, coupling, completeness/order/digest, rejection/zero mutation, continuations/response equivalence, budgets, canonical equivalence, replay, worker determinism, paired-world privacy, closure ownership, interruption, fail-closed mapping, reset after failure, G28 witness, reward independence, and version rejection. Keep reward as a test-only external function and fault injection test-only.

- [ ] **Step 3: Add version mutation tests.**

  Mutate each public contract/schema identity independently and require rejection before mutation. Cover episodic, decision, action, seed, script/closure, observation, candidate, environment, episode, and semantic-decision IDs.

- [ ] **Step 4: Add the 500-episode persistent soak.**

  Use `A -> B -> C -> A -> D -> A`, mixed seeds/seats/starting players, terminal/interrupted/continuation-heavy/atomic episodes, and an injected failure before a valid reset. Compare every repeated A with a fresh-process reference and report stale-token acceptance, candidate loss/duplication/truncation, privacy mismatches, semantic drift, and resource growth.

- [ ] **Step 5: Add reward-independence harness.**

  Apply two external reward policies to one terminal environment execution, compare all environment values/hashes/actions/outcome, and allow only reward and policy ID to differ. Assign no implicit reward to interruption/failure.

- [ ] **Step 6: Run G01–G30 in the local Zig environment and generate evidence.**

  ```powershell
  python tests/episodic/episodic_acceptance.py --all --probe build/dev-windows/ygo_episodic_probe.exe --output artifacts/episodic/v1
  ```

  Expected: each gate has an explicit result; any unavailable native/hosted gate remains `NOT_RUN` or `BLOCKED` with a reason.

- [ ] **Step 7: Commit evidence generators and generated artifacts only when generated by the runner.**

  ```powershell
  git diff --check
  git add tests/episodic/episodic_acceptance.py tools/episodic/acceptance.py artifacts/episodic/v1 CMakeLists.txt
  git commit -m "test: add Episodic V1 acceptance evidence"
  ```

## Task 9: Run compatibility/regression gates and perform self-review

**Files:**

- Modify only files required by verified failures.
- Generate: `artifacts/episodic/v1/regression_m0_m4.json`, `artifacts/episodic/v1/episodic_acceptance_manifest.json`.

- [ ] **Step 1: Run the focused new C++ suite.**

  ```powershell
  cmake --build build/dev-windows --parallel 4
  ctest --test-dir build/dev-windows -R "episodic_|episode_driver_|normative_prerequisites_test" --output-on-failure
  ```

- [ ] **Step 2: Run all declared repository checks.**

  ```powershell
  ctest --test-dir build/dev-windows --output-on-failure
  python -m unittest discover -s tests/python -v
  python tests/protocol/decision_coverage_test.py
  python tests/observation/observation_coverage_test.py
  python tools/verify_rules_bundle.py --lock third_party/rules_bundle.lock.json --cache C:\yogiohML\.cache\rules_bundle
  python -m unittest discover -s tests/m3 -v
  python -m unittest tests/m4/test_shared_simulation_compatibility.py tests/m4/test_worker_protocol.py tests/m4/test_worker_integration_fast.py -v
  git diff --check
  ```

  Report the executed native-equivalent Zig result separately from native-MSVC, which is `NOT_RUN` when the compiler/Ninja are absent.

- [ ] **Step 3: Verify authority and privacy by source inspection.**

  ```powershell
  python tests/episodic/episode_driver_ownership_guard.py
  rg -n "OCG_Duel(Process|SetResponse)|submit_response|apply_continuation_action|exact_response_bytes|raw_message_hash" src/environment/episodic_environment.cpp include/ygo/environment/episodic_environment.hpp
  rg -n "candidate.*(sort|unique|erase|filter|truncate|resize|cap)|std::sort|std::unique" src/environment/episodic_environment.cpp
  ```

  The facade must not own advancement, response construction/submission, continuation transitions, hidden queries, or candidate repair.

- [ ] **Step 4: Review the complete diff against the accepted contracts.**

  Check that no public DTO contains privileged fields, EngineTrace v2 and PlayerObservation v1 canonical meanings are untouched, canonical simulation retains `nonterminal` process-budget mapping, response hashes come from Driver metadata, IDs exclude tokens/run-control/provenance, rejected calls are zero-mutation, and all G01–G32 requirements have an executable owner or an explicit non-PASS status.

- [ ] **Step 5: Run the generated evidence twice and compare bytes.**

  Run the complete local acceptance command twice with the same exact HEAD and compare every non-provenance artifact byte-for-byte. Do not manually edit generated JSON.

- [ ] **Step 6: Commit only verified compatibility fixes.**

  ```powershell
  git diff --check
  git status --short
  git diff --stat
  git add -A include src tests tools artifacts CMakeLists.txt
  git commit -m "test: close Episodic V1 regression evidence"
  ```

## Task 10: Clean-checkout G31/G32, final verification, and PR preparation

**Files:**

- Create: `tools/episodic/clean_checkout_acceptance.ps1`
- Generate: `artifacts/episodic/v1/episodic_acceptance_manifest.json`
- Generate: `artifacts/episodic/v1/pr_body.md`
- Modify: `.github/workflows/*` only if the existing CI workflow cannot execute the declared new test target and the change is narrowly limited to registration.

- [ ] **Step 1: Commit all implementation work and capture the exact final HEAD.**

  ```powershell
  git status --short
  git diff --check
  git log --oneline --decorate -20
  $finalHead = git rev-parse HEAD
  ```

- [ ] **Step 2: Create a fresh clean checkout at `$finalHead`.**

  Use a separate temporary checkout/worktree, materialize the exact pinned rules bundle through the repository fetch/verify tools, and do not copy ignored build/evidence/baseline output from the working worktree. Run the complete declared configure/build/CTest/Python/coverage/evidence command set from that checkout.

- [ ] **Step 3: Verify G31 and G32.**

  G31 must show fresh M0–M4 regression results with unchanged rules/decks/trace/observation identities. G32 must show that the manifest binds exact HEAD, schema IDs, bundle/decks, all gate results, G28 witness, and artifact hashes; a second render must be byte-identical; `git diff --check` must pass.

- [ ] **Step 4: Run the final verification checklist before any PR claim.**

  Confirm every required command was actually executed, preserve `NOT_RUN` for native-MSVC/hosted checks unavailable locally, keep `HISTORICAL_UNCLASSIFIED_ANOMALY` unchanged, and do not call the milestone `FINAL PASS` unless G01–G32 are all freshly passing.

- [ ] **Step 5: Commit the clean-checkout/evidence source.**

  ```powershell
  git add tools/episodic/clean_checkout_acceptance.ps1 artifacts/episodic/v1
  git commit -m "test/evidence: close Episodic V1 clean-checkout gate"
  ```

- [ ] **Step 6: Self-review the final branch and prepare one PR.**

  Compare `origin/main...HEAD`, classify findings as BLOCKER/MAJOR/MINOR/NOTE, fix all BLOCKER/MAJOR findings, and include the exact base SHA, final HEAD, public API, ownership, identity/token/privacy/candidate-completeness/lifecycle/Driver/replay sections, G01–G32 table, G28 witness, regression results, rules/deck/trace/observation diffs, historical anomaly, and native/hosted CI status in the PR body.

- [ ] **Step 7: Push and open the PR without merging or auto-merge.**

  ```powershell
  git push -u origin chris/episodic-v1-phase2-implementation
  gh pr create --base main --head chris/episodic-v1-phase2-implementation --title "feat: implement Episodic Environment V1" --body-file artifacts/episodic/v1/pr_body.md
  ```

  Verify the returned PR number/URL and hosted check status. Do not merge, enable auto-merge, or begin trajectory/ML work.

## Verification matrix used during execution

| Requirement | Primary proof | Fresh status rule |
| --- | --- | --- |
| Sole advancement owner | Driver ownership guard + source inspection | `PASS` only after command execution |
| Identity bytes | Independent C++ vectors + independent Python recomputation | `PASS` only after both agree |
| Complete ordered domains | Projection tests + replay corpus | Any loss/sort/filter/truncation is a blocker |
| Privacy | Paired-world live/terminal tests | Unsafe key/domain fails closed |
| Freshness/rejection | Mutation snapshots for every rejection class | Any authoritative delta is a blocker |
| Driver equivalence | Canonical simulation and Phase-1 evidence | Historical artifacts are not fresh proof |
| G28 | Generated max/witness and independent replay | Historical `1344` is never a domain max |
| G31/G32 | Clean exact-head checkout | Last gate; no stale/hand-edited evidence |
