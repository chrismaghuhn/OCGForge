# M1.1 select-option fixture

- Fixture ID: `m1.1.select_option`
- Target: `MSG_SELECT_OPTION` (`14`), player 0
- Cards/scripts: `1006081` (`official/c1006081.lua`) is Junk Changer; `63977008` is a face-up Junk target. The summon trigger exposes two legal level-change options.
- Deck hashes: player A `cfca09c881f9859f69f0ceab05fc9f1c71f37f9e8a5a72ac28f1f8b6c19e67bc`; player B `dae2e76f2fd5f86c87da77c225556dce37d3121307d4b97635d4cf7d12196e97`.
- SeedBundle: `0123456789abcdef fedcba9876543210 13579bdf2468ace0 0eca8642fdb97531`.
- Duel flags: `0`; starting draw count `5`; draw count per turn `0`.
- Expected player/family: player 0, atomic option selection with two preserved ordinals and payloads.
- Continuation behavior: none; the real path first emits `MSG_SELECT_EFFECTYN`, accepts the yes branch, then emits the two-option decision as one atomic response after the preceding target selection.
- Bounded success: both option ordinals are exposed without collapsing their distinct payloads, one exact ordinal response is accepted, no `MSG_RETRY` occurs, and the engine emits a subsequent non-empty state.

No CardScripts or ocgcore files are modified. The two scenario cards are inserted through the pinned public setup API.
