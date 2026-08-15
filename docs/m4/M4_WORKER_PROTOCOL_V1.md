# OCGForge M4 Worker Protocol v1

Status: Task 4 native worker contract

Protocol schema and version:

`ocgforge.m4.worker.v1`

The worker is a persistent native process. It initializes the canonical
configuration once, emits one `ready` line, then consumes newline-delimited
JSON job requests sequentially. It creates a fresh `CoreHost` and duel inside
each call to `run_canonical_simulation`; no duel-local mutable state crosses a
job boundary.

## Streams and lifecycle

- stdout contains only one complete JSON object per line.
- Every response line is flushed before the worker reads the next request.
- stderr is diagnostic-only. It must be redirected or continuously drained by
  the future coordinator so diagnostic output cannot block the worker.
- A successful startup emits exactly one `ready` envelope before reading
  stdin.
- A startup failure emits no `ready` envelope and exits nonzero. The
  coordinator must classify this as an abnormal worker exit.
- EOF after startup is a normal exit. The worker never retries a job.

The worker identity, PID, scheduling order, and wall-clock time do not enter
seed derivation or gameplay.

## Canonical handshake

The first line is an object with these keys in this deterministic order:

```json
{"schema":"ocgforge.m4.worker.v1","type":"ready","protocol_version":"ocgforge.m4.worker.v1","pid":1234,"rules_bundle_id":"3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f","core_patchset_sha256":"6b5421b3a852085f48fa161a5ba1540f902aa00784a337694b21c9efc34f69bd","deck_hashes":["8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7","6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188"],"format_id":"TCG_ADVANCED_2026_05_18","duel_mode_name":"DUEL_MODE_MR5","duel_flags":190464,"compiler_identity":"Clang-19.1.7","build_type":"Debug","worker_identity":"ocgforge.m4.native_worker.v1"}
```

The coordinator must validate all identity fields before sending any job:

| Field | Required value |
| --- | --- |
| `schema` | `ocgforge.m4.worker.v1` |
| `type` | `ready` |
| `protocol_version` | `ocgforge.m4.worker.v1` |
| `rules_bundle_id` | `3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f` |
| `core_patchset_sha256` | `6b5421b3a852085f48fa161a5ba1540f902aa00784a337694b21c9efc34f69bd` |
| `deck_hashes` | The ordered pair `[Deck A, Deck B]` below |
| `format_id` | `TCG_ADVANCED_2026_05_18` |
| `duel_mode_name` | `DUEL_MODE_MR5` |
| `duel_flags` | `190464` (`0x2E800`) |

Ordered locked deck hashes:

1. `8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7`
2. `6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188`

`pid`, `compiler_identity`, and `build_type` are worker metadata. The
`worker_identity` value identifies this native implementation.

## Job request

Each request is one JSON object with these fields:

```json
{"schema":"ocgforge.m4.worker.v1","type":"job","job_id":"m4-000001","seed":123,"seat_assignment":"normal","starting_player":0,"max_steps":2200,"canonical_rules_id":"3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f","mode":"throughput","observation_mode":"full","instrumentation":false,"persist_trace":false,"replay_actions":[],"focus_codes":[],"setup_script":"","force_unsupported":false,"trace_output":""}
```

All fields except `trace_output` are required. `trace_output` is optional and
defaults to an empty path. The value mapping is:

- `seat_assignment`: `normal` or `mirror`.
- `starting_player`: unsigned `0` or `1`.
- `mode`: `conformance` or `throughput`.
- `observation_mode`: `full` or `off_diagnostic`.
- `seed`, `max_steps`, and every `focus_codes` element are unsigned integers.
- `replay_actions` is an array of strings; `focus_codes` is an array of
  unsigned 32-bit integers.

The request parser rejects unknown fields, duplicate keys, trailing
non-whitespace, missing fields, wrong scalar types, negative integers,
non-integer numbers, invalid UTF-8 strings, and invalid enum values. JSON
object ordering is not used for seed derivation or gameplay.

`canonical_rules_id` is parsed as a value, then checked against the worker's
canonical configuration by `run_canonical_simulation`. A mismatch produces a
failed result for that job ID; the worker never substitutes another identity.

## Result response

Every fully parsed request with a non-empty job ID receives exactly one
`result` line, including failed requests and failed simulations. The worker
sets `coordinator_elapsed_us` to JSON `null`; the coordinator owns that timing
domain.

The result envelope contains, in deterministic order:

```json
{"schema":"ocgforge.m4.worker.v1","type":"result","status":"passed","job_id":"m4-000001","terminal":true,"winner":0,"win_reason":1,"engine_steps":100,"interactive_decisions":20,"semantic_action_count":20,"gameplay_hash":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","trace_hash":null,"simulation_elapsed_us":123456,"coordinator_elapsed_us":null,"errors":{"retries":0,"unsupported":0,"automatic":0,"truncated":0,"core_errors":0,"worker_errors":0},"timing_us":{"core_process":1,"protocol_candidate":2,"continuation":0,"observation":3,"trace_hash":4,"serialization":0,"other":5,"trace_persistence":0},"counters":{"ocg_duel_process":100,"ocg_duel_query":0,"ocg_duel_query_location":0,"ocg_duel_query_field":0,"ocg_duel_query_count":0,"script_reader_requests":1,"script_loads":1,"observations":20,"entities_projected":0,"candidate_sets":20,"candidate_total":40,"candidate_max":4,"semantic_hashes":1,"trace_bytes_serialized":0},"worker":{"pid":1234,"restart_index":0,"crashed":false,"restarted":false},"failure_code":null,"error_message":null}
```

For `status: "failed"`, `failure_code` and `error_message` are non-empty,
`terminal` is false, winner/reason are null, and gameplay/trace hashes are
null. A failed request caused by worker-side validation increments
`errors.worker_errors`. A simulation failure preserves the relevant native
error counter. No failure is converted into `passed`.

`simulation_elapsed_us` is worker-local and covers fresh CoreHost creation,
the native simulation, result metrics, and CoreHost destruction. It excludes
the worker's JSON serialization and pipe flush. `timing_us` contains the
coarse opt-in simulation buckets and `trace_persistence` separately.

## Trace semantics by mode

Both modes use the same native gameplay loop, policy, candidate construction,
continuations, observations, and full in-memory `EngineTrace` construction.

- `conformance` computes the canonical trace hash and may persist the full
  JSONL trace when `persist_trace` and `trace_output` are set.
- `throughput` still computes the semantic gameplay hash and retains the full
  in-memory trace records. It suppresses only canonical full-trace
  serialization/persistence unless explicitly requested by the job. A null
  `trace_hash` in throughput is therefore not evidence that trace records were
  omitted.

The worker protocol itself does not compare modes; the later coordinator must
run the explicit semantic-equivalence gate before accepting throughput data.

## Protocol errors and malformed input

If a line is malformed or has no recoverable job ID, the worker emits a
`protocol_error` envelope rather than a fake result:

```json
{"schema":"ocgforge.m4.worker.v1","type":"protocol_error","job_id":null,"failure_code":"malformed_request","error_message":"..."}
```

If a malformed line contains a recoverable non-empty `job_id`, the worker
emits a failed `result` for that ID. This preserves one-result-per-job
accountability while keeping syntax errors distinguishable from normal
simulation results.
