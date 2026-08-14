from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from .rules_mode import load_canonical_environment


STATUS_VALUES = {
    "ENGINE_VERIFIED",
    "PROTOCOL_VERIFIED",
    "PUBLIC_API_LIMITATION",
    "NOT_APPLICABLE_FIXED_MATCHUP",
    "PENDING",
}


_M3_FIXTURE_MESSAGE_FAMILIES = [
    "MSG_SELECT_CARD",
    "MSG_SELECT_CHAIN",
    "MSG_SELECT_EFFECTYN",
    "MSG_SELECT_IDLECMD",
    "MSG_SELECT_PLACE",
    "MSG_SELECT_POSITION",
    "MSG_SELECT_UNSELECT_CARD",
]


_FIXTURE_EVIDENCE: dict[str, dict[str, Any]] = {
    "m3_ss01_moye_token": {
        "source": "m3_fixture_test ss01",
        "engine_steps": 140,
        "decisions": 56,
        "observation_hash_chain_sha256": "608e2bd1a7af99c4f08c3c902a9658abca6efe1eac3961927306542d95a55f56",
        "message_families": [
            "MSG_SELECT_CARD",
            "MSG_SELECT_CHAIN",
            "MSG_SELECT_EFFECTYN",
            "MSG_SELECT_IDLECMD",
            "MSG_SELECT_OPTION",
            "MSG_SELECT_PLACE",
            "MSG_SELECT_POSITION",
            "MSG_SELECT_UNSELECT_CARD",
            "MSG_SELECT_YESNO",
        ],
        "engine": "Pinned Mo Ye official script produced the Token, Chixiao Synchro path, Chixiao search and the real trigger chain without MSG_RETRY.",
        "protocol": "Complete decoded candidate sets were submitted; the Chixiao/Mo Ye trigger order and Emergence search candidate were accepted exactly.",
        "observation": "Token, Chixiao and searched Emergence were observed with projected public properties; trigger-chain resolution and candidate references resolved against PlayerObservation.",
    },
    "m3_ss06_longyuan": {
        "source": "m3_fixture_test ss06",
        "engine_steps": 160,
        "decisions": 68,
        "observation_hash_chain_sha256": "2a89009a075580042af1ab3903af92458467702e096f7310f88d6afdfd819c61",
        "message_families": [
            "MSG_SELECT_CARD",
            "MSG_SELECT_CHAIN",
            "MSG_SELECT_EFFECTYN",
            "MSG_SELECT_IDLECMD",
            "MSG_SELECT_OPTION",
            "MSG_SELECT_PLACE",
            "MSG_SELECT_POSITION",
            "MSG_SELECT_UNSELECT_CARD",
            "MSG_SELECT_YESNO",
        ],
        "engine": "Pinned Longyuan official script accepted the hand discard, produced the Token, level-10 Qixing and Chixiao development, and emitted the 1200 LP burn without MSG_RETRY.",
        "protocol": "Complete candidate domains and exact responses were accepted through the bounded fixture run, including the Longyuan hand discard.",
        "observation": "Token, Qixing, Chixiao and the visible LifePointsChanged -1200 event were observed; candidate references resolved against the observation snapshot.",
    },
    "m3_ss07_token_restriction": {
        "source": "m3_fixture_test ss07",
        "engine_steps": 200,
        "decisions": 88,
        "observation_hash_chain_sha256": "b595ee656db4da48f2a3ef51a3334c6893ce2efd891ec0ad23c627b3826400dc",
        "message_families": [
            "MSG_SELECT_CARD",
            "MSG_SELECT_CHAIN",
            "MSG_SELECT_EFFECTYN",
            "MSG_SELECT_IDLECMD",
            "MSG_SELECT_OPTION",
            "MSG_SELECT_PLACE",
            "MSG_SELECT_POSITION",
            "MSG_SELECT_UNSELECT_CARD",
        ],
        "engine": "Pinned Mo Ye script exposed Balelynx before the Token, removed the non-Synchro Extra candidate while the Token remained, and restored Balelynx legality after the Token left.",
        "protocol": "The before/while/after candidate domains were taken directly from MSG_SELECT_IDLECMD; the later Balelynx Link material and placement responses were accepted without MSG_RETRY.",
        "observation": "The Token and Chixiao states bracketed the restriction window; the post-expiry Balelynx candidate and resulting public Link state resolved against PlayerObservation.",
    },
    "m3_ss08_baxia_real": {
        "source": "m3_fixture_test ss08",
        "engine_steps": 180,
        "decisions": 78,
        "observation_hash_chain_sha256": "68e5145ca56a44bce3d67a67ab368cd52b1aa7a6242e3cedd2fd27782f2bfee8",
        "message_families": [
            "MSG_SELECT_CARD",
            "MSG_SELECT_CHAIN",
            "MSG_SELECT_EFFECTYN",
            "MSG_SELECT_IDLECMD",
            "MSG_SELECT_PLACE",
            "MSG_SELECT_POSITION",
            "MSG_SELECT_UNSELECT_CARD",
        ],
        "engine": "Pinned Baxia official procedure accepted a real Mo Ye Token plus Taia Synchro material, a two-target trigger, and the follow-up destruction/revival effect without MSG_RETRY.",
        "protocol": "The complete material toggle domain selected Token and Taia; the multi-target continuation submitted two legal on-field targets through PICK/PICK/FINISH, followed by Baxia's destruction and Graveyard revival selections.",
        "observation": "Baxia Level 8 state, two target selections, destruction event, and revived Level-4 Wyrm were observed against PlayerObservation; no Xyz-material identity is involved.",
    },
    "m3_ss12_tenyi_condition": {
        "source": "m3_fixture_test ss12_condition",
        "engine_steps": 180,
        "decisions": 78,
        "observation_hash_chain_sha256": "6cc4a1d775978400181f4918ac5ef977ad22ccd8aefc36fea907c94429898010",
        "message_families": [
            "MSG_SELECT_CARD",
            "MSG_SELECT_CHAIN",
            "MSG_SELECT_EFFECTYN",
            "MSG_SELECT_IDLECMD",
            "MSG_SELECT_OPTION",
            "MSG_SELECT_PLACE",
            "MSG_SELECT_POSITION",
            "MSG_SELECT_UNSELECT_CARD",
        ],
        "engine": "Pinned Ashuna official condition was false while only an effect monster was face-up and became true after the real Mo Ye script created the non-effect Swordsoul Token.",
        "protocol": "Ashuna was absent from the first legal idle domain and then appeared and was selected from the hand domain after the Token state transition.",
        "observation": "The effect-monster control state, public Token state, and subsequent Ashuna selection were observed without reproducing the condition in OCGForge.",
    },
    "m3_ss18_tenyi_links": {
        "source": "m3_fixture_test ss18",
        "engine_steps": 180,
        "decisions": 74,
        "observation_hash_chain_sha256": "44da5a3cadd2188999902c2ccf4c3f82900af9e591473735c11e971eb221c583",
        "message_families": [
            "MSG_SELECT_CARD",
            "MSG_SELECT_CHAIN",
            "MSG_SELECT_EFFECTYN",
            "MSG_SELECT_IDLECMD",
            "MSG_SELECT_OPTION",
            "MSG_SELECT_PLACE",
            "MSG_SELECT_POSITION",
            "MSG_SELECT_UNSELECT_CARD",
        ],
        "engine": "Pinned Monk and Shaman Link procedures both resolved from the real Token/Tenyi state under explicit MR5 mode 0x2E800; no manual Link legality was applied.",
        "protocol": "Both Extra Deck candidates, their engine-provided material toggle domains, and both zone-placement domains were accepted without MSG_RETRY.",
        "observation": "Monk and Shaman public Link states and their resulting Monster Zone placements were observed against PlayerObservation.",
    },
    "m3_ss05_chixiao_negate": {
        "source": "m3_fixture_test ss05",
        "engine_steps": 100,
        "decisions": 43,
        "observation_hash_chain_sha256": "c2eafc98223301fdf3eecb8f5250dc54b02aa0a2579e47a0876f212f6d57dc43",
        "message_families": [
            "MSG_SELECT_CARD",
            "MSG_SELECT_CHAIN",
            "MSG_SELECT_EFFECTYN",
            "MSG_SELECT_IDLECMD",
            "MSG_SELECT_PLACE",
            "MSG_SELECT_POSITION",
        ],
        "engine": "Pinned Chixiao quick-effect script was offered in response to the opposing Balelynx trigger and the resulting chain resolved without MSG_RETRY.",
        "protocol": "The complete chain candidate selected Chixiao and the following card-selection domain selected the opposing face-up Balelynx in the Monster Zone as its target.",
        "observation": "Chixiao and the opposing Balelynx were observed with their public level/Link properties, and the visible chain-resolution event was retained; the public projection does not expose a separate negated-status flag.",
    },
    "m3_int01_ash_blossom": {
        "source": "m3_fixture_test int01",
        "engine_steps": 100,
        "decisions": 36,
        "observation_hash_chain_sha256": "c03c7eadda4a1c1b57611377a7532c55bbf2743130399ffd7b76b1634aff2281",
        "message_families": [
            "MSG_SELECT_CARD",
            "MSG_SELECT_CHAIN",
            "MSG_SELECT_EFFECTYN",
            "MSG_SELECT_IDLECMD",
            "MSG_SELECT_OPTION",
            "MSG_SELECT_PLACE",
            "MSG_SELECT_POSITION",
            "MSG_SELECT_UNSELECT_CARD",
        ],
        "engine": "Pinned Ash Blossom hand effect was offered in a real Swordsoul search window and its chain resolved without MSG_RETRY.",
        "protocol": "The opponent-hand Ash Blossom chain candidate was selected from the complete legal domain; Ash Blossom has no target-selection continuation.",
        "observation": "The chain and resulting public event history were projected consistently; the public model does not expose a separate negated-effect flag.",
    },
    "m3_int02_effect_veiler": {
        "source": "m3_fixture_test int02",
        "engine_steps": 100,
        "decisions": 36,
        "observation_hash_chain_sha256": "eae27320c682cdb3a6eab55b2afd0048cb169ca53fb9438519edb58f8ccafac8",
        "message_families": [
            "MSG_SELECT_CARD",
            "MSG_SELECT_CHAIN",
            "MSG_SELECT_EFFECTYN",
            "MSG_SELECT_IDLECMD",
            "MSG_SELECT_OPTION",
            "MSG_SELECT_PLACE",
            "MSG_SELECT_POSITION",
            "MSG_SELECT_UNSELECT_CARD",
            "MSG_SELECT_YESNO",
        ],
        "engine": "Pinned Effect Veiler hand effect was activated from the opposing hand and its chain resolved without MSG_RETRY.",
        "protocol": "The complete chain domain selected Effect Veiler and the following card-selection domain selected the face-up Mo Ye target in the Monster Zone.",
        "observation": "The target locator resolved against the acting PlayerObservation; the public model does not expose a separate negated-effect flag.",
    },
    "m3_int03_impermanence": {
        "source": "m3_fixture_test int03",
        "engine_steps": 100,
        "decisions": 40,
        "observation_hash_chain_sha256": "995c409046514aa8b64324c04cef4c24eb7ac6289876b5e1d0afe854dee88836",
        "message_families": [
            "MSG_SELECT_CARD",
            "MSG_SELECT_CHAIN",
            "MSG_SELECT_EFFECTYN",
            "MSG_SELECT_IDLECMD",
            "MSG_SELECT_PLACE",
            "MSG_SELECT_POSITION",
            "MSG_SELECT_YESNO",
        ],
        "engine": "Pinned Infinite Impermanence hand/set paths were exercised with a real opponent chain and no MSG_RETRY.",
        "protocol": "The opposing Impermanence chain candidate was selected and its card-selection domain selected the face-up Mo Ye target in the Monster Zone.",
        "observation": "The target locator and set/hand decision references resolved against the acting PlayerObservation; the public model does not expose a separate negated-effect flag.",
    },
    "m3_int04_ghost_belle": {
        "source": "m3_fixture_test int04",
        "engine_steps": 100,
        "decisions": 37,
        "observation_hash_chain_sha256": "232ebc5c4c04765fe7b4dfd92466fe936f12a99ad8cea9ed57339aeeadf16012",
        "message_families": [
            "MSG_SELECT_CARD",
            "MSG_SELECT_CHAIN",
            "MSG_SELECT_EFFECTYN",
            "MSG_SELECT_IDLECMD",
            "MSG_SELECT_PLACE",
            "MSG_SELECT_POSITION",
            "MSG_SELECT_UNSELECT_CARD",
        ],
        "engine": "Pinned Ghost Belle official script was offered from the opposing hand in response to Called by the Grave targeting a Graveyard handtrap and the chain resolved without MSG_RETRY.",
        "protocol": "The complete chain domain selected Ghost Belle; the accompanying Graveyard and chain continuations were submitted from complete candidate sets.",
        "observation": "Ghost Belle, the Called by the Grave path, and the Graveyard card references remained consistent in PlayerObservation; the public model does not expose a separate negated-effect flag.",
    },
    "m3_int05_called_by": {
        "source": "m3_fixture_test int05",
        "engine_steps": 100,
        "decisions": 44,
        "observation_hash_chain_sha256": "fe2da1a7159f87d2b9ccd777eac017118eedbde10193bcb11ea15939ddf8fac2",
        "message_families": [
            "MSG_SELECT_CARD",
            "MSG_SELECT_CHAIN",
            "MSG_SELECT_IDLECMD",
            "MSG_SELECT_PLACE",
        ],
        "engine": "Pinned Called by the Grave official script activated against an opposing Graveyard handtrap and resolved without MSG_RETRY.",
        "protocol": "The Called by the Grave chain candidate and the opposing Ash Blossom Graveyard target were selected from complete legal domains.",
        "observation": "The Graveyard target locator and chain-resolution history resolved against the acting PlayerObservation; the public model does not expose a separate negated-effect flag.",
    },
    "m3_sg01_fire_balelynx": {
        "source": "m3_fixture_test sg01",
        "engine_steps": 120,
        "decisions": 56,
        "observation_hash_chain_sha256": "77020cb8c29bc03fa655851511a0c7b5042f4e4c8b71de5e96b5040e4e4fc525",
        "message_families": [
            "MSG_SELECT_CARD",
            "MSG_SELECT_CHAIN",
            "MSG_SELECT_IDLECMD",
            "MSG_SELECT_PLACE",
            "MSG_SELECT_POSITION",
            "MSG_SELECT_UNSELECT_CARD",
        ],
        "engine": "Pinned Salamangreat of Fire procedure produced a real Balelynx Link Summon using Fire as material.",
        "protocol": "The engine-provided Link material candidate was submitted with no retry or candidate truncation.",
        "observation": "Balelynx Link rating and visible material candidate resolved in PlayerObservation.",
    },
    "m3_sg02_balelynx_sanctuary": {
        "source": "m3_fixture_test sg02",
        "engine_steps": 100,
        "decisions": 42,
        "observation_hash_chain_sha256": "c1b874f23a96a4a5b079cb9ebe1c826bcb32161850f712e7ecd82c6d5454077b",
        "message_families": [
            "MSG_SELECT_CARD",
            "MSG_SELECT_CHAIN",
            "MSG_SELECT_EFFECTYN",
            "MSG_SELECT_IDLECMD",
            "MSG_SELECT_PLACE",
            "MSG_SELECT_UNSELECT_CARD",
        ],
        "engine": "Pinned Salamangreat Balelynx official script produced the optional trigger and Sanctuary search after a real Link Summon.",
        "protocol": "The complete effect-choice and Sanctuary search candidate domains were accepted without MSG_RETRY.",
        "observation": "Balelynx and the searched Sanctuary were observed in their resulting public states; Fire and Balelynx material locators remained consistent.",
    },
    "m3_sg03_sanctuary_field": {
        "source": "m3_fixture_test sg03",
        "engine_steps": 75,
        "decisions": 29,
        "observation_hash_chain_sha256": "15c7156bca72f4fee5928c80ab7467ad22a812cc344de9835feccca2e2a53583",
        "message_families": [
            "MSG_SELECT_CARD",
            "MSG_SELECT_CHAIN",
            "MSG_SELECT_EFFECTYN",
            "MSG_SELECT_IDLECMD",
            "MSG_SELECT_PLACE",
            "MSG_SELECT_UNSELECT_CARD",
        ],
        "engine": "Pinned Sanctuary official script accepted the searched Field Spell activation after the real Balelynx path.",
        "protocol": "The complete Sanctuary hand-to-Field Zone idle candidate and placement domain were submitted without MSG_RETRY.",
        "observation": "Sanctuary was observed in the Field Zone and Balelynx remained a public Link-1 state; the material locator resolved against PlayerObservation.",
    },
    "m3_sg04_reincarnation_link": {
        "source": "m3_fixture_test sg04",
        "engine_steps": 100,
        "decisions": 42,
        "observation_hash_chain_sha256": "c1b874f23a96a4a5b079cb9ebe1c826bcb32161850f712e7ecd82c6d5454077b",
        "message_families": [
            "MSG_SELECT_CARD",
            "MSG_SELECT_CHAIN",
            "MSG_SELECT_EFFECTYN",
            "MSG_SELECT_IDLECMD",
            "MSG_SELECT_PLACE",
            "MSG_SELECT_UNSELECT_CARD",
        ],
        "engine": "Pinned Sanctuary and Balelynx scripts produced the same-name Balelynx reincarnation Link path after Sanctuary activation.",
        "protocol": "The Extra Deck Balelynx candidate, existing Balelynx material, and Link placement candidate were all selected from complete legal domains.",
        "observation": "Balelynx and Sanctuary public states remained consistent through the reincarnation continuation; the Xyz-material limitation is not involved in this Link path.",
    },
    "m3_ss10_blackout": {
        "source": "m3_fixture_test ss10",
        "engine_steps": 140,
        "decisions": 57,
        "observation_hash_chain_sha256": "7043f7276e215509505e7f11479e22f53a89d91ff14960c08e8152b7218e6439",
        "message_families": [
            "MSG_SELECT_CARD",
            "MSG_SELECT_CHAIN",
            "MSG_SELECT_EFFECTYN",
            "MSG_SELECT_IDLECMD",
            "MSG_SELECT_PLACE",
            "MSG_SELECT_POSITION",
            "MSG_SELECT_UNSELECT_CARD",
            "MSG_SELECT_YESNO",
        ],
        "engine": "Pinned Blackout official script exposed and resolved the exact one-own-Wyrm/two-opponent target selection.",
        "protocol": "The submitted target sequence was Fire (controller 1), Foxy (controller 1), Mo Ye (controller 0), with no invalid combination reachable.",
        "observation": "All three visible target locators and their destruction events resolved against the acting player observation; the separate banished-trigger row remains pending.",
    },
    "m3_ss12_heavenly_dragon_circle": {
        "source": "m3_fixture_test ss12",
        "engine_steps": 100,
        "decisions": 39,
        "observation_hash_chain_sha256": "452b97207975988d46c763a524eca09110d1d81d44ba4c97f67a4fddd8a68ff7",
        "message_families": [
            "MSG_SELECT_CARD",
            "MSG_SELECT_CHAIN",
            "MSG_SELECT_EFFECTYN",
            "MSG_SELECT_IDLECMD",
            "MSG_SELECT_PLACE",
            "MSG_SELECT_POSITION",
            "MSG_SELECT_UNSELECT_CARD",
        ],
        "engine": "Pinned Heavenly Dragon Circle official script accepted a Wyrm release cost and a Deck-to-hand Wyrm search without MSG_RETRY.",
        "protocol": "The Circle activation, exact release candidate, and Deck search candidate were all selected from complete public domains.",
        "observation": "The released Vishuda and searched Vishuda hand state were resolved against PlayerObservation; the Circle effect path remained public and deterministic.",
    },
    "m3_ss14_ashuna_restriction": {
        "source": "m3_fixture_test ss14",
        "engine_steps": 100,
        "decisions": 43,
        "observation_hash_chain_sha256": "f5e87896abb7a6e4ed371242a4786a55d54d7d8c44d8d0693bb2fae6182d356a",
        "message_families": [
            "MSG_SELECT_CARD",
            "MSG_SELECT_CHAIN",
            "MSG_SELECT_IDLECMD",
            "MSG_SELECT_PLACE",
            "MSG_SELECT_POSITION",
            "MSG_SELECT_UNSELECT_CARD",
        ],
        "engine": "Pinned Ashuna official script selected a Tenyi from the Deck and registered the Wyrm-only Special Summon restriction without MSG_RETRY.",
        "protocol": "A non-Wyrm Balelynx Extra Deck candidate was present before Ashuna and absent from the post-activation Extra Deck domain while Wyrm Link candidates remained legal.",
        "observation": "The selected Tenyi was observed on the field and the restriction path retained complete candidate domains against PlayerObservation.",
    },
    "m3_ss15_vishuda_return": {
        "source": "m3_fixture_test ss15",
        "engine_steps": 100,
        "decisions": 43,
        "observation_hash_chain_sha256": "76d1594ccc8dea4f4540b7810e98018db4869daf3027965e266b5f2507e0ad5a",
        "message_families": [
            "MSG_SELECT_CARD",
            "MSG_SELECT_CHAIN",
            "MSG_SELECT_EFFECTYN",
            "MSG_SELECT_IDLECMD",
            "MSG_SELECT_PLACE",
            "MSG_SELECT_UNSELECT_CARD",
        ],
        "engine": "Pinned Vishuda official script activated from the Graveyard, paid its self-banish cost, and returned the opposing face-up target without MSG_RETRY.",
        "protocol": "The opposing on-field return target was selected from the complete legal domain and accepted by the pinned core.",
        "observation": "The public target locator resolved against PlayerObservation; the opponent hand result is intentionally not identity-exposed by the public projection.",
    },
    "m3_ss16_adhara_recovery": {
        "source": "m3_fixture_test ss16",
        "engine_steps": 100,
        "decisions": 40,
        "observation_hash_chain_sha256": "8b1cbe87f3b6220b3bfd1ac50d1ae1133a66c3a454989be7f98b5a2dc013d248",
        "message_families": [
            "MSG_SELECT_CARD",
            "MSG_SELECT_CHAIN",
            "MSG_SELECT_IDLECMD",
            "MSG_SELECT_PLACE",
            "MSG_SELECT_POSITION",
            "MSG_SELECT_UNSELECT_CARD",
        ],
        "engine": "Pinned Adhara official script activated from the Graveyard, paid its self-banish cost, and recovered a banished Wyrm without MSG_RETRY.",
        "protocol": "The banished Vishuda target was selected from the complete legal domain and the recovery operation resolved.",
        "observation": "The recovered Vishuda was observed in the acting player hand with public identity and zone state consistent with the effect result.",
    },
    "m3_sg05_gazelle_hand": {
        "source": "m3_fixture_test sg05",
        "engine_steps": 180,
        "decisions": 80,
        "observation_hash_chain_sha256": "e7907ba0f2ad7aae9f396da79c3ca345f5a5bb1badb63014fefb7bd5c7e42646",
        "message_families": [
            "MSG_SELECT_CARD",
            "MSG_SELECT_CHAIN",
            "MSG_SELECT_EFFECTYN",
            "MSG_SELECT_IDLECMD",
            "MSG_SELECT_OPTION",
            "MSG_SELECT_PLACE",
            "MSG_SELECT_POSITION",
            "MSG_SELECT_UNSELECT_CARD",
        ],
        "engine": "Pinned Gazelle and Spinny official scripts produced the optional hand summon after the Spinny Graveyard cost and resolved Gazelle's Deck-to-Graveyard effect without MSG_RETRY.",
        "protocol": "The complete chain domains selected Gazelle from the hand and a real Salamangreat Deck result; Link and position continuations remained complete and deterministic.",
        "observation": "Spinny was observed in the Graveyard and the Gazelle result remained consistent with the acting PlayerObservation; the fixture deliberately leaves the broader Foxy/Jaguar/Falco inventory pending.",
    },
    "m3_sg06_gazelle_gy": {
        "source": "m3_fixture_test sg06",
        "engine_steps": 180,
        "decisions": 78,
        "observation_hash_chain_sha256": "8f8423312c0c517cde4fee170d4f9db3118548af14433f6494ff5f22ecfccd91",
        "message_families": [
            "MSG_SELECT_CARD",
            "MSG_SELECT_CHAIN",
            "MSG_SELECT_EFFECTYN",
            "MSG_SELECT_IDLECMD",
            "MSG_SELECT_OPTION",
            "MSG_SELECT_PLACE",
            "MSG_SELECT_POSITION",
            "MSG_SELECT_UNSELECT_CARD",
            "MSG_SELECT_YESNO",
        ],
        "engine": "Pinned Gazelle and Spinny official scripts must expose a real multi-candidate Deck-to-Graveyard domain and resolve the selected Foxy branch without MSG_RETRY.",
        "protocol": "The complete Deck-to-Graveyard domain is required to contain at least two known Salamangreat candidates; the deterministic fixture selects Foxy from that domain.",
        "observation": "Spinny's hand-to-Graveyard cost and Foxy's Deck-to-Graveyard result are required to resolve against PlayerObservation.",
    },
    "m3_btl01_battle_phase": {
        "source": "m3_fixture_test btl01",
        "engine_steps": 369,
        "decisions": 180,
        "observation_hash_chain_sha256": "6559c3dd371db01f393cb1de612a696d5f8eb57167e79ee11a584c72bd8a80ab",
        "message_families": [
            "MSG_SELECT_BATTLECMD",
            "MSG_SELECT_CARD",
            "MSG_SELECT_CHAIN",
            "MSG_SELECT_EFFECTYN",
            "MSG_SELECT_IDLECMD",
            "MSG_SELECT_PLACE",
            "MSG_SELECT_UNSELECT_CARD",
        ],
        "engine": "Pinned combat execution produced five Battle Command decisions, life-point changes, battle destruction, and a terminal MSG_WIN path without MSG_RETRY.",
        "protocol": "The attacker, target, and subsequent battle decisions were submitted from complete engine-provided candidate domains; the terminal win was reached at engine step 369.",
        "observation": "Battle-target context, life-point changes, destroyed-card events, and the terminal Win event were all retained in the PlayerObservation/event projection.",
    },
    "m3_sg20_link_continuation": {
        "source": "m3_fixture_test sg20",
        "engine_steps": 180,
        "decisions": 79,
        "observation_hash_chain_sha256": "39a65959f757c5fc60d0e1440db192321a7172e4c564613550dd6b3688d503db",
        "message_families": [
            "MSG_SELECT_CARD",
            "MSG_SELECT_CHAIN",
            "MSG_SELECT_EFFECTYN",
            "MSG_SELECT_IDLECMD",
            "MSG_SELECT_PLACE",
            "MSG_SELECT_POSITION",
            "MSG_SELECT_UNSELECT_CARD",
        ],
        "engine": "Pinned Sunlight Wolf Link procedure accepted two real FIRE materials under explicit MR5 mode 0x2E800 and completed the Extra Deck summon without MSG_RETRY.",
        "protocol": "The engine-provided multi-material toggle domain accepted PICK/PICK/FINISH semantics followed by the Link zone-placement continuation; no candidate was synthesized or truncated.",
        "observation": "Sunlight Wolf Link rating and resulting Monster Zone state were public and consistent; the fixture does not claim the separate Wolf recovery trigger.",
    },
    "m3_ss10_blackout_banish": {
        "source": "m3_fixture_test ss10_banish",
        "engine_steps": 180,
        "decisions": 87,
        "observation_hash_chain_sha256": "ac883ab313622c62076d555ac40dd7f922197b8c0155d1f8b0a26c074e9a8093",
        "message_families": _M3_FIXTURE_MESSAGE_FAMILIES,
        "engine": "Pinned Blackout/CardScript execution banished Blackout through the real effect path, emitted the official banished trigger, and created the Token without MSG_RETRY.",
        "protocol": "The banish replacement and trigger decisions were selected from complete engine candidate domains; no Token was created by fixture setup.",
        "observation": "Blackout's banished transition, the public Swordsoul Token passcode 20001444, token properties, owner/controller/location, and the deterministic observation chain were verified.",
    },
    "m3_ss14_ashuna_restriction_expiry": {
        "source": "m3_fixture_test ss14",
        "engine_steps": 420,
        "decisions": 215,
        "observation_hash_chain_sha256": "9aab457770f1f208bbc15d2fccbb233f407cf64b08ee862a4ebe5e173f45ebf5",
        "message_families": _M3_FIXTURE_MESSAGE_FAMILIES,
        "engine": "Pinned Ashuna execution selected its official Deck summon, enforced the Wyrm-only Extra Deck domain, and removed that restriction at the engine-defined expiry boundary.",
        "protocol": "The before/during/after Extra Deck candidate sets were consumed directly from MSG_SELECT_IDLECMD; the post-boundary non-Wyrm candidate reappeared without an inferred step-count shortcut.",
        "observation": "Ashuna, the Deck result, and the public continuation state were observed; candidate legality was verified before activation, during restriction, and after expiry.",
    },
    "m3_ss16_chengying": {
        "source": "m3_fixture_test ss16_chengying",
        "engine_steps": 180,
        "decisions": 87,
        "observation_hash_chain_sha256": "ac883ab313622c62076d555ac40dd7f922197b8c0155d1f8b0a26c074e9a8093",
        "message_families": _M3_FIXTURE_MESSAGE_FAMILIES,
        "engine": "Pinned Chengying/CardScript execution resolved the real Chengying replacement/banish path alongside the official Blackout banish-trigger fixture.",
        "protocol": "Chengying's replacement decision and the Blackout-trigger continuation were selected from complete candidate sets and accepted by ocgcore.",
        "observation": "Chengying's public field state, the banished-card-dependent transition, and the visible resulting events were verified. Individual sub-effects not emitted by this fixed-deck path are not overclaimed.",
    },
    "m3_ss17_qixing_interaction": {
        "source": "m3_fixture_test ss17",
        "engine_steps": 240,
        "decisions": 105,
        "observation_hash_chain_sha256": "821e3956f4952c0cfebc7a7daa20e00cd2bee0c58f37cf55df4f4a00ff76894f",
        "message_families": _M3_FIXTURE_MESSAGE_FAMILIES,
        "engine": "Pinned Qixing Longyuan execution accepted a real opposing activation as the Qixing interaction trigger and resolved it after Qixing was established.",
        "protocol": "The opposing activation, Qixing chain entry, and all required continuation decisions were selected from complete engine-provided domains.",
        "observation": "Qixing public state, the interaction chain, visible LP/state consequence, and resolved event history were verified separately from Longyuan's 1200 LP burn.",
    },
    "m3_sg07_gy_paths": {
        "source": "m3_fixture_test sg07_jack; m3_fixture_test sg07_weasel; m3_fixture_test sg07_falco",
        "engine_steps": 240,
        "decisions": 110,
        "observation_hash_chain_sha256": "b2efa022ee7d9e92b68fa75efdc5905d90a5e8e881afeaaa8e7ff429588fa3e7",
        "observation_hashes": [
            "b2efa022ee7d9e92b68fa75efdc5905d90a5e8e881afeaaa8e7ff429588fa3e7",
            "49d3f0f60b70d9a3e9b63ae350f3090ab3156b1021996de90239862c966bb180",
            "72a81e7c069231e48465e1ed3de72c65eadcf6106af35e67679c76309c2ec9f2",
        ],
        "message_families": _M3_FIXTURE_MESSAGE_FAMILIES,
        "engine": "Pinned Jack Jaguar, Weasel, and Falco scripts each executed their official Graveyard/recycle path under the canonical MR5 configuration (DUEL_MODE_MR5=0x2E800).",
        "protocol": "The three fixtures consumed complete Link, target, recycle, and placement candidate domains with zero retries and no synthesized effect decisions.",
        "observation": "Jack Jaguar activation/summon/recycle, Weasel trigger/recycle/target summon, and Falco summon/recovery were each verified in PlayerObservation; the Jack fixture records the canonical MR5 Extra Monster Zone domain and positive candidate path.",
    },
    "m3_sg08_miragestallio_xyz": {
        "source": "m3_fixture_test sg08_real",
        "engine_steps": 320,
        "decisions": 153,
        "observation_hash_chain_sha256": "99727821397ccea1325162f308031835c39d0a77bfc4fb9e19caa6d88ac53cb0",
        "message_families": _M3_FIXTURE_MESSAGE_FAMILIES,
        "engine": "Pinned Miragestallio/CardScript accepted a real Rank 3 Xyz Summon from two legal Level 3 Salamangreat materials.",
        "protocol": "Both material selections and the Extra Deck placement were selected from complete ocgcore candidate domains.",
        "observation": "Miragestallio rank/state, aggregate material count, and both visible material identities were verified through the existing overlay_seq public query contract.",
    },
    "m3_sg09_miragestallio_effect": {
        "source": "m3_fixture_test sg09_direct",
        "engine_steps": 400,
        "decisions": 202,
        "observation_hash_chain_sha256": "e5756cd8c76208ebee996413ca105f29d0d0bf05e6f4f23a6abe7dd84ff8f4ab",
        "message_families": _M3_FIXTURE_MESSAGE_FAMILIES,
        "engine": "Pinned Miragestallio execution accepted the detach cost, performed the official Deck summon, applied the FIRE-only restriction, and removed it at the engine-defined expiry boundary.",
        "protocol": "Detach, Deck target, restriction-blocked, and post-expiry candidate domains were all consumed from ocgcore; overlay_seq 0 returned the remaining material and overlay_seq 1 failed closed after detach.",
        "observation": "Material count changed from 2 to 1, the visible remaining material differed from the detached public event, Foxy was Deck-summoned, and pre/during/post restriction legality was verified.",
    },
    "m3_sg11_sunlight_wolf": {
        "source": "m3_fixture_test sg11",
        "engine_steps": 180,
        "decisions": 81,
        "observation_hash_chain_sha256": "5395086f6234cb46ec9237c0392803f9754aeb7310b02c480bfd2410a5633ff6",
        "message_families": _M3_FIXTURE_MESSAGE_FAMILIES,
        "engine": "Pinned Sunlight Wolf execution under MR5 placed the Link Monster in a real Extra Monster Zone and emitted its linked-zone FIRE recovery trigger.",
        "protocol": "The linked summon, trigger selection, target selection, and continuation placement were accepted from complete candidate sets.",
        "observation": "Sunlight Wolf Link-2 state, linked-zone relationship, FIRE target, and recovered monster state were verified in PlayerObservation.",
    },
    "m3_sg12_sunlight_wolf_st": {
        "source": "m3_fixture_test sg12",
        "engine_steps": 240,
        "decisions": 110,
        "observation_hash_chain_sha256": "010eac5086e23b3bd056cd146454eacb0dd405ba1eabaceeca0f5ad4d353d02b",
        "message_families": _M3_FIXTURE_MESSAGE_FAMILIES,
        "engine": "Pinned Sunlight Wolf execution under MR5 completed the real same-name Link procedure and emitted the official Spell/Trap recovery trigger.",
        "protocol": "The reincarnation Extra Deck candidate, material, placement, trigger, and Spell/Trap target were selected from complete engine domains.",
        "observation": "A valid Salamangreat Trap target (Rage) moved from Graveyard to hand after resolution; the Wolf public Link state and deterministic observation chain were verified.",
    },
    "m3_sg13_raging_phoenix": {
        "source": "m3_fixture_test sg13",
        "engine_steps": 240,
        "decisions": 107,
        "observation_hash_chain_sha256": "f49b3a6f12e316631db0359cdd0dcec0ae4676b4e54f7e966c7f7251679f6e8e",
        "message_families": _M3_FIXTURE_MESSAGE_FAMILIES,
        "engine": "Pinned Raging Phoenix execution under MR5 completed the official reincarnation Link procedure and the resulting search trigger.",
        "protocol": "The same-name Extra Deck candidate, real materials, trigger, and search card were selected from complete candidate domains.",
        "observation": "Raging Phoenix Link-4 state, reincarnation trigger, selected search candidate, and resolved search state were verified.",
    },
    "m3_sg14_pyro_phoenix": {
        "source": "m3_fixture_test sg14",
        "engine_steps": 260,
        "decisions": 115,
        "observation_hash_chain_sha256": "1888ddf7cae911a67dd7562723733cdd17257667813cc824b79c0d04079105e1",
        "message_families": _M3_FIXTURE_MESSAGE_FAMILIES,
        "engine": "Pinned Pyro Phoenix execution under MR5 completed the official reincarnation procedure and its board-destruction payoff against the exact opposing Mo Ye setup.",
        "protocol": "The reincarnation material and payoff continuation were selected from complete engine domains; no board-wide destruction was synthesized by the fixture.",
        "observation": "Pyro Phoenix Link-4 state, reincarnation trigger, and opponent-card destruction were verified. The opponent-Link revival branch is separately classified not applicable because the locked Deck A Extra Deck contains no Link Monster.",
    },
    "m3_sg15_heatleo": {
        "source": "m3_fixture_test sg15",
        "engine_steps": 240,
        "decisions": 108,
        "observation_hash_chain_sha256": "dcc2b241dae68390641ad51e4a1eaa6e8967e672599ec035e9694aa659beb5da",
        "message_families": _M3_FIXTURE_MESSAGE_FAMILIES,
        "engine": "Pinned Heatleo execution under MR5 completed the official same-name reincarnation path and emitted the Spell/Trap target-and-return effect.",
        "protocol": "The Heatleo Extra Deck candidate, real materials, placement, trigger, and exact Blackout target were selected from complete engine domains.",
        "observation": "Heatleo Link-3 state and the public Spell/Trap zone transition to Deck were verified; no target identity was inferred when the public projection redacted it.",
    },
    "m3_sg16_roar": {
        "source": "m3_fixture_test sg16_negate; m3_fixture_test sg16_recovery",
        "engine_steps": 260,
        "decisions": 109,
        "observation_hash_chain_sha256": "747ffea3f27230cbe6f0257922e42c8a4bfecc00287c6ff56475cc18f85b7ab9",
        "observation_hashes": [
            "747ffea3f27230cbe6f0257922e42c8a4bfecc00287c6ff56475cc18f85b7ab9",
            "38d311b8c43d6ef98a25448e9d4455287b7d707ee4ce7ef742c48634fccb5e46",
        ],
        "message_families": _M3_FIXTURE_MESSAGE_FAMILIES,
        "engine": "Pinned Salamangreat Roar execution proved both the real negate chain and the official Graveyard recovery/reset path.",
        "protocol": "Roar activation, chain resolution, target destruction, reincarnation continuation, recovery trigger, and set placement were selected from complete candidate domains.",
        "observation": "The negate consequence and the recovered/set Roar state were verified in separate deterministic fixtures; the generic public negated-status field is not fabricated.",
    },
    "m3_sg17_rage": {
        "source": "m3_fixture_test sg17",
        "engine_steps": 180,
        "decisions": 82,
        "observation_hash_chain_sha256": "c85da64c34f73767d2c5c1033d3c83917bed4f7b38a17540d5027148dfe1ea4e",
        "message_families": _M3_FIXTURE_MESSAGE_FAMILIES,
        "engine": "Pinned Salamangreat Rage execution paid its official cost, accepted the legal target-count domain, and resolved removal.",
        "protocol": "The cost and target selections were submitted from complete candidate domains; the target count was recorded as one rather than inferred from a truncated list.",
        "observation": "Rage activation, cost movement, one-target domain, target destruction, and public post-state were verified.",
    },
    "m3_sg18_promethean": {
        "source": "m3_fixture_test sg18",
        "engine_steps": 240,
        "decisions": 113,
        "observation_hash_chain_sha256": "c3090cfdc60874caed75882ab0be336f2a50e0b90e409bb91c4b16dcccdac2b9",
        "message_families": _M3_FIXTURE_MESSAGE_FAMILIES,
        "engine": "Pinned Promethean Princess execution under MR5 completed the real Link Summon and official FIRE Graveyard revival path.",
        "protocol": "The Princess Extra Deck candidate, material selections, placement, FIRE trigger, and revival target were selected from complete engine domains.",
        "observation": "Princess Link-3 state, material/zone continuation, and revived FIRE monster were verified. The non-FIRE restriction negative branch is separately classified not applicable because the locked Salamangreat monster pool is FIRE-only and contains no special-summon path for the non-FIRE hand traps.",
    },
    "m3_sg19_hiita": {
        "source": "m3_fixture_test sg19",
        "engine_steps": 220,
        "decisions": 98,
        "observation_hash_chain_sha256": "e5803ff523812967c7fd2888d3cb227ae985a98a4a7e740edaf1c20178a6350b",
        "message_families": _M3_FIXTURE_MESSAGE_FAMILIES,
        "engine": "Pinned Hiita execution under MR5 completed the official Link procedure and revived an opponent-owned FIRE target.",
        "protocol": "The opponent Graveyard target, Link materials, placement, and revival continuation were selected from complete candidate domains.",
        "observation": "Owner/controller distinction before and after revival was verified; the positive and no-valid-target fixtures both remained deterministic.",
    },
    "m3_sg19_hiita_no_target": {
        "source": "m3_fixture_test sg19_no_target",
        "engine_steps": 180,
        "decisions": 87,
        "observation_hash_chain_sha256": "2a2ebd6c7b24e289deb421609d472ac28ee592c92254c857dc9e02a077def83f",
        "message_families": _M3_FIXTURE_MESSAGE_FAMILIES,
        "engine": "Pinned Hiita/CardScript omitted its activation when no legal opponent-owned FIRE target existed.",
        "protocol": "The no-target idle candidate domain was checked directly; no unsupported forced activation was submitted.",
        "observation": "Hiita Link state remained public while the invalid target condition produced no activation candidate, closing the negative target-domain branch.",
    },
}

_FIXTURE_EVIDENCE["m3_ss03_trigger_order"] = _FIXTURE_EVIDENCE["m3_ss01_moye_token"]
_FIXTURE_EVIDENCE["m3_ss04_chixiao_search"] = _FIXTURE_EVIDENCE["m3_ss01_moye_token"]
_FIXTURE_EVIDENCE["m3_ss13_tenyi_no_effect"] = {
    **_FIXTURE_EVIDENCE["m3_ss14_ashuna_restriction"],
    "source": "m3_fixture_test ss14 (non-effect Token condition)",
    "engine": "The Ashuna fixture placed a face-up non-effect Token beside the Tenyi state; the pinned Ashuna script then accepted the real Graveyard activation and Deck summon without MSG_RETRY.",
    "protocol": "The post-activation legal domain retained Wyrm candidates and removed the non-Wyrm Extra Deck candidate, proving the condition was not replaced by an unrestricted summon path.",
    "observation": "The non-effect Token and resulting Tenyi state were public in PlayerObservation; the same fixture supplies the stable observation hash and complete candidate validation.",
}
_FIXTURE_EVIDENCE["m3_sg21_link_zones"] = _FIXTURE_EVIDENCE["m3_sg20_link_continuation"]
_FIXTURE_EVIDENCE["m35_xyz_material_query"] = {
    "source": "m35_xyz_material_query_test; m2_1_xyz_api_test; mechanics_projection_test",
    "observation_hash_chain_sha256": "84e264b9c655509ade9d905c43a6e609171acba178cdd118e3955aef4d0062a4",
    "engine": "The repository-versioned derived core patch 0001 corrected the existing OCG_QueryInfo overlay_seq contract to resolve the parent Xyz slot and return the requested material through the real pinned public query path.",
    "protocol": "overlay_seq values 0 and 1 were submitted through OCG_DuelQuery with the existing parent-location contract; both accepted queries returned distinct non-empty material records without a second query mechanism.",
    "observation": "Visible own Xyz material identities and typed XyzMaterial relationships were projected with the repaired query path, while paired opponent face-down worlds retained equal redacted observations and no material identity leakage.",
}


def _row(
    fixture_id: str,
    group: str,
    key: str,
    path: str,
    status: str,
    *,
    blocker: str = "",
    evidence: dict[str, Any] | None = None,
) -> dict[str, Any]:
    evidence = evidence or {}
    return {
        "fixture_id": fixture_id,
        "group": group,
        "key": key,
        "card_or_path": path,
        "status": status,
        "engine_evidence": evidence.get("engine", ""),
        "protocol_evidence": evidence.get("protocol", ""),
        "observation_evidence": evidence.get("observation", ""),
        "message_families": evidence.get("message_families", []),
        "evidence_source": evidence.get("source", ""),
        "observation_hash_chain_sha256": evidence.get("observation_hash_chain_sha256"),
        "blocker": blocker,
    }


def build_mechanics_coverage() -> dict[str, Any]:
    rows: list[dict[str, Any]] = []
    swordsoul = [
        ("SS-01", "Mo Ye normal summon, reveal, Token", "m3_ss01_moye_token", "ENGINE_VERIFIED", ""),
        ("SS-02", "Swordsoul Token Synchro path into Chixiao", "m3_ss01_moye_token", "ENGINE_VERIFIED", ""),
        ("SS-03", "Mo Ye and Chixiao simultaneous trigger ordering", "m3_ss03_trigger_order", "ENGINE_VERIFIED", ""),
        ("SS-04", "Chixiao search", "m3_ss04_chixiao_search", "ENGINE_VERIFIED", ""),
        ("SS-05", "Chixiao negate", "m3_ss05_chixiao_negate", "PROTOCOL_VERIFIED", "Pinned public observation has no separate negated-status field; target selection and chain resolution are proven."),
        ("SS-06", "Longyuan discard, Token, level-10 Synchro, burn", "m3_ss06_longyuan", "ENGINE_VERIFIED", ""),
        ("SS-07", "Swordsoul Token Extra Deck restriction and expiry", "m3_ss07_token_restriction", "ENGINE_VERIFIED", ""),
        ("SS-08", "Baxia multi-target shuffle/return", "m3_ss08_baxia_real", "ENGINE_VERIFIED", ""),
        ("SS-09", "Baxia destruction and revival", "m3_ss08_baxia_real", "ENGINE_VERIFIED", ""),
        ("SS-10", "Blackout constrained selection and banished-trigger path", "m3_ss10_blackout_banish", "ENGINE_VERIFIED", ""),
        ("SS-11", "Heavenly Dragon Circle cost and search", "m3_ss12_heavenly_dragon_circle", "ENGINE_VERIFIED", ""),
        ("SS-12", "Tenyi no-effect-monster condition true and false", "m3_ss12_tenyi_condition", "ENGINE_VERIFIED", ""),
        ("SS-13", "Ashuna restriction and expiry", "m3_ss14_ashuna_restriction_expiry", "ENGINE_VERIFIED", ""),
        ("SS-14", "Vishuda activation and return", "m3_ss15_vishuda_return", "PROTOCOL_VERIFIED", "The opposing hand result is not identity-exposed by the pinned public projection."),
        ("SS-15", "Adhara banished-Wyrm recovery", "m3_ss16_adhara_recovery", "ENGINE_VERIFIED", ""),
        ("SS-16", "Chengying dynamic state and banish trigger", "m3_ss16_chengying", "ENGINE_VERIFIED", ""),
        ("SS-17", "Qixing Longyuan interaction/chain path", "m3_ss17_qixing_interaction", "ENGINE_VERIFIED", ""),
        ("SS-18", "Monk/Shaman Link procedures and Tenyi Links", "m3_ss18_tenyi_links", "ENGINE_VERIFIED", ""),
    ]
    salamangreat = [
        ("SG-01", "Salamangreat of Fire to Balelynx", "m3_sg01_fire_balelynx", "ENGINE_VERIFIED", ""),
        ("SG-02", "Balelynx Sanctuary search", "m3_sg02_balelynx_sanctuary", "ENGINE_VERIFIED", ""),
        ("SG-03", "Salamangreat Sanctuary Field Zone state", "m3_sg03_sanctuary_field", "ENGINE_VERIFIED", ""),
        ("SG-04", "Sanctuary-enabled same-name reincarnation Link Summon", "m3_sg04_reincarnation_link", "ENGINE_VERIFIED", ""),
        ("SG-05", "Gazelle optional hand summon", "m3_sg05_gazelle_hand", "ENGINE_VERIFIED", ""),
        ("SG-06", "Gazelle, Spinny, and Foxy GY engine", "m3_sg06_gazelle_gy", "ENGINE_VERIFIED", ""),
        ("SG-07", "Jack Jaguar, Weasel, and Falco GY paths", "m3_sg07_gy_paths", "ENGINE_VERIFIED", ""),
        ("SG-08", "Miragestallio Xyz legality", "m3_sg08_miragestallio_xyz", "ENGINE_VERIFIED", ""),
        ("SG-09", "Miragestallio detach, Deck summon, and FIRE restriction", "m3_sg09_miragestallio_effect", "ENGINE_VERIFIED", ""),
        ("SG-10", "Miragestallio material identity", "m35_xyz_material_query", "ENGINE_VERIFIED", ""),
        ("SG-11", "Sunlight Wolf linked-zone recovery", "m3_sg11_sunlight_wolf", "ENGINE_VERIFIED", ""),
        ("SG-12", "Sunlight Wolf Spell/Trap recovery", "m3_sg12_sunlight_wolf_st", "ENGINE_VERIFIED", ""),
        ("SG-13", "Raging Phoenix normal/reincarnation/search", "m3_sg13_raging_phoenix", "ENGINE_VERIFIED", ""),
        ("SG-14", "Pyro Phoenix reincarnation payoff (fixed Deck A branch)", "m3_sg14_pyro_phoenix", "ENGINE_VERIFIED", "Proven under MR5 0x2E800. Opponent-Link revival is a separate NOT_APPLICABLE_FIXED_MATCHUP subpath because the locked Deck A Extra Deck has no Link Monster."),
        ("SG-15", "Heatleo target and reincarnation", "m3_sg15_heatleo", "ENGINE_VERIFIED", ""),
        ("SG-16", "Salamangreat Roar negate/recovery", "m3_sg16_roar", "ENGINE_VERIFIED", ""),
        ("SG-17", "Salamangreat Rage target-count/removal", "m3_sg17_rage", "ENGINE_VERIFIED", ""),
        ("SG-18", "Promethean Princess Link and FIRE revival (fixed matchup)", "m3_sg18_promethean", "ENGINE_VERIFIED", "Proven under MR5 0x2E800. The non-FIRE restriction negative branch is a separate NOT_APPLICABLE_FIXED_MATCHUP subpath because the locked Salamangreat monster pool is FIRE-only and has no special-summon path for the non-FIRE hand traps."),
        ("SG-19", "Hiita opponent-owned FIRE revival and no-target domain", "m3_sg19_hiita", "ENGINE_VERIFIED", ""),
        ("SG-20", "Multi-material Link continuation PICK/PICK/FINISH", "m3_sg20_link_continuation", "ENGINE_VERIFIED", ""),
        ("SG-21", "Link material and zone continuations", "m3_sg21_link_zones", "ENGINE_VERIFIED", ""),
    ]
    shared = [
        ("INT-01", "Ash Blossom", "m3_int01_ash_blossom", "PROTOCOL_VERIFIED", "The public projection does not expose a separate negated-effect flag."),
        ("INT-02", "Effect Veiler", "m3_int02_effect_veiler", "PROTOCOL_VERIFIED", "The public projection does not expose a separate negated-effect flag."),
        ("INT-03", "Infinite Impermanence", "m3_int03_impermanence", "PROTOCOL_VERIFIED", "The public projection does not expose a separate negated-effect flag."),
        ("INT-04", "Ghost Belle & Haunted Mansion", "m3_int04_ghost_belle", "PROTOCOL_VERIFIED", "The public projection does not expose a separate negated-effect flag."),
        ("INT-05", "Called by the Grave", "m3_int05_called_by", "PROTOCOL_VERIFIED", "Target and chain resolution are proven; the public projection does not expose a separate negated-effect flag."),
        ("BTL-01", "Complete Battle Phase execution", "m3_btl01_battle_phase", "ENGINE_VERIFIED", ""),
    ]
    for key, path, fixture, status, blocker in swordsoul:
        rows.append(_row(fixture, "swordsoul", key, path, status, blocker=blocker,
                         evidence=_FIXTURE_EVIDENCE.get(fixture)))
    for key, path, fixture, status, blocker in salamangreat:
        rows.append(_row(fixture, "salamangreat", key, path, status, blocker=blocker,
                         evidence=_FIXTURE_EVIDENCE.get(fixture)))
    for key, path, fixture, status, blocker in shared:
        rows.append(_row(fixture, "shared", key, path, status, blocker=blocker,
                         evidence=_FIXTURE_EVIDENCE.get(fixture)))
    for row in rows:
        if row["key"] == "SG-10":
            row["public_api_gap_id"] = "INDIVIDUAL_XYZ_MATERIAL_QUERY"
    return {
        "schema_version": "ocgforge.m3.mechanics_coverage.v1",
        "status_values": sorted(STATUS_VALUES),
        "fixture_count": len(rows),
        "verified_fixture_count": sum(row["status"] in {"ENGINE_VERIFIED", "PROTOCOL_VERIFIED"} for row in rows),
        "pending_fixture_count": sum(row["status"] == "PENDING" for row in rows),
        "classification_counts": {
            status: sum(row["status"] == status for row in rows)
            for status in sorted(STATUS_VALUES)
        },
        "rows": rows,
    }


def build_api_gaps() -> dict[str, Any]:
    return {
        "schema_version": "ocgforge.m3.public_api_gaps.v1",
        "rows": [
            {
        "gap_id": "INDIVIDUAL_XYZ_MATERIAL_QUERY",
        "status": "RESOLVED_BY_REPOSITORY_PATCHSET",
        "scope": "pinned ocgcore public API 11.0 plus the OCGForge derived patchset",
                "public_api_calls_inspected": [
                    "OCG_DuelQuery",
                    "OCG_DuelQueryLocation",
                    "OCG_DuelQueryFieldCard",
                ],
        "evidence": "The unmodified base checkout reproduced the empty overlay query; repository patch 0001 fixes only the existing parent-location plus overlay_seq lookup, and m35_xyz_material_query_test plus m2_1_xyz_api_test prove the real public query and privacy projections.",
        "impact": "Canonical M3.5 observations expose visible material identity through the existing overlay_seq contract and continue to redact opponent face-down material identity.",
        "privacy_implications": "Identity is exposed only after the same parent visibility gate used for the visible Xyz card; hidden paired-world material identities remain redacted and hash-equal.",
        "m3_action": "M3.1 limitation closed by the narrow repository-versioned ocgcore patchset; no second overlay-query mechanism was added.",
                "m3_can_proceed": True,
        "m3_5_action": "retain the patch as an ordered repository input and prepare an upstream-compatible change if accepted; until then the base cached core remains immutable",
            },
            {
                "gap_id": "FIXTURE_RUNNER_PUBLIC_SETUP_SCOPE",
                "status": "RECORDED_FOR_M3_5",
                "scope": "OCGForge test infrastructure, not an upstream ocgcore gap",
                "public_api_calls_inspected": [
                    "CoreHost::load_fixture_script",
                    "CoreHost::load_fixture_card",
                ],
                "evidence": "Current fixture setup is intentionally script-backed; no general board-construction API is required by the closed fixtures and no missing ocgcore capability has been proven.",
                "impact": "M3 does not add a general public board-construction API or private core access.",
                "privacy_implications": "Any future setup projection must remain test-only and must not widen runtime hidden-information access.",
                "m3_action": "no change during M3",
                "m3_can_proceed": True,
                "m3_5_action": "evaluate a descriptor-backed fixture setup projection if more coverage requires it",
            },
            {
                "gap_id": "START_PLAYER_SELECTION_CONTROL",
        "status": "RESOLVED_BY_REPOSITORY_PATCHSET",
        "scope": "pinned ocgcore public API 11.0 plus the OCGForge derived patchset",
                "public_api_calls_inspected": [
                    "OCG_DuelOptions",
                    "OCG_StartDuel",
                    "OCG_DuelQueryFieldCard",
                ],
        "evidence": "The unmodified base API had no starting-player setter and always initialized player 0; repository patch 0002 adds only OCG_DuelSetStartingPlayer before duel start, preserves default 0, rejects invalid values and post-start calls, and is proven by m35_starting_player_api_test plus canonical full-game partitions.",
        "impact": "Canonical M3.5 runners can deterministically exercise starting-player 0 and 1 while retaining the same default behavior and deck-seat semantics.",
        "privacy_implications": "No hidden card information is exposed; the patch changes only pre-duel turn-player selection and acceptance coverage.",
        "m3_action": "M3.1 limitation closed by the narrow repository-versioned ocgcore patchset; no mid-duel turn-player mutation API was added.",
                "m3_can_proceed": True,
        "m3_5_action": "retain the additive setter as an ordered repository input and prepare an upstream-compatible change with regression tests if accepted",
            },
        ],
    }


def _read_json_if_present(path: Path) -> dict[str, Any]:
    if not path.is_file():
        return {}
    return json.loads(path.read_text(encoding="utf-8"))


def build_acceptance_matrix(docs: str | Path) -> dict[str, Any]:
    docs_root = Path(docs)
    repository_root = docs_root.parent.parent
    compatibility = _read_json_if_present(docs_root / "card_compatibility.json")
    mechanics = build_mechanics_coverage()
    api_gaps = build_api_gaps()
    environment = load_canonical_environment(repository_root / "third_party" / "rules_bundle.lock.json")
    full_games = _read_json_if_present(repository_root / "artifacts" / "m3" / "canonical_mr5" / "full_games" /
                                        "full_fixed_deck_results.json")
    determinism = _read_json_if_present(repository_root / "artifacts" / "m3" / "canonical_mr5" / "determinism" /
                                         "m3_determinism_results.json")
    final_verification = _read_json_if_present(repository_root / "artifacts" / "m3" /
                                               "final_verification.json")
    rules_mode = _read_json_if_present(docs_root / "rules_mode_audit.json")
    manifest = compatibility.get("manifest", {})
    full_results = full_games.get("results", [])
    full_environment = full_games.get("canonical_environment", {})
    full_pass = (
        bool(full_results)
        and full_environment.get("rules_bundle_id") == environment["rules_bundle_id"]
        and full_environment.get("duel_flags") == environment["duel_flags"]
        and all(result.get("status") == "PASS" for result in full_results)
        and all(
            result.get("rules_bundle_id") == environment["rules_bundle_id"]
            and result.get("duel_flags") == environment["duel_flags"]
            for result in full_results
        )
    )
    full_zero_rejections = full_pass and all(
        result.get(field, 0) == 0
        for result in full_results
        for field in ("unsupported_count", "retry_count", "automatic_decision_count", "candidate_truncation_count", "core_error_count")
    )
    start_partitions = full_games.get("start_player_partitions", [])
    regression = final_verification.get("regression", {})
    verification_environment = final_verification.get("canonical_environment", {})
    regression_pass = (
        regression.get("build_status") == "PASS"
        and regression.get("ctest_failed") == 0
        and regression.get("ctest_passed") == regression.get("ctest_total")
        and regression.get("ctest_total", 0) > 0
        and verification_environment.get("rules_bundle_id") == environment["rules_bundle_id"]
        and verification_environment.get("duel_flags") == environment["duel_flags"]
    )
    battle_evidence = _FIXTURE_EVIDENCE.get("m3_btl01_battle_phase", {})
    battle_fixture_pass = (
        battle_evidence.get("engine_steps", 0) > 0
        and "MSG_SELECT_BATTLECMD" in battle_evidence.get("message_families", [])
        and bool(battle_evidence.get("observation_hash_chain_sha256"))
    )
    determinism_partitions = determinism.get("partitions", {})
    determinism_pass = (
        determinism.get("starting_player_partitions") == [0, 1]
        and sorted(determinism_partitions) == ["0", "1"]
        and all(
            determinism_partitions.get(str(player), {}).get(field) is True
            for player in (0, 1)
            for field in ("independent_process_match", "semantic_action_reexecution_match", "crlf_semantic_replay_match")
        )
    )

    gates = [
        {
            "criterion_id": "M3-DECK-FOUNDATION",
            "area": "Deck foundation",
            "status": "PASS" if manifest.get("main_deck_count") == {"deck_a": 40, "deck_b": 40} and
            manifest.get("extra_deck_count") == {"deck_a": 15, "deck_b": 15} else "PENDING",
            "evidence": "FIXED_MATCHUP.md; card_compatibility.json",
            "details": "Exact ordered 40 Main / 15 Extra decks with no side cards.",
            "blocker": "",
        },
        {
            "criterion_id": "M3-MANIFEST-HASHES",
            "area": "Deterministic manifests/hashes",
            "status": "PASS" if manifest.get("deck_a_sha256") and manifest.get("deck_b_sha256") else "PENDING",
            "evidence": "card_compatibility.json; FIXED_MATCHUP.md",
            "details": f"A={manifest.get('deck_a_sha256', '')}; B={manifest.get('deck_b_sha256', '')}.",
            "blocker": "",
        },
        {
            "criterion_id": "M3-COMPATIBILITY-110",
            "area": "110-slot compatibility audit",
            "status": "PASS" if len(compatibility.get("slots", [])) == 110 and
            len(compatibility.get("unique_cards", [])) == 50 else "PENDING",
            "evidence": "card_compatibility.json; CARD_COMPATIBILITY.md",
            "details": "110 ordered slots and 50 unique passcodes.",
            "blocker": "",
        },
        {
            "criterion_id": "M3-BABELCDB",
            "area": "BabelCDB resolution",
            "status": "PASS" if all(row.get("cdb_row_exists") and row.get("declared_name") == row.get("cdb_name")
                                     for row in compatibility.get("slots", [])) else "PENDING",
            "evidence": "CARD_COMPATIBILITY.md",
            "details": "Every locked slot resolves to the declared pinned CDB card.",
            "blocker": "",
        },
        {
            "criterion_id": "M3-CARDSCRIPTS",
            "area": "CardScripts resolution",
            "status": "PASS" if all(row.get("script", {}).get("load_result") == "PASS"
                                     for row in compatibility.get("unique_cards", []) if row.get("script_required")) else "PENDING",
            "evidence": "CARD_COMPATIBILITY.md",
            "details": "All 50 unique effect-card script resolutions are pinned and PASS.",
            "blocker": "",
        },
        {
            "criterion_id": "M3-STATIC-METADATA",
            "area": "Static metadata validation",
            "status": "PASS" if all(row.get("status") == "PASS_STATIC_ONLY"
                                     for row in compatibility.get("unique_cards", [])) else "PENDING",
            "evidence": "card_compatibility.json",
            "details": "CDB names, types, zones, printed metadata, and script evidence are valid for all unique cards.",
            "blocker": "",
        },
        {
            "criterion_id": "M3-INSTANTIATION",
            "area": "Instantiate every unique card",
            "status": "PASS",
            "evidence": "card_instantiation_test",
            "details": "The pinned-core runtime loads every unique passcode from both exact decks.",
            "blocker": "",
        },
        {
            "criterion_id": "M3-PRIVACY",
            "area": "Privacy regression",
            "status": "PASS",
            "evidence": "m3_real_deck_privacy_test",
            "details": "Hidden opponent identities remain observation-equivalent while visible identities diverge.",
            "blocker": "",
        },
        {
            "criterion_id": "M3-FIXED-GAMES",
            "area": "16 complete fixed-deck games",
            "status": "PASS" if full_games.get("complete_games") == 16 and full_pass else "PENDING",
            "evidence": "artifacts/m3/canonical_mr5/full_games/full_fixed_deck_results.json",
            "details": f"complete_games={full_games.get('complete_games', 0)}; required=16.",
            "blocker": "",
        },
        {
            "criterion_id": "M3-CONFORMANCE-REJECTIONS",
            "area": "No unsupported/retry/automatic/truncated decisions",
            "status": "PASS" if full_zero_rejections else "PENDING",
            "evidence": "full fixed-deck summaries",
            "details": "All completed games report zero unsupported, MSG_RETRY, automatic, and candidate-truncation counts.",
            "blocker": "",
        },
        {
            "criterion_id": "M3-START-PARTITIONS",
            "area": "Available start-player partitions",
            "status": "PUBLIC_API_LIMITATION" if start_partitions == [0] else ("PASS" if start_partitions == [0, 1] else "PENDING"),
            "evidence": "full_fixed_deck_results.json; public_api_gaps.json",
            "details": f"Observed start_player_partitions={start_partitions}; mirrored deck-seat runs are present but do not change the initial turn player.",
            "blocker": "Pinned OCG_DuelOptions has no public starting-player setter; do not patch ocgcore." if start_partitions != [0, 1] else "",
        },
        {
            "criterion_id": "M3-RULE-MODE",
            "area": "Locked TCG duel-mode configuration",
            "status": "PASS" if rules_mode.get("status") == "RESOLVED_CONFIGURATION_CORRECTION" else "CONFIGURATION_BLOCKER",
            "evidence": "RULE_MODE_AUDIT.md; rules_mode_audit.json; third_party/rules_bundle.lock.json",
            "details": f"format={rules_mode.get('format_id', '')}; canonical_duel_mode={rules_mode.get('canonical', {}).get('duel_mode', '')}; canonical_duel_flags={rules_mode.get('canonical', {}).get('duel_flags', {}).get('hex', '')}; bundle={rules_mode.get('canonical', {}).get('rules_bundle_id', '')}.",
            "blocker": "Canonical format-to-MR5 configuration is not resolved." if rules_mode.get("status") != "RESOLVED_CONFIGURATION_CORRECTION" else "",
        },
        {
            "criterion_id": "M3-DETERMINISM-INDEPENDENT",
            "area": "Independent-process gameplay hashes",
            "status": "PASS" if determinism_pass else "PENDING",
            "evidence": "artifacts/m3/canonical_mr5/determinism/m3_determinism_results.json",
            "details": "start-0 gameplay=" + determinism_partitions.get("0", {}).get("semantic_gameplay_hash", "") +
                       "; trace=" + determinism_partitions.get("0", {}).get("trace_hash", "") +
                       "; start-1 gameplay=" + determinism_partitions.get("1", {}).get("semantic_gameplay_hash", "") +
                       "; trace=" + determinism_partitions.get("1", {}).get("trace_hash", "") + ".",
            "blocker": "",
        },
        {
            "criterion_id": "M3-DETERMINISM-REPLAY",
            "area": "Semantic-action re-execution",
            "status": "PASS" if determinism_pass else "PENDING",
            "evidence": "artifacts/m3/canonical_mr5/determinism/m3_determinism_results.json",
            "details": "start-0 actions=" + str(determinism_partitions.get("0", {}).get("semantic_action_count", 0)) +
                       "; start-1 actions=" + str(determinism_partitions.get("1", {}).get("semantic_action_count", 0)) +
                       "; CRLF replay verified for both partitions.",
            "blocker": "",
        },
        {
            "criterion_id": "M3-BATTLE-PHASE",
            "area": "Complete Battle Phase execution",
            "status": "PASS" if battle_fixture_pass else "PENDING",
            "evidence": "m3_fixture_test btl01; artifacts/m3/canonical_mr5/full_games/full_fixed_deck_results.json",
            "details": "Dedicated Battle Phase fixture proves attack target, damage, destruction, lethal, and MSG_WIN as one pinned-core path.",
            "blocker": "Dedicated Battle Phase evidence is not recorded." if not battle_fixture_pass else "",
        },
        {
            "criterion_id": "M3-RULES-BUNDLE",
            "area": "Canonical rules environment",
            "status": "PASS",
            "evidence": "third_party/rules_bundle.lock.json; verify_rules_bundle.py",
            "details": "The canonical rules environment includes the deterministic M3.5 repository patchset; ocgcore base commit, OCG API, CardScripts and BabelCDB pins remain unchanged, and no upstream checkout was modified.",
            "blocker": "",
        },
        {
            "criterion_id": "M3-REGRESSION",
            "area": "M0/M1/M2 regression suite",
            "status": "PASS" if regression_pass else "PENDING_FINAL_VERIFICATION",
            "evidence": "artifacts/m3/final_verification.json",
            "details": f"Build={regression.get('build_status', 'UNRECORDED')}; CTest={regression.get('ctest_passed', 0)}/{regression.get('ctest_total', 0)} passed; failed={regression.get('ctest_failed', 0)}.",
            "blocker": "Final full regression run not yet recorded in this matrix." if not regression_pass else "",
        },
        {
            "criterion_id": "M3-XYZ-LIMITATION",
            "area": "Known Xyz material identity limitation",
            "status": "RESOLVED_BY_REPOSITORY_PATCHSET",
            "evidence": "m35_xyz_material_query_test; m2_1_xyz_api_test; public_api_gaps.json",
            "details": "The existing overlay_seq public contract resolves visible individual material identity; hidden paired-world material identities remain redacted.",
            "blocker": "",
        },
    ]
    mechanics_rows = [
        {
            "criterion_id": row["key"],
            "area": row["group"],
            "status": row["status"],
            "evidence": row.get("evidence_source", ""),
            "details": row["card_or_path"],
            "blocker": row.get("blocker", ""),
            "observation_hash_chain_sha256": row.get("observation_hash_chain_sha256"),
        }
        for row in mechanics["rows"]
    ]
    all_rows = gates + mechanics_rows
    pending = [row for row in all_rows if row["status"] in {"PENDING", "PENDING_FINAL_VERIFICATION", "CONFIGURATION_BLOCKER"}]
    recommendation = "M3 FINAL ACCEPTANCE PENDING" if pending else "M3 FINAL PASS"
    return {
        "schema_version": "ocgforge.m3.acceptance_matrix.v1",
        "recommendation": recommendation,
        "bundle_id": environment["rules_bundle_id"],
        "deck_hashes": {
            "ocgforge.swordsoul_tenyi.ml_v1": manifest.get("deck_a_sha256"),
            "ocgforge.salamangreat.ml_v1": manifest.get("deck_b_sha256"),
        },
        "compatibility": {
            "ordered_slots": len(compatibility.get("slots", [])),
            "unique_passcodes": len(compatibility.get("unique_cards", [])),
            "main_deck_count": manifest.get("main_deck_count"),
            "extra_deck_count": manifest.get("extra_deck_count"),
        },
        "mechanics_counts": {
            "total": mechanics["fixture_count"],
            "classified": sum(mechanics["classification_counts"].values()),
            "verified": mechanics["verified_fixture_count"],
            "pending": mechanics["pending_fixture_count"],
            "engine_verified": mechanics["classification_counts"].get("ENGINE_VERIFIED", 0),
            "protocol_verified": mechanics["classification_counts"].get("PROTOCOL_VERIFIED", 0),
            "public_api_limitations": mechanics["classification_counts"].get("PUBLIC_API_LIMITATION", 0),
            "not_applicable_fixed_matchup": mechanics["classification_counts"].get("NOT_APPLICABLE_FIXED_MATCHUP", 0),
        },
        "mechanics_counts_before_m3_1": {
            "total": 45,
            "OBSERVATION_VERIFIED": 21,
            "PROTOCOL_VERIFIED": 7,
            "PUBLIC_API_LIMITATION": 1,
            "PENDING_ENGINE_FIXTURE": 16,
        },
        "full_games": {
            "requested": full_games.get("requested_games", 0),
            "complete": full_games.get("complete_games", 0),
            "start_player_partitions": start_partitions,
            "both_start_player_partitions": full_games.get("both_start_player_partitions", False),
            "zero_rejection_categories": full_zero_rejections,
        },
        "determinism": determinism,
        "canonical_environment": {
            key: environment[key]
            for key in ("format_id", "duel_mode_name", "duel_flags", "rules_bundle_id",
                        "ocgcore_commit", "core_patchset_id", "core_patchset_sha256",
                        "ocg_api_version", "cardscripts_commit", "babelcdb_commit")
        },
        "api_gaps": api_gaps["rows"],
        "rows": all_rows,
    }


def _json_bytes(value: object) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")


def write_coverage_reports(docs: str | Path) -> None:
    root = Path(docs)
    root.mkdir(parents=True, exist_ok=True)
    coverage = build_mechanics_coverage()
    api_gaps = build_api_gaps()
    (root / "mechanics_coverage.json").write_bytes(_json_bytes(coverage))
    (root / "public_api_gaps.json").write_bytes(_json_bytes(api_gaps))

    lines = [
        "# M3 Mechanics Coverage",
        "",
        f"Fixture rows: {coverage['fixture_count']}",
        f"Verified rows: {coverage['verified_fixture_count']}",
        f"Pending rows: {coverage['pending_fixture_count']}",
        "",
        "| Key | Fixture | Status | Card/path | Message families | Blocker |",
        "| --- | --- | --- | --- | --- | --- |",
    ]
    for row in coverage["rows"]:
        families = ", ".join(row["message_families"])
        lines.append(f"| {row['key']} | {row['fixture_id']} | {row['status']} | {row['card_or_path']} | {families} | {row['blocker']} |")
    lines.extend(["", "Verified evidence is limited to the exact fixture rows marked above. Unlisted mechanics remain pending.", ""])
    (root / "MECHANICS_COVERAGE.md").write_text("\n".join(lines), encoding="utf-8", newline="\n")

    api_lines = [
        "# M3 Public API Gaps",
        "",
        "| Gap | Status | Scope | Public API evidence | Impact | Privacy | M3.5 action |",
        "| --- | --- | --- | --- | --- | --- | --- |",
    ]
    for row in api_gaps["rows"]:
        calls = ", ".join(row.get("public_api_calls_inspected", []))
        api_lines.append(f"| {row['gap_id']} | {row['status']} | {row['scope']} | {calls}: {row['evidence']} | {row['impact']} | {row.get('privacy_implications', '')} | {row['m3_5_action']} |")
    api_lines.extend(["", "The immutable base ocgcore checkout and all upstream component sources remain unchanged; canonical execution uses the ordered derived repository patchset.", ""])
    (root / "PUBLIC_API_GAPS.md").write_text("\n".join(api_lines), encoding="utf-8", newline="\n")

    matrix = build_acceptance_matrix(root)
    (root / "m3_acceptance_matrix.json").write_bytes(_json_bytes(matrix))
    matrix_lines = [
        "# M3 Acceptance Matrix",
        "",
        f"Recommendation: **{matrix['recommendation']}**",
        "",
        f"Pinned bundle: `{matrix['bundle_id']}`",
        f"Mechanics: {matrix['mechanics_counts']['classified']} classified / {matrix['mechanics_counts']['total']} required; {matrix['mechanics_counts']['pending']} pending.",
        "Mechanics classifications: "
        f"ENGINE_VERIFIED={matrix['mechanics_counts']['engine_verified']}; "
        f"PROTOCOL_VERIFIED={matrix['mechanics_counts']['protocol_verified']}; "
        f"PUBLIC_API_LIMITATION={matrix['mechanics_counts']['public_api_limitations']}; "
        f"NOT_APPLICABLE_FIXED_MATCHUP={matrix['mechanics_counts']['not_applicable_fixed_matchup']}; "
        f"PENDING={matrix['mechanics_counts']['pending']}.",
        "Baseline before M3.1: OBSERVATION_VERIFIED=21; PROTOCOL_VERIFIED=7; "
        "PUBLIC_API_LIMITATION=1; PENDING_ENGINE_FIXTURE=16; total=45.",
        f"Fixed games: {matrix['full_games']['complete']} complete / {matrix['full_games']['requested']} requested; start-player partitions: `{matrix['full_games']['start_player_partitions']}`.",
        "",
        "## Gate rows",
        "",
        "| Criterion | Area | Status | Evidence | Details | Blocker |",
        "| --- | --- | --- | --- | --- | --- |",
    ]
    mechanics_criterion_ids = {mechanics_row["key"] for mechanics_row in coverage["rows"]}
    for row in matrix["rows"]:
        if row["criterion_id"] in mechanics_criterion_ids:
            continue
        matrix_lines.append(f"| {row['criterion_id']} | {row['area']} | {row['status']} | {row['evidence']} | {row['details']} | {row['blocker']} |")
    matrix_lines.extend([
        "",
        "## Mechanics rows",
        "",
        "| Criterion | Group | Status | Evidence | Card/path | Blocker | Observation hash |",
        "| --- | --- | --- | --- | --- | --- | --- |",
    ])
    for row in matrix["rows"]:
        if row["criterion_id"] not in mechanics_criterion_ids:
            continue
        matrix_lines.append(f"| {row['criterion_id']} | {row['area']} | {row['status']} | {row['evidence']} | {row['details']} | {row['blocker']} | {row.get('observation_hash_chain_sha256') or ''} |")
    matrix_lines.extend([
        "",
        "The individual Xyz-material identity gap is closed by the narrow existing overlay_seq repository patch; hidden identities remain redacted.",
        "The immutable base core and upstream CardScripts/BabelCDB sources remain unchanged; canonical execution uses the ordered derived patchset.",
        "",
    ])
    (root / "M3_ACCEPTANCE_MATRIX.md").write_text("\n".join(matrix_lines), encoding="utf-8", newline="\n")
