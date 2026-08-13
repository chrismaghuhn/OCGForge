# Third-party rules bundle

M0 resolves all runtime rule inputs into the ignored repository-local cache `.cache/rules_bundle`. The cache is populated by `tools/fetch_rules_bundle.py` from exact detached Git commits and is verified by `tools/verify_rules_bundle.py`. No sibling checkout, floating branch, symlink, or upstream source mutation is required.

| Component | Exact revision | License record | Runtime role |
| --- | --- | --- | --- |
| `edo9300/ygopro-core` | `9a0c558c2d686542f7914a6d529fd7aa57746aed` | `AGPL-3.0-or-later` | OCG rules engine and public C API |
| `ProjectIgnis/CardScripts` | `f337c87018ca723c1aded5143e616bb649555273` | `AGPL-3.0-or-later` | Canonical Lua scripts |
| `ProjectIgnis/BabelCDB` | `89ad6837b0766a52984d8c715a7d5d4f8447946b` | `UNRESOLVED-LICENSE` | Card database; the snapshot does not provide an explicit license notice used by this project |
| `sbl1996/ygo-agent` | `dbf5142d49aab2e6beb4150788d4fffec39ae3e5` | Audit-only reference; license status is not used to authorize runtime inclusion | Static ygoenv audit reference |

The current OCG core and CardScripts explicitly identify themselves as AGPL-3.0-or-later. BabelCDB licensing is recorded as unresolved rather than guessed. Consequently, this complete project is not represented as MIT-only.

The runtime database artifact for M0 is `BabelCDB/cards.cdb` with SHA-256 `7a6570fe313ae0affe4e1e2047c564b669397500777da6c3d76e79aac393726f`. The lock file also records the deterministic checkout hashes and expected public core API version `11.0`.
