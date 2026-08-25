# M4.3.3 Release Characterization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Characterize the canonical M4 workload and the existing scalar SHA-256/observation serialization paths under ordinary Release Zig compilation, with Debug/Release semantic equivalence proven first.

**Architecture:** Add only a reproducible `release-windows-zig` CMake preset and a focused SHA-256 benchmark/KAT executable. Use the existing M4 worker protocol and performance-audit sidecars for the real 16-game observation measurements. Run a separate conformance trace sample to compare per-job semantic fields, trace hashes, and every final observation hash without changing production simulation behavior.

**Tech Stack:** CMake/Ninja presets, Zig 0.14.1 Windows fallback, C++17, existing `ygo_m4_worker`, existing Python `tools.m4` benchmark/audit runner, strict JSON/Markdown evidence artifacts.

---

### Task 1: Lock the canonical build and workload identity

**Files:**
- Read: `CMakePresets.json`, `cmake/zig-toolchain.cmake`, `tools/m4/job_generation.py`, `tools/m4/worker_protocol.py`
- Read: `docs/m4/m4_3_2_observation_serialization_lifecycle.json`

- [ ] Record the worktree state explicitly. The characterization branch contains
  the already-reviewed M4.3.3 measurement changes, so the working tree is not
  expected to be clean. No unrelated production changes may be present:

```powershell
git status --short
git show --stat --oneline HEAD
Get-Content docs/m4/m4_3_2_observation_serialization_lifecycle.json | ConvertFrom-Json
```

Expected: current commit `a61e346`; seed `20260815`; 16 games; one worker; max 2200; FULL; throughput; trace persistence off. Record the planned measurement changes separately from the source baseline.

- [ ] Record current Debug facts:

```powershell
Get-Content build/windows-zig/CMakeCache.txt | Select-String -Pattern 'CMAKE_BUILD_TYPE|CMAKE_CXX_COMPILER:|CMAKE_CXX_FLAGS_RELEASE|CMAKE_TOOLCHAIN_FILE|CMAKE_MAKE_PROGRAM'
& .cache/toolchain/zig-x86_64-windows-0.14.1/zig.exe version
```

Expected: Debug Zig build, standard Release flags `-O3 -DNDEBUG`, and Zig `0.14.1`.

### Task 2: Add or validate the reproducible Release Zig preset

**Files:**
- Modify: `CMakePresets.json`

- [ ] Validate the existing `release-windows-zig` configure/build/test presets, or add them only if absent. They must be equivalent to `dev-windows-zig` except for:

```json
"binaryDir": "${sourceDir}/build/release-windows-zig",
"CMAKE_BUILD_TYPE": "Release"
```

Keep `dev-windows-zig` unchanged. Do not add LTO, PGO, architecture flags, fast-math, SIMD, SHA intrinsics, or custom Release flags.

- [ ] Validate the preset:

```powershell
cmake --list-presets
cmake --preset release-windows-zig
cmake --build --preset release-windows-zig --target help
```

### Task 3: Add focused SHA-256 KAT and benchmark

**Files:**
- Create: `tools/ygo_sha256_benchmark/main.cpp`
- Create: `tests/trace/sha256_known_answer_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] Add a CTest executable for standard SHA-256 vectors: empty string, `abc`, and one million `a` bytes, using both `sha256_string` and `sha256_bytes`. Do not change `src/trace/sha256.cpp`.

- [ ] Add `sha256_benchmark` that:
  1. runs the same KATs before timing;
  2. uses deterministic payloads of exactly 4096, 32768, 139264, and 1048576 bytes;
  3. hashes at least 33554432 bytes per size by default;
  4. emits strict JSON with compiler/build identity, size, calls, total bytes, elapsed microseconds, MB/s, and final digest;
  5. consumes the digest so Release cannot remove the calls.

- [ ] Build and run Debug:

```powershell
cmake --build --preset dev-windows-zig --target sha256_known_answer_test sha256_benchmark --parallel 4
ctest --preset dev-windows-zig -R sha256_known_answer_test --output-on-failure
build/windows-zig/sha256_benchmark.exe --target-bytes 33554432
```

### Task 4: Build both audit workers and prove semantic equivalence

**Files:**
- Create ignored build output: `build/release-windows-zig`, `build/m4-audit-release`
- Create ignored run output: `artifacts/m4/m4-3-3/semantic-debug`, `artifacts/m4/m4-3-3/semantic-release`

- [ ] Build and identity-lock both ordinary Release and Debug audit workers. For each actual worker used below record source commit, compiler identity/version, CMake build type, `YGO_M4_PERFORMANCE_AUDIT`, compile flags, executable SHA-256, and the worker ready handshake. Do not substitute an unspecified existing Debug executable.

Build ordinary Release:

```powershell
cmake --build --preset release-windows-zig --parallel 4
```

Record CMake cache, `compile_commands.json` flags, compiler version, and SHA-256 of `ygo_m4_worker.exe`.

- [ ] Configure separate Debug and Release audit builds with the same M4.3.2 sidecar define:

```powershell
cmake -S . -B build/m4-audit-release -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=cmake/zig-toolchain.cmake `
  -DCMAKE_MAKE_PROGRAM="$PWD/.cache/toolchain/ninja/ninja.exe" `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_CXX_FLAGS=-DYGO_M4_PERFORMANCE_AUDIT `
  -DM0_AUTO_FETCH_RULES=ON
cmake --build build/m4-audit-release --target ygo_m4_worker --parallel 4
```

Also build `build/m4-audit` from the current source with `CMAKE_BUILD_TYPE=Debug` and `-DYGO_M4_PERFORMANCE_AUDIT`, then record its executable identity before using it.

- [ ] Run 16 conformance jobs for Debug and Release with identical derived jobs, FULL observations, and trace persistence on. Use an explicit per-job `trace_output` path; the stock benchmark CLI's `persist_trace` flag alone does not create files:

```powershell
python -B -m tools.m4.benchmark --worker-executable <debug-audit-worker> --games 16 --workers 1 --master-seed 20260815 --mode conformance --warmup-games 0 --observation-mode full --trace-persistence on --output <semantic-debug.json> --result-timeout-seconds 120
python -B tools/m4/compare_build_modes.py --debug-worker <debug-audit-worker> --release-worker <release-audit-worker> --master-seed 20260815 --games 16 --max-steps 2200 --output-dir artifacts/m4/m4-3-3/semantic
```

The harness must set a unique trace path for every derived job, require trace hashes, and compare per-job terminal, winner, win reason, engine steps, interactive decisions, semantic action count, gameplay hash, zero error counters, persisted trace JSONL bytes, every `observation_hash`, and final `trace_hash`. Because the existing v2 trace hash includes `build_type` and `compiler_identity` in its manifest, record the raw Debug/Release trace hashes and additionally compare a manifest-normalized trace hash. Any mismatch in the step payload, observation hashes, gameplay hash, or normalized trace hash blocks performance measurement; the expected raw trace-hash difference must be documented as build metadata only.

- [ ] Run named Release privacy/paired-world/hidden-information gates before timing and record each PASS: `privacy_projection_test`, `m2_1_xyz_api_test`, `m3_real_deck_privacy_test`, `observation_builder_test`, `observation_contract_test`, and `continuation_privacy_test` (where present in the configured CTest set). Include the paired-world byte/hash equality assertions from those fixtures.

### Task 5: Run the controlled one-worker characterization

**Files:**
- Create ignored run output: `artifacts/m4/m4-3-3/performance-debug`, `artifacts/m4/m4-3-3/performance-release`

- [ ] Run one fresh instrumented Debug FULL sample with the existing M4 audit runner: Swordsoul Tenyi ML v1 vs Salamangreat ML v1, seed `20260815`, 16 games, one worker, max 2200, throughput, FULL, trace off. Preserve both existing sidecars.

- [ ] Run the exact same audit command with the Release audit worker. Do not run the 1/2/4/8/16/32 scaling matrix.

- [ ] Aggregate Debug/Release worker-local time, games/s, outer observation time, all observation buckets, operation counters, lifecycle counts, errors, and `serialize_without_hash` calls/bytes/elapsed/MB/s. Report absolute values, worker-runtime fractions, observation-runtime fractions, and Debug-to-Release ratios.

- [ ] Classify every M4.3.2 candidate as exactly one of `REMAINS_MAJOR`, `REMAINS_MEASURABLE`, `DEBUG_BUILD_ARTIFACT_OR_MUCH_REDUCED`, or `NOT_MATERIAL` using Release evidence.

- [ ] Run `sha256_benchmark.exe --target-bytes 33554432` from Debug and Release; verify that the timed loop explicitly calls the existing `sha256_string(std::string_view)` implementation, that KATs pass, that equal-size hashes match, and calculate MB/s and speedups.

- [ ] Derive real serializer throughput only after the audit evidence proves `canonical_serialize_calls == 0`, one `serialize_without_hash` call per lifecycle, and that `observation_canonical_serialization` is the exclusive serializer timing bucket. Do not infer serializer elapsed time from an ambiguous nested total.

### Task 6: Produce, review, and validate the reports

**Files:**
- Create: `docs/m4/M4_3_3_RELEASE_CHARACTERIZATION.md`
- Create: `docs/m4/m4_3_3_release_characterization.json`

- [ ] Include exact build identities/flags/hashes, semantic-equivalence evidence, workload identity, raw SHA cases, serializer calls/bytes/elapsed/MB/s, operation-counter equality, timing tables, and candidate classifications.

- [ ] Answer explicitly: whether `observation_hash` remains the largest Release bucket; whether serialization remains material; whether `query_decode` becomes dominant; total Release throughput improvement; and whether saturation requires a later fresh Release scaling matrix.

- [ ] Validate:

```powershell
python -B -m json.tool docs/m4/m4_3_3_release_characterization.json > $null
git diff --check
git status --short
```

Expected: strict JSON, no whitespace errors, no production optimization, no ocgcore/CardScripts/M5 changes.

- [ ] Have an independent review subagent inspect the final diff and evidence. Resolve blocker/major findings, rerun affected gates, and stop with `M4.3.3 RELEASE CHARACTERIZATION PASS` or an evidence-based failure status.
