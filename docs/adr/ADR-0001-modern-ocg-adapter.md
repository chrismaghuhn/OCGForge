# ADR-0001: Build a modern adapter over the pinned OCG C API

## Status

Accepted: option B — build a new adapter and use `sbl1996/ygo-agent` only as a read-only reference.

## Context

M0 needs a correctness-focused foundation for deterministic Yu-Gi-Oh! environment work. The rules bundle is pinned to the following revisions:

| Component | Repository | Revision | Role |
| --- | --- | --- | --- |
| Reference implementation | `sbl1996/ygo-agent` | `dbf5142d49aab2e6beb4150788d4fffec39ae3e5` | Read-only audit/reference only |
| Rules engine | `edo9300/ygopro-core` | `9a0c558c2d686542f7914a6d529fd7aa57746aed` | Runtime dependency |
| Card scripts | `ProjectIgnis/CardScripts` | `f337c87018ca723c1aded5143e616bb649555273` | Runtime data dependency |
| Card database | `ProjectIgnis/BabelCDB` | `89ad6837b0766a52984d8c715a7d5d4f8447946b` | Runtime data dependency |

The new project must expose a narrow, typed, fail-closed protocol boundary around the public OCG C API. It must not modify upstream source, depend on sibling checkouts, or present incomplete legal action sets as complete.

## Alternatives considered

### A. Fork and continue `ygoenv`

This would minimize initial adapter work by retaining the existing Python/engine integration. It is rejected because the reference implementation has a stale `main` branch, an unfinished EDOPro adapter, unversioned and brittle build integration, silent legal-action truncation, unsupported or automatically answered decisions, unversioned observation and checkpoint contracts, insufficient correctness tests, and large duplicated adapter files. It would also make continued upstream OCG-core evolution harder to track cleanly.

### B. Build a new adapter and use `ygoenv` as a reference

This creates a small C++ RAII host around the public API, a typed candidate protocol, an explicit player-view projection, and versioned deterministic traces. The old implementation remains useful for comparative audit and vocabulary discovery without becoming a semantic dependency.

This is the accepted option because it gives M0 a testable boundary where ownership, callback failures, legal candidate completeness, hidden information, and determinism are explicit. It also keeps the engine pin and rule data independently verifiable while allowing the upstream OCG core to evolve in later bundles.

### C. Complete the existing `ygoenv` EDOPro port in place

This could eventually reduce migration effort for code that already expects the ygoenv API. It is rejected for M0 because it preserves the same unfinished adapter and build assumptions, would require broad changes before correctness can be measured, and would make it difficult to prove whether a behavior is inherited from the old implementation or deliberately specified by the new environment contract.

## Decision

Choose **B — new adapter, selective reference reuse**.

The project will use CMake for project code and a repository-local, ignored dependency cache populated from exact commits. `CoreHost` owns the OCG duel lifecycle, callbacks, seeds, processing, message retrieval, response submission, and public query access. The protocol layer translates only the controlled fixture slice into typed `DecisionRequest` and `ActionCandidate` values. Every other interactive message is a structured unsupported-decision failure.

The public observation boundary requires a player perspective and redacts opponent hidden identities. Original card passcodes remain the environment representation; model vocabulary mapping is out of scope for M0.

## Consequences

This costs more initial adapter work but provides a testable, versioned, and maintainable correctness boundary. It also means M0 intentionally supports only a small fixture slice and must remain honest about terminal-duel coverage when the pinned core emits an unsupported decision.

The current OCG core is AGPL-3.0-or-later. The current CardScripts repository identifies itself as AGPL-3.0-or-later. BabelCDB licensing must be recorded as unresolved unless the pinned snapshot provides an explicit license notice. The complete project must not be represented as MIT-only.

## Verification references

- [Pinned OCG core](https://github.com/edo9300/ygopro-core/tree/9a0c558c2d686542f7914a6d529fd7aa57746aed)
- [Pinned ygo-agent reference](https://github.com/sbl1996/ygo-agent/tree/dbf5142d49aab2e6beb4150788d4fffec39ae3e5)
- [Pinned CardScripts](https://github.com/ProjectIgnis/CardScripts/tree/f337c87018ca723c1aded5143e616bb649555273)
- [Pinned BabelCDB](https://github.com/ProjectIgnis/BabelCDB/tree/89ad6837b0766a52984d8c715a7d5d4f8447946b)
