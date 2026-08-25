# OCGForge

OCGForge is a deterministic Yu-Gi-Oh! simulation and game-AI research environment built around a narrow C++ adapter to a pinned OCG rules stack.

The project is correctness-first. It aims to make engine interaction, legal decisions, player-visible observations, hidden-information handling, deterministic traces, and conformance evidence explicit and reproducible before adding ML-facing scale or training code.

> **Current maturity:** the repository has a verified fixed-matchup M3/M3.5 checkpoint, not general all-deck Yu-Gi-Oh! support.

## What OCGForge is

OCGForge currently provides:

- a repository-pinned OCG rules bundle;
- a C++ RAII host around the public OCG C API;
- a typed, fail-closed decision protocol;
- deterministic adapter-local continuations for combinatorial decisions;
- perspective-safe player observations;
- canonical observation and gameplay hashing;
- deterministic engine traces;
- focused fixtures and conformance inventories;
- a locked Swordsoul Tenyi vs. Salamangreat fixed-deck validation slice;
- two narrow repository-versioned ocgcore API-hardening patches.

OCGForge intentionally does **not** currently claim:

- full Yu-Gi-Oh! card or deck compatibility;
- a stable general-purpose Gym-style API;
- checkpoint/fork support;
- high-throughput vectorized simulation;
- a learned policy, teacher, search system, or training stack;
- competitive playing strength.

## Project priorities

When goals conflict, use this order:

1. correctness;
2. determinism;
3. information safety;
4. complete and honest legal-decision representation;
5. replayability and auditability;
6. maintainability;
7. performance;
8. ML throughput and scale.

Performance work must not weaken the first six properties.

## Current checkpoint

Documentation baseline: `main` at the M4 finalization base `bafe75b97e03d796b318d6f7757cc555873f1fb9` (2026-08-25).

Repository-recorded M3/M3.5 acceptance evidence reports:

- M3: **FINAL PASS**;
- M3.5: **FINAL PASS**;
- 45/45 required fixed-matchup mechanics classified, with 0 pending;
- 16/16 complete fixed-deck games over both starting-player partitions and mirrored deck seats;
- 0 unsupported required decisions, retries, automatic decisions, candidate truncations, or core errors in those fixed games;
- 85/85 CTest tests;
- 17/17 M3 Python tests;
- 8/8 repository Python tests;
- independent-process determinism, semantic-action replay, and CRLF replay gates recorded as passing.

M4 adds the parallel-simulation foundation and its benchmark/audit contracts.
The finalized branch records **M4 FINAL PASS** from fresh Release evidence,
with semantic validation through 64 workers and 16 workers recommended for
production concurrency. This is a parallel-simulation foundation result; it
is not a general ML-readiness or M5 claim. M4.3.5 remains a documented
rejected experiment, while accepted internal equivalence-preserving work is
recorded separately in `docs/m4/M4_FINAL.md`.

These values are acceptance evidence committed to the repository. They are **not a claim that this documentation-only package re-ran the test suite**.

See [Current Project State](docs/CURRENT_PROJECT_STATE.md) for the exact scope boundary.

## Architecture at a glance

```text
Pinned rules bundle
  ocgcore + CardScripts + BabelCDB
              |
              v
        ygo::core::CoreHost
              |
      raw engine messages
              |
              v
      Decision Protocol v1
  typed complete candidates
  + local continuations
              |
              +--------------------+
              |                    |
              v                    v
   selected engine response     Trace v1/v2
              |
              v
          ocgcore
              |
       public queries/events
              |
              v
    PlayerObservation v1
   perspective-safe state
              |
              v
 future search / teacher / model /
       environment adapters
```

The pinned OCG rules stack remains authoritative for game legality and current engine state. OCGForge contracts define what the environment exposes and how it fails.

## Quick start

### Native Windows / MSVC

From a Visual Studio developer environment with Ninja available:

```text
cmake --preset dev-windows
cmake --build --preset dev-windows --parallel
ctest --preset dev-windows --output-on-failure
```

The configure step uses the repository rules-bundle machinery and an ignored local cache.

### Local Windows Zig fallback

The repository also contains a Windows-oriented Zig/Ninja fallback preset:

```text
cmake --preset dev-windows-zig
cmake --build --preset dev-windows-zig --parallel
ctest --preset dev-windows-zig --output-on-failure
```

The fallback toolchain binaries are local ignored inputs; they are not part of the canonical rules bundle.

### Verify the pinned rules bundle

```text
python tools/fetch_rules_bundle.py --lock third_party/rules_bundle.lock.json --cache .cache/rules_bundle
python tools/verify_rules_bundle.py --lock third_party/rules_bundle.lock.json --cache .cache/rules_bundle
```

## Useful probes

Controlled duel trace:

```text
build/dev-windows/ygo_core_probe.exe --max-steps 1000 --output artifacts/probe-trace.jsonl
```

Deliberate fail-closed diagnostic:

```text
build/dev-windows/ygo_core_probe.exe --force-unsupported
```

Player-observation probe:

```text
build/dev-windows/ygo_observation_probe.exe --fixture m2 --seed 123 --player 0 --output artifacts/m2-observation.json
```

Use the equivalent `build/windows-zig` path when building with the Zig fallback preset.

## Documentation

Start with [docs/README.md](docs/README.md).

Core project documents:

- [Project Charter](docs/PROJECT_CHARTER.md)
- [Normative Hierarchy](docs/NORMATIVE_HIERARCHY.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Current Project State](docs/CURRENT_PROJECT_STATE.md)
- [Roadmap](docs/ROADMAP.md)
- [Development Guide](docs/DEVELOPMENT.md)
- [Testing and Acceptance](docs/TESTING.md)
- [Determinism and Information Safety](docs/DETERMINISM_AND_INFORMATION_SAFETY.md)
- [Versioning and Compatibility](docs/VERSIONING_AND_COMPATIBILITY.md)
- [Glossary](docs/GLOSSARY.md)

Existing detailed evidence and contracts remain authoritative within their scope:

- `docs/contracts/`
- `docs/protocol/`
- `docs/observation/`
- `docs/m3/`
- `docs/m3_5/`
- `docs/adr/`
- `third_party/rules_bundle.lock.json`

## Third-party and licensing

The runtime rules stack includes components recorded as AGPL-3.0-or-later, and the pinned BabelCDB snapshot has an unresolved license record in this repository. Do not describe the complete project as MIT-only.

See [THIRD_PARTY.md](THIRD_PARTY.md) and `third_party/rules_bundle.lock.json`.
