# M4.3.5 Reserve-Backed Canonical Serialization Design

## Goal

Run one isolated Release A/B experiment that changes only the top-level
canonical output buffer from `std::ostringstream` to a private reserve-backed
`std::string` stream buffer, while proving byte, hash, privacy, and gameplay
equivalence.

## Constraints

The existing serializer remains the control. Field order, sorting, ostream
formatting, JSON escaping, canonical bytes, SHA-256 input, observation
semantics, event-history contents, and all engine/rules inputs remain fixed.
The experiment is enabled only by a build definition and is not a public
observation API or a runtime/global switch.

## Architecture

`src/observation/serialization.cpp` will keep the current writer functions and
their primitive insertion expressions. Their stream parameters will be
generalized from `std::ostringstream&` to `std::ostream&` where required by the
existing callback templates. A private `SerializationOutput` wrapper will
select the control `std::ostringstream` or, under
`YGO_M4_RESERVE_BACKED_SERIALIZATION`, a local `std::string` plus a custom
`std::streambuf`. The experimental stream buffer appends `xsputn()` and
`overflow()` directly to a string whose initial capacity comes from a cheap,
deterministic structure-only hint. It also exposes its logical write position
so existing shape offsets remain correct if the audit macro is combined with
the experiment.

The wrapper returns the same string at the end of one serialization lifetime.
No bytes are reused across observations, no exact-size prepass is added, and
the existing `json_escape()` temporary stream is unchanged. Reserve telemetry
is diagnostic-only: performance-audit snapshots expose a future-prefixed
sidecar object containing mode, call count, requested capacity, final bytes,
growth events, and unused capacity. Control reports the same object with the
control mode and zero reserve counters.

## Equivalence and measurement flow

Both Release variants build the same focused fixture and existing observation,
privacy, continuation, and XYZ tests. A comparison runner captures the
deterministic test output from each binary and requires exact equality. It
then runs the canonical 16-game workload with the same seed and inputs in
conformance mode for semantic comparison, followed by clean throughput A/B
runs with the existing performance sidecar but without M4.3.4 shape
instrumentation. Repetitions alternate control and experiment. Raw values,
medians, ranges, sidecar counters, reserve telemetry, and binary identities
are retained in the M4.3.5 JSON evidence.

Acceptance uses a predeclared materiality rule: the experimental median must
improve serializer time by at least 5% and worker-local time by at least 3%,
with at least two of three paired repetitions improving in both directions.
If exact equivalence passes but this rule does not, the production buffer
change is removed and the characterization evidence remains.

## Failure handling

Any byte/hash mismatch, sidecar counter mismatch, semantic/privacy divergence,
build identity drift, or required regression failure rejects the experiment.
Reserve telemetry is never allowed to affect serialization output or worker
behavior; a telemetry failure is reported as an audit failure rather than
silently changing the workload.

## Verification scope

Focused fixtures cover ordinary visible observations, both perspectives,
continuations, hidden and paired worlds, terminal observations, relationships
including the XYZ/privacy shape, and all existing JSON escape forms. The
canonical workload compares observation hashes, gameplay hashes, terminal
results, winners/reasons, engine steps, decision/action counts, errors, and
candidate/observation and privacy gates before any timing result is considered.
