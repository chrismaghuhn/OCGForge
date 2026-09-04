# OCGForge Current Project State

**Snapshot date:** 2026-09-04
**Repository baseline inspected:** `main` at `c0156a3451a7f8cc4495d544f7a34cab925e3c5a` (T5D merge; post-merge CI acceptance pending)
**Latest project checkpoint described here:** T5D merged; Task 5 tooling final pass pending post-merge CI run `33892943953` and independent supervisor review

This document is a summary. Detailed acceptance evidence remains in the milestone files.

## Executive status

OCGForge has a substantial deterministic environment foundation despite being a young repository.

The current strongest claim is:

> The locked Swordsoul Tenyi ML v1 vs. Salamangreat ML v1 matchup has repository-recorded M3/M3.5 acceptance evidence for rules-bundle identity, card/script/database resolution, required mechanics, decision handling, perspective-safe observations, deterministic execution, semantic-action replay, and complete fixed games.

The repository also records final acceptance for Episodic V2, Phase 3A/3B,
Phase 4A/4B/4C, and Phase 5. Phase 6 Tasks 1–4B are accepted and merged;
Task 5 contract freeze and T5A–T5C are final and merged, and T5D is merged at
`c0156a3451a7f8cc4495d544f7a34cab925e3c5a`. T5D main acceptance and the Task
5 tooling final pass remain pending post-merge CI review.

The project must **not** generalize that claim to arbitrary Yu-Gi-Oh! decks.

## Milestone summary

| Milestone | Repository state | Meaning |
| --- | --- | --- |
| M0 | **FINAL PASS** | deterministic pinned-core foundation, lifecycle, trace, and basic protocol/privacy gates |
| M1 | **FINAL PASS** | typed complete candidate protocol and continuation machinery; some global message families remain protocol-only or unsupported |
| M2 | **FINAL PASS** | perspective-safe `PlayerObservation`, canonical serialization/hash, and event/history projection |
| M2.1 | **FINAL PASS** | Xyz-material public-query limitation investigated and resolved by the M3.5 patchset |
| M3 | **FINAL PASS recorded** | locked fixed-matchup conformance and mechanics closure |
| M3.5 | **FINAL PASS recorded** | narrow public API hardening for Xyz material query + starting-player control |
| M4 | **FINAL PASS** | persistent-worker parallel-simulation foundation with fresh Release acceptance evidence; only accepted semantic-equivalent internal optimizations are integrated; no general ML-readiness or Phase 6 claim |
| Episodic V2 | **FINAL PASS** | public reset/step boundary, complete public domains, privacy, determinism, replay, and failure-closed lifecycle |
| Phase 3A / 3B | **FINAL PASS** | trusted trajectory, provenance, persistence, replay admission, receipts, and dataset identity |
| Phase 4A / 4B / 4C | **FINAL PASS** | public policy, Teacher, battle-sidecar, and frozen evaluation scope |
| Phase 5 | **FINAL PASS** | framework-neutral logical/encoded/batch model input plus admission-backed supervision samples |
| Phase 6 Tasks 1–4B | **FINAL / MERGED** | contract, data, model-input, provisional backend, and bounded smoke/recovery infrastructure |
| Task 5 contract freeze; T5A–T5C | **FINAL / MERGED** | evaluation contracts, codecs, offline evaluator, and frozen gameplay evaluator |
| T5D | **MERGED / POST-MERGE ACCEPTANCE PENDING** | public audit, first divergence, distribution shift, and derived report |

## M3/M3.5 recorded acceptance

The committed acceptance evidence reports:

- bundle ID `3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f`;
- format `TCG_ADVANCED_2026_05_18`;
- `DUEL_MODE_MR5 = 0x2E800`;
- fixed decks: exact 40 Main + 15 Extra for each player;
- 110 deck slots;
- 50 unique passcodes;
- 45/45 required fixed-matchup mechanics classified;
- 38 `ENGINE_VERIFIED`;
- 7 `PROTOCOL_VERIFIED`;
- 0 pending mechanics;
- 16/16 complete games;
- starting-player partitions `[0, 1]`;
- normal and mirrored deck-seat partitions;
- zero unsupported required decisions, retries, automatic decisions, truncation, and core errors in the fixed-game matrix;
- independent-process determinism and semantic-action replay evidence.

Recorded regression evidence also reports 85/85 CTest tests, 17/17 M3 Python tests, and 8/8 repository Python tests at the accepted checkpoint.

### Verification provenance

This status document is derived from repository content, GitHub history, and the
accepted Phase-5 machine-readable evidence. The current-state summary does not
replace the milestone artifacts or claim that this documentation refresh
reran every acceptance command.

## What is implemented

### Reproducible rules stack

The repository pins ocgcore, CardScripts, BabelCDB, API expectations, hashes, and patchset inputs through the rules-bundle lock and verification tools.

### Core lifecycle

`CoreHost` wraps duel lifecycle, callbacks, seeded configuration, engine processing, response submission, and public queries.

### Decision protocol

The protocol layer supports typed candidates and deterministic continuation state for complex decision families.

It has no global fixed candidate cap.

Unsupported or unproven decisions fail closed.

### Player observation

`ygo.player_observation.v1` provides a perspective-safe semantic state representation with canonical JSON serialization and SHA-256 observation identity.

### Privacy boundaries

The environment deliberately separates omniscient engine state from player-visible observations and uses redaction/omission for hidden identities.

### Trace and determinism

The repository contains trace contracts, gameplay determinism tests, and continuation-aware trace semantics.

### Fixed-matchup certification

M3 provides a large fixed-matchup evidence set rather than relying on “the duel ran” as proof of support.

### Episodic V2 and trusted trajectory

Episodic V2 is accepted as the public reset/step environment boundary with
complete ordered public candidate domains, perspective-safe observations,
deterministic identities, replay, admission, and fail-closed lifecycle
semantics. Phase 3A/3B is accepted above that boundary for trusted trajectory
records, provenance, immutable persistence, replay admission, receipts, and
dataset identity.

### Phase 4 policy and evaluation

Phase 4A/4B/4C is accepted for the defined public-policy, Teacher,
battle-sidecar, and frozen evaluation scopes. Those results do not expand the
fixed certified gameplay matchup into arbitrary-deck support.

### Phase 5 model-facing boundary

Phase 5 is accepted as a framework-neutral downstream representation:

```text
PublicEnvironmentObservation
+ complete ordered EnvironmentActionCandidate[]
    → LogicalModelInputV1
    → EncodedModelInputV1
    → ModelBatchLayoutV1
```

The accepted path also derives admission-backed
`ModelSupervisionSampleV1`. `public_action_key` remains selection/routing
identity; candidate ordinals are training-label metadata only. Padding,
bucketing, batch composition, and physical tensor details are not model-input
identity.

The committed Phase-5 evidence records `H_exec`
`3c99e86c487361fc4e0f5f12678b4867e59232b7`, `H_evidence`
`da3376fc2ab645377f9de2dd9fd6195c1aa8c081`, and fresh `163/163` native CTest
regression.

### Phase 6 evaluation tooling

Phase 6 now contains provisional framework execution and inference tooling,
the bounded Task4B CUDA smoke/recovery path, and the Task 5 evaluation stack.
Task 5 is implemented through T5D: T5A owns schemas/codecs/identities and job
manifests, T5B owns offline evaluation and deterministic slicing, T5C owns
the frozen gameplay evaluator, and T5D owns public audit, first divergence,
distribution shift, and deterministic derived reporting.

T5D branch review, PR review, and PR CI passed before the merge. P6-G15 is
supported by T5D but remains pending post-merge acceptance. The post-merge CI
run is `33892943953` on this `main` head; its acceptance remains with the
independent supervisor.

P6-G14 remains `NOT_RUN/BLOCKED_BY_MEANINGFUL_BASELINE`. P6-G15 is supported
by T5D but remains `SUPPORTED_PENDING_POST_MERGE_ACCEPTANCE`.

The Task4B checkpoint
`phase6_checkpoint.v1.62f4532a5e551886affbd65bc47f7645017dedf6c5ca3a0b7b87b4a978943327`
remains a bounded technical smoke artifact. It is not an accepted strategic
baseline, a playable policy, a converged model, or a backend decision.

## Important remaining limitations

### 1. General deck support is not certified

The M3 PASS is matchup-specific.

Adding a new deck can introduce:

- new card scripts;
- new engine messages;
- new decision-family shapes;
- new observation/event requirements;
- new public API gaps;
- new mechanics that have no evidence.

### 2. Global decision coverage is not equivalent to M3 closure

The M1 acceptance inventory records several families as protocol-verified without a stable real-engine fixture, including examples such as:

- `MSG_SELECT_YESNO`;
- `MSG_SELECT_POSITION`;
- `MSG_SORT_CHAIN`;
- `MSG_SELECT_UNSELECT_CARD`;
- `MSG_ANNOUNCE_RACE`;
- `MSG_ANNOUNCE_ATTRIB`;
- `MSG_ANNOUNCE_NUMBER`.

`MSG_ANNOUNCE_CARD` is explicitly `UNSUPPORTED_FAIL_CLOSED` in that inventory.

The fixed M3 decks may simply not require those global gaps.

### 3. Visible-event coverage is intentionally partial

The event projector supports a documented subset.

Unknown/deferred event families are omitted rather than guessed.

### 4. No general production board-construction API

Fixture setup helpers are test infrastructure.

### 5. No selected backend or meaningful BC baseline

Phase 5 defines logical and deterministic encoded model representations plus a
lossless ragged/padded execution layout. Phase 6 adds provisional framework
execution, inference, and evaluation tooling, but it does not select PyTorch or
JAX as the primary backend and does not provide a meaningful BC baseline.

The accepted Task4B checkpoint is limited to technical smoke behavior. It does
not establish strategic playability, convergence, Teacher parity, or gameplay
strength.

### 6. No strategically meaningful accepted BC baseline

No strategically meaningful accepted BC baseline exists yet. The repository
does not claim a backend winner, a general Yu-Gi-Oh! policy, RL, self-play,
league training, or a multi-deck trained policy.

### 7. M4 parallel-simulation foundation is finalized

M4 FINAL PASS is backed by repository-committed Release matrix, integrity,
equivalence, lifecycle, full-game, and soak evidence. Semantic equivalence is
validated through 64 workers; 16 workers is the recommended production
concurrency for the measured host.

This closes the parallel-simulation foundation only. It does not claim general
ML readiness or replace the later Phase 5 model-facing acceptance. M4.3.5 remains explicitly rejected; its negative
experiment evidence is retained without its production implementation.

### 8. Windows is the canonical CI path

The hosted integration workflow is Windows-native. The repository also has a Windows Zig fallback preset for local development.

### 9. BabelCDB licensing remains unresolved in repository records

The project should continue to describe this accurately rather than guessing a license.

## Repository shape

Key directories:

```text
include/ygo/core/          public core host types
include/ygo/protocol/      decision/candidate/continuation types
include/ygo/observation/   player observation types
include/ygo/trace/         trace/hash types
include/ygo/m3/            fixed-matchup policy
include/ygo/environment/   public episodic environment types
include/ygo/trajectory/    trusted trajectory and admission types
include/ygo/model/         framework-neutral model-facing types

src/core/
src/protocol/
src/observation/
src/trace/
src/m3/
src/environment/
src/trajectory/
src/model/

tests/core/
tests/protocol/
tests/observation/
tests/privacy/
tests/determinism/
tests/trace/
tests/m3/
tests/m3_5/
tests/episodic/
tests/model/

fixtures/
tools/
third_party/
docs/
```

## Tracker state

Issue/PR state is external and time-dependent; consult GitHub for current
tracker status. It is not a repository invariant.

## Immediate documentation/maintenance priorities

1. complete independent post-merge acceptance of T5D and the Task 5 tooling;
2. require explicit Task 6 authorization before any PyTorch/JAX bakeoff;
3. require an accepted Task 6 decision and explicit authorization before Task 7;
4. create issues before broadening certified deck/card scope;
5. preserve the distinction between global protocol coverage and deck-specific
   conformance and keep model-facing representations downstream of the public
   environment and trusted trajectory boundaries.
