# M3.2 Canonical Duel-Mode Audit

Status: `RESOLVED_CONFIGURATION_CORRECTION`

The locked format `TCG_ADVANCED_2026_05_18` now resolves through one
lock-backed mapping to `DUEL_MODE_MR5`:

```text
DUEL_MODE_MR5 = DUEL_PZONE
                 | DUEL_EMZONE
                 | DUEL_FSX_MMZONE
                 | DUEL_TRAP_MONSTERS_NOT_USE_ZONE
                 | DUEL_TRIGGER_ONLY_IN_LOCATION
               = 0x2E800 = 190464
```

Before M3.2 the format used `duel_flags = 0` and rules bundle
`6fbbd212ae4be2df36170dcbfcdf5c46aaaa0e3091cf815c2d0261fd01640ea4`; that
configuration was a `CONFIGURATION_BLOCKER` because the pinned core did not
expose the modern Extra Monster Zone topology.

After M3.2 the intermediate canonical MR5 environment used rules bundle
`ff8721aae1a17da6a72079e65ae75a05012c0c367b6f249651c1de713c1fbf91`. M3.5
adds the two ordered, repository-versioned ocgcore API-hardening patches and
therefore the current canonical environment is bundle
`3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f`. The
identity is derived deterministically from stable canonical
`rule_affecting_inputs`, including the ordered patch filenames and hashes, so
both gameplay configuration and public-core behavior are visible in
environment identity. The base ocgcore, CardScripts, BabelCDB, and OCG API
pins are unchanged; the build uses an immutable base checkout plus the
tracked derived patchset.

`cmake/RulesBundle.cmake` reads the lock once, prepares the exact derived core
from the immutable base checkout, applies the ordered patchset, and exposes the
format, mode, numeric flags, patchset identity, and bundle identity to the
canonical rules header. The header checks the lock value against the pinned
public `DUEL_MODE_MR5` constant at compile time. `CoreHost` passes the
canonical flags to `OCG_DuelOptions.flags`. Fixtures, full games,
deterministic replays, and semantic replays all consume this same
configuration; no `YGO_M3_DUEL_FLAGS` override remains.

The real pinned-core topology gate and the Jack Jaguar fixture now run under
the same canonical MR5 configuration as the fixed-deck games. Link placement
and linked Graveyard legality are verified from engine candidate domains; they
are not inferred from the presence of extra field slots.

Historical `duel_flags = 0` traces remain under the explicit classification
`PRE_MR5_CONFIGURATION_CORRECTION` / `NON_CANONICAL_FOR_TCG_ADVANCED_2026_05_18`
and are excluded from final M3 acceptance.

Evidence:

- `third_party/rules_bundle.lock.json`
- `fixtures/decks/ocgforge.matchup.swordsoul_salamangreat.v1.json`
- `.cache/rules_bundle/ocgcore/ocgapi_constants.h`
- `.cache/rules_bundle/ocgcore/field.cpp`
- `cmake/RulesBundle.cmake`
- `include/ygo/m3/canonical_rules.hpp`
- `tests/m3/runtime/rules_mode_test.cpp`
- `tests/m3/runtime/m3_fixture_test.cpp`
- `third_party/patches/ocgcore/0001-fix-overlay-seq-parent-query.patch`
- `third_party/patches/ocgcore/0002-add-starting-player-control.patch`
