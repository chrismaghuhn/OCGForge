-- SG-14: real Sanctuary-enabled Pyro Phoenix reincarnation.  The opposing
-- Swordsoul monster makes the official destroy-all trigger observable.  The
-- locked opposing Swordsoul Extra Deck contains no Link Monster, so Pyro
-- Phoenix's opponent-Link revival branch is not a fixed-matchup path.
local phoenix = Debug.AddCard(31313405, 0, 0, LOCATION_MZONE, 5, POS_FACEUP_ATTACK, false)
Debug.PreSummon(phoenix, SUMMON_TYPE_LINK)
Debug.AddCard(1295111, 0, 0, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE, true)
Debug.AddCard(20001443, 1, 1, LOCATION_MZONE, 0, POS_FACEUP_ATTACK, true)
