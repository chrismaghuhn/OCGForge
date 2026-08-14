# M3.5 Acceptance — ocgcore Public API Hardening

Recommendation: **M3.5 FINAL PASS**

This milestone uses two ordered repository-versioned patches against an immutable pinned base checkout. It does not modify upstream source caches, CardScripts, BabelCDB, locked decks, or the public OCGForge runtime beyond the approved integration.

## Canonical identity

- Bundle: `3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f` (previous M3: `ff8721aae1a17da6a72079e65ae75a05012c0c367b6f249651c1de713c1fbf91`)
- Format/mode: `TCG_ADVANCED_2026_05_18` → `DUEL_MODE_MR5` = `0x2e800`
- Core base: `9a0c558c2d686542f7914a6d529fd7aa57746aed`; patchset `ocgforge.ocgcore.api_hardening.v1` / `6b5421b3a852085f48fa161a5ba1540f902aa00784a337694b21c9efc34f69bd`

## Public capabilities

| Capability | Classification | Evidence |
| --- | --- | --- |
| INDIVIDUAL_XYZ_MATERIAL_QUERY | RESOLVED_BY_REPOSITORY_PATCHSET | m35_xyz_material_query_test; m2_1_xyz_api_test; m3_fixture_test sg08_real; m3_fixture_test sg09_direct |
| START_PLAYER_SELECTION_CONTROL | RESOLVED_BY_REPOSITORY_PATCHSET | m35_starting_player_api_test; m3 full-game matrix; m3 determinism matrix |
| FIXTURE_RUNNER_PUBLIC_SETUP_SCOPE | OCGFORGE_TEST_INFRASTRUCTURE | CoreHost::load_fixture_script; CoreHost::load_fixture_card |

## Mechanics and games

- Mechanics: 38 ENGINE_VERIFIED, 7 PROTOCOL_VERIFIED, 0 PUBLIC_API_LIMITATION, 0 NOT_APPLICABLE_FIXED_MATCHUP, 0 PENDING.
- Full games: 16/16 terminal; start partitions `[0, 1]`; seat partitions `['normal', 'mirror']`; errors `{'unsupported': 0, 'retries': 0, 'automatic': 0, 'truncated': 0, 'core_errors': 0}`.
- Determinism start 0: gameplay `e19349b22796b18eaf1fb35cf34b0b2c95cbb0ad36c161376c1d431dd9798320`, trace `afb6ee362c5cabf850c5ec4c7098a12981ce0d550be7658a28309090343457a8`, actions `612`.
- Determinism start 1: gameplay `39df99786adc36af6c40aa6fc88f78c47947ea3861e01688eb4d8586cd46d680`, trace `7a252b56a5f4b22b9e9a58f17be1414137614fc5f7e8ff95e84ff4069f214a36`, actions `621`.
- Independent-process, semantic-action, and CRLF replay gates are recorded per starting-player partition.

## Scope boundary

The individual Xyz query and starting-player control are repository-patched capabilities prepared for upstream review. No upstream PR, commit, push, tag, or external dependency update is part of this milestone. The fixture-runner setup item remains OCGForge test infrastructure, not an ocgcore API claim.
