# M0/M1 fixture decks

Each `.deck` file contains one original card passcode per line. The order is
committed and is the order supplied to `OCG_DuelNewCard`; the core may perform
its own shuffle according to the pinned rules flags. The M0 player decks have
40 entries made only from simple normal monsters in the pinned `cards.cdb`;
the M1 tribute fixtures intentionally add a scripted tribute-summon card.

The `m1_tribute_a.deck` and `m1_tribute_b.deck` fixtures add four copies of
the pinned `VIP Whale` script card to each side. They are used
only by the bounded Windows engine-fixture test to reach
`MSG_SELECT_TRIBUTE`; they do not change the M0 fixture decks or the rules
bundle.

The fixture deliberately does not target official tournament legality. Copy
limits are not asserted because M0 validates the engine/protocol boundary, not
competitive deck construction. No card in this slice has an effect script.
