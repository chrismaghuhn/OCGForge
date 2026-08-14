# M3.5 Public API Hardening

M3.5 carries two minimal, independent repository-versioned ocgcore changes against the exact pinned base commit. The base checkout at `.cache/rules_bundle/ocgcore` remains immutable; the canonical build uses `.cache/derived/ocgcore`.

## Ordered patchset

| Order | Patch | Scope |
| --- | --- | --- |
| 1 | `0001-fix-overlay-seq-parent-query.patch` | Correct the existing `OCG_DuelQuery` parent lookup while preserving `OCG_QueryInfo.overlay_seq`, bounds behavior, and the public query shape. |
| 2 | `0002-add-starting-player-control.patch` | Add `OCG_DuelSetStartingPlayer(OCG_Duel, uint8_t)` before `OCG_StartDuel`; default player 0 remains unchanged and invalid/post-start calls fail closed. |

The exact individual and aggregate hashes are machine-readable in [m35_acceptance.json](m35_acceptance.json) and in `third_party/rules_bundle.lock.json`. The ordered patchset participates in the canonical bundle hash.

## Xyz contract

The existing public locator remains authoritative:

```text
loc         = parent_location | LOCATION_OVERLAY
seq         = parent_sequence
overlay_seq = material index
```

No second overlay-query mechanism was introduced. Visible material identities are projected only when the parent is visible. Hidden paired-world fixtures remain canonical-observation-equal and redact material identity.

## Starting-player contract

`OCG_DuelSetStartingPlayer` accepts only player `0` or `1` before duel start. If it is not called, the core starts player `0` as before. Calls with another value, null duel, or after `OCG_StartDuel` return failure. There is no API for mutating the active turn player.

## Upstream handoff

The patches are prepared for independent upstream review with direct regression tests. No upstream checkout was patched, fetched, committed, pushed, tagged, or submitted during M3.5.
