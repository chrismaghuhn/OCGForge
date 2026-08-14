-- SS-10 closure setup: an official Salamangreat Rage effect destroys a real
-- Synchro-summoned Chengying.  Chengying's official replacement must banish
-- Blackout from its owner's Graveyard, which must then raise Blackout's own
-- official EVENT_REMOVE Token trigger.
local chengying = Debug.AddCard(96633955, 0, 0, LOCATION_MZONE, 0, POS_FACEUP_ATTACK, true)
Debug.PreSummon(chengying, SUMMON_TYPE_SYNCHRO)
Debug.AddCard(14821890, 0, 0, LOCATION_GRAVE, 0, POS_FACEUP_ATTACK, true)
local balelynx = Debug.AddCard(14812471, 1, 1, LOCATION_MZONE, 0, POS_FACEUP_ATTACK, false)
Debug.PreSummon(balelynx, SUMMON_TYPE_LINK)
Debug.AddCard(14934922, 1, 1, LOCATION_SZONE, 1, POS_FACEUP, true)
