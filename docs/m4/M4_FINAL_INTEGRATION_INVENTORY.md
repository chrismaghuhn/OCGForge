# M4 Finalization Integration Inventory

Status: inventory recorded before post-foundation changes are applied.

Authoritative integration base:

- `bafe75b97e03d796b318d6f7757cc555873f1fb9` (`origin/main` after PR #3 merge)
- target branch: `chris/m4-finalization`
- source worktree: `C:\yogiohML`
- historical M4.3.5/3.6 worktree: `C:\yogiohML-m4-3-1`, HEAD
  `db7e5af2c97d9b6eccd697b903e9ba6fcea70a30`; this worktree remains untouched

## Candidate classification

| Candidate | Status | Owning layer | Semantic impact | Determinism impact | Privacy impact | Replay impact | Evidence/tests |
|---|---|---|---|---|---|---|---|
| `604992d` through `ee314e5` | ACCEPTED | M4 audit contracts, observation/core telemetry, worker protocol, audit tools and tests | Adds opt-in measurement and durable audit contracts; default gameplay/observation semantics remain unchanged | Must preserve job identity, counters and canonical results | Must preserve perspective/privacy gates; telemetry must not expose hidden state | Audit sidecars and lifecycle IDs only; no replay semantics change | `M4_PERFORMANCE_AUDIT.md`, audit JSON/schema, M4 audit contract/report/runner tests |
| `4f60fab` | ACCEPTED | Observation builder and canonical simulation decision path | Defers only intermediate decision-observation finalization; default immediate finalization remains | Final bytes/hashes and decisions must remain identical | No visibility or redaction change | Final observation hash/trace inputs remain unchanged | `M4_3_1_OBSERVATION_FINALIZATION.md`, observation-builder equivalence tests |
| `a61e346` | ACCEPTED | Observation lifecycle telemetry and worker sidecars | Records one serialize/hash operation per observation epoch; no representation change | Lifecycle IDs and call counts must remain deterministic | No privacy change | No trace contract change | `M4_3_2_OBSERVATION_SERIALIZATION_LIFECYCLE.md`, lifecycle tests/evidence |
| `cf16e9b` | ACCEPTED | Release build preset and characterization tooling | Adds ordinary Release characterization only | Build identity and semantic comparison gates required | No privacy change | Trace comparison remains required | Release characterization report, SHA/serialization diagnostics and tests |
| `76c1a02` | EVIDENCE_ONLY | Serialization-shape audit instrumentation and reports | Characterizes copies, sorting, rendering, history growth and byte shape; no production optimization | Instrumented binaries are not throughput evidence; semantic equivalence is gated | Privacy fixtures remain required | Trace/observation equivalence is required | Serialization-shape report/JSON and audit tools |
| `db7e5af` M4.3.5 plan and dirty reserve experiment | REJECTED / EVIDENCE_ONLY | Reserve-backed serializer experiment | Reserve-backed production implementation was rejected; it must not be imported | Historical A/B evidence only | Historical equivalence evidence only | No replay change | `M4_3_5_RESERVE_BACKED_SERIALIZATION.md`, JSON and fail-closed harness evidence may remain historical |
| Dirty M4.3.6 changes at `db7e5af` | ACCEPTED | Private direct canonical writer and serializer build switch | Internal `YGO_M4_DIRECT_CANONICAL_WRITER` opt-in only; no public/schema contract change | Exact bytes, hashes, semantic runs and repeated determinism must be revalidated after integration | Paired-world/privacy equivalence required | Trace hashes and canonical inputs must remain equal | `M4_3_6_DIRECT_CANONICAL_WRITER.md`, direct-writer fixture/harness and A/B evidence |

## Integration constraints

1. The PR #3 merge and its review fixes are already present in `bafe75b`.
   Historical M4 branches predate those fixes; their old workflow/docs/test
   versions must not overwrite the merged baseline.
2. The M4.2–M4.3.4 source/tooling changes will be applied in dependency order,
   with focused gates after each meaningful step.
3. The dirty M4.3.6 implementation will be imported only after the accepted
   M4.3.1–M4.3.4 layers are present. Its build option remains OFF initially.
4. No M4.3.5 reserve-backed production source or reserve telemetry is allowed
   into the normal serializer path.
5. Historical reports are evidence of their original experiment and are not
   the M4 FINAL acceptance evidence. M4 FINAL requires fresh Release evidence
   bound to the finalized source commit.

## Planned gate sequence

- apply M4.2 audit chain; run contract/report and foundation gates;
- apply M4.3.1; run observation finalization equivalence gates;
- apply M4.3.2–M4.3.4 evidence/tooling; run lifecycle and serialization tests;
- import M4.3.6 opt-in direct writer and run control/opt-in byte/hash/privacy
  equivalence before any default-path decision;
- close the worker lifecycle regression with repeated executions;
- run fresh Release scaling, final equivalence gates, and recommended-concurrency
  soak;
- generate repository-backed M4 FINAL evidence only if every required gate is
  fresh and complete.

