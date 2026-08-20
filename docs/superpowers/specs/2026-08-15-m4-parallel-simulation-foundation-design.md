# M4.0/M4.1 Parallel Simulation Foundation Design

Status: approved for implementation on 2026-08-15.

## Goal

Establish a correctness-first parallel simulation foundation and produce the
first trustworthy throughput baseline for the locked Swordsoul Tenyi ML v1 vs
Salamangreat ML v1 matchup. This milestone measures the existing deterministic
simulation path; it does not optimize ocgcore, Lua, serialization, or memory
allocation.

## Scope boundaries

M4 uses the existing canonical environment without changing its rules bundle,
ocgcore base or patchset, CardScripts, BabelCDB, locked decks, duel flags,
policy, legal candidate sets, continuation semantics, or PlayerObservation
privacy rules. It adds only the process orchestration, simulation contracts,
coarse opt-in measurements, tests, benchmark tooling, and baseline documents
needed for M4.0/M4.1.

No teacher bot, learning model, RL loop, self-play, new deck, new mechanic,
approximate observation, candidate truncation, ocgcore performance patch,
thread pool, CoreHost pool, mutable-engine cache, shared-memory transport,
binary IPC, allocator change, PGO/LTO experiment, or serialization rewrite is
part of this design.

## Architecture audit

The inspected implementation gives each `CoreHost` a private `OCG_Duel`.
`OCG_CreateDuel` constructs a duel-local RNG, field, card/effect/group sets,
message buffer, query buffer, card-data cache, and Lua interpreter;
`OCG_DestroyDuel` releases them. The Lua interpreter creates and closes its own
`lua_State`. The native source contains static helper functions and constant
tables, but no identified mutable process-global gameplay registry. The core
does not provide an explicit thread-safety guarantee, so this evidence does
not authorize concurrent use of independent duels in one process.

The following ownership rules are authoritative for M4:

| State or object | Classification | M4 rule |
| --- | --- | --- |
| Rules identity, immutable CardScripts/BabelCDB files, locked deck definitions, canonical policy constants | `IMMUTABLE_SHARED` | Workers may reuse read-only paths and startup-loaded deck values. |
| `CoreHost`, `OCG_Duel`, duel RNG, field, cards, effects, groups, Lua state, card-data cache, engine buffers | `PER_DUEL` / `UNSAFE_TO_SHARE` | Construct a fresh instance for every simulation job. Never send pointers or references through the worker protocol. |
| `CardDataStore`, `ScriptStore`, callback error/log strings and callback payloads | `PER_ENVIRONMENT` | Keep private to the fresh `CoreHost`; count reads/loads where observable. |
| `ObservationSession`, observation scratch structures, decoded messages, candidate vectors, continuation state, trace, hash state | `PER_DECISION` or `PER_ENVIRONMENT` | Allocate within the job and never share between jobs. |
| Static helper functions, compile-time constants and read-only function tables | `IMMUTABLE_SHARED` | No synchronization or cache redesign is added. |
| Coordinator queues, worker pipes, result publication state | `PROCESS_GLOBAL` to the coordinator | Protect only with the coordinator's existing ownership and I/O structure; job order never determines gameplay. |
| Explicit OCGForge thread-local state | `THREAD_LOCAL` | None is introduced in M4. Workers do not create simulation threads. |

Query methods are stateful from the caller's perspective because ocgcore
reuses duel-local `query_buffer`; process messages similarly use duel-local
`buff`. Observation sessions and traces are mutable. These buffers and all
callback payloads therefore remain inside one job.

## Isolation model

M4 uses one coordinator and a persistent pool of worker processes. A worker
initializes once, accepts multiple jobs sequentially, creates one fresh
`CoreHost`/`OCG_Duel` for each job, destroys it before accepting the next job,
and writes one result for every accepted job. Workers contain no simulation
threads and do not pool or reuse duel objects.

The process boundary is the v1 safety boundary. It isolates any unproven
process-global behavior in ocgcore/Lua and makes abnormal exits observable.
The coordinator shares only immutable configuration that the current
architecture already represents as file paths or value objects. It does not
change CoreHost to manufacture shared card-data or script caches. If each fresh
duel reloads card data or scripts, the worker reports that cost.

## Components

### Shared simulation contract

The native simulation implementation is extracted behind value-based types:

```text
SimulationJob
  job_id
  seed
  seat_assignment       normal | mirror
  starting_player       0 | 1
  max_steps
  canonical_rules_id
  mode                  conformance | throughput
  observation_mode      full | off_diagnostic

SimulationResult
  job_id
  status                PASS | FAIL
  terminal
  winner
  win_reason
  engine_steps
  interactive_decisions
  semantic_action_count
  gameplay_hash
  trace_hash            optional when full trace is disabled
  elapsed_time
  simulation_elapsed_us  worker-local per-job timing domain
  coordinator_elapsed_us coordinator send-to-result timing, reported separately
  error counters
  timing buckets
  operation counters
  worker metadata
```

Both CONFORMANCE and THROUGHPUT call this same native simulation function.
Only evidence collection and persistence differ. The existing M3 probe is
adapted to use the shared path so M4 does not create a second gameplay driver.

### Versioned worker protocol

Workers use line-delimited UTF-8 JSON on standard input and output. Standard
output contains only protocol JSON; diagnostics go to standard error. Every
accepted job receives exactly one flushed result line. A startup `ready`
message and a final `stopped` message are not jobs and carry the same protocol
schema family.

Request envelope:

```json
{
  "schema": "ocgforge.m4.worker_request.v1",
  "type": "job",
  "job_id": "m4-000000",
  "seed": 123,
  "seat_assignment": "normal",
  "starting_player": 0,
  "max_steps": 2200,
  "canonical_rules_id": "3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f",
  "mode": "throughput",
  "observation_mode": "full",
  "instrumentation": true,
  "trace_output": null
}
```

Before accepting any job, the worker emits a `ready` envelope containing the
exact `ocgforge.m4.worker.v1` protocol version, canonical rules bundle ID,
ocgcore patchset SHA-256, and both locked deck hashes. The coordinator validates
all of these values against the repository lock and refuses to dispatch jobs to
any worker whose handshake differs. A worker that has not completed this
handshake never accepts a job. The handshake also reports the worker PID and
build identity for benchmark metadata.

Result envelopes retain the job identity and include a status even when the
simulation fails:

```json
{
  "schema": "ocgforge.m4.worker_result.v1",
  "type": "result",
  "job_id": "m4-000000",
  "status": "PASS",
  "terminal": true,
  "winner": 0,
  "win_reason": 1,
  "engine_steps": 612,
  "interactive_decisions": 180,
  "semantic_action_count": 180,
  "gameplay_hash": "...",
  "trace_hash": "...",
  "errors": {
    "retries": 0,
    "unsupported": 0,
    "automatic": 0,
    "truncated": 0,
    "core_errors": 0,
    "worker_errors": 0
  },
  "timing_us": {},
  "counters": {},
  "worker": {
    "pid": 0,
    "restart_index": 0
  }
}
```

The implementation fills all required fields and uses explicit `null` or
`NOT_MEASURED` values where a field is inapplicable. A syntactically valid job
with invalid fields produces a failed result when a valid `job_id` is present.
A line that cannot be parsed and has no recoverable job identity produces a
protocol error, invalidates the benchmark, and is never counted as a successful
game.

### Coordinator

The coordinator generates the complete job set before starting workers,
launches the requested number of worker processes, assigns jobs through pipes,
reads one result per accepted job, and publishes results sorted by `job_id`.
Worker assignment and completion order do not enter seed derivation or result
identity.

The coordinator redirects each worker's standard error to a dedicated
per-worker diagnostic file at process creation, or drains it continuously when
an in-memory diagnostic stream is explicitly requested. It never leaves a
worker stderr pipe unread. Throughput workers emit no verbose diagnostics by
default; conformance diagnostics remain available in the redirected files.
The stderr path and byte count are recorded separately from machine-readable
results so diagnostic I/O cannot fill a pipe and block a worker.

If a worker exits abnormally, the coordinator marks its in-flight job as a
failed worker result, records the abnormal exit, and may start one replacement
worker for remaining jobs. It does not silently retry the in-flight game or
convert a replacement result into the original successful sample. Restart and
retry counts remain visible in the report and make a benchmark invalid unless
the requested integrity policy explicitly permits them.

## Deterministic job generation

The coordinator derives every job from `(master_seed, job_index)` only. The
stable mapping is:

```text
job_id          = m4-<zero-padded decimal job_index>
job_seed        = SplitMix64(master_seed + job_index * 0x9E3779B97F4A7C15)
partition       = job_index modulo 4
  0             normal seats, starting player 0
  1             mirror seats, starting player 0
  2             normal seats, starting player 1
  3             mirror seats, starting player 1
```

The 64-bit arithmetic is modulo `2^64` and is implemented identically in the
coordinator and deterministic unit tests. No worker count, PID, scheduling
order, wall clock, OS randomness, or unordered-container iteration contributes
to a job.

## Execution modes and equivalence

CONFORMANCE mode keeps the canonical trace records, observation hashes,
candidate/observation consistency checks, semantic gameplay hash, full trace
hash, and optional per-job trace persistence. THROUGHPUT mode uses the same
rules, policy, candidate construction, continuation application,
PlayerObservation construction, terminal handling, and semantic gameplay hash.
For the initial M4 measurement, THROUGHPUT also constructs the same full
in-memory `EngineTrace` records and runs the same semantic gameplay hash over
those records. This work remains in the `trace_hash` timing bucket and is
reported honestly. THROUGHPUT suppresses only canonical full-trace
serialization, trace-file persistence, and verbose diagnostics. It does not
claim that trace construction or semantic hashing is disabled. A minimal
semantic accumulator is deliberately deferred because it would be a new
measurement-affecting implementation rather than a necessary v1 isolation
boundary.

For a fixed representative job set, the equivalence gate compares, per job:
`job_id`, terminal state, winner, win reason, semantic action count, gameplay
hash, error counters, and trace hash when tracing is enabled. It also compares
the ordered semantic action sequence or its canonical semantic hash. Any
difference blocks M4 acceptance.

An `off_diagnostic` observation mode runs the same engine/protocol/policy path
without constructing PlayerObservation. Its output is labeled
`DIAGNOSTIC ONLY — NOT TRAINING THROUGHPUT` and is used only for the observation
cost comparison. Its games per second is excluded from the primary throughput
matrix.

## Instrumentation and counters

Instrumentation is opt-in and coarse. The native result records elapsed time in
these buckets when enabled:

- `core_process`: `OCG_DuelProcess` and message acquisition;
- `protocol_candidate`: message decode, validation, candidate accounting and
  policy selection;
- `continuation`: continuation transition construction;
- `observation`: PlayerObservation construction and candidate consistency
  checks;
- `trace_hash`: semantic/full trace hashing and trace record work;
- `serialization`: canonical full-trace serialization;
- `other`: setup, teardown, and unassigned native work.

`CoreHost` exposes diagnostic counters for `OCG_DuelProcess`,
`OCG_DuelQuery`, `OCG_DuelQueryLocation`, and `OCG_DuelQueryField`. The
simulation adds observation count, candidate-set count, total candidates, and
maximum candidates. Script-reader requests/file loads are counted when
observable; no cache is added to make those counts smaller.

The coordinator separately measures wall time, pipe/JSON coordination time,
worker process count, and observed worker memory. A metric that cannot be
measured reliably is emitted as `NOT_MEASURED`.

## Verification gates

The implementation must add focused tests for:

1. deterministic job generation and balanced seat/start partitions;
2. worker JSONL framing, one-result-per-accepted-job, flush behavior, and
   canonical result ordering;
3. CONFORMANCE/THROUGHPUT semantic equivalence on representative canonical
   jobs;
4. worker-count equivalence at 1, 2, 4, and 8 workers;
5. one invalid job alongside valid jobs, with explicit failure and no valid-job
   corruption;
6. abnormal worker exit detection, explicit worker-error accounting, and
   non-silent restart behavior;
7. benchmark integrity rejection for nonzero retry, unsupported, automatic,
   truncation, core-error, or worker-error counts.

Before benchmark numbers are trusted, the existing build, all 85 CTests,
repository Python tests, M3 tests, M3.5 tests, privacy checks,
candidate/observation checks, and canonical full-game regression must remain
green. The canonical mechanics inventory remains 38 `ENGINE_VERIFIED`, 7
`PROTOCOL_VERIFIED`, 0 `PUBLIC_API_LIMITATION`, and 0 `PENDING`.

## Benchmark and handoff

The benchmark runner is a dedicated tool, not a unit test. It accepts:

```text
--games N
--workers N
--master-seed N
--mode conformance|throughput
--warmup-games N
--output PATH
--starting-player-mode balanced
--seat-mode balanced
--instrument
```

Its versioned output schema is `ocgforge.m4.throughput_benchmark.v1`. The
primary workload is the locked matchup with both seat assignments and both
starting players. Warmup policy is explicit: cold startup is measured
separately, and steady-state samples begin only after the requested warmup jobs
complete. Warmup jobs are never silently folded into steady-state counts.

Every worker starts `simulation_elapsed_us` immediately before it enters the
native job function and stops it after the fresh `CoreHost` has been destroyed
and the native result has been assembled, before worker JSONL serialization and
pipe flush. This domain includes per-job CoreHost/card-data/script setup,
ocgcore processing, protocol/candidate work, observations, trace/hash work,
native result assembly, and teardown. It excludes coordinator queueing, pipe
wait time, worker JSON serialization, and result flush time. The primary
per-game mean/p50/p95/p99 latency values use this worker-local
`simulation_elapsed_us` domain and only steady-state completed games.

The coordinator separately records `coordinator_elapsed_us` from job dispatch
through result receipt, including coordinator queueing, pipe transport, worker
JSONL serialization, and flush. It never substitutes this value for the
primary per-game latency percentiles. Total steady-state `wall_clock_seconds`
starts after warmup and ends after the last steady-state result is published;
games per second is `games_completed / wall_clock_seconds`, so it includes all
steady-state coordinator and IPC cost.

The baseline matrix is 1, 2, 4, 8, 16, and 32 workers. Counts of 64 and 128
are attempted only when observed resource usage remains safe and the lower
matrix remains useful. Every row reports requested/completed/terminal/failed
games, wall time, games per second, engine steps per second, interactive
decisions per second, semantic action totals, mean/p50/p95/p99 game duration,
memory or `NOT_MEASURED`, worker/error counters, canonical identities, build
metadata, timing buckets, operation counters, and speedup/parallel efficiency
relative to the one-worker row.

Two additional representative experiments are reported separately:

- full CONFORMANCE tracing versus THROUGHPUT without full trace persistence;
- full PlayerObservation construction versus the diagnostic observation-off
  path.

Only measured runtime fractions become PERFORMANCE AUDIT CANDIDATES. M4 ends
after `docs/m4/M4_BASELINE.md` and `docs/m4/m4_baseline.json` are written and
the final correctness/integrity gates pass. The intended status is
`M4 BASELINE PASS — PERFORMANCE AUDIT READY`; otherwise the report uses the
specified pending or blocked status and stops without optimization work.

No commit, push, tag, PR, dependency update, CardScripts change, BabelCDB
change, locked-deck change, or ocgcore gameplay/performance change is part of
this design.
