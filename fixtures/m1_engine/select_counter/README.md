# M1.1 select-counter fixture

- Fixture ID: `m1.1.select_counter`
- Target: `MSG_SELECT_COUNTER` (`22`), player 0
- Cards/scripts: `97127906` (`official/c97127906.lua`) normal summon; `15475415` (`official/c15475415.lua`) creates counters; `91070115` (`official/c91070115.lua`) removes one A counter. Player 1 has `3732747` and `3606209` face-up monsters, each with one available counter after the setup sequence.
- Deck hashes: player A `cfca09c881f9859f69f0ceab05fc9f1c71f37f9e8a5a72ac28f1f8b6c19e67bc`; player B `dae2e76f2fd5f86c87da77c225556dce37d3121307d4b97635d4cf7d12196e97`.
- SeedBundle: `0123456789abcdef fedcba9876543210 13579bdf2468ace0 0eca8642fdb97531`.
- Duel flags: `0`; starting draw count `5`; draw count per turn `0`.
- Expected player/family: player 0, counter-allocation continuation with required amount `1`, two listed cards, and capacity `1` on each card.
- Continuation behavior: assign `0` to the first card, assign `1` to the second card, then finish. The adapter exposes allocations and remaining amount; it never automatically distributes counters.
- Bounded success: the final two-entry allocation response is accepted, no `MSG_RETRY` occurs, the engine emits a subsequent non-empty state, and the v2 trace contains two intermediate records plus one final response record.

No CardScripts or ocgcore files are modified. All setup cards are inserted through the pinned public setup API.
