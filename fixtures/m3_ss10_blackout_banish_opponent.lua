-- SS-10 closure setup: Blackout is in player 0's public Graveyard and
-- Called by the Grave is in player 1's hand.  The banish and Token trigger
-- must still be produced by the pinned official scripts.
Debug.AddCard(14821890, 0, 0, LOCATION_GRAVE, 0, POS_FACEUP_ATTACK, true)
Debug.AddCard(24224830, 1, 1, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE, true)
Debug.AddCard(14558127, 0, 0, LOCATION_GRAVE, 1, POS_FACEUP_ATTACK, true)
Debug.AddCard(11962031, 0, 0, LOCATION_MZONE, 0, POS_FACEUP_ATTACK, true)
