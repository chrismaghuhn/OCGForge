-- SS-05 places a real opposing effect monster on the field so Chixiao's
-- official quick-effect target and negation path can execute. Balelynx's
-- pinned Link Summon trigger is the effect that Chixiao negates.
local chixiao = Debug.AddCard(69248256, 0, 0, LOCATION_MZONE, 0, POS_FACEUP_ATTACK, true)
Debug.PreSummon(chixiao, SUMMON_TYPE_SYNCHRO)
local balelynx = Debug.AddCard(14812471, 1, 1, LOCATION_MZONE, 0, POS_FACEUP_ATTACK, false)
Debug.PreSummon(balelynx, SUMMON_TYPE_LINK)
Debug.AddCard(11962031, 1, 1, LOCATION_GRAVE, 0, POS_FACEUP_ATTACK, true)
