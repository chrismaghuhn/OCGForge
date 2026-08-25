# OCGForge Current Project State

**Snapshot date:** 2026-08-25
**Repository baseline inspected:** `main` at `7bcb907f09996ddb65471b62b8d6e045ff02eb6e`
**Latest project checkpoint described here:** PR #3 — M4 parallel-simulation foundation; baseline acceptance pending

This document is a summary. Detailed acceptance evidence remains in the milestone files.

## Executive status

OCGForge has a substantial deterministic environment foundation despite being a young repository.

The current strongest claim is:

> The locked Swordsoul Tenyi ML v1 vs. Salamangreat ML v1 matchup has repository-recorded M3/M3.5 acceptance evidence for rules-bundle identity, card/script/database resolution, required mechanics, decision handling, perspective-safe observations, deterministic execution, semantic-action replay, and complete fixed games.

The project must **not** generalize that claim to arbitrary Yu-Gi-Oh! decks.

## Milestone summary

| Milestone | Repository state | Meaning |
| --- | --- | --- |
| M0 | established foundation | deterministic pinned-core spike, lifecycle, trace, basic protocol/privacy gates |
| M1 | protocol foundation implemented | typed complete candidate protocol and continuation machinery; some global message families remain protocol-only or unsupported |
| M2 | observation foundation implemented | perspective-safe `PlayerObservation`, canonical serialization/hash, event/history projection |
| M2.1 | investigated then resolved by M3.5 | individual Xyz-material public-query limitation identified; later fixed through repository patchset |
| M3 | **FINAL PASS recorded** | locked fixed-matchup conformance and mechanics closure |
| M3.5 | **FINAL PASS recorded** | narrow public API hardening for Xyz material query + starting-player control |
| M4 | **foundation implemented; baseline acceptance pending** | persistent-worker parallel-simulation foundation and benchmark/audit handoff; no optimization completion claim |

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

This status document was prepared from repository content and GitHub history.

The documentation preparation did **not** execute the build or test suite. Therefore the values above are described as *repository-recorded acceptance evidence*, not as a newly verified local run.

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

### 5. No authoritative ML tensor schema

The authoritative contracts intentionally use variable-length semantic data.

A fixed tensor/action vocabulary should be a downstream versioned adapter.

### 6. No ML implementation

The repository describes itself as a game-AI research environment but contains no learning algorithm or training stack at the inspected baseline.

### 7. M4 baseline acceptance is pending

PR #3 adds the parallel-simulation foundation and baseline report generator.
The committed baseline remains acceptance pending because the clean checkout
does not contain the matrix and acceptance artifacts required to verify a PASS.

No optimization-completion, general ML-readiness, or M5 claim should be
inferred from this foundation checkpoint.

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

src/core/
src/protocol/
src/observation/
src/trace/
src/m3/

tests/core/
tests/protocol/
tests/observation/
tests/privacy/
tests/determinism/
tests/trace/
tests/m3/
tests/m3_5/

fixtures/
tools/
third_party/
docs/
```

## Tracker state at this snapshot

No open GitHub Issues were returned during the 2026-08-20 repository inspection.

This is a dated tracker observation, not a project invariant.

## Immediate documentation/maintenance priorities

1. keep this current-state document aligned with accepted evidence;
2. treat M4 as a new milestone with explicit performance methodology rather than ad-hoc optimization;
3. create issues before broadening certified deck/card scope;
4. add ADRs for future environment/checkpoint/vectorization architecture rather than encoding them implicitly in code;
5. preserve the distinction between global protocol coverage and deck-specific conformance.
