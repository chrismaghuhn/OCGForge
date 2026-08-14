-- SS-17 closure setup: Qixing is controlled by player 0 while player 1
-- controls a legitimate face-down Salamangreat Rage activation.  The
-- face-down set is the normal Trap activation path and makes Qixing's
-- EVENT_CHAINING effect the first relevant interaction.
local qixing = Debug.AddCard(47710198, 0, 0, LOCATION_MZONE, 0, POS_FACEUP_ATTACK, true)
Debug.PreSummon(qixing, SUMMON_TYPE_SYNCHRO)
local balelynx = Debug.AddCard(14812471, 1, 1, LOCATION_MZONE, 0, POS_FACEUP_ATTACK, false)
Debug.PreSummon(balelynx, SUMMON_TYPE_LINK)
Debug.AddCard(14934922, 1, 1, LOCATION_SZONE, 1, POS_FACEDOWN_DEFENSE, true)
