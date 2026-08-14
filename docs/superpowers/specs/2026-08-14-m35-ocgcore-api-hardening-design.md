# M3.5 ocgcore Public API Hardening Design

Status: approved for implementation on 2026-08-14.

## Goal

Close exactly the two confirmed pinned public ocgcore gaps while keeping OCGForge entirely on the public C API boundary:

1. resolve individual Xyz materials through the existing OCG_QueryInfo overlay_seq contract;
2. select the initial turn player before duel start with default behavior unchanged.

## Findings that constrain the design

The pinned OCG_QueryInfo already contains overlay_seq and OCG_DuelQuery already has an overlay branch. The defect is the parent lookup: the overlay bit is passed as the complete location instead of masking it from the parent location. The patch therefore preserves the existing locator convention (parent location OR LOCATION_OVERLAY) and fixes only parent resolution.

Startup currently queues Processors::Startup and that processor unconditionally creates Turn(0). OCG_DuelOptions has no starting-player field. A narrow pre-start public setter is therefore safer than changing the public options struct ABI.

## Architecture

Two independent, ordered, repository-tracked patches are applied to an immutable checkout of the pinned base core:

- 0001: correct OCG_DuelQuery overlay parent resolution and preserve safe bounds behavior;
- 0002: add a public pre-start starting-player setter, internal pre-start state, validation, and startup use.

The base checkout in .cache/rules_bundle/ocgcore is never modified. A preparation step creates a derived checkout, verifies the exact base commit, applies the ordered patches, and builds only that derived checkout.

## Public API

Patch 0001 keeps OCG_QueryInfo unchanged. Callers use the existing convention:

    loc = parent_location | LOCATION_OVERLAY
    seq = parent_sequence
    overlay_seq = attached_material_index

Patch 0002 adds one generic C API function:

    int OCG_DuelSetStartingPlayer(OCG_Duel duel, uint8_t player)

It returns success only for player 0 or 1 before OCG_StartDuel. It rejects all other values and all calls after duel start. No mid-duel mutation API is introduced. If the setter is never called, the core starts player 0 exactly as before.

## OCGForge integration

CoreHostConfig gains an optional starting-player value. CoreHost applies it before OCG_StartDuel and fails closed if the public setter rejects it. Observation building queries individual visible Xyz materials through the public query and retains redaction whenever the perspective policy does not make identity visible.

Canonical full-game metadata records the explicit seat assignment and starting player. The full matrix is 4 seeds by 2 seat assignments by 2 starting-player values.

## Environment identity

The canonical rules lock records the base core commit, ordered patch filenames, individual patch hashes, and a deterministic combined patchset hash. These values participate in rule_affecting_inputs and therefore generate a new M3.5 bundle ID. The MR5 format and duel flags remain unchanged.

## Verification

Direct core tests cover two-material ordering, bounds, zero-material behavior, default/explicit starting player, invalid and post-start setter calls, seat ownership, and opening state. The minimal public-core harness does not provide action/chain response orchestration, so detach is proven through the real public-API `m3_fixture_test sg09_direct` integration: the official Miragestallio effect accepts the detach, decrements the material count, resolves the remaining `overlay_seq`, and fails closed for the detached slot. No private setup API or synthetic direct detach path is introduced. OCGForge tests cover observation identity, privacy paired worlds, detach transitions, configuration equality, 45-row mechanics revalidation, the 16-game matrix, determinism for both starting-player partitions, replay, and all previous M0-M3 gates.

No commit, push, tag, PR, upstream fetch, CardScripts change, BabelCDB change, or locked-deck change is part of M3.5.
