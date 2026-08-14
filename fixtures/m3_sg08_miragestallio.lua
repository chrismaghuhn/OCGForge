-- SG-09 uses the real Rank-3 Xyz procedure. Detach, Deck summon, and the
-- FIRE restriction remain owned by the pinned Miragestallio script.
Debug.AddCard(94620082, 0, 0, LOCATION_MZONE, 0, POS_FACEUP_ATTACK, true)
Debug.AddCard(52277807, 0, 0, LOCATION_MZONE, 1, POS_FACEUP_ATTACK, true)
Debug.AddCard(87327776, 0, 0, LOCATION_EXTRA, 15, POS_FACEDOWN_DEFENSE, false)
Debug.AddCard(94620082, 0, 0, LOCATION_DECK, 0, POS_FACEDOWN_DEFENSE, true)
Debug.AddCard(94620082, 0, 0, LOCATION_DECK, 1, POS_FACEDOWN_DEFENSE, true)

-- Vishuda is the opposing non-FIRE monster-effect candidate. A face-up
-- non-effect Token satisfies its official Graveyard target condition.
Debug.AddCard(20001444, 1, 1, LOCATION_MZONE, 0, POS_FACEUP_ATTACK, true)
Debug.AddCard(23431858, 1, 1, LOCATION_GRAVE, 0, POS_FACEUP_ATTACK, true)
