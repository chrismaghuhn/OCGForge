# OCGForge

OCGForge is a deterministic Yu-Gi-Oh! simulation and game-AI research environment built around a narrow C++ adapter to a pinned OCG rules stack.

The project is correctness-first. It aims to make engine interaction, legal
decisions, player-visible observations, hidden-information handling,
deterministic traces, model-facing representations, and conformance evidence
explicit and reproducible before adding learner or training code.

> **Current maturity:** M0–M4, Episodic V2, Phase 3A/3B, Phase 4A/4B/4C,
> Phase 5, and Phase 6 Tasks 1–4B are accepted final checkpoints for their
> defined scopes. Task 5 evaluation tooling is implemented through T5D and
> merged; post-merge acceptance of T5D and the Task 5 tooling final pass are
> pending supervisor review. Task 6 and Task 7 remain unauthorized. The
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
- Phase 6 evaluation tooling through T5D: offline metrics, frozen gameplay
  evidence, first-divergence audit, distribution-shift comparison, and a
  deterministic derived report.

OCGForge intentionally does **not** currently claim:

- full Yu-Gi-Oh! card or deck compatibility;
- a stable general-purpose Gym-style API;
- checkpoint/fork support;
- high-throughput vectorized simulation;
- a learned/neural policy;
- a search system;
- a training stack;
- a strategically meaningful accepted BC baseline;
- a selected PyTorch/JAX backend;
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

Documentation baseline: `main` at post-T5D merge
`c0156a3451a7f8cc4495d544f7a34cab925e3c5a` (2026-09-04). T5D main
acceptance remains pending post-merge CI review.

The current accepted milestone sequence is:

- M0–M4: **FINAL PASS**;
- Episodic V2: **FINAL PASS**;
- Phase 3A / 3B: **FINAL PASS**;
- Phase 4A / 4B / 4C: **FINAL PASS**;
- Phase 5: **FINAL PASS**.
- Phase 6 Tasks 1–4B: **FINAL / MERGED**;
- Task 5 contract freeze and T5A–T5C: **FINAL / MERGED**;
- T5D: **MERGED / POST-MERGE ACCEPTANCE PENDING**.

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

Phase 6 is active. Tasks 1–4B are accepted and merged. Task 5 evaluation
tooling is implemented through T5D and merged at
`c0156a3451a7f8cc4495d544f7a34cab925e3c5a`; T5D main acceptance and the Task
5 tooling final pass are pending post-merge CI review. Task 6 remains the
PyTorch/JAX backend bakeoff, and Task 7 remains the first meaningful
feed-forward BC baseline; neither task is authorized.

The accepted Task4B checkpoint,
`phase6_checkpoint.v1.62f4532a5e551886affbd65bc47f7645017dedf6c5ca3a0b7b87b4a978943327`,
is a bounded technical smoke artifact. It is not a meaningful BC baseline,
playable policy, converged model, or strategic-strength claim.

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
- [Phase 6 implementation plan](docs/p6/P6_IMPLEMENTATION_PLAN.md)
- [Phase 6 Task5 evaluation execution contract](docs/p6/P6_TASK5_EVALUATION_EXECUTION_CONTRACT.md)
- [Phase 6 Task5 execution plan](docs/p6/task5/task5_execution_plan.v1.json)
- [Phase 6 Task4A numeric/provenance contract](docs/p6/P6_TASK4A_NUMERIC_AND_PROVENANCE_CONTRACT.md)
- [Phase 6 Task4B acceptance/recovery contract](docs/p6/P6_TASK4B_ACCEPTANCE_RECOVERY_V1.md)

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
