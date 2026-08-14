-- SS-08/SS-09: a real Synchro-summoned Baxia with both its trigger target
-- domain and its ignition destroy/revival domain available. The effects and
-- all resulting moves are produced by the pinned official script.
local baxia = Debug.AddCard(83755611, 0, 0, LOCATION_MZONE, 0, POS_FACEUP_ATTACK, false)
Debug.PreSummon(baxia, SUMMON_TYPE_SYNCHRO)
-- Keep the alternate own target face-down so it cannot steal the initial
-- idle-effect focus; it remains a legal destruction target for Baxia.
Debug.AddCard(20001443, 0, 0, LOCATION_MZONE, 1, POS_FACEDOWN_DEFENSE, true)
Debug.AddCard(20001443, 0, 0, LOCATION_GRAVE, 0, POS_FACEUP_ATTACK, true)
Debug.AddCard(11962031, 1, 1, LOCATION_MZONE, 0, POS_FACEUP_ATTACK, true)
