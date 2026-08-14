-- Extra Monster Zone placement exposes Sunlight Wolf's bottom arrow to the
-- first main Monster Zone, where the FIRE summon is forced.
local wolf = Debug.AddCard(87871125, 0, 0, LOCATION_MZONE, 5, POS_FACEUP_ATTACK, false)
Debug.PreSummon(wolf, SUMMON_TYPE_LINK)
Debug.AddCard(11962031, 0, 0, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE, true)
Debug.AddCard(2772337, 0, 0, LOCATION_GRAVE, 0, POS_FACEUP_ATTACK, true)
