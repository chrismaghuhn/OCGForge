-- SS-12 exercises both branches of Ashuna's Tenyi condition.  Salamangreat
-- of Fire is an effect monster, so the hand effect is initially illegal. Mo
-- Ye then creates the official non-effect Swordsoul Token, after which the
-- Graveyard/hand Deck-summon branch becomes legal through CardScript state.
Debug.AddCard(20001443, 0, 0, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE, true)
Debug.AddCard(87052196, 0, 0, LOCATION_HAND, 1, POS_FACEDOWN_DEFENSE, true)
Debug.AddCard(11962031, 0, 0, LOCATION_MZONE, 1, POS_FACEUP_ATTACK, true)
