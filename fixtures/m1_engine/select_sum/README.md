# M1.1 select-sum fixture

- Fixture ID: `m1.1.select_sum`
- Target: `MSG_SELECT_SUM` (`23`), player 0
- Cards/scripts: `12148078` (`official/c12148078.lua`) is the selector; the contribution domain contains `16725505`, `96708940`, two `53932291` cards, `17328157`, and `21516908`. All are supplied in the player-0 hand through the public setup API.
- Deck hashes: player A `cfca09c881f9859f69f0ceab05fc9f1c71f37f9e8a5a72ac28f1f8b6c19e67bc`; player B `dae2e76f2fd5f86c87da77c225556dce37d3121307d4b97635d4cf7d12196e97`.
- SeedBundle: `0123456789abcdef fedcba9876543210 13579bdf2468ace0 0eca8642fdb97531`.
- Duel flags: `0`; starting draw count `5`; draw count per turn `0`.
- Expected player/family: player 0, `MSG_SELECT_SUM` continuation with positive contribution values and one pinned exact-sum/greater-sum mode selected by the engine.
- Continuation behavior: the pinned message decodes as exact mode, target `6`, `min=1`, `max=2`, no mandatory items, and optional contributions `2:257`, `3:0`, `3:0`, `4:0` (primary:secondary). The policy selects two legal contribution items, then finishes. The adapter preserves the engine-emitted domain; it does not substitute a generic subset-sum interpretation.
- Bounded success: the exact encoded response is accepted, no `MSG_RETRY` occurs, the engine advances to a subsequent non-empty state, and the v2 trace contains two intermediate records plus one final response record.

No CardScripts or ocgcore files are modified. The selector and contribution cards are inserted only through the pinned public setup API.
