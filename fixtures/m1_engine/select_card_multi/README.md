# M1.1 select-card multi fixture

- Fixture ID: `m1.1.select_card_multi`
- Target: `MSG_SELECT_CARD` (`15`), player 0
- Cards/scripts: `43898403` (`official/c43898403.lua`) activates Twin Twisters; two `12148078` cards (`official/c12148078.lua`) are placed face-down in player 1's Spell/Trap zone. The setup also supplies one discardable player-0 hand card through the public `OCG_DuelNewCard` API.
- Deck hashes: player A `cfca09c881f9859f69f0ceab05fc9f1c71f37f9e8a5a72ac28f1f8b6c19e67bc`; player B `dae2e76f2fd5f86c87da77c225556dce37d3121307d4b97635d4cf7d12196e97`.
- SeedBundle: `0123456789abcdef fedcba9876543210 13579bdf2468ace0 0eca8642fdb97531`.
- Duel flags: `0`; starting draw count `5`; draw count per turn `0`.
- Expected player/family: player 0, unordered multi-card continuation with `min=1`, `max=2`, and exactly two candidates.
- Continuation behavior: pick card 0, pick card 1, then finish. `FINISH` is absent before the minimum is met and present after both picks. Intermediate choices are adapter-local and do not submit an engine response.
- Bounded success: the final indexed response is accepted, no `MSG_RETRY` occurs, the engine emits a subsequent non-empty state, and the v2 trace contains two `engine_advanced=false` records followed by one final record.

No CardScripts or ocgcore files are modified. The focused cards are inserted only through the pinned public setup API after `OCG_StartDuel` and before the first process call.
