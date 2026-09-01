# OCGForge

OCGForge is a deterministic Yu-Gi-Oh! simulation and game-AI research environment built around a narrow C++ adapter to a pinned OCG rules stack.

The project is correctness-first. It aims to make engine interaction, legal
decisions, player-visible observations, hidden-information handling,
deterministic traces, model-facing representations, and conformance evidence
explicit and reproducible before adding learner or training code.

> **Current maturity:** M0–M4, Episodic V2, Phase 3A/3B, Phase 4A/4B/4C,
> and Phase 5 are accepted final checkpoints for their defined scopes. The
> certified gameplay slice remains fixed-matchup and is not general all-deck
> Yu-Gi-Oh! support.

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
- two narrow repository-versioned ocgcore API-hardening patches;
- the accepted Episodic V2 public environment and trusted trajectory/admission
  path;
- a framework-neutral `ygo::model` path from public observations and complete
  candidate domains through logical input, encoded input, and ragged/padded
  batch layout;
- admission-backed `ModelSupervisionSampleV1` derivation.

OCGForge intentionally does **not** currently claim:

- full Yu-Gi-Oh! card or deck compatibility;
- a stable general-purpose Gym-style API;
- checkpoint/fork support;
- high-throughput vectorized simulation;
- a learned/neural policy;
- a search system;
- a training stack;
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

Documentation baseline: `main` at post-Phase-5 merge `6c238addb353fc0bf7e68c6dfdc6f19b36c84bf4` (2026-09-01).

The current accepted milestone sequence is:

- M0–M4: **FINAL PASS**;
- Episodic V2: **FINAL PASS**;
- Phase 3A / 3B: **FINAL PASS**;
- Phase 4A / 4B / 4C: **FINAL PASS**;
- Phase 5: **FINAL PASS**.

Phase 5 is documented by the [model contract](docs/p5/P5_MODEL_CONTRACT.md)
and [final acceptance evidence](docs/p5/P5_ACCEPTANCE_EVIDENCE.md). Its
public model-facing path is:

```text
PublicEnvironmentObservation
  + complete ordered EnvironmentActionCandidate[]
    → LogicalModelInputV1
    → EncodedModelInputV1
    → ModelBatchLayoutV1
```

The Phase-5 supervision path derives `ModelSupervisionSampleV1` only from an
admission-backed trusted trajectory record. Candidate ordinals remain training
labels, while `public_action_key` remains selection/routing identity.

Phase 6 is the next milestone. It has not started.

The accepted Phase-5 evidence records `H_exec=3c99e86c487361fc4e0f5f12678b4867e59232b7`,
`H_evidence=da3376fc2ab645377f9de2dd9fd6195c1aa8c081`, and a fresh `163/163`
CTest regression.

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
is not a general ML-readiness or Phase 6 claim. M4.3.5 remains a documented
rejected experiment, while accepted internal equivalence-preserving work is
recorded separately in `docs/m4/M4_FINAL.md`.

These values are historical M3/M3.5 acceptance evidence committed to the
repository. They are not a claim that this documentation refresh re-ran those
milestone suites.

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
 EpisodicEnvironment V2
 public observation +
 complete ordered candidates
              |
              v
 PublicEnvironmentObservation
 + complete ordered candidates
          |                         |
          | direct model input      | trajectory source values
          v                         v
   +--------------+          +------------------+
   | ygo::model   |          | ygo::trajectory  |
   | Logical      |          | trusted records |
   | → Encoded    |          | replay/admission|
   | → Batch      |          +--------+---------+
   | Supervision  |                   |
   | Sample       |<------------------+
   +------+-------+       admitted record
          |
          v
     Phase 6 adapters
```

The logical/encoded model-input path consumes the public observation and
complete candidate vector directly from EpisodicEnvironment V2. The trusted
trajectory/admission path is a separate branch used when deriving
admission-backed supervision samples; it is not a second model-input authority.

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
- [Phase 5 model contract](docs/p5/P5_MODEL_CONTRACT.md)
- [Phase 5 acceptance evidence](docs/p5/P5_ACCEPTANCE_EVIDENCE.md)

Existing detailed evidence and contracts remain authoritative within their scope:

- `docs/contracts/`
- `docs/protocol/`
- `docs/observation/`
- `docs/trajectory/`
- `docs/p4a/`, `docs/p4b/`, `docs/p4c/`
- `docs/p5/`
- `docs/m3/`
- `docs/m3_5/`
- `docs/adr/`
- `third_party/rules_bundle.lock.json`

## Third-party and licensing

The runtime rules stack includes components recorded as AGPL-3.0-or-later, and the pinned BabelCDB snapshot has an unresolved license record in this repository. Do not describe the complete project as MIT-only.

See [THIRD_PARTY.md](THIRD_PARTY.md) and `third_party/rules_bundle.lock.json`.
