-- SG-01/SG-02 pre-effect setup. All summon/search behavior remains in the
-- pinned Salamangreat scripts.
Debug.AddCard(11962031, 0, 0, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE, true)
Debug.AddCard(52277807, 0, 0, LOCATION_HAND, 1, POS_FACEDOWN_DEFENSE, true)
Debug.AddCard(94620082, 0, 0, LOCATION_HAND, 2, POS_FACEDOWN_DEFENSE, true)
Debug.AddCard(52155219, 0, 0, LOCATION_HAND, 3, POS_FACEDOWN_DEFENSE, true)
-- The exact locked Salamangreat Extra Deck belongs to player 1. This narrow
-- player-0 fixture adds the same pinned card to the setup Extra Deck so the
-- engine can expose the procedure without changing either locked YDK list.
Debug.AddCard(14812471, 0, 0, LOCATION_EXTRA, 15, POS_FACEDOWN_DEFENSE, false)
