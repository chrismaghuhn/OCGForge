# OCGForge Documentation Index

OCGForge has three kinds of documentation:

1. **project-level governance and architecture** — where the project is going and how its boundaries fit together;
2. **versioned contracts and ADRs** — stable semantics and accepted architectural decisions;
3. **milestone evidence** — what a particular verified slice actually proved.

This index keeps those layers separate.

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
- [ADR index and policy](adr/README.md)

## Proposed architecture decisions

- [ADR-0002 — Shared semantic EpisodeDriver for episodic environment](adr/ADR-0002-episodic-environment.md) — **Proposed; not yet architectural authority**

## Versioned contracts

- [Decision protocol v1](contracts/decision-protocol-v1.md)
- [Engine trace v1](contracts/engine-trace-v1.md)
- [Engine trace v2](contracts/engine-trace-v2.md)
- [Player observation v1](contracts/player-observation-v1.md)
- [Player view v1](contracts/player-view-v1.md)
- [Episodic environment v1](contracts/episodic-environment-v1.md) — **Proposed; bound to ADR-0002 review/acceptance**

A contract is not a milestone completion claim. It defines semantics for the surface that uses that version. Proposed contracts are not normative until their owning architectural decision is accepted.

## Episodic environment design and acceptance

- [Episodic V1 acceptance plan](episodic/EPISODIC_V1_ACCEPTANCE.md)
- [2026-08-26 episodic research decision record](research/episodic/EPISODIC_CONTRACT_RESEARCH_DECISION_2026-08-26.md)

These documents define proposed post-M4 work only. They do not claim episodic implementation, ML readiness, trajectory support, or a new milestone PASS.

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

## Audits and historical implementation material

`audits/` contains reference audits.

`superpowers/specs/` and `superpowers/plans/` contain implementation-era design/provenance material. They are useful for history but are not the highest current authority after behavior is implemented and accepted.

See [NORMATIVE_HIERARCHY.md](NORMATIVE_HIERARCHY.md).
