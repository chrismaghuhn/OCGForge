-- SG-15: real Heatleo reincarnation with the exact opposing Swordsoul
-- Blackout as the Spell/Trap target.  The resolved Deck destination is
-- intentionally observed only as a public zone transition.
local heatleo = Debug.AddCard(41463181, 0, 0, LOCATION_MZONE, 5, POS_FACEUP_ATTACK, false)
Debug.PreSummon(heatleo, SUMMON_TYPE_LINK)
Debug.AddCard(11962031, 0, 0, LOCATION_MZONE, 0, POS_FACEUP_ATTACK, true)
Debug.AddCard(52277807, 0, 0, LOCATION_MZONE, 4, POS_FACEUP_ATTACK, true)
Debug.AddCard(1295111, 0, 0, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE, true)
Debug.AddCard(14821890, 1, 1, LOCATION_SZONE, 2, POS_FACEUP_DEFENSE, true)
