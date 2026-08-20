
# M4.0/M4.1 Parallel Simulation Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox ( - [ ] ) syntax for tracking.

**Goal:** Build persistent process workers, prove deterministic/failure-isolated simulation, and produce the measured canonical throughput baseline.

**Architecture:** One coordinator starts persistent native worker processes. Each worker validates an exact ready handshake, accepts JSONL jobs sequentially, and creates a fresh private CoreHost/OCG_Duel per job. One shared native simulation function serves CONFORMANCE and THROUGHPUT; the latter suppresses only full trace serialization/persistence and verbose diagnostics while retaining full in-memory trace construction and semantic hashing for M4 v1.

**Tech Stack:** C++17, CMake/Ninja Zig Windows preset, pinned ocgcore/Lua/CardScripts/BabelCDB, Python 3 standard library, UTF-8 JSONL over stdin/stdout, lightweight Windows ctypes memory sampling, existing CTest/unittest gates.

---

## Fixed constraints

The following identities must remain unchanged:

    format: TCG_ADVANCED_2026_05_18
    duel mode: DUEL_MODE_MR5
    flags: 0x2E800 / 190464
    rules bundle: 3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f
    ocgcore base: 9a0c558c2d686542f7914a6d529fd7aa57746aed
    patchset: ocgforge.ocgcore.api_hardening.v1
    patchset SHA-256: 6b5421b3a852085f48fa161a5ba1540f902aa00784a337694b21c9efc34f69bd
    Deck A SHA-256: 8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7
    Deck B SHA-256: 6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188

No worker creates simulation threads. No CoreHost or OCG_Duel is reused between jobs. Only immutable values already natural to the architecture may be reused at worker startup. Card-data/script initialization remains per fresh CoreHost and is measured.

The primary per-game latency domain is worker-local simulation_elapsed_us: the monotonic interval immediately before native job execution through fresh CoreHost destruction and native result assembly, before worker JSON serialization and pipe flush. It includes per-job setup, ocgcore, protocol/candidate work, observations, full in-memory trace construction, semantic hashing, and teardown. Primary steady-state mean/p50/p95/p99 values use this field only. Coordinator elapsed time covers dispatch through result receipt and is reported separately. Games per second uses total steady-state coordinator wall clock and includes queueing, JSONL, pipe, and flush overhead.

THROUGHPUT retains full EngineTrace record construction and the same semantic gameplay hash as CONFORMANCE. Its trace_hash is absent when full trace serialization is disabled. Only canonical full-trace serialization, trace persistence, and verbose diagnostics are suppressed.

## File map

Create:

- include/ygo/simulation/simulation_contract.hpp — value job/result/error/timing/counter/config types.
- include/ygo/simulation/canonical_simulation.hpp — one-job native entry point.
- src/simulation/canonical_simulation.cpp — extracted canonical M3 full-game loop.
- tools/ygo_m4_worker/json_protocol.hpp and .cpp — strict JSONL parser and serializers.
- tools/ygo_m4_worker/main.cpp — persistent native worker.
- tools/m4/__init__.py, job_generation.py, worker_protocol.py, process_metrics.py, benchmark.py, report.py.
- tests/m4/__init__.py, test_job_generation.py, test_worker_protocol.py, test_worker_integration.py, test_failure_isolation.py, fake_worker_crash.py, test_benchmark_integrity.py.
- docs/m4/M4_ARCHITECTURE.md, M4_WORKER_PROTOCOL_V1.md, m4_benchmark_schema.json.

Modify:

- include/ygo/core/core_host.hpp and src/core/core_host.cpp — coarse read-only API counters.
- src/core/script_reader.hpp and src/core/script_reader.cpp — script callback/load counters without caching.
- CMakeLists.txt — native simulation library, worker executable, and focused tests.
- tools/ygo_core_probe/main.cpp — delegate the M3 full-game branch to the shared native function while preserving the CLI/output contract.

Generate only after verification:

- artifacts/m4/*.json and per-worker stderr files.
- docs/m4/M4_BASELINE.md and docs/m4/m4_baseline.json.

Do not modify or delete CardScripts, BabelCDB, locked decks, rules locks, derived ocgcore, or unrelated files.

### Task 1: Add value contracts and deterministic job generation

Files:

- Create include/ygo/simulation/simulation_contract.hpp
- Create tools/m4/__init__.py and tools/m4/job_generation.py
- Create tests/m4/__init__.py and tests/m4/test_job_generation.py

- [ ] Step 1: Write the failing deterministic tests.

Use this test content:

    import unittest
    from tools.m4.job_generation import derive_job, splitmix64

    class JobGenerationTests(unittest.TestCase):
        def test_splitmix64_reference_vectors(self):
            vectors = {
                (0x0123456789ABCDEF, 0): 0x157A3807A48FAA9D,
                (0x0123456789ABCDEF, 1): 0xD573529B34A1D093,
                (0x0123456789ABCDEF, 2): 0x2F90B72E996DCCBE,
            }
            for (seed, index), expected in vectors.items():
                value = (seed + index * 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF
                self.assertEqual(splitmix64(value), expected)

        def test_partition_cycle_and_job_ids(self):
            jobs = [derive_job(123, index, 2200) for index in range(8)]
            self.assertEqual(
                [(job["seat_assignment"], job["starting_player"]) for job in jobs],
                [("normal", 0), ("mirror", 0), ("normal", 1), ("mirror", 1),
                 ("normal", 0), ("mirror", 0), ("normal", 1), ("mirror", 1)],
            )
            self.assertEqual([job["job_id"] for job in jobs],
                             [f"m4-{index:06d}" for index in range(8)])

        def test_mapping_does_not_depend_on_worker_count(self):
            first = [derive_job(987654321, i, 2200) for i in range(32)]
            second = [derive_job(987654321, i, 2200) for i in range(32)]
            self.assertEqual(first, second)

    if __name__ == "__main__":
        unittest.main()

- [ ] Step 2: Run the focused test and verify the expected import failure.

    python -m unittest tests/m4/test_job_generation.py -v

Expected: import failure because tools.m4.job_generation does not exist.

- [ ] Step 3: Add the C++17 value types.

simulation_contract.hpp must define SimulationMode (Conformance/Throughput), ObservationMode (Full/OffDiagnostic), SeatAssignment (Normal/Mirror), SimulationJob, ErrorCounters, TimingBuckets, OperationCounters, CanonicalSimulationConfig, and SimulationResult.

Use these exact fields:

    struct SimulationJob {
        std::string job_id;
        std::uint64_t seed = 0;
        SeatAssignment seat_assignment = SeatAssignment::Normal;
        std::uint8_t starting_player = 0;
        std::uint32_t max_steps = 2200;
        std::string canonical_rules_id;
        SimulationMode mode = SimulationMode::Throughput;
        ObservationMode observation_mode = ObservationMode::Full;
        bool instrumentation = false;
        bool persist_trace = false;
        std::filesystem::path trace_output;
    };

    struct ErrorCounters {
        std::uint64_t retries = 0;
        std::uint64_t unsupported = 0;
        std::uint64_t automatic = 0;
        std::uint64_t truncated = 0;
        std::uint64_t core_errors = 0;
        std::uint64_t worker_errors = 0;
    };

    struct TimingBuckets {
        std::uint64_t simulation_elapsed_us = 0;
        std::uint64_t core_process_us = 0;
        std::uint64_t protocol_candidate_us = 0;
        std::uint64_t continuation_us = 0;
        std::uint64_t observation_us = 0;
        std::uint64_t trace_hash_us = 0;
        std::uint64_t serialization_us = 0;
        std::uint64_t other_us = 0;
    };

    struct OperationCounters {
        std::uint64_t ocg_duel_process = 0;
        std::uint64_t ocg_duel_query = 0;
        std::uint64_t ocg_duel_query_location = 0;
        std::uint64_t ocg_duel_query_field = 0;
        std::uint64_t ocg_duel_query_count = 0;
        std::uint64_t script_reader_requests = 0;
        std::uint64_t script_loads = 0;
        std::uint64_t observations = 0;
        std::uint64_t entities_projected = 0;
        std::uint64_t candidate_sets = 0;
        std::uint64_t candidate_total = 0;
        std::uint64_t candidate_max = 0;
        std::uint64_t semantic_hashes = 0;
        std::uint64_t trace_bytes_serialized = 0;
    };

SimulationResult must contain job_id, pass, terminal, optional winner/reason, engine_steps, interactive_decisions, semantic_action_count, gameplay_hash, optional trace_hash, failure_code/message, ErrorCounters, TimingBuckets, and OperationCounters. CanonicalSimulationConfig contains RulesBundlePaths, both FixtureDeck values, and the sorted unique required script-code vector. Include filesystem, optional, string, vector, and current core/rules headers. Do not include CoreHost, OCG_Duel, Lua pointers, or shared mutable state.

- [ ] Step 4: Implement job_generation.py.

Use masked 64-bit arithmetic only:

    MASK64 = (1 << 64) - 1
    GOLDEN = 0x9E3779B97F4A7C15
    PARTITIONS = (("normal", 0), ("mirror", 0), ("normal", 1), ("mirror", 1))

    def splitmix64(value):
        value = (value + GOLDEN) & MASK64
        value = ((value ^ (value >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
        value = ((value ^ (value >> 27)) * 0x94D049BB133111EB) & MASK64
        return (value ^ (value >> 31)) & MASK64

    def derive_job(master_seed, index, max_steps):
        seat, starting_player = PARTITIONS[index % len(PARTITIONS)]
        return {
            "job_id": f"m4-{index:06d}",
            "seed": splitmix64((master_seed + index * GOLDEN) & MASK64),
            "seat_assignment": seat,
            "starting_player": starting_player,
            "max_steps": max_steps,
            "canonical_rules_id": RULES_BUNDLE_ID,
            "mode": "throughput",
            "observation_mode": "full",
            "instrumentation": False,
            "persist_trace": False,
        }

Expose a separate constructor that changes only mode, observation_mode, instrumentation, and persist_trace. Do not let worker count, PID, scheduling, wall time, unordered iteration, random_device, or OS randomness enter the mapping.

- [ ] Step 5: Run the focused tests and verify all three pass.

    python -m unittest tests/m4/test_job_generation.py -v

### Task 2: Add coarse CoreHost and script-reader counters

Files:

- Modify include/ygo/core/core_host.hpp and src/core/core_host.cpp.
- Modify src/core/script_reader.hpp and src/core/script_reader.cpp.
- Create tests/core/core_metrics_test.cpp.
- Modify CMakeLists.txt.

- [ ] Step 1: Add the failing C++ metric test.

Construct a CoreHost with the existing M0 fixture macros, load both decks, start, process once, call query, query_location, query_field, and query_count once, then assert that each metric is exactly one and duel_process_calls equals process_call_count(). Register the test with the existing M0 compile definitions.

- [ ] Step 2: Build and run the focused test.

    cmake --preset dev-windows-zig
    cmake --build --preset dev-windows-zig --parallel
    ctest --preset dev-windows-zig -R core_metrics_test --output-on-failure

Expected before implementation: compilation fails because CoreHost::metrics does not exist.

- [ ] Step 3: Implement CoreHostMetrics.

Add this public snapshot:

    struct CoreHostMetrics {
        std::size_t duel_process_calls = 0;
        std::size_t duel_query_calls = 0;
        std::size_t duel_query_location_calls = 0;
        std::size_t duel_query_field_calls = 0;
        std::size_t duel_query_count_calls = 0;
        std::size_t script_reader_requests = 0;
        std::size_t script_loads = 0;
    };

Increment the process/query counters at the entry to the corresponding CoreHost methods and return a copy from metrics() const noexcept. Preserve existing process_call_count and response_submission_count behavior.

Add ScriptStore reader_requests() and successful_loads() accessors. Increment reader_requests_ for every script-reader callback and successful_loads_ only after OCG_LoadScript returns nonzero. Do not add a script cache, lock, static store, or callback behavior change.

- [ ] Step 4: Rebuild and run core_metrics_test; expected result is PASS.

### Task 3: Extract one shared canonical simulation path

Files:

- Create include/ygo/simulation/canonical_simulation.hpp and src/simulation/canonical_simulation.cpp.
- Modify tools/ygo_core_probe/main.cpp and CMakeLists.txt.
- Create tests/m4/test_shared_simulation_compatibility.py.

- [ ] Step 1: Add a characterization test for the current probe.

Invoke the existing ygo_core_probe.exe with --m3-full-game --seed 2 --starting-player 0 --max-steps 1800, parse # m3_summary=, and assert terminal true, 64-character semantic_gameplay_hash, 64-character trace_hash, and all five existing error counters equal zero.

- [ ] Step 2: Run the characterization test before extraction.

    python -m unittest tests/m4/test_shared_simulation_compatibility.py -v

Expected: the current probe completes one canonical terminal game. Preserve its emitted hashes as evidence; do not replace the test with hashes from another seed/seat/start partition.

- [ ] Step 3: Add the shared entry point.

canonical_simulation.hpp must expose:

    namespace ygo::simulation {
    SimulationResult run_canonical_simulation(
        const SimulationJob& job,
        const CanonicalSimulationConfig& config);
    }

The implementation must own CoreHost, ObservationSession[2], EngineTrace, decoded requests, candidate vectors, continuations, and hash/serialization temporaries inside the call. It returns only SimulationResult values and exposes no duel pointer.

- [ ] Step 4: Move the canonical full-game loop without gameplay edits.

Move the canonical M3 full-game setup and loop from tools/ygo_core_probe/main.cpp into canonical_simulation.cpp, including seed_bundle, required_script_codes, choose_candidate, public_state_hash, manifest, unsupported diagnostics, existing policy, decoder, continuation, candidate validation, privacy config, observation builder, and consistency checks. Keep the M0/non-full probe branch until all M3 tests pass.

Both modes construct the same full EngineTrace records. Full observation builds and attaches the same PlayerObservation metadata. OffDiagnostic skips only PlayerObservation construction. Both modes calculate semantic_gameplay_hash over the same records. Conformance additionally calls canonical_trace_jsonl_v2, canonical_trace_hash_v2, and optional trace-file persistence. Throughput does not call full serialization or persistence, but its trace/hash bucket includes record construction and semantic hash.

Measure simulation_elapsed_us from immediately before fresh CoreHost construction to after CoreHost destruction/result assembly. Measure the approved coarse buckets around host.process, decode/validation/policy, apply_continuation_action, observation/consistency, trace/semantic hashing, and full serialization. Set native other_us to the nonnegative residual. Copy CoreHost metrics and loop counters into OperationCounters.

- [ ] Step 5: Adapt --m3-full-game to the shared function.

Build SimulationJob and CanonicalSimulationConfig, call run_canonical_simulation, and translate the result back to the existing ygo.engine_trace.v2 output and ocgforge.m3.game_summary.v1 footer. Preserve existing exit codes for protocol/core/general errors and all output filenames.

- [ ] Step 6: Add ygo_m4.

Create a ygo_m4 static library containing canonical_simulation.cpp, linked to ygo_m0, and link ygo_core_probe to ygo_m4. Pass the existing canonical deck/CardScripts path definitions. Do not alter rules-bundle fetches or derived ocgcore.

- [ ] Step 7: Rebuild and rerun shared/M3 gates.

    cmake --preset dev-windows-zig
    cmake --build --preset dev-windows-zig --parallel
    python -m unittest tests/m4/test_shared_simulation_compatibility.py -v
    python -m unittest discover -s tests/m3 -v

Expected: characterization and all 17 M3 Python tests pass. Stop on unexplained canonical hash/summary changes.

### Task 4: Implement the native worker JSONL protocol and strict handshake

Files:

- Create tools/ygo_m4_worker/json_protocol.hpp and .cpp.
- Create tools/ygo_m4_worker/main.cpp.
- Modify CMakeLists.txt.
- Create docs/m4/M4_WORKER_PROTOCOL_V1.md and tests/m4/test_worker_protocol.py.

- [ ] Step 1: Write pure protocol tests.

Test matching ready, wrong protocol version, wrong bundle ID, wrong patchset SHA, wrong ordered deck hashes, result job-ID mismatch, valid failed result, nonzero integrity counter, and malformed JSON without a recoverable job ID.

Use exact constants:

    EXPECTED_PROTOCOL = "ocgforge.m4.worker.v1"
    EXPECTED_RULES_BUNDLE = "3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f"
    EXPECTED_PATCHSET = "6b5421b3a852085f48fa161a5ba1540f902aa00784a337694b21c9efc34f69bd"
    EXPECTED_DECK_HASHES = [
        "8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7",
        "6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188",
    ]

- [ ] Step 2: Run pure protocol tests and verify the expected import failure.

    python -m unittest tests/m4/test_worker_protocol.py -v

- [ ] Step 3: Implement strict scalar JSON parsing and deterministic serialization.

The C++ parser accepts one UTF-8 object per line with only the scalar types needed by requests. Reject duplicate keys, trailing non-whitespace, missing required fields, negative unsigned values, and wrong schema/version. A std::map may hold parsed fields, but map iteration must never affect gameplay or seed derivation.

The ready envelope must contain schema/type, protocol_version, PID, rules_bundle_id, core_patchset_sha256, ordered deck_hashes, format_id, duel_mode_name, duel_flags, compiler_identity, and build_type. The result envelope must contain schema/type, job_id, status, terminal, winner/reason or null, engine_steps, interactive_decisions, semantic_action_count, gameplay_hash, trace_hash or null, simulation_elapsed_us, coordinator_elapsed_us as null from the worker, errors, timing_us, counters, and worker metadata.

Flush std::cout after each JSON line. Write diagnostics only to std::cerr.

- [ ] Step 4: Implement startup handshake and canonical validation.

Load both locked decks and derive sorted unique required script codes once as immutable worker values. Build canonical identity from ygo::m3::canonical_rules and current compile-time paths. Emit exactly one ready line before reading stdin. The coordinator must validate exact protocol version, bundle ID, patchset SHA, ordered deck hashes, format, mode, and flags before dispatching any job.

A request whose canonical_rules_id differs from the worker config is a recoverable failed job, not a silent substitution.

- [ ] Step 5: Implement the persistent loop.

For every valid-job-ID line, emit exactly one result even when validation or simulation fails. Failed results have terminal false, null winner/reason/hashes, stable failure code/message, and the relevant nonzero error counter. The worker continues after recoverable job failures. Abnormal process exits are not converted to PASS.

- [ ] Step 6: Build and smoke test the worker.

    cmake --preset dev-windows-zig
    cmake --build --preset dev-windows-zig --parallel
    python -m unittest tests/m4/test_worker_protocol.py -v
    "{}" | .\build\windows-zig\ygo_m4_worker.exe

Expected: pure tests pass and the worker emits only one ready JSON line before EOF.

### Task 5: Implement the persistent coordinator and stderr safety

Files:

- Create tools/m4/worker_protocol.py, process_metrics.py, benchmark.py.
- Create tests/m4/test_failure_isolation.py and fake_worker_crash.py.

- [ ] Step 1: Implement Python handshake/result validation.

Add validate_ready(message, expected), validate_result(message, job_id), and assert_primary_integrity(result). Validate schema/type, all handshake identities, job identity, required numeric values, hash shape, error-counter keys, and terminal status. A primary row rejects any nonzero retries, unsupported, automatic, truncated, core_errors, worker_errors, handshake, malformed-protocol, or failed-game count.

- [ ] Step 2: Launch workers with safe stderr redirection.

Use a dedicated file, never an unread stderr pipe:

    stderr_path = output_dir / f"worker-{worker_index:03d}.stderr.log"
    stderr_file = stderr_path.open("w", encoding="utf-8", newline="")
    process = subprocess.Popen(
        [str(worker_executable)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=stderr_file,
        text=True,
        encoding="utf-8",
        errors="replace",
        bufsize=1,
    )
    stderr_file.close()

Record file path and final byte count. Start one coordinator-side stdout reader thread per worker and pass ready/result/malformed/eof events through queue.Queue. These are coordinator I/O helpers only; workers remain single-threaded.

- [ ] Step 3: Implement dispatch, timing, canonical publication, and failures.

Keep one in-flight job per worker. After handshake validation, dispatch up to workers jobs. Store dispatch perf_counter_ns and compute coordinator_elapsed_us at result receipt. Assign the next job only after validating the previous result. Sort final results by numeric job index from job_id.

Publish a failed simulation result under its original ID without retry. On malformed output, EOF, or abnormal exit, synthesize a failed worker result for the in-flight job with worker_errors 1 and record worker ID/exit code. A replacement worker may process only remaining unassigned jobs; never requeue the in-flight job. Record worker_restarts and leave retries visible.

- [ ] Step 4: Add lightweight Windows memory sampling.

Use ctypes OpenProcess/GetProcessMemoryInfo/CloseHandle in process_metrics.py. Sample live workers every 50 ms from one coordinator monitor thread and track maximum observed working set. Return NOT_MEASURED on unsupported platforms/API failure. Report coordinator plus live workers as process count. Do not estimate from Python object sizes.

- [ ] Step 5: Add fake crash worker and failure tests.

fake_worker_crash.py emits a matching ready line, reads one job line, and exits code 17 without a result. Inject this command only in the coordinator test. Assert original job ID, worker_errors 1, worker_crashes 1, no successful retry, and valid neighboring jobs unaffected.

- [ ] Step 6: Run the coordinator tests.

    python -m unittest tests/m4/test_failure_isolation.py -v
    python -m unittest tests/m4/test_worker_protocol.py -v

### Task 6: Add benchmark CLI, schema, and report aggregation

Files:

- Modify tools/m4/benchmark.py.
- Create tools/m4/report.py and docs/m4/m4_benchmark_schema.json.
- Create docs/m4/M4_ARCHITECTURE.md.
- Create tests/m4/test_benchmark_integrity.py.

- [ ] Step 1: Define the versioned schema.

Use top-level schema_version ocgforge.m4.throughput_benchmark.v1, canonical_environment, hardware, build, warmup_policy, mode, games_requested, workers_requested, cold_start, steady_state, and jobs. steady_state must include games_requested, games_completed, terminal_games, failed_games, wall_clock_seconds, games_per_second, engine_steps_total, engine_steps_per_second, interactive_decisions_total, interactive_decisions_per_second, semantic_actions_total, simulation_elapsed_us percentiles, coordinator_elapsed_us percentiles, memory, errors, timing_buckets_us, and operation_counters.

Require rules bundle, patchset, deck hashes, flags, compiler/build/platform, exact sample counts, partitions, and NOT_MEASURED values where unavailable. Do not leave required fields absent.

- [ ] Step 2: Implement the exact benchmark CLI and warmup policy.

Support:

    --worker-executable PATH
    --games N
    --workers N
    --master-seed N
    --mode conformance|throughput
    --warmup-games N
    --output PATH
    --starting-player-mode balanced
    --seat-mode balanced
    --instrument
    --observation-mode full|off-diagnostic
    --trace-persistence on|off

Generate warmup indices [0, warmup_games) and steady-state indices [warmup_games, warmup_games + games), with the same master seed for every row. Measure cold process/ready time separately. Start steady-state wall timer after warmup completion and stop after the final result is published.

- [ ] Step 3: Implement metric aggregation.

Use:

    games_per_second = games_completed / wall_clock_seconds
    engine_steps_per_second = engine_steps_total / wall_clock_seconds
    decisions_per_second = interactive_decisions_total / wall_clock_seconds
    speedup = games_per_second / worker_1_games_per_second
    parallel_efficiency = speedup / workers

Calculate percentiles from sorted worker simulation_elapsed_us only. Use index min(n - 1, ceil(q * n) - 1) for q 0.50, 0.95, and 0.99. Aggregate coordinator percentiles separately. Sum native timing/counter buckets across jobs and report coordinator IPC/other separately. Do not infer native buckets from unrelated wall-clock subtraction.

- [ ] Step 4: Implement integrity validation tests.

Reject one nonzero error counter at a time, missing/duplicate/unexpected job IDs, mismatched equivalence hashes, failed handshake, malformed protocol, failed game, and nonterminal game. Accept only complete terminal rows with all required integrity counters zero.

- [ ] Step 5: Write M4_ARCHITECTURE.md.

Document CoreHost/OCG_Duel/Lua/query-buffer ownership, process isolation rationale, fresh-duel lifetime, ready validation, stderr redirection, simulation_elapsed_us and coordinator_elapsed_us domains, and full in-memory trace/semantic hash retention in THROUGHPUT. Do not claim thread safety or disabled trace construction.

### Task 7: Add native integration and determinism tests

Files:

- Create tests/m4/test_worker_integration.py.
- Modify CMakeLists.txt to register the test with $<TARGET_FILE:ygo_m4_worker>.

- [ ] Step 1: Test native handshake and valid/invalid/valid sequence.

Launch the native worker, validate ready/PID, send one canonical full-observation throughput job, assert terminal PASS, nonempty gameplay hash, positive simulation_elapsed_us, and zero integrity errors. Send max_steps 0, assert one explicit FAIL with the same job ID, then send another valid job and assert PASS.

- [ ] Step 2: Test mode equivalence.

Run the same four deterministic jobs in CONFORMANCE and THROUGHPUT. Compare job ID, terminal, winner, reason, engine steps, decisions, semantic actions, gameplay hash, and error counters. Require a conformance trace hash; require no throughput trace hash when persistence is off. Do not compare timing.

- [ ] Step 3: Test worker counts 1/2/4/8.

Run the same eight-job set through the coordinator at 1, 2, 4, and 8 workers. Sort by job ID and compare terminal, winner, reason, gameplay hash, semantic action count, and error counters. Repeat with conformance trace persistence and compare trace hashes. Any mismatch is a nonzero test result.

- [ ] Step 4: Run the integration gate.

    cmake --preset dev-windows-zig
    cmake --build --preset dev-windows-zig --parallel
    python -m unittest tests/m4/test_worker_integration.py -v
    ctest --preset dev-windows-zig -R m4_worker_integration --output-on-failure

### Task 8: Re-run all pre-benchmark correctness gates

Files:

- No new source files. Inspect all M4/CMake/native changes.

- [ ] Step 1: Verify bundle and patchset.

    python tools/verify_rules_bundle.py
    python tools/verify_ocgcore_patchset.py

Confirm exact expected bundle, patchset SHA, deck hashes, flags, format, and worker handshake.

- [ ] Step 2: Build.

    cmake --preset dev-windows-zig
    cmake --build --preset dev-windows-zig --parallel

Expected: ygo_core_probe.exe and ygo_m4_worker.exe exist under build/windows-zig.

- [ ] Step 3: Run all CTests and Python suites.

    ctest --preset dev-windows-zig --output-on-failure
    python -m unittest discover -s tests/python -v
    python -m unittest discover -s tests/m3 -v
    python -m unittest discover -s tests/m4 -v

Report the exact new CTest numerator/denominator after M4 tests are registered. Existing tests must remain green; M3 must remain 17/17.

- [ ] Step 4: Run the canonical 16-game regression.

    python tests/m3/full_game/full_fixed_deck_test.py --probe .\build\windows-zig\ygo_core_probe.exe --games 16 --max-steps 2200 --timeout 240 --output artifacts/m3/canonical_mr5/full_games_m4

Expected: 16/16 terminal, both starting players, normal/mirror seats, zero unsupported/retry/automatic/truncation/core errors, privacy PASS, candidate/observation PASS.

### Task 9: Collect the worker matrix

Files:

- Generate artifacts/m4/matrix/*.json and artifacts/m4/matrix/*.stderr.log.

- [ ] Step 1: Run the throughput smoke sample.

    python tools/m4/benchmark.py --worker-executable .\build\windows-zig\ygo_m4_worker.exe --games 8 --workers 2 --master-seed 20260815 --mode throughput --warmup-games 2 --starting-player-mode balanced --seat-mode balanced --instrument --output artifacts/m4/smoke-throughput.json

Require eight terminal steady-state games, zero integrity errors, sorted job IDs, valid handshake, positive simulation_elapsed_us, and machine-readable output.

- [ ] Step 2: Run 64-game rows at 1/2/4/8/16/32 workers.

Use the same seed 20260815, games 64, warmup 4, mode throughput, full observations, balanced partitions, instrument on, and build for every row. Write throughput-w1.json through throughput-w32.json. Do not change seed/sample/warmup/policy/decks/flags between rows.

A row with any failed game, nonterminal game, error counter, worker crash/restart, handshake mismatch, malformed output, missing/duplicate result, or semantic mismatch is invalid and cannot appear as a valid throughput result.

- [ ] Step 3: Gate 64/128 workers.

Inspect the 32-worker observed working-set total, process count, stderr bytes, and failures. Run 64 only if the machine is responsive, no worker failed, and observed worker working set is below 70% of physical memory. Run 128 only if the 64 row also satisfies those conditions and remains useful. Otherwise record NOT_RUN and the reason; never spawn 256.

- [ ] Step 4: Re-run the exact 1/2/4/8 semantic gate before accepting scaling.

If any per-job result differs, stop with M4 BLOCKED — PARALLEL DETERMINISM FAILURE and do not write a passing baseline.

### Task 10: Run separate trace and observation experiments

Files:

- Generate artifacts/m4/experiments/*.json.

- [ ] Step 1: Compare conformance/full trace and throughput/no persistence.

Run the same 16 jobs, one worker, two warmup jobs, seed 20260815:

    python tools/m4/benchmark.py --worker-executable .\build\windows-zig\ygo_m4_worker.exe --games 16 --workers 1 --master-seed 20260815 --mode conformance --warmup-games 2 --starting-player-mode balanced --seat-mode balanced --instrument --trace-persistence on --output artifacts/m4/experiments/conformance.json
    python tools/m4/benchmark.py --worker-executable .\build\windows-zig\ygo_m4_worker.exe --games 16 --workers 1 --master-seed 20260815 --mode throughput --warmup-games 2 --starting-player-mode balanced --seat-mode balanced --instrument --trace-persistence off --output artifacts/m4/experiments/throughput-no-persistence.json

Compare only valid rows. Report games/s, worker-local simulation elapsed, trace/hash/serialization buckets, and semantic equality. State that both modes retain full in-memory trace construction.

- [ ] Step 2: Compare full observations and diagnostic observation-off.

Run the same sample with throughput mode once with observation-mode full and once with observation-mode off-diagnostic. The latter must contain literal label DIAGNOSTIC ONLY — NOT TRAINING THROUGHPUT. Compare gameplay hashes, terminal results, engine steps, decisions, and worker-local elapsed. Exclude its games/s from the primary matrix.

- [ ] Step 3: Reject divergence.

Any semantic gameplay hash difference makes the experiment invalid and requires fixing the mode boundary before more measurements. Do not change policy, candidate sets, traces, or seeds to make it pass.

### Task 11: Generate the baseline handoff

Files:

- Modify tools/m4/report.py.
- Generate docs/m4/M4_BASELINE.md and docs/m4/m4_baseline.json.

- [ ] Step 1: Refuse invalid/missing rows.

report.py must refuse a passing handoff when a required matrix row is missing, invalid, identity-mismatched, nonterminal, or has any integrity error. Record skipped 64/128 rows as NOT_RUN with reason.

- [ ] Step 2: Write the scaling table.

Include workers, games, wall time, games/s, engine steps/s, decisions/s, speedup, parallel efficiency, mean/p50/p95/p99 simulation_elapsed_us, observed memory, and every error counter. Speedup is relative to the valid one-worker row; efficiency is speedup/workers. State saturation only from measured games/s; do not name an unmeasured cause.

- [ ] Step 3: Write timing and counter evidence.

Include measured percentages for core_process, protocol_candidate, continuation, observation, trace_hash, serialization, native other, and coordinator/IPC other. Include OCG_DuelProcess, OCG_DuelQuery, OCG_DuelQueryLocation, OCG_DuelQueryField, observations/entities, candidate sets/total/max, semantic hashes, trace bytes, scripts, errors, memory, and handshake data. List PERFORMANCE AUDIT CANDIDATES only when a measured bucket/counter supports them; do not implement any candidate.

- [ ] Step 4: Write one final status.

Use exactly one:

    M4 BASELINE PASS — PERFORMANCE AUDIT READY
    M4 BASELINE ACCEPTANCE PENDING
    M4 BLOCKED — PARALLEL DETERMINISM FAILURE

Use PASS only after parallel determinism, mode equivalence, failure isolation, handshake identity, integrity, all existing regressions, privacy, and candidate/observation checks are freshly verified. Stop without M4.2 or M5 work.

### Task 12: Final verification and handoff

Files:

- Inspect all M4 source/docs/artifacts and git state.

- [ ] Step 1: Run fresh final verification.

    cmake --preset dev-windows-zig
    cmake --build --preset dev-windows-zig --parallel
    ctest --preset dev-windows-zig --output-on-failure
    python -m unittest discover -s tests/python -v
    python -m unittest discover -s tests/m3 -v
    python -m unittest discover -s tests/m4 -v
    python tests/m3/full_game/full_fixed_deck_test.py --probe .\build\windows-zig\ygo_core_probe.exe --games 16 --max-steps 2200 --timeout 240 --output artifacts/m3/canonical_mr5/full_games_m4_final

Read every exit code and count every test. Do not report a partial or earlier run as final evidence.

- [ ] Step 2: Verify scope and identity.

    git status --short --branch
    git diff --stat
    git diff --check

Confirm no CardScripts, BabelCDB, locked deck, rules-lock, derived ocgcore, or unrelated source change. Report starting/final branch and HEAD, every added/modified file, and generated artifacts. Do not reset, discard, or silently remove user work.

- [ ] Step 3: Stop.

Do not optimize hot paths, add threads, pool duel state, enable PGO/LTO, modify ocgcore, or begin M5 after the baseline handoff. The next action is a separately reviewed performance audit.

## Plan self-review

- Determinism is covered by Tasks 1, 7, 9.
- Process isolation, handshake identity, stderr safety, and abnormal exits are covered by Tasks 3–5 and 7.
- Worker-local latency, coordinator latency, wall-clock throughput, memory, timing buckets, and operation counters are covered by Tasks 1–2, 3, 5–6, and 9.
- CONFORMANCE/THROUGHPUT and diagnostic observation-off semantics are covered by Tasks 3, 7, and 10.
- Integrity and all M0–M3.5 regression gates are covered by Tasks 6, 8, and 11–12.
- No task changes canonical rule inputs or adds M4.2/M5 behavior.
- Placeholder review: every implementation step names its files, behavior, command, and expected verification.
- The names SimulationJob, SimulationResult, TimingBuckets, OperationCounters, CanonicalSimulationConfig, simulation_elapsed_us, and coordinator_elapsed_us are consistent across native code, protocol, coordinator, tests, and reports.
