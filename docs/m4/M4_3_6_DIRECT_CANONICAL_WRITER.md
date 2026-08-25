# M4.3.6 Direct Canonical Writer A/B

**Status:** M4.3.6 ACCEPTED

## Scope and exact workload

This experiment changes only primitive canonical rendering behind an internal build switch. Canonical ordering, preparation, escaping contract, observation schema, event history, SHA-256, privacy, and engine inputs are unchanged.

| Field | Value |
|---|---|
| Matchup | Swordsoul Tenyi ML v1 vs Salamangreat ML v1 |
| Seed / games / workers / max steps | `20260815` / `16` / `1` / `2200` |
| Observation / mode / trace persistence | FULL / throughput / off |
| Starting HEAD | `db7e5af2c97d9b6eccd697b903e9ba6fcea70a30` |

## Implementation boundary

- Control: the existing `std::ostringstream` canonical output path.
- Experiment: private `DirectCanonicalWriter` selected only by the PRIVATE `YGO_M4_DIRECT_CANONICAL_WRITER` build definition.
- Integer primitives use `std::to_chars`; literals and punctuation append directly to `std::string`.
- `json_escape_impl()` remains unchanged and continues to use its existing temporary `std::ostringstream`; escaping is therefore a controlled, unchanged variable in this experiment.
- Preparation, copies, sorting, field order, event history, schema, hashing, privacy, queries, and engine inputs are unchanged.

## Equivalence

- Focused cross-build fixture/unit/privacy tests: **PASS**.
- Deterministic worker conformance and trace comparison: **PASS**.

### Raw canonical fixture dumps

- Exact bytewise fixture gate: **PASS**.
- Control dump directory: `C:\yogiohML-m4-3-1\artifacts\m4\m4-3-6\bytewise_fixture_gate\control`.
- Experiment dump directory: `C:\yogiohML-m4-3-1\artifacts\m4\m4-3-6\bytewise_fixture_gate\experiment`.

| Fixture | Artifact | Control bytes | Control SHA-256 | Experiment bytes | Experiment SHA-256 | Exact bytes |
|---|---|---:|---|---:|---|---|
| rich | canonical_without_hash | 3890 | `ce3915625281afa3dca321e7e15a55cf2d0816e14124edfcccda3857d9119b3c` | 3890 | `ce3915625281afa3dca321e7e15a55cf2d0816e14124edfcccda3857d9119b3c` | PASS |
| rich | canonical | 3977 | `7d68197445943415c61c6da590f2eaa404d7490d3542ba804fd2fef7018d94bb` | 3977 | `7d68197445943415c61c6da590f2eaa404d7490d3542ba804fd2fef7018d94bb` | PASS |
| terminal | canonical_without_hash | 3854 | `90083ca35abf20d9429a83c81ab29e5e745932180c952f2efb7479a25e71a4f3` | 3854 | `90083ca35abf20d9429a83c81ab29e5e745932180c952f2efb7479a25e71a4f3` | PASS |
| terminal | canonical | 3941 | `d40a2ba656a48577b2ec422cfa2ffa864603dde77a42536549041b27fc9d19d7` | 3941 | `d40a2ba656a48577b2ec422cfa2ffa864603dde77a42536549041b27fc9d19d7` | PASS |

## Raw alternating Release repetitions

| Run | Variant | Worker-local us | Games/s | Serializer us | Observation us | Hash us |
|---|---|---:|---:|---:|---:|---:|
| A1 | control | 33751252 | 0.474056488 | 21018384 | 30680773 | 4669712 |
| B1 | experiment | 17432976 | 0.917800839 | 4138077 | 14402894 | 4915489 |
| A2 | control | 35592634 | 0.449531215 | 22446419 | 32437123 | 4800920 |
| B2 | experiment | 16293041 | 0.982014346 | 3817578 | 13456357 | 4764398 |
| A3 | control | 31620929 | 0.505993989 | 19714620 | 28831667 | 4547945 |
| B3 | experiment | 17123425 | 0.934392506 | 4025996 | 14103179 | 4854047 |

## Median and range

| Metric | Control median | Experiment median | Control min–max | Experiment min–max |
|---|---:|---:|---:|---:|
| Worker-local simulation (us) | 33751252 | 17123425 | 31620929–35592634 | 16293041–17432976 |
| Games/s | 0.474056488 | 0.934392506 | 0.449531215–0.505993989 | 0.917800839–0.982014346 |
| Serializer (us) | 21018384 | 4025996 | 19714620–22446419 | 3817578–4138077 |
| Observation (us) | 30680773 | 14103179 | 28831667–32437123 | 13456357–14402894 |
| Observation hash (us) | 4669712 | 4854047 | 4547945–4800920 | 4764398–4915489 |

- Serializer median speedup: `80.845359%`.
- Worker median speedup: `49.265808%`.
- Paired serializer improvements: `3/3`.
- Paired worker improvements: `3/3`.

## Structural closure

- Observations: `9908`.
- Canonical-without-hash bytes: `1345246987`.
- Serialization calls, SHA calls, query/entity/candidate/script counters are required identical across all repetitions.
- `canonical_serialize()` consumption in THROUGHPUT remains zero.

## Regression gates

| Gate | Status | Evidence |
|---|---|---|
| Full CTest — control | **PASS** | `build/m4-3-6-control (93/93, exit 0, 162.66 s)` |
| Full CTest — experiment | **PASS** | `build/m4-3-6-experiment2 (93/93, exit 0, 108.63 s)` |
| Repository Python | **PASS** | `8 tests passed, exit 0` |
| M3 Python | **PASS** | `17 tests passed, exit 0` |
| M4 Python | **PASS** | `second full invocation: 124 tests, 3 skipped, exit 0` |
| Privacy — control / experiment | **PASS / PASS** | focused fixtures plus full CTest |
| Candidate/observation consistency | **PASS** | full CTest / worker structural counters |
| Fixed-deck regression — control / experiment | **PASS** | fixed-deck artifacts |
| Deterministic worker gate | **PASS** | worker conformance and M3 determinism artifacts |

The first full M4 Python invocation had one scheduling-sensitive failure in `test_result_then_exit_never_publishes_passed_under_repeated_scheduling`; the isolated retry passed, and the complete second invocation passed. The first failure is retained as evidence and is not promoted to a gate pass.

| M4 Python evidence | Result |
|---|---|
| First full invocation | **FAIL** (123 tests) |
| Isolated retry | **PASS** |
| Second full invocation | **PASS** (124 tests, 3 skipped) |

## Remaining Release observation buckets

The direct writer removes the ostream primitive-rendering bottleneck in this A/B. The next measured buckets are reported descriptively; no follow-up optimization is started here.

| Rank | Bucket | Median us | Fraction of experiment observation median |
|---:|---|---:|---:|
| 1 | `observation_hash` | 4854047 | 34.418% |
| 2 | `observation_canonical_serialization` | 4025996 | 28.547% |
| 3 | `observation_query_decode` | 2979794 | 21.129% |
| 4 | `observation_other` | 1765868 | 12.521% |
| 5 | `observation_query_location` | 475652 | 3.373% |
| 6 | `observation_entity_projection` | 867 | 0.006% |
| 7 | `observation_visibility_privacy` | 448 | 0.003% |

## Acceptance

**M4.3.6 ACCEPTED**. The M4.3.5 benchmark was not rerun. The historical
experiment was opt-in; after the separate M4 finalization equivalence gates,
the direct writer is enabled as the default internal production path. The
public serialization API and canonical byte contract remain unchanged.

## Scope boundary

No visible-event history change, schema change, event delta, hash change, query optimization, ocgcore change, or M5 work was performed.
