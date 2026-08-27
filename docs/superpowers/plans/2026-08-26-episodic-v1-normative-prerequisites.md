# Episodic V1 Normative Prerequisites Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ratify the Episodic Environment V1 identity contracts, define the required-script resolution-environment identity, correct G28 metric vocabulary, and add executable golden/oracle evidence without implementing the Phase-2 facade.

**Architecture:** Accepted versioned contracts own public semantics and ADR-0003 records the ownership decision. A small pure C++ contract layer mirrors the exact IDs and implements only canonical seed, required-card-set, closure, candidate-domain, and G28 witness/metric helpers. Existing CoreHost, ScriptStore, EpisodeDriver lifecycle, decision publication, trace, observation, rules, and M4 report semantics remain unchanged.

**Tech Stack:** C++17, CMake/CTest, the existing `ygo::trace::sha256_*` implementation, Markdown contracts/ADRs, and the repository Python regression suites.

---

### Task 1: Record normative ownership and exact wire-independent definitions

**Files:**
- Create: `docs/adr/ADR-0003-episodic-v1-normative-prerequisites.md`
- Modify: `docs/adr/README.md`
- Modify: `docs/contracts/decision-protocol-v1.md`
- Create: `docs/contracts/action-identity-v1.md`
- Create: `docs/contracts/seed-derivation-v1.md`
- Create: `docs/contracts/script-resolution-v1.md`
- Create: `docs/contracts/candidate-domain-evidence-v1.md`
- Modify: `docs/contracts/episodic-environment-v1.md`
- Modify: `docs/episodic/EPISODIC_V1_ACCEPTANCE.md`
- Modify: `docs/superpowers/specs/2026-08-26-episodic-phase2-public-facade.md`

- [x] **Step 1: Add the accepted ADR and contract ownership map**

Document these exact IDs and owners:

```text
decision_contract_id       = ocgforge.decision_protocol.v1
action_identity_schema_id  = ocgforge.action_identity.v1
seed_derivation_id         = ocgforge.seed_derivation.v1
script_resolution_contract = ocgforge.script_resolution.v1
closure_schema_id          = ocgforge.required_script_closure.v1
closure_hash_domain        = ocgforge.required_script_closure_identity.v1
candidate_domain_schema    = ocgforge.candidate_domain.v1
candidate_evidence_schema  = ocgforge.candidate_domain_evidence.v1
```

State that the Decision Protocol contract owns the first ID, the dedicated Action Identity contract owns the second, the Seed Derivation contract owns the third, the Script Resolution contract owns the closure pair, and the Candidate Domain Evidence contract owns `candidate_domain_max`, `candidate_max_total`, and G28 witness selection. The C++ constants are implementation mirrors, not alternate normative sources.

- [x] **Step 2: Specify the seed, action-key, closure, metric, and privacy rules**

The documents must state the exact four-word unsigned-u64 seed mapping, the separate semantic-key lifecycle, the current CoreHost bootstrap order, ScriptStore lookup/failure behavior, the sorted-unique deck code seed set, the exact closure field order and primitive encoding, the independent candidate-domain codec, the MAX-versus-SUM metric distinction, and the deterministic G28 tie-break. They must explicitly exclude paths, timing, process/thread/worker identity, compiler identity, hidden gameplay data, response bytes, and raw engine state.

- [x] **Step 3: Correct references without rewriting historical evidence**

Update the Episodic contract and acceptance text to reference the concrete IDs and corrected G28 terms. Add a resolution note to the merged Phase-2 specification and correct its stale “aggregate maximum” wording. Leave `docs/m4/M4_BASELINE.md`, M4 JSON evidence, and existing M4 code field names unchanged; describe `candidate_max=1344` as historical aggregate accounting only.

- [x] **Step 4: Self-review the normative documents**

Run:

```powershell
rg -n "decision_contract_id|action_identity_schema_id|seed_derivation_id|required_script_closure|candidate_domain_max|candidate_max_total|1344|G28" docs/contracts docs/adr docs/episodic docs/superpowers/specs/2026-08-26-episodic-phase2-public-facade.md
git diff --check
```

Expected: every public field has one owner, historical `1344` is not described as a single legal domain, and no document claims Phase-2 production code was added.

### Task 2: Add RED executable contract tests

**Files:**
- Create: `tests/episodic/normative_prerequisites_test.cpp`
- Modify: `CMakeLists.txt`

- [x] **Step 1: Write the failing test before implementation**

The test must include the planned public headers and assert:

```cpp
static_assert(ygo::environment::kDecisionContractId == "ocgforge.decision_protocol.v1");
static_assert(ygo::environment::kActionIdentitySchemaId == "ocgforge.action_identity.v1");
static_assert(ygo::environment::kSeedDerivationId == "ocgforge.seed_derivation.v1");

derive_seed_bundle(0);
derive_seed_bundle(1);
derive_seed_bundle(UINT64_MAX);
derive_seed_bundle(0x8000000000000000ULL);

required_script_code_seed_set(deck_a, deck_b);
required_script_closure_bytes(input);
required_script_closure_identity(input);
canonical_candidate_domain_bytes("CARD_SELECTION", {"card.0.1", "card.0.2"});
candidate_domain_digest(...);
candidate_domain_max(witnesses);
candidate_max_total({5, 21, 7});
select_g28_witness_index(equal_max_witnesses);
```

Assert the independently computed seed words, closure byte vector/hash, candidate-domain byte vector/hash, permutation invariance of the card seed set, closure mutation sensitivity, `21` versus `33`, and the full G28 tie-break order.

- [x] **Step 2: Register the test target**

Add `normative_prerequisites_test` as a CTest executable linked to `ygo_m4`, with no new environment facade or runtime target.

- [x] **Step 3: Run the RED test**

Run:

```powershell
cmake --build .build --target normative_prerequisites_test
```

Expected: FAIL because the planned public contract headers/functions do not yet exist. Do not add production code before recording this failure.

### Task 3: Implement the pure contract helpers and centralize existing derivations

**Files:**
- Modify: `include/ygo/core/seed_bundle.hpp`
- Modify: `include/ygo/core/rules_bundle.hpp`
- Modify: `src/core/rules_bundle.cpp`
- Create: `include/ygo/environment/identity_contract.hpp`
- Create: `src/environment/identity_contract.cpp`
- Create: `include/ygo/environment/candidate_domain_evidence.hpp`
- Create: `src/environment/candidate_domain_evidence.cpp`
- Modify: `CMakeLists.txt`

- [x] **Step 1: Add the shared seed helper**

Implement `ygo::core::derive_seed_bundle(std::uint64_t root_seed)` inline with unsigned-u64 arithmetic exactly:

```cpp
return {{root_seed,
         root_seed ^ 0x9e3779b97f4a7c15ULL,
         root_seed + 0x6a09e667f3bcc909ULL,
         (root_seed << 1) ^ 0xbb67ae8584caa73bULL}};
```

- [x] **Step 2: Add the canonical required-card seed-set helper**

Implement `ygo::core::canonical_required_script_codes(deck_a, deck_b)` by concatenating A main, A extra, B main, B extra, sorting ascending as `std::uint32_t`, and erasing duplicates. This helper is a diagnostic/expected-card seed set and is never used as a ScriptStore allowlist.

- [x] **Step 3: Implement the closure codec**

Implement `RequiredScriptClosureInput`, the exact compile-time IDs, and:

```cpp
std::vector<std::uint8_t> canonical_required_script_closure_bytes(
    const RequiredScriptClosureInput& input);
std::string required_script_closure_identity(
    const RequiredScriptClosureInput& input);
```

Encode, in order, length-prefixed UTF-8 strings for the closure hash domain, schema ID, CardScripts commit, resolved CardScripts tree SHA-256, and ScriptStore resolution-contract ID; then encode the ordered global-script-name vector and the sorted-unique u32be required-card vector. Hash the exact bytes with SHA-256 and return lowercase hex.

- [x] **Step 4: Implement the independent candidate/G28 evidence helper**

Implement the candidate-domain codec using only `candidate_domain` domain, request kind, derived u32be count, and semantic keys in supplied protocol order. Implement `CandidateDomainWitness`, `candidate_domain_max` as MAX over witness rows, `candidate_max_total` as a checked SUM over one per-job maximum value, and `select_g28_witness_index` using count descending, episode ID ascending, environment decision index ascending, engine step index ascending, protocol decision ID ascending, and digest ascending.

- [x] **Step 5: Register the sources and run the GREEN test**

Add the two pure sources to `ygo_m4`, then run:

```powershell
cmake --build .build --target normative_prerequisites_test
ctest --test-dir .build --output-on-failure -R normative_prerequisites_test
```

Expected: the target builds and the focused test passes.

### Task 4: Replace duplicate derivation call sites without changing behavior

**Files:**
- Modify: `src/environment/episode_driver.cpp`
- Modify: `tools/ygo_core_probe/main.cpp`
- Modify: `tools/ygo_observation_probe/main.cpp`
- Modify: `tools/ygo_m4_worker/main.cpp`
- Modify: `tests/m3/runtime/m3_fixture_test.cpp`
- Modify: `tests/m3/runtime/real_deck_privacy_test.cpp`
- Modify: `tests/m3/runtime/card_instantiation_test.cpp`

- [x] **Step 1: Replace local seed implementations**

Call `ygo::core::derive_seed_bundle` at each existing call site. Preserve all existing seed inputs and output fields.

- [x] **Step 2: Replace duplicated required-script sorting**

Call `ygo::core::canonical_required_script_codes` at each existing fixture/probe/worker call site. Preserve the current deck concatenation order before sorting and keep fixture-script loading behavior unchanged.

- [x] **Step 3: Verify no duplicate algorithm remains**

Run:

```powershell
rg -n "seed \^ 0x9e3779b97f4a7c15|seed \+ 0x6a09e667f3bcc909|sort\(codes\.begin\(\)|sort\(unique_codes\.begin\(\)" src tools tests -g '*.{cpp,hpp}'
```

Expected: no local seed formulas or required-code sort blocks remain outside the shared helpers.

### Task 5: Run the complete applicable verification and review the diff

**Files:**
- No additional planned files.

- [x] **Step 1: Configure and build the established Windows tree**

Run the repository’s existing configure/build command for `.build`, then build all targets. If the repository’s `just` entry point is unavailable on Windows, record the native CMake/CTest command and its exact status separately.

- [x] **Step 2: Run native and Python gates**

Run:

```powershell
ctest --test-dir .build --output-on-failure
python -m unittest discover -s tests/python -v
python -m unittest discover -s tests/m3 -v
python tests/protocol/decision_coverage_test.py
python tests/observation/observation_coverage_test.py
python tools/verify_rules_bundle.py --lock third_party/rules_bundle.lock.json --cache .cache/rules_bundle
```

The native CTest run covers the repository's M4 Python integration/integrity tests. Report every unavailable or intentionally non-applicable gate as `NOT_RUN`/`NOT_APPLICABLE` rather than PASS.

- [x] **Step 3: Perform a focused self-review**

Inspect `git diff --check`, the full diff, changed-file list, and `git diff --name-only origin/main...HEAD`. Confirm no rules lock, CardScripts/Babel input, locked deck, EngineTrace v2 behavior, PlayerObservation v1 behavior, Decision Protocol candidate behavior, ScriptStore lookup, M4 aggregation, or Phase-2 facade file changed semantically.

- [x] **Step 4: Commit the focused prerequisite change**

Run:

```powershell
git add CMakeLists.txt include src tools tests docs
git diff --cached --check
git commit -m "docs: ratify Episodic V1 prerequisite identities"
```

- [x] **Step 5: Push and open one PR without merging**

Run:

```powershell
git push -u origin chris/episodic-v1-normative-prerequisites
gh pr create --base main --head chris/episodic-v1-normative-prerequisites --title "docs: ratify Episodic V1 prerequisite identities" --body-file docs/superpowers/plans/2026-08-26-episodic-v1-normative-prerequisites-pr-body.md
```

Immediately before this command, create the uncommitted body file at the exact path above. It must include the exact base SHA, every ratified ID and owner, closure bytes/inputs, no-Allowlist/no-runtime-change statement, B2/G28 status, changed files, rules/public/privacy/determinism/replay impact, exact local verification evidence, and hosted CI status. Do not merge or enable auto-merge.

---

## Self-review checklist

- [x] Every requirement in the prerequisite task maps to a contract, ADR, helper, test, or verification step above.
- [x] No implementation detail is left unspecified; the PR body is a temporary, explicitly named file created immediately before PR creation.
- [x] All helper names and types are consistent across the plan and tests.
- [x] The plan contains no EpisodeEnvironment/reset/step/interrupt/facade production work.
- [x] Historical M4 evidence remains immutable and the G28 witness remains pending until Phase-2 acceptance.
