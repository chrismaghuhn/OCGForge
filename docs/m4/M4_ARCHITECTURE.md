# M4 benchmark architecture

M4 measures persistent native worker processes through a value-only JSONL
boundary. The native worker is authoritative for gameplay and the Python
coordinator is authoritative for process lifecycle, dispatch order, receipt
timing, integrity gates, and report aggregation.

## Ownership and lifetime

Each worker owns one fresh `CoreHost`/`OCG_Duel` environment for a simulation
job. `CoreHost` owns the duel pointer, the script store and its callback
context. The script reader resolves requests through the locked script store;
it does not introduce a process-global cache or shared mutable store.

The canonical simulation path owns its `CoreHost`, both observation sessions,
engine trace records, decoded requests, candidate vectors, continuations and
hash/serialization temporaries inside the call. No duel pointer crosses the
simulation return boundary. A fresh duel lifetime is therefore the unit of
simulation timing and worker jobs cannot reuse stale duel state.

The observation layer owns decoded query values and the public projection.
Query buffers are owned by the response/observation boundary and are copied
into value records before the native callback scope ends. Observation data is
not used to infer private state.

## Process isolation

Workers are separate native processes because the engine and its Lua runtime
are not treated as thread-safe. M4 does not claim thread safety, and it does
not pool duel state across jobs. Workers remain single-threaded; parallelism is
process-level isolation coordinated by one Python process.

The coordinator launches one stdout reader per worker, keeps at most one
in-flight job per worker, validates the ready identity before dispatch, and
assigns a new job only after the previous result has been validated and
published. A crash, malformed line, EOF, failed handshake, or replacement
failure is visible in the result/metadata and cannot become a passing row.

Worker stderr is redirected to a dedicated file per worker and restart. It is
never held in an unread pipe. The report records the path and final byte count
so diagnostics remain available without coupling stderr volume to worker
progress.

## Hosted CI tier versus acceptance characterization

Hosted Windows CI runs the bounded `m4_worker_integration_fast_test` together
with the protocol, failure-isolation, and benchmark-integrity tests. The fast
test covers persistent startup and handshake, one- versus two-worker semantic
equivalence, throughput-versus-conformance semantic equivalence, complete
primary-result validation, zero error counters, and trace-hash equivalence on
a small deterministic corpus. It is a hosted integrity proof, not evidence
for the full scaling matrix.

The larger `m4_worker_integration_test` remains registered with the
`M4_ACCEPTANCE_SCALE` label and is still the local/dedicated acceptance and
characterization path. Hosted CI excludes that label because the full
1/2/4/8-worker throughput and trace workload is not a reliable Debug-runner
budget. Excluding it from hosted CI does not remove or weaken the larger test.

When a pool timeout occurs and `YGO_M4_FAILURE_ARTIFACT_DIR` is set, the
integration test writes a bounded diagnostic JSON containing the test phase,
job IDs, timeout category, elapsed time, worker lifecycle state, return codes,
and stderr tails. These values are failure diagnostics only and are not
semantic or gameplay evidence.

## Canonical identity and determinism

The ready envelope is checked against the pinned protocol, rules bundle,
ocgcore patchset, ordered locked-deck hashes, format, duel mode, duel flags,
worker identity, compiler identity and build type. Job identity is derived
only from the master seed and numeric job index; worker count and scheduling
cannot enter the mapping.

Conformance and throughput use the same canonical simulation records and
semantic gameplay hash. Throughput skips only external full serialization and
trace-file persistence. It still constructs and retains the full in-memory
trace records and computes the semantic hash; this keeps the throughput
semantic boundary comparable without making a claim that trace construction
is disabled.

## Timing and report domains

`simulation_elapsed_us` is worker-local: it spans fresh native simulation
construction through result assembly/destruction as defined by the native
contract. Native timing buckets are summed only from the worker result.

`coordinator_elapsed_us` is coordinator-side: it spans dispatch write/flush to
validated result receipt. It is reported as end-to-end dispatch/result
latency, includes waiting for worker computation, and is not isolated IPC CPU
time. It is never subtracted from wall time to manufacture a native bucket.

Steady-state wall time starts after all warmup results are complete and ends
after the final steady-state result is published. Cold process/ready time is a
separate field. Rates use completed steady-state games divided by this wall
interval. Percentiles use sorted worker-local or coordinator samples with the
specified ceiling index `min(n - 1, ceil(q*n) - 1)` for p50, p95 and p99.

Memory is sampled from the coordinator and live worker processes on Windows.
When the operating-system API cannot provide a complete sample, the report
uses the literal `NOT_MEASURED` rather than estimating from Python objects.
