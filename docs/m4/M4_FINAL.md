# OCGForge M4 Final

**M4 FINAL PASS**

Source commit: `a49639bbb7ef8ce3406ac0d9aad295272872dda9`
Baseline evidence identity: `8ddeadc9def8fe217c5e52f99fff19126f095e863e3e355a9e7536a2383fae61`

M4 closes the parallel-simulation foundation with fresh Release evidence. It does not claim ML readiness and does not start M5.

## Concurrency

| workers | games/s | speedup | efficiency | classification |
|---:|---:|---:|---:|---|
| 1 | 0.966788 | 1.000000 | 1.000000 | STRONG reference |
| 2 | 1.370746 | 1.417836 | 0.708918 | MODERATE |
| 4 | 2.322365 | 2.402145 | 0.600536 | MODERATE |
| 8 | 5.135227 | 5.311639 | 0.663955 | MODERATE |
| 16 | 7.333773 | 7.585712 | 0.474107 | SATURATED / recommended peak |
| 32 | 6.903415 | 7.140570 | 0.223143 | COLLAPSED relative to 16-worker peak |
| 64 | 5.563955 | 5.755095 | 0.089923 | COLLAPSED relative to 16-worker peak |

- Recommended production concurrency: **16 workers**.
- Maximum semantically validated concurrency: **64 workers**.
- 64-worker performance saturation is not a semantic failure.

## Accepted and rejected post-foundation work

- M4.3.1, M4.3.2, and M4.3.6 are integrated accepted internal changes.
- M4.3.3 and M4.3.4 remain characterization/evidence records.
- M4.3.5 is explicitly **REJECTED — NO MATERIAL BENEFIT**. Its negative evidence is retained; the reserve-backed implementation is not present.

## Fresh gates

- Native CTest: **94/94**.
- Repository Python: **8/8**; M3 Python: **17/17**; M4 Python: **127/127**.
- Canonical full-game regression: **16/16**, both starting-player partitions, zero truncation/automatic/core errors.
- Lifecycle stress: **500/500** cases across five independent processes; fail-closed result-after-invalid-exit behavior preserved.
- Recommended-concurrency soak: **128/128** at 16 workers; zero errors/restarts/crashes.
- Direct Writer byte/semantic/privacy equivalence: **PASS**; default is an internal build path with unchanged public serialization contract.

Detailed machine-verifiable evidence: [`m4_final_verification.json`](m4_final_verification.json), [`m4_acceptance_manifest.json`](m4_acceptance_manifest.json), [`m4_baseline.json`](m4_baseline.json), [`m4_final.json`](m4_final.json).
