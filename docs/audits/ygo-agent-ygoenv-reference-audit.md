# ygo-agent ygoenv reference audit

## Scope and method

This audit rechecks the exact read-only snapshot
`sbl1996/ygo-agent@dbf5142d49aab2e6beb4150788d4fffec39ae3e5` stored in the
ignored `.cache/rules_bundle/ygo-agent` cache. The evidence below is static
source inspection at the cited paths and line ranges. The ygo-agent runtime
was not built or executed during M0, so runtime behavior is not claimed where
the source alone cannot establish it.

Classifications:

- **CONFIRMED**: the cited source contains the behavior or contract coupling.
- **LIKELY - runtime verification required**: the source is a strong
  indicator, but a runtime artifact or checkpoint is needed to prove the
  observed effect.
- **NOT REPRODUCED**: the requested source pattern was not found.
- **OUT OF SCOPE**: not evaluated by this static audit.

## Findings

| Finding | Classification | Evidence and qualification |
| --- | --- | --- |
| `legal_actions_.resize(max_options())` silently removes candidates | **CONFIRMED** | `ygoenv/ygoenv/ygopro/ygopro.h:2335-2378` computes `n_options`, resizes `legal_actions_` when it exceeds `max_options()`, and then reports the resized count. The same pattern occurs in `ygoenv/ygoenv/edopro/edopro.h:2347-2349` and `ygoenv/ygoenv/ygopro0/ygopro.h:2471-2473`. No overflow diagnostic or complete-set contract is present at those sites. |
| Partial-observation card path computes `card_id` but does not pass it to `_set_obs_card_` | **CONFIRMED** as a source-level finding; runtime observation effect not executed | In `ygoenv/ygoenv/ygopro/ygopro.h:2359-2365`, the `oppo_info` branch selects `_set_obs_mask`; inside that path `:2461-2468` computes `card_id` and calls `_set_obs_card_(f_cards, offset, c, hide)` without the value. The callee's default is `card_id = 0` at `:2556-2570`. This establishes the source data path; M0 did not build ygo-agent to inspect a runtime tensor. |
| Model decodes the first two observation bytes as card identity | **CONFIRMED** | `ygoai/rl/jax/agent.py:250-254` calls `decode_id(x_cards[:, :, :2])` and uses the result as the card embedding input. The feature contract independently labels bytes `0,1` as card id in `docs/feature_engineering.md:8-13`. |
| Training commonly runs with `oppo_info=false` | **CONFIRMED** for the inspected training entry points | `scripts/cleanba.py:203-219` and `scripts/cleanba_nnx.py:203-223` pass `oppo_info=False` directly to `ygoenv.make`. `scripts/cleanba_g.py:203-220` uses `False` in evaluation and `True` otherwise. This is a source/configuration finding, not a claim about every historical run. |
| Several legal decision families are unsupported or automatically answered | **CONFIRMED** | Unsupported branches include the generic `throw` in `ygoenv/ygoenv/ygopro/ygopro.h:2805-2806`, the EDOPro action path at `ygoenv/ygoenv/edopro/edopro.h:2108-2117`, and explicit unimplemented constraints such as `MSG_SELECT_TRIBUTE` at `edopro.h:3913-3963` and `MSG_SELECT_CARD` minimum-zero handling at `ygopro.h:4316-4325`. Automatic answering is visible in `ygopro.h:3030-3047`, where a single legal action invokes `callback_(0)` without exposing a choice to the caller. |
| The EDOPro dependency is not pinned | **CONFIRMED** | `xmake.lua:5-8` declares `edopro-core` without a version. The local package recipe `repo/packages/e/edopro-core/xmake.lua:1-5` provides only the repository URL and no exact revision or version hash. |
| The build mutates `interpreter.h` using fixed line numbers | **CONFIRMED** | `repo/packages/e/edopro-core/xmake.lua:25-37` defines `check_and_insert(file, line, insert)` and calls it with hard-coded positions in `interpreter.h`. The legacy `repo/packages/y/ygopro-core/xmake.lua:22-32` performs the same style of fixed-line mutation. |
| The normal Makefile path builds the old ygopro backend | **CONFIRMED** | `Makefile:17-20` defines `ygoenv_so` as `ygoenv/ygoenv/ygopro/ygopro_ygoenv.so` and builds target `ygopro_ygoenv`. `xmake.lua:29-32` binds that target to the `ygopro` source and the `ygopro-core` package. The EDOPro target is separate at `xmake.lua:46-49`; it is not the normal Makefile target. |
| EDOPro remains listed as unfinished in the roadmap | **CONFIRMED** | `README.md:272-275` lists `Support EDOPro` under the Environment roadmap. |
| The CI lacks engine-correctness and deterministic-trace tests | **CONFIRMED** for the repository CI file | The only CI file in the pinned snapshot is `.gitlab-ci.yml:1-42`. It defines Docker image build/push and deploy stages; it has no engine lifecycle test, trace hash test, privacy test, or deterministic conformance job. This does not make a claim about untracked external CI. |
| Model checkpoints depend implicitly on `code_list` and observation preprocessing configuration | **LIKELY - runtime verification required** | The source confirms configuration coupling: `README.md:247-254` ties supported cards and embedding file names to `scripts/code_list.txt`; `ygoai/utils.py:60-72` loads an external code list, asserts embedding length equality, reorders embeddings by that list, and pads them; `scripts/cleanba.py:75-77,660-664,761-764` keeps `code_list_file`, embedding shape, and checkpoint loading as separate runtime inputs. A checkpoint compatibility failure would require loading a representative checkpoint under changed configurations, which M0 did not do. |

## Boundary conclusion

The audit supports the M0 architecture decision: the old ygoenv layer is a
useful reference for message vocabulary and historical behavior, but it is not
a correctness boundary. In particular, action truncation, automatic
single-choice responses, fixed-width observation assumptions, and external
model preprocessing files are incompatible with an environment contract that
must expose complete legal candidates and version its player-view trace.
