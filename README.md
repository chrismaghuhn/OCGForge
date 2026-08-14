# OCGForge

OCGForge is a correctness-focused M0 spike for a deterministic Yu-Gi-Oh!
environment. It builds a thin C++ adapter over the public C API of one pinned
OCG core snapshot. It is not a general Yu-Gi-Oh! environment and it does not
contain machine-learning code.

## M0 result

The verified fixture slice uses 40 normal-monster entries per player and
reaches a terminal duel result with the pinned core. The adapter exposes typed
candidate vectors for the controlled protocol slice:

- idle commands
- battle commands
- chain pass/effect choices
- single-card selection
- zone placement
- position selection
- yes/no selection

The terminal trace exercised idle commands, battle commands, chain choices,
single-card selection, and zone placement. Position and yes/no decoding are
covered by protocol tests but were not emitted by this simple fixture.

Other interactive messages fail closed with a structured diagnostic. Candidate
sets are not resized to a fixed maximum. Original card passcodes remain the
environment representation; model vocabulary mapping is deliberately outside
the core and protocol layers.

The probe policy is `m0.deterministic_priority.seeded_tie.v1`. It uses the
complete semantic-key ordering for normal-summon ties and applies the first
seed word only within that tie, so seed sensitivity is explicit without relying
on pointer order, hash-map iteration, wall-clock time, or thread scheduling.

## M2 player observation

M2 adds a deterministic, perspective-safe `PlayerObservation` contract for
zones, visible entities, relationships, mechanics state, chain/event history,
decision context, and canonical observation hashes. Hidden hand, deck,
face-down, Extra Deck, and knowledge-destroying transitions fail closed rather
than exposing raw engine state. The observation probe is
`build/dev-windows/ygo_observation_probe.exe` (or the equivalent
`build/windows-zig` path).

The unmodified pinned base API exposes Xyz material counts and the parent
query's aggregate ordered material-code vector, but its existing
`overlay_seq` path did not resolve the individual public material record
correctly. M3.5 keeps the existing public contract and supplies a narrow,
repository-versioned core patch so that
`LOCATION_OVERLAY + parent sequence + overlay_seq` resolves the requested
material. Visibility gates still redact hidden identities. See
[M2.1 Xyz API investigation](docs/observation/M2_1_XYZ_API_INVESTIGATION.md)
the [player observation contract](docs/contracts/player-observation-v1.md),
and the [M3.5 API-hardening acceptance](docs/m3_5/M3_5_ACCEPTANCE.md).

The current local M3/M3.5 verification is 84/84 CTest tests, 8/8 repository
Python tests, and 17/17 M3 Python tests. The hosted Windows workflow remains
the repository integration gate and runs on pushes and pull requests.

## M3 fixed-deck conformance and M3.5 API hardening

The locked matchup is now validated end to end:

- `ocgforge.swordsoul_tenyi.ml_v1` versus
  `ocgforge.salamangreat.ml_v1`
- exact 40-card Main Decks and 15-card Extra Decks, with 110 valid slots and
  50 unique cards
- canonical `TCG_ADVANCED_2026_05_18` configuration using
  `DUEL_MODE_MR5 = 0x2E800`
- mechanics matrix: 38 engine-verified, 7 protocol-verified, 0 pending
- 16/16 complete deterministic games across both start-player and mirrored
  deck-seat partitions
- zero retries, unsupported required decisions, automatic decisions,
  candidate truncation, and core errors

The canonical rules environment is recorded in
[third_party/rules_bundle.lock.json](third_party/rules_bundle.lock.json) as
bundle `3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f`.
The pinned ocgcore, OCG API, CardScripts, and BabelCDB versions remain
unchanged. The runtime uses an immutable pinned base core plus the ordered,
repository-versioned `ocgforge.ocgcore.api_hardening.v1` patchset; no upstream
source checkout is modified.

M3.5 adds two narrow public API capabilities:

- the existing `overlay_seq` query contract resolves the correct individual
  Xyz material while preserving the existing privacy projection;
- `OCG_DuelSetStartingPlayer` accepts only player 0 or 1 before duel start,
  preserves default player 0 behavior, and rejects invalid or post-start calls.

It does not add a general mid-duel turn-player mutation API, a second Xyz query
mechanism, machine-learning code, or a general board-construction runtime API.
The complete acceptance evidence is in
[docs/m3/M3_ACCEPTANCE_MATRIX.md](docs/m3/M3_ACCEPTANCE_MATRIX.md) and
[docs/m3_5/M3_5_ACCEPTANCE.md](docs/m3_5/M3_5_ACCEPTANCE.md).

## Reproducible rules bundle

The exact runtime inputs and the deterministic `bundle_id` are recorded in
[third_party/rules_bundle.lock.json](third_party/rules_bundle.lock.json).
The repository-local dependency cache is ignored by Git and is populated by
the exact-commit fetcher:

```text
python tools/fetch_rules_bundle.py --lock third_party/rules_bundle.lock.json --cache .cache/rules_bundle
python tools/verify_rules_bundle.py --lock third_party/rules_bundle.lock.json --cache .cache/rules_bundle
```

See [THIRD_PARTY.md](THIRD_PARTY.md) for the license records. The complete
project is not represented as MIT-only.

## Windows build

The documented native path uses MSVC and Ninja from a Visual Studio developer
environment:

```text
cmake --preset dev-windows
cmake --build --preset dev-windows
ctest --preset dev-windows
```

The `dev-windows` configure preset verifies or fetches the pinned rules bundle
into `.cache/rules_bundle`. A clean build starts with an empty
`build/dev-windows` and an empty dependency cache; no sibling checkout is
required.

When native MSVC is unavailable, the repository also contains a local fallback
using pinned repository-local Zig 0.14.1 and Ninja binaries:

```text
cmake --preset dev-windows-zig
cmake --build --preset dev-windows-zig
ctest --preset dev-windows-zig
```

Those fallback binaries are ignored cache inputs and are not part of the
rules bundle.

## Useful probes

Run the controlled duel and write a canonical JSONL trace:

```text
build/dev-windows/ygo_core_probe.exe --max-steps 1000 --output artifacts/probe-trace.jsonl
```

The probe exits nonzero on an unsupported interactive message. The deliberate
diagnostic path is:

```text
build/dev-windows/ygo_core_probe.exe --force-unsupported
```

The output contains the message type, raw-message hash, step, player, bundle,
deck hashes, complete seed bundle, and recent trace context. It never submits
a fabricated response.

## Contracts and audit

- [engine trace v1](docs/contracts/engine-trace-v1.md)
- [player view v1](docs/contracts/player-view-v1.md)
- [adapter ADR](docs/adr/ADR-0001-modern-ocg-adapter.md)
- [ygo-agent reference audit](docs/audits/ygo-agent-ygoenv-reference-audit.md)

The pinned `sbl1996/ygo-agent` snapshot is read-only audit material. The
project does not fork its ygoenv implementation.
