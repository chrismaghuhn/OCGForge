# M1.1 sort-card fixture

- Fixture ID: `m1.1.sort_card`
- Target: `MSG_SORT_CARD` (`25`), player 0
- Cards/scripts: `47222536` (`official/c47222536.lua`) invokes the pinned deck-top sort path when its Dark Magician branch is unavailable; the three top cards are the deterministic player-0 deck prefix.
- Deck hashes: player A `cfca09c881f9859f69f0ceab05fc9f1c71f37f9e8a5a72ac28f1f8b6c19e67bc`; player B `dae2e76f2fd5f86c87da77c225556dce37d3121307d4b97635d4cf7d12196e97`.
- SeedBundle: `0123456789abcdef fedcba9876543210 13579bdf2468ace0 0eca8642fdb97531`.
- Duel flags: `0`; starting draw count `5`; draw count per turn `0`.
- Expected player/family: player 0, ordered continuation over source indices `0,1,2`.
- Continuation behavior: choose source indices in order `0`, `1`, `2`. The remaining candidate domain shrinks after every pick; no permutation list is materialized up front.
- Bounded success: the final ordered response is accepted, no `MSG_RETRY` occurs, the engine emits a subsequent non-empty state, and the v2 trace contains three intermediate records plus one final response record.

No CardScripts or ocgcore files are modified. The selector is inserted through the pinned public setup API.
