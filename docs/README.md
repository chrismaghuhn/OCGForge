# OCGForge Documentation Index

OCGForge has three kinds of documentation:

1. **project-level governance and architecture** — where the project is going and how its boundaries fit together;
2. **versioned contracts and ADRs** — stable semantics and accepted architectural decisions;
3. **milestone evidence** — what a particular verified slice actually proved.

This index keeps those layers separate.

Current repository status: M0–M4, Episodic V2, Phase 3A/3B,
Phase 4A/4B/4C, and Phase 5 are **FINAL PASS** for their defined scopes.
Phase 6 Task 1 is **CURRENT / AUTHORIZED** as a documentation-only contract
freeze; Phase-6 implementation and training have not started.

## Start here

| Document | Purpose |
| --- | --- |
| [PROJECT_CHARTER.md](PROJECT_CHARTER.md) | Mission, priorities, scope, non-goals |
| [NORMATIVE_HIERARCHY.md](NORMATIVE_HIERARCHY.md) | Which source wins when documents disagree |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Component boundaries and data flow |
| [CURRENT_PROJECT_STATE.md](CURRENT_PROJECT_STATE.md) | What is actually verified now |
| [ROADMAP.md](ROADMAP.md) | Completed milestones and future direction |
| [DEVELOPMENT.md](DEVELOPMENT.md) | Local workflow and repository layout |
| [TESTING.md](TESTING.md) | Verification layers and commands |
| [DETERMINISM_AND_INFORMATION_SAFETY.md](DETERMINISM_AND_INFORMATION_SAFETY.md) | Cross-cutting invariants |
| [VERSIONING_AND_COMPATIBILITY.md](VERSIONING_AND_COMPATIBILITY.md) | Schema, bundle, trace, and evidence evolution |
| [GLOSSARY.md](GLOSSARY.md) | Project terminology |

## Accepted architecture decisions

- [ADR-0001 — Modern OCG adapter](adr/ADR-0001-modern-ocg-adapter.md)
- [ADR-0002 — Shared semantic EpisodeDriver for episodic environment](adr/ADR-0002-episodic-environment.md) — **Accepted**
- [ADR-0003 — Episodic V1 prerequisite identities](adr/ADR-0003-episodic-v1-normative-prerequisites.md) — **Accepted**
- [ADR-0004 — Perspective-safe public episodic action identity](adr/ADR-0004-perspective-safe-episodic-action-identity.md) — **Accepted**
- [ADR-0005 — Trusted trajectory core above EpisodicEnvironment V2](adr/ADR-0005-trusted-trajectory-core.md) — **Accepted Phase 3A architectural authority**
- [ADR-0006 — Framework-neutral model-facing candidate-scoring boundary](adr/ADR-0006-model-facing-candidate-scoring-adapter.md) — **Accepted Phase 5 contract freeze**
- [ADR-0007 — Phase-6 Behavior Cloning boundary](adr/ADR-0007-phase6-behavior-cloning-boundary.md) — **Accepted Phase 6 Task 1 contract freeze**
- [ADR index and policy](adr/README.md)

## Versioned contracts

- [Decision protocol v1](contracts/decision-protocol-v1.md)
- [Engine trace v1](contracts/engine-trace-v1.md)
- [Engine trace v2](contracts/engine-trace-v2.md)
- [Player observation v1](contracts/player-observation-v1.md)
- [Perspective-safe public environment observation v1](contracts/public-environment-observation-v1.md) — **Accepted and implemented public projection**
- [Player view v1](contracts/player-view-v1.md)
- [Episodic environment v1](contracts/episodic-environment-v1.md) — **Accepted historical predecessor**
- [Perspective-safe public action identity v1](contracts/public-action-identity-v1.md) — **Accepted and implemented prerequisite**
- [Episodic environment v2](contracts/episodic-environment-v2.md) — **Accepted and implemented public-identity successor**
- [Trusted trajectory core v1](contracts/trusted-trajectory-v1.md) — **Accepted Phase-3A logical trajectory contract**
- [Policy provenance v1](contracts/policy-provenance-v1.md) — **Accepted Phase-3A collection-provenance contract; not learner-visible data**
- [Candidate trajectory shard v1](contracts/trajectory-shard-v1.md) — **Accepted Phase-3B physical contract**
- [Restricted collection evidence bundle v1](contracts/restricted-collection-evidence-bundle-v1.md) — **Accepted Phase-3B physical contract**
- [Admission receipt v1](contracts/admission-receipt-v1.md) — **Accepted Phase-3B admission contract**
- [Dataset identity v1](contracts/dataset-identity-v1.md) — **Accepted Phase-3B identity contract**
- [Dataset manifest v1](contracts/dataset-manifest-v1.md) — **Accepted Phase-3B physical contract**

A contract is not a milestone completion claim. It defines semantics for the surface that uses that version. Accepted contract semantics do not imply that an implementation or acceptance milestone has passed.

## Episodic environment design and acceptance

- [Episodic V1 acceptance plan](episodic/EPISODIC_V1_ACCEPTANCE.md)
- [2026-08-26 episodic research decision record](research/episodic/EPISODIC_CONTRACT_RESEARCH_DECISION_2026-08-26.md)

These documents preserve the accepted Episodic V2 design and its historical
acceptance record. The implementation and final acceptance are complete; they
do not imply general ML readiness or arbitrary-deck support.

## Trusted trajectory design and acceptance

- [Phase-3A trusted trajectory acceptance matrix](trajectory/PHASE3A_ACCEPTANCE.md) — **Historical accepted Phase-3A evidence**
- [Phase-3B persistence and replay-admission acceptance matrix](trajectory/PHASE3B_ACCEPTANCE.md) — **Historical accepted Phase-3B evidence**

The PR #17 research record is design provenance only. ADR-0005 and
`trusted_trajectory.v1` are the accepted Phase-3A logical trajectory
authority. Phase 3B adds the accepted physical persistence, replay/admission,
receipt, and dataset-manifest contracts layered above that logical contract.
Phase 5 consumes those public/admitted values without changing the trusted
trajectory schema.

## Phase 5 model-facing acceptance

- [Phase-5 model contract](p5/P5_MODEL_CONTRACT.md)
- [Phase-5 acceptance plan](p5/P5_ACCEPTANCE_PLAN.md) — **frozen historical plan**
- [Phase-5 final acceptance evidence](p5/P5_ACCEPTANCE_EVIDENCE.md) — **FINAL PASS**

The accepted framework-neutral path is:

```text
PublicEnvironmentObservation
+ complete ordered EnvironmentActionCandidate[]
    → LogicalModelInputV1
    → EncodedModelInputV1
    → ModelBatchLayoutV1
    + admission-backed ModelSupervisionSampleV1
```

`public_action_key` remains selection/routing identity, candidate ordinals are
derived training-label metadata, and physical batch layout is excluded from
model-input identity. Phase 6 Task 1 is the current documentation-only
contract freeze; no learner, neural network, framework implementation, RL, or
self-play implementation has started.

Accepted evidence records `H_exec=3c99e86c487361fc4e0f5f12678b4867e59232b7`,
`H_evidence=da3376fc2ab645377f9de2dd9fd6195c1aa8c081`, and fresh `163/163`
native CTest regression.

## Phase 6 Behavior Cloning contract freeze

Task 1 freezes the first framework-neutral BC boundary. It keeps the
Environment as the legality and complete-candidate authority, accepts only
admitted DatasetManifest membership, partitions by episode/duel identity, and
binds training provenance, canonical checkpoint export, deterministic
inference, offline imitation, frozen gameplay, and first-divergence evaluation
without adding a learner or selecting PyTorch/JAX.

- [Phase-6 BC contract](p6/P6_BC_CONTRACT.md) — current Task-1 contract
- [Phase-6 dataset and split contract](p6/P6_DATASET_AND_SPLIT_CONTRACT.md)
- [Phase-6 checkpoint and inference contract](p6/P6_CHECKPOINT_AND_INFERENCE_CONTRACT.md)
- [Phase-6 evaluation plan](p6/P6_EVALUATION_PLAN.md) — future gates remain `NOT_RUN`
- [Phase-6 implementation plan](p6/P6_IMPLEMENTATION_PLAN.md) — later tasks remain `NOT AUTHORIZED`

Task 1 does not authorize neural networks, training, checkpoint generation,
ML dependencies, Project Ignis/EDOPro, RL, self-play, broader decks, or Phase
7. See [ADR-0007](adr/ADR-0007-phase6-behavior-cloning-boundary.md) for the
architecture rationale.

## Decision-protocol evidence

- [Decision coverage](protocol/DECISION_COVERAGE.md)
- [M1 acceptance matrix](protocol/M1_ACCEPTANCE_MATRIX.md)
- `protocol/decision_coverage.json`

## Observation evidence

- [Observation field coverage](observation/OBSERVATION_FIELD_COVERAGE.md)
- [Visible event coverage](observation/EVENT_COVERAGE.md)
- [M2 fixtures](observation/M2_FIXTURES.md)
- [M2.1 Xyz API investigation](observation/M2_1_XYZ_API_INVESTIGATION.md)
- `observation/observation_field_coverage.json`
- `observation/event_coverage.json`

## M3 fixed-matchup conformance

- [M3 acceptance matrix](m3/M3_ACCEPTANCE_MATRIX.md)
- [Fixed matchup](m3/FIXED_MATCHUP.md)
- [Card compatibility](m3/CARD_COMPATIBILITY.md)
- [Mechanics coverage](m3/MECHANICS_COVERAGE.md)
- [Rules mode audit](m3/RULE_MODE_AUDIT.md)
- [Public API gaps](m3/PUBLIC_API_GAPS.md)
- machine-readable JSON companions in `m3/`

## M3.5 API hardening

- [M3.5 acceptance](m3_5/M3_5_ACCEPTANCE.md)
- [Public API hardening](m3_5/PUBLIC_API_HARDENING.md)
- `m3_5/m35_acceptance.json`

## M4 and Phase 4 acceptance

- [M4 final acceptance](m4/M4_FINAL.md) — **FINAL PASS**
- [Phase-4A acceptance](p4a/P4A_ACCEPTANCE.md) — **FINAL PASS**
- [Phase-4B acceptance](p4b/P4B_ACCEPTANCE.md) — **FINAL PASS**
- [Phase-4C acceptance](p4c/P4C_ACCEPTANCE.md) — **FINAL PASS**

These are milestone acceptance records for their defined scopes. Historical
benchmark values and evidence remain unchanged.

## Audits and historical implementation material

`audits/` contains reference audits.

`superpowers/specs/` and `superpowers/plans/` contain implementation-era design/provenance material. They are useful for history but are not the highest current authority after behavior is implemented and accepted.

See [NORMATIVE_HIERARCHY.md](NORMATIVE_HIERARCHY.md).
