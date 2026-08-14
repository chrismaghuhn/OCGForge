-- SG-12: an already legal first Sunlight Wolf is the precondition, then the
-- official Sanctuary procedure performs the same-name Link Summon.  The
-- fixture helper establishes only the board precondition; Sanctuary,
-- material selection, placement, and the Spell/Trap recovery remain
-- CardScript/ocgcore behavior.
local wolf = Debug.AddCard(87871125, 0, 0, LOCATION_MZONE, 5, POS_FACEUP_ATTACK, false)
Debug.PreSummon(wolf, SUMMON_TYPE_LINK)
Debug.AddCard(1295111, 0, 0, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE, true)
Debug.AddCard(14934922, 0, 0, LOCATION_GRAVE, 0, POS_FACEUP_ATTACK, true)
