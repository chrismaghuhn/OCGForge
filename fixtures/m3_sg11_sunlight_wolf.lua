-- SG-11/12 places a real Link-summoned Sunlight Wolf so a subsequent FIRE
-- summon can exercise the linked-zone recovery script.
local wolf = Debug.AddCard(87871125, 0, 0, LOCATION_MZONE, 0, POS_FACEUP_ATTACK, false)
Debug.PreSummon(wolf, SUMMON_TYPE_LINK)
Debug.AddCard(11962031, 0, 0, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE, true)
Debug.AddCard(11962031, 0, 0, LOCATION_GRAVE, 0, POS_FACEUP_ATTACK, true)
