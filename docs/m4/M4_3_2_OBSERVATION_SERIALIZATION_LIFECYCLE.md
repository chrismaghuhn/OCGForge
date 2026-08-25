# OCGForge M4.3.2 — Observation Serialization Lifecycle

Status: **M4.3.2 CHARACTERIZATION PASS**

This audit characterizes the remaining observation serialization and hashing
lifecycle after M4.3.1. It does not implement an optimization. No streaming
hashing, serializer replacement, cross-observation cache, format change,
privacy change, ocgcore change, CardScripts change, or M5 work was performed.

## Scope and workload

The audit used the canonical deterministic workload:

- Swordsoul Tenyi ML v1 vs Salamangreat ML v1
- master seed `20260815`
- 16 games, one worker, maximum 2200 steps
- FULL observation mode, no trace persistence
- the existing diagnostic observation-off run as a cross-check only
- the audit-instrumented native worker build

The existing M4.2 integrity and canonical identity checks passed for both
samples. The worker-local FULL runtime was `385051844 us`; the observation-off
diagnostic runtime was `34589592 us`. The latter remains **DIAGNOSTIC ONLY —
NOT TRAINING THROUGHPUT** and is not used as a throughput result.

The raw run artifacts were produced under
`C:\yogiohML-m4-3-2\characterization\runs`. Each worker emits a separate
`M4_SERIALIZATION_LIFECYCLE` sidecar so the existing M4.2 report parser and
its performance contract remain unchanged.

## Lifecycle instrumentation

Each `ObservationScope` receives a monotonically increasing diagnostic
`lifecycle_id`. The ID is paired with the worker `job_id`; it resets for a
fresh worker job and is therefore not treated as a global observation identity.
Mutation epochs are advanced by the instrumented observation-context mutation.
Repeated serialization in the same epoch is classified as duplicate
materialization; serialization after a mutation is counted but not classified
as a same-state duplicate.

The sidecar counts independently:

- `serialize_without_hash` calls and total bytes returned;
- SHA-256 calls;
- complete `canonical_serialize` calls and total bytes returned;
- same-mutation-epoch duplicate materializations;
- per-lifecycle records containing the lifecycle ID and mutation count.

## FULL / THROUGHPUT result

Across the 16 FULL jobs:

| Metric | Measured result |
|---|---:|
| observations / serialization lifecycles | 9,908 |
| `serialize_without_hash` calls | 9,908 |
| bytes produced by `serialize_without_hash` | 1,345,246,987 |
| SHA-256 calls | 9,908 |
| `canonical_serialize` calls | 0 |
| complete canonical bytes produced | 0 |
| same-mutation-epoch duplicate materializations | 0 |
| decision lifecycles | 9,892 |
| terminal lifecycles | 16 |
| unexpected mutation counts | 0 |

The mean `serialize_without_hash` result is `135,773.82` bytes. Every FULL
lifecycle had exactly one `serialize_without_hash` call and exactly one
SHA-256 call. Every lifecycle had zero complete `canonical_serialize` calls.
Lifecycle IDs were contiguous from 1 through the per-job lifecycle count for
all 16 jobs.

Therefore canonical bytes are **not consumed by `canonical_serialize()` in
the THROUGHPUT worker path**. The worker needs the canonical-without-hash
bytes as the input to `observation_hash`; it does not materialize the final
newline-terminated canonical JSON representation in this path.

The answer to the duplicate question for the actual target path is:

> No. The same finalized observation was not serialized more than once
> without mutation in FULL / THROUGHPUT.

The observed lifecycle is one materialization followed by one SHA-256. The
prior M4.3.1 redundant decision finalization was removed and does not remain
as a second lifecycle-local serialization.

## Observation-off cross-check

The observation-off sample produced zero observations and zero serialization
lifecycle records. It therefore confirms that the measured serialization work
is local to the enabled observation path, but it cannot characterize a final
canonical serialization consumer. Its runtime is reported only as a worker
local diagnostic cross-check.

## Direct API consumer characterization

The audit test intentionally exercises the API combination that could create
duplicate materialization:

```text
observation_hash(observation)
canonical_serialize(observation)
```

For one unchanged observation lifecycle, the diagnostic record was:

| Metric | Result |
|---|---:|
| lifecycle ID | 1 |
| `serialize_without_hash` calls | 2 |
| SHA-256 calls | 2 |
| `canonical_serialize` calls | 1 |
| same-epoch duplicate materializations | 1 |

A second test lifecycle inserted an explicit mutation epoch between two hash
calls. It recorded two serializations but zero same-epoch duplicates. This
demonstrates that identical call counts alone do not classify work as
duplicate; the lifecycle ID and mutation epoch are required.

Source inspection confirms that the complete `canonical_serialize()` API is
used by the observation probe and tests, not by the native THROUGHPUT worker
decision loop. Consequently the direct API duplication is a valid future API
optimization candidate, but it is not a measured THROUGHPUT bottleneck in
this audit.

## Exact byte and hash contract

The focused audit regression test proves the current contract for an
unchanged observation:

1. `canonical_serialize_without_hash(observation)` returns byte buffer `B`.
2. `observation_hash(observation)` equals `SHA-256(B)`.
3. `canonical_serialize(observation)` equals the existing `B` with exactly the
   current `observation_hash` field inserted before the final `}` and the
   current trailing newline retained.

The test compares the complete strings, not only lengths or hashes. The
existing field ordering, escaping, hash algorithm, and privacy projection are
unchanged. This establishes that one immutable `B` can safely serve both the
hash operation and final canonical rendering **within one unchanged
observation lifetime**.

It does not establish that a buffer may survive a mutable observation change.
After any semantic mutation, the buffer must be discarded or recomputed.

## Smallest future reuse design

If a production caller is later proven to need both results, the smallest
safe design is an explicit, observation-lifetime-scoped finalization value:

```text
FinalizedObservationSerialization {
    immutable canonical_without_hash bytes B
    observation_hash = SHA-256(B)
    canonical bytes = B + existing hash field and newline
}
```

The value would be created only after all observation/context mutations are
complete. It would own one immutable `B`, return the stored hash, and append
the existing final field representation without re-running the serializer.
Any later observation mutation would require a new finalization value. The
reuse must not be a hidden mutable cache on `PlayerObservation`, must not cross
observation lifetimes, and must not be used for hidden information.

This design is **not implemented** because the measured THROUGHPUT path never
consumes `canonical_serialize()`. Implementing it now would add representation
and invalidation complexity without removing measured target work.

## Decision

**No optimization benchmark is authorized by this characterization.** There
is no FULL / THROUGHPUT duplicate materialization to benchmark. The direct API
test proves a conditional reuse opportunity, but the next experiment should
only be scheduled if a real production consumer of both
`canonical_serialize()` and `observation_hash()` is introduced or measured.

M4.3.2 stops here. M5 is not started.
