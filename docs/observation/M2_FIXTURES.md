# M2 fixture inventory

The focused fixtures use the pinned BabelCDB/CardScripts data and the pinned
OCG core. They are observation fixtures, not full mechanics-conformance
decks.

| Fixture category | Evidence | Result |
| --- | --- | --- |
| `m2.hidden_hand` | paired worlds in `observation_builder_test` | opponent identity omitted; bytes/hash equal |
| `m2.hidden_deck` | reversed hidden opponent Deck with zero draws | bytes/hash equal for both perspectives |
| `m2.hidden_facedown` | paired opponent face-down field cards | identity-derived properties redacted; bytes/hash equal |
| `m2.extra_deck` | paired hidden and face-up Extra Deck worlds | hidden equal; public reveal diverges |
| `m2.xyz` | passcode 359563 plus pinned two-material overlay setup | Rank 4, count 2, explicit redacted material edges; M2.1 confirms aggregate-only public API |
| `m2.1.xyz_api` | public-ABI query probe plus paired material worlds | parent/field count 2; `overlay_seq` 0/1 return no individual records; paired observations byte/hash equal |
| `m2.link` | passcode 146746 | Link rating 2, two typed markers, ATK present, DEF absent |
| `m2.fusion` | passcode 43227 | Fusion type and current field state |
| `m2.synchro` | passcode 109401 | Synchro type and Level 5 |
| `m2.pendulum` | passcode 41546 | scales 6/6, Pendulum-relevant zone, face-up Extra Deck state |
| `m2.chain` | query decoder and event projection chain frames | structured link/index/description fields with privacy filter |
| `m2.counters` | pinned `Debug.PreAddCounter` setup | two typed counter kinds on the Xyz card |
| `m2.knowledge_boundary` | field-to-hidden-Deck move followed by shuffle | no hidden-destination locator and explicit boundary event |

`fixtures/m2_setup.lua` is intentionally a deterministic setup script. It
does not alter the pinned rules bundle or claim that every summoning line is
conformant. The pinned core remains authoritative for legality.
