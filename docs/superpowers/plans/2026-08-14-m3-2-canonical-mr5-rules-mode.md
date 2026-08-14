# M3.2 Canonical MR5 Rules-Mode Correction

> **Execution note:** Follow this plan incrementally in the existing dirty M3/M3.1 worktree. Do not commit, push, tag, or open a PR.

## Goal

Make `TCG_ADVANCED_2026_05_18` resolve to the pinned core's `DUEL_MODE_MR5` configuration (`0x2E800`) through one authoritative lock-backed mapping, then revalidate all M3 gates under that configuration.

## Steps

1. **Record and reproduce the pre-correction baseline**
   - Preserve the existing worktree and capture HEAD, branch, dirty files, and old bundle identity.
   - Re-run CTest, legacy Python, M3 Python, compatibility, documentation, privacy/observation, and rules-bundle checks before edits.

2. **Trace and lock the canonical environment identity**
   - Add explicit format/mode fields to the rule-affecting lock input.
   - Set the locked format to `DUEL_MODE_MR5`, compute the new bundle ID from canonical JSON, and validate lock/top-level consistency without changing pinned components.
   - Propagate the lock-backed values through CMake into a single public canonical-rules header with a compile-time equality check against the pinned `DUEL_MODE_MR5` constant.

3. **Add failing configuration-equality gates first**
   - Add a C++ runtime rules-mode test and Python lock/artifact validation that reject flags `0`, divergent runner overrides, missing provenance, or mismatched bundle identity.
   - Run the new focused gate before implementation so the pre-correction state fails for the intended reason.

4. **Unify all acceptance-critical execution paths**
   - Make fixtures, full-game probe, deterministic replay, and semantic replay consume the canonical mapping.
   - Remove `YGO_M3_DUEL_FLAGS` test-only overrides and replace them with the canonical CMake configuration.
   - Add explicit format, mode, flags, and bundle provenance to canonical trace/run artifacts.

5. **Revalidate MR5 topology and M3 mechanics**
   - Run the real pinned-core MR5 topology/Link placement gate, including Jack Jaguar.
   - Re-run all 45 mechanics rows, preserving only evidence-supported API limitations and fixed-matchup subpath classifications.

6. **Regenerate canonical conformance evidence**
   - Preserve old flags-0 evidence as historical non-canonical evidence.
   - Generate fresh 16-game MR5 evidence, deterministic repeat/independent-process evidence, semantic replay, CRLF replay, privacy, and candidate/observation reports.

7. **Close documentation and final verification**
   - Update machine-readable and human-readable M3/M3.1/rule-mode/API-gap artifacts, validators, and acceptance counts.
   - Run the complete CTest/Python/M3/documentation/bundle/diff-check suite and report M3 FINAL PASS only if every gate is green and `PENDING == 0`.
