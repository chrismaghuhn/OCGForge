# OCGForge M4.3.3 — Optimized-Build Performance Characterization

Status: **M4.3.3 RELEASE CHARACTERIZATION PASS**

No optimization was implemented. SHA-256, serialization, observations, query
paths, ocgcore, CardScripts, the worker architecture, and M5 were unchanged.

## Workload and build identity

The exact M4.3.2 workload was used:

| Field | Value |
|---|---|
| Matchup | Swordsoul Tenyi ML v1 vs Salamangreat ML v1 |
| Master seed | `20260815` |
| Games / workers | 16 / 1 |
| Max steps | 2200 |
| Mode | FULL, `throughput` |
| Trace persistence | off for performance; explicit per-job trace files for semantic equivalence |
| Source commit | `a61e3465b5903b21ad5eae17f45889e2a2fccef3` |
| Compiler | Clang-19.1.7 through Zig 0.14.1 |
| Release flags | `-O3 -DNDEBUG` |
| Audit define | `YGO_M4_PERFORMANCE_AUDIT` only on audit workers |

The reproducible `release-windows-zig` preset is present in
`CMakePresets.json`. Audit-worker SHA-256 values are:

| Build | Worker | SHA-256 |
|---|---|---|
| Debug | `build/m4-audit/ygo_m4_worker.exe` | `940b251371ae1c663abbbc7ac342cee007f3eaa182fab3f640f9f962669f5473` |
| Release | `build/m4-audit-release/ygo_m4_worker.exe` | `ac29e35faab302416f12fb556667b9d550d0285622a5d4e66c221ffbb3771b07` |

Both workers reported the same canonical rules bundle, patchset, deck hashes,
protocol schema, and worker identity. Their only intended build-mode changes
were `Debug` versus ordinary `Release` compilation and the corresponding trace
manifest identity.

## Semantic and privacy gates

The explicit `tools/m4/compare_build_modes.py` harness supplied a unique
`trace_output` path for every job, then compared the persisted traces. This is
required because `persist_trace` alone does not request native file persistence.

Result: **PASS**, 16/16 jobs.

- Result semantic fields were equal per job: terminal result, winner, reason,
  engine steps, interactive decisions, semantic action count, gameplay hash,
  and error counters.
- All 9,908 final observation hashes matched in order. The combined sequence
  digest was
  `0fe46fac3521c05ad56a25a6dbfe202f939d4d9a93936a5856963e0c94a270c9`.
- All trace-step payloads and step bytes matched.
- Manifest-normalized trace hashes matched for all jobs.
- Raw `canonical_trace_hash_v2` values differed as expected: the existing v2
  manifest hashes `build_type` and `compiler_identity`. This is build identity,
  not a gameplay/observation divergence; the normalized trace and every
  observation hash were equal.

The following seven tests passed in both Debug and Release (7/7 each):

`sha256_known_answer_test`, `observation_contract_test`,
`privacy_projection_test`, `observation_builder_test`, `m2_1_xyz_api_test`,
`continuation_privacy_test`, and `m3_real_deck_privacy_test`.

The paired-world assertions cover hidden Xyz materials, hidden/revealed real
deck identities, hidden-field redaction, and continuation privacy.

The repository Python regression suites also passed: M4, 121 tests with 3
expected skips; M3, 17 tests.

## Worker throughput

| Metric | Debug | Release | Debug → Release |
|---|---:|---:|---:|
| Worker-local simulation | 383.388106 s | 32.466957 s | 11.808563× |
| Games/s | 0.041733 | 0.492809 | 11.808563× |
| Outer observation time | 349.316775 s | 29.713129 s | 11.756311× |
| Outer observation / worker | 91.113097% | 91.518059% | — |

The Release build improves measured one-worker throughput by 11.808563× on
this run. This is a measured build-mode result, not a projected optimization
speedup.

## Observation timing buckets

Times are native worker-local sidecar totals. Fractions use worker-local
simulation time and the outer observation scope respectively. The outer scope
is the correct denominator for the nested observation buckets.

| Bucket | Calls | Debug us | Release us | Debug % worker | Release % worker | Release % observation | Debug → Release |
|---|---:|---:|---:|---:|---:|---:|---:|
| `observation_canonical_serialization` | 9,908 | 88,895,918 | 20,329,019 | 23.186926% | 62.614488% | **68.417631%** | 4.372858× |
| `observation_hash` | 9,908 | 138,780,159 | 4,636,641 | 36.198348% | 14.281107% | **15.604688%** | 29.931185× |
| `observation_query_decode` | 128,804 | 56,776,135 | 2,697,643 | 14.809050% | 8.308888% | 9.078960% | 21.046571× |
| `observation_other` | 9,908 | 26,998,148 | 1,577,293 | 7.041989% | 4.858149% | 5.308404% | 17.116761× |
| `observation_query_location` | 118,896 | 35,384,875 | 470,102 | 9.229518% | 1.447940% | 1.582136% | 75.270633× |
| `observation_entity_projection` | 526,004 | 2,339,588 | 1,207 | 0.610240% | 0.003718% | 0.004062% | 1,938.349627× |
| `observation_candidate_consistency` | 33,247 | 8,542 | 70 | 0.002228% | 0.000216% | 0.000236% | 122.028571× |
| `observation_query_field` | 9,908 | 99,285 | 207 | 0.025897% | 0.000638% | 0.000697% | 479.637681× |
| `observation_visibility_privacy` | 1,476,104 | 1,876 | 716 | 0.000489% | 0.002205% | 0.002410% | 2.620112× |
| `observation_zone_projection` | 508,088 | 31,955 | 231 | 0.008335% | 0.000711% | 0.000777% | 138.333333× |
| `observation_relationship_projection` | 9,908 | 294 | 0 | 0.000077% | 0.000000% | 0.000000% | — |
| `observation_query_individual` | 0 | 0 | 0 | 0.000000% | 0.000000% | 0.000000% | — |

### Lifecycle and serialization evidence

Both builds recorded exactly:

- 9,908 observation lifecycles;
- 9,908 `serialize_without_hash` calls producing 1,345,246,987 bytes;
- 9,908 SHA-256 calls;
- zero `canonical_serialize()` calls and zero canonical-with-hash bytes in
  THROUGHPUT;
- zero same-mutation-epoch duplicate materializations.

The average canonical-without-hash materialization was 135,773.82 bytes.
Using the exclusive `observation_canonical_serialization` bucket, measured
serializer throughput was 15.132832 MB/s in Debug and 66.173729 MB/s in
Release. Therefore no canonical-byte reuse optimization is justified by this
audit: the duplicate materialization was not present.

### Raw SHA-256 characterization

This diagnostic calls the existing `sha256_string(std::string_view)` directly;
the implementation was not changed. Known-answer tests passed in both builds.

| Input | Calls | Debug MB/s | Release MB/s | Speedup | Digest equal |
|---:|---:|---:|---:|---:|:---:|
| 4 KiB | 8,192 | 6.839540 | 301.622 | 44.099977× | yes |
| 32 KiB | 1,024 | 6.796100 | 310.339 | 45.664663× | yes |
| 136 KiB | 241 | 7.791030 | 308.962 | 39.656200× | yes |
| 1 MiB | 32 | 7.803870 | 309.913 | 39.712912× | yes |

## Counters, entities, and setup

Debug and Release operation counters were byte-for-byte equal:

| Counter | Value |
|---|---:|
| Observations | 9,908 |
| Candidate sets | 9,892 |
| Candidate total / sample maximum | 46,897 / 336 |
| Entities projected | 526,004 |
| `OCG_DuelQueryField` | 19,816 |
| `OCG_DuelQueryLocation` | 118,896 |
| `OCG_DuelQuery` | 0 |
| `OCG_DuelProcess` | 24,272 |
| Script loads | 1,232 |
| Script reader requests | 1,200 |

Entity projection consisted of 64,656 HAND, 312,552 GRAVEYARD, 148,620
EXTRA_DECK, and 176 PENDULUM_RELEVANT_STATE entities. The controlled sample
had 526,004 identity-known entities and zero redacted entities. It recorded
526,004 static card-data lookups, 526,004 current-property projections, and
3,970,235 allocation/copy events. Printed metadata is still reconstructed, but
Release reduced its measured projection bucket to 1,207 us (0.004062% of
outer observation time).

Script setup timing was 3,621,746 us in Debug and 409,878 us in Release. That
is 0.944668% and 1.262447% of worker-local runtime respectively: measurable
setup work, but not a dominant observation bucket.

## Observation-off cross-check

This mode remains diagnostic only and was not used as training throughput.

| Build | FULL worker us | Observation-off worker us | FULL minus off | FULL minus off / worker |
|---|---:|---:|---:|---:|
| Debug | 383,388,106 | 36,452,639 | 346,935,467 | 90.491975% |
| Release | 32,466,957 | 1,860,979 | 30,605,978 | 94.268083% |

The cross-check supports the sidecar decomposition: observation work remains
the dominant worker-local cost after Release, but its dominant sub-bucket is
now serialization rather than SHA-256.

## Candidate classification

| Candidate | Classification | Release evidence |
|---|---|---|
| Single-observation canonical-byte reuse | `NOT_MATERIAL` | One materialization and one hash per lifecycle; zero duplicates and zero `canonical_serialize()` consumption. |
| Canonical serialization copy reduction | `REMAINS_MAJOR` | 68.417631% of observation time. |
| Observation-hash cost audit | `REMAINS_MEASURABLE` | 15.604688% of observation time; raw SHA itself was strongly Debug-sensitive. |
| Query decode | `REMAINS_MEASURABLE` | 9.078960% of observation time. |
| Query location | `REMAINS_MEASURABLE` | 1.582136%; 12 semantically distinct calls per observation remain. |
| Static metadata reuse | `DEBUG_BUILD_ARTIFACT_OR_MUCH_REDUCED` | 0.004062% in Release despite 526,004 lookups. |
| Field reuse for public-state hash | `NOT_MATERIAL` | 0.000697% in Release, despite one same-state duplicate query. |
| Visibility/privacy projection reduction | `NOT_MATERIAL` | 0.002410%; privacy gates PASS. |
| Relationship projection reuse | `NOT_MATERIAL` | 0 us in Release and zero relationship objects in this sample. |
| Candidate generation | `NOT_MATERIAL` | Baseline protocol-candidate fraction remains 0.189337%; no candidate optimization was run. |

## Interpretation and stop point

1. `observation_hash` is **not** the largest Release bucket. Ordinary Release
   compilation reduces it to 15.604688% of observation time.
2. Serialization remains strongly material and is the Release bottleneck at
   68.417631% of observation time.
3. `query_decode` is not dominant; it is the third measured bucket at
   9.078960%, behind serialization and hashing.
4. One-worker Release throughput improved 11.808563× versus the fresh Debug
   sample.
5. The prior worker saturation point is stale for Release and needs a later
   fresh Release scaling matrix. That matrix was intentionally not run here.

The first future experiment should characterize ownership and temporary-copy
boundaries in the existing `serialize_without_hash` path under Release. That
experiment is not implemented in M4.3.3. No optimization, serializer rewrite,
hash change, query change, or M5 work begins here.

Evidence artifacts:

- `artifacts/m4/m4-3-3/semantic/build_mode_equivalence.json`
- `artifacts/m4/m4-3-3/performance-debug/audit.json`
- `artifacts/m4/m4-3-3/performance-release/audit.json`
- `artifacts/m4/m4-3-3/raw-sha/debug.json`
- `artifacts/m4/m4-3-3/raw-sha/release.json`
