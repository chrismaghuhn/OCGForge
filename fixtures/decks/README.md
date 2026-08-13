# M0 fixture decks

Each `.deck` file contains one original card passcode per line. The order is
committed and is the order supplied to `OCG_DuelNewCard`; the core may perform
its own shuffle according to the pinned rules flags. Each player has 40 main
deck entries made only from simple normal monsters in the pinned `cards.cdb`.

The fixture deliberately does not target official tournament legality. Copy
limits are not asserted because M0 validates the engine/protocol boundary, not
competitive deck construction. No card in this slice has an effect script.
