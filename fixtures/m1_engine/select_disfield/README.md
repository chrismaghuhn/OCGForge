# M1.1 select-disfield fixture

- Fixture ID: `m1.1.select_disfield`
- Target: `MSG_SELECT_DISFIELD` (`24`), player 0
- Cards/scripts: `90502999` (`official/c90502999.lua`) invokes the pinned two-zone disable-field path.
- Deck hashes: player A `cfca09c881f9859f69f0ceab05fc9f1c71f37f9e8a5a72ac28f1f8b6c19e67bc`; player B `dae2e76f2fd5f86c87da77c225556dce37d3121307d4b97635d4cf7d12196e97`.
- SeedBundle: `0123456789abcdef fedcba9876543210 13579bdf2468ace0 0eca8642fdb97531`.
- Duel flags: `0`; starting draw count `5`; draw count per turn `0`.
- Expected player/family: player 0, typed distinct-zone continuation with exactly two selected zones.
- Continuation behavior: expose controller, location, and sequence for each legal zone; choose two distinct zones, then finish. Duplicate zone identities are rejected by the fixture.
- Bounded success: the final two-zone response is accepted, no `MSG_RETRY` occurs, the engine emits a subsequent non-empty state, and the v2 trace contains two intermediate records plus one final response record.

No CardScripts or ocgcore files are modified. The selector is inserted through the pinned public setup API.
