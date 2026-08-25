# AGENTS.md — OCGForge

This file is the working contract for coding agents and automated contributors operating in this repository.

Read it before changing code, tests, contracts, fixtures, rules-bundle inputs, or acceptance evidence.

## 1. Mission

OCGForge is a deterministic Yu-Gi-Oh! simulation and game-AI research environment.

The project is not trying to reach ML throughput by weakening correctness. Its job is to establish a trustworthy engine boundary first.

Priority order:

```text
correctness
→ determinism
→ information safety
→ complete legal decision representation
→ replayability / auditability
→ maintainability
→ performance
→ ML scale
```

## 2. Current maturity

The accepted repository baseline is M3/M3.5 fixed-deck conformance and narrow ocgcore API hardening.

Do not silently upgrade this claim into:

- full Yu-Gi-Oh! support;
- arbitrary-deck certification;
- complete engine-message coverage;
- a production ML environment;
- a stable checkpoint/fork API;
- performance or vectorization readiness.

The repository-recorded M3/M3.5 acceptance evidence is historical evidence. A new agent may report a gate as freshly passing only if it actually executes the relevant command successfully in its current environment.

Never report `PASS` for a command that was not run.

## 3. Read order before architectural work

At minimum read:

1. `README.md`
2. `docs/PROJECT_CHARTER.md`
3. `docs/NORMATIVE_HIERARCHY.md`
4. `docs/CURRENT_PROJECT_STATE.md`
5. `docs/ARCHITECTURE.md`
6. `docs/ROADMAP.md`
7. relevant accepted ADRs
8. relevant versioned contracts
9. relevant coverage/acceptance evidence
10. the code and tests being changed

Historical files in `docs/superpowers/plans/` and `docs/superpowers/specs/` are useful provenance, but they do not override accepted contracts, current code, or acceptance evidence.

## 4. Authority boundaries

For game legality and current engine semantics, the pinned rules bundle is authoritative.

For the OCGForge public environment boundary, versioned contracts are authoritative.

For fixed-milestone claims, machine-readable coverage/evidence plus executable tests are authoritative.

Human-readable summaries must derive from those sources rather than inventing a parallel truth.

See `docs/NORMATIVE_HIERARCHY.md`.

## 5. Rules-bundle discipline

Do not:

- replace exact dependency pins with floating branches;
- mutate the ignored cached upstream base checkout;
- depend on sibling repositories;
- introduce symlink-dependent runtime resolution;
- silently change CardScripts, BabelCDB, ocgcore, the OCG API expectation, or patch ordering;
- edit generated lock/evidence artifacts by hand when a generator is the source.

The canonical rules identity is part of determinism and conformance.

Any rules-bundle change requires an explicit migration with updated evidence.

## 6. Fail-closed protocol rule

A player-facing engine decision must never be guessed, auto-selected, truncated, or replaced by a heuristic subset merely to keep a duel running.

Required behavior:

- decode the engine message;
- expose the complete legal semantic candidate set for supported families;
- preserve meaningful ordering;
- use deterministic semantic identities;
- represent combinatorial choices through adapter-local continuation when needed;
- submit exactly one final engine response for the original engine message;
- fail with a structured diagnostic when support or completeness is not proven.

Unsupported is preferable to plausible-but-unproven.

## 7. Continuation invariant

During an adapter-local continuation:

- the OCG engine remains paused;
- intermediate actions do not call `OCG_DuelSetResponse`;
- intermediate actions do not call `OCG_DuelProcess`;
- stale continuation/action identities fail closed;
- a terminal continuation emits the exact final response bytes;
- only that terminal action may advance the engine.

Do not flatten a combinatorial legal space into an arbitrary fixed action cap.

## 8. Information-safety rule

`CoreHost` and the pinned engine may be omniscient.

An agent, teacher, search adapter, or model adapter must consume perspective-safe state through `PlayerObservation`, not raw omniscient engine queries.

Never expose or derive hidden identity from:

- opponent Main Deck order or entries;
- opponent hidden Hand identities;
- opponent face-down Extra Deck identities;
- opponent face-down field identity unless the visibility contract permits it;
- raw engine pointers or addresses;
- persistent IDs that survive knowledge-destroying transitions;
- debug-only omniscient query payloads.

Do not add beliefs, probabilities, inferred archetypes, or reconstructed hidden hands to the authoritative observation schema.

## 9. Knowledge-destroying transitions

Shuffle/randomization boundaries are semantic privacy boundaries.

After a hidden card moves through a knowledge-destroying transition:

- do not preserve a physical-card identity into hidden state;
- do not treat a later slot locator as the same physical object;
- do not reuse hidden engine identity as an observation locator.

## 10. Determinism rule

Authoritative outputs must not depend on:

- pointer addresses;
- object layout;
- unordered-container iteration;
- wall-clock time;
- random UUIDs;
- thread scheduling;
- host-specific filesystem paths;
- compiler-specific incidental ordering.

If ordering is semantic, preserve it explicitly.

If ordering is non-semantic, define a stable canonical sort.

## 11. Hash discipline

Hashes are meaningful only together with their schema/domain and canonical encoding.

Do not reuse a digest field for a different semantic input without versioning the contract.

Cross-process gameplay determinism must compare semantic gameplay identities, not toolchain/provenance hashes that legitimately include build-specific context.

## 12. Public API hardening patches

The current repository uses an immutable pinned ocgcore base plus the ordered repository patchset `ocgforge.ocgcore.api_hardening.v1`.

Treat the patches as explicit canonical inputs, not invisible local modifications.

Do not:

- patch the cached base in place;
- broaden the patches beyond the documented capability;
- present a repository patch as already-upstream behavior;
- change the public API claim without tests and documentation.

## 13. Scope of test-only fixture setup

`CoreHost::load_fixture_script` and `CoreHost::load_fixture_card` are conformance/test infrastructure.

Do not accidentally turn them into a general runtime board-construction API or a hidden-information bypass.

## 14. Tests and acceptance

A behavioral change should normally include the narrowest applicable evidence:

- unit test for local semantics;
- protocol oracle/fail-closed test for decision changes;
- paired-world/privacy test for observation changes;
- independent-process test for determinism-sensitive changes;
- fixed-game regression for matchup closure changes;
- generated coverage/evidence update where applicable.

Prefer targeted failure diagnostics over broad retries.

## 15. Generated evidence

Machine-readable inventories under areas such as `docs/m3/`, `docs/protocol/`, and `docs/observation/` may be generated or validated by repository tooling.

Do not hand-edit a generated artifact merely to make a gate green.

Change the source, generator, or evidence-producing test and regenerate.

## 16. Documentation rule

When behavior changes, update the narrowest normative document that owns the behavior.

Do not copy the same normative rule into many milestone documents.

Use:

- contracts for stable public semantics;
- ADRs for architectural decisions;
- coverage/acceptance files for evidence;
- `CURRENT_PROJECT_STATE.md` for the present summary;
- `ROADMAP.md` for future intent.

## 17. Review classification

Classify findings by impact:

- **BLOCKER** — correctness, determinism, information leak, incomplete legal action set presented as complete, replay/contract break, invalid acceptance claim;
- **MAJOR** — substantial maintainability, compatibility, test, or architecture defect likely to cause incorrect future work;
- **MINOR** — localized quality issue without current semantic risk;
- **NOTE** — optional cleanup or future improvement.

## 18. Definition of done

A change is not done merely because it compiles.

For the changed scope:

- behavior is specified or intentionally internal;
- tests prove the important semantics;
- privacy implications are checked;
- deterministic ordering/identity is explicit;
- failure behavior is explicit;
- relevant evidence is regenerated;
- documentation is consistent;
- commands claimed as passing were actually executed.

## 19. Final agent rule

When uncertain whether an engine behavior, legal domain, visibility fact, or deterministic identity is proven, do not guess.

Investigate, narrow the claim, or fail closed.
