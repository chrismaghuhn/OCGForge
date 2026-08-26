# OCGForge Research Addendum
# IceYGO/WindBot BotWrapper and WindBot Arena

**Addendum date:** 2026-08-26  
**Parent research:** `OCGForge_EDOPro_WindBot_Capability_Mining_2026-08-25.md`  
**Scope:** research / analysis / recommendation only; no production integration is authorized by this addendum.

---

## Purpose

This addendum extends the EDOPro/WindBot capability-mining report with two secondary reference repositories discovered after the original 2026-08-25 research snapshot:

1. `IceYGO/windbot`, specifically its legacy `BotWrapper` integration and HTTP `ServerMode`;
2. `mercury233/windbot-arena`, a self-hosted WindBot-vs-WindBot experiment orchestrator.

These repositories are **secondary references**. They do not replace `ProjectIgnis/windbot`, EDOPro, or pinned ocgcore as the primary sources used by the original report, and they do not change OCGForge's legality, determinism, privacy, replay, or rules-authority hierarchy.

---

# Repository identities

The following default-branch heads were inspected for this addendum:

| Repository | Branch | Inspected commit | Role in this addendum |
|---|---|---|---|
| IceYGO/windbot | `master` | `8ad1601096f6c7aef4624f07874878d2a3f83e8f` | Secondary WindBot deployment/orchestration reference |
| mercury233/windbot-arena | `master` | `64e4df634bb3609ea10b7a2183a3affedea1b092` | Secondary automated external-evaluation reference |

These identities are dated research references only. They are not OCGForge rules inputs and must never be folded into gameplay semantic hashes or rules-bundle identity.

---

# IceYGO/windbot

## BotWrapper

`IceYGO/windbot` documents `BotWrapper` as a lightweight launcher for YGOPro's built-in bot mode. Its job is not to play the duel. It translates the arguments supplied by YGOPro into WindBot command-line options and starts `WindBot.exe`.

The inspected `BotWrapper/BotWrapper.cs` implementation is correspondingly thin:

```text
YGOPro built-in bot mode
        |
        v
BotWrapper.exe
        |
        | translate Random / Hand / Port and bot.conf selection
        v
WindBot/WindBot.exe
        |
        v
WindBot joins the duel host as a network client
```

When invoked with the expected three arguments, the wrapper can resolve a `Random=<flag>` entry from `bot.conf`, optionally force the rock-paper-scissors hand, append the duel port, and launch `WindBot.exe`.

`BotWrapper/bot.conf` is the bot-list format consumed by the YGOPro integration. It contains display names, launch commands, descriptions, and capability flags such as Master Rule support and optional deck-file selection.

### OCGForge classification

| Component / idea | Classification | OCGForge relevance |
|---|---|---|
| Thin process-launch shim | `REFERENCE_ONLY` | Demonstrates that client integration can remain separate from policy/runtime internals. |
| `bot.conf`-style visible bot catalog | `POTENTIALLY_USEFUL_LATER` | Could inspire a deployment-only bot catalog, but must not become policy or rules authority. |
| Random bot selection in wrapper | `DO_NOT_COPY_AS_AUTHORITATIVE_BEHAVIOR` | Uses ordinary process-local randomness and is unrelated to deterministic gameplay semantics. |
| Hard-coded `WindBot.exe` process launch | `CLIENT_SPECIFIC` | Useful historical reference; OCGForge should prefer an explicit configurable deployment boundary. |

**Conclusion:** OCGForge does not need to port BotWrapper. The useful lesson is architectural: a compatibility launcher can be extremely thin, and the actual bot can remain a standalone process that joins a duel through the normal network protocol.

---

## WindBot HTTP ServerMode

The inspected `Program.cs` exposes a second deployment pattern: `ServerMode=True` starts an HTTP listener, defaulting to port `2399`. A valid request supplies at least `name`, `host`, and `port`; additional parameters include `deck`, `dialog`, `version`, `password`, `hand`, `debug`, and `chat`. The server then starts a WindBot client on a new thread.

Simplified:

```text
experiment controller
        |
        | HTTP launch request
        v
WindBot ServerMode
        |
        | create bot client
        v
YGOPro / SRVPro duel host
```

This is materially more relevant to automated evaluation than BotWrapper because an external controller can request bot instances without directly owning the WindBot process model.

### Safety and determinism implications

The inspected implementation binds an anonymous `HttpListener` to `http://+:<port>/`. That is appropriate only inside a trusted local/private deployment boundary. OCGForge must not reproduce this as an unauthenticated public control plane.

The same implementation initializes `Program.Rand = new Random()`, preserving the original report's warning that WindBot's normal runtime is not a deterministic OCGForge teacher contract.

### OCGForge classification

`ServerMode` is a **deployment/control-plane reference**, not a gameplay architecture to adopt. A future `OCGForgeBotServer` could optionally provide a narrow compatibility surface that accepts launch requests and starts a frozen policy client, while preserving OCGForge-owned checkpoint, observation, action, and provenance contracts internally.

Any such compatibility layer must remain outside the authoritative environment:

```text
launch request
    -> deployment adapter
    -> frozen PolicyRunner / EDOPro network client
    -> PlayerObservation + DecisionRequest + complete ActionCandidate[]
```

It must not expose raw `CoreHost`, hidden state, engine pointers, or non-semantic locators to the policy.

---

# mercury233/windbot-arena

## What it is

`windbot-arena` is a self-hosted automated duel experimentation console for WindBot. The inspected README and project guidance describe:

- a Vue 3 web frontend;
- a Node.js server;
- SQLite persistence for configuration, experiment progress, and statistics;
- a dedicated `arena-srvpro` duel server;
- local WindBot mode, where Arena owns WindBot process lifecycle;
- remote WindBot mode, where Arena calls a WindBot HTTP ServerMode endpoint.

The experiment modes documented by the repository are:

1. **new-vs-old regression** — compare a current WindBot/deck against a baseline;
2. **deck challenge** — challenge a set of selected opponents;
3. **tag-duel smoke test** — sample four decks for team duels;
4. **win-rate ranking** — continuously generate random pairings.

This is an **evaluation/orchestration system**, not a rules engine and not an ML trainer.

---

## Match orchestration

The project guidance states that Arena gives both bots in a matchup the same unique room name of the form:

```text
M#123456789
```

SRVPro uses the shared room identifier to place the two requested bots into the same duel. Different concurrent matchups must not reuse the same room identifier.

Arena can run local or remote WindBot endpoints. In remote mode, it does not inspect the remote filesystem; the relevant `bot.conf` is supplied separately to Arena for deck/configuration discovery.

This makes the architecture conceptually compatible with future black-box OCGForge evaluation:

```text
                 external arena/controller
                         |
             +-----------+-----------+
             |                       |
             v                       v
      OCGForge Bot A           WindBot / OCGForge Bot B
             |                       |
             +-----------+-----------+
                         |
                    arena-srvpro
                         |
                  aggregate results
```

---

## Result semantics are not sufficient for authoritative OCGForge evaluation

A major limitation is explicit in the inspected project guidance: Arena currently has **no independent per-game result source**. Win/loss accounting is derived from SRVPro's cumulative match/ranking data.

That is acceptable for WindBot regression experiments, but it is insufficient for OCGForge's authoritative evaluation and replay requirements.

OCGForge evaluation must retain provenance such as:

```text
checkpoint / policy identity
opponent policy identity
rules-bundle identity
locked deck identities
observation schema
action schema
trajectory/replay identity
semantic gameplay hash
terminal result / winner / reason
```

Therefore:

> **WindBot Arena results may be useful external black-box evidence, but they must not replace OCGForge-native deterministic evaluation, replay verification, or acceptance evidence.**

Similarly, SQLite in WindBot Arena is a derived experiment database. It must never become authoritative game state.

---

## API boundary

The repository's `API.md` documents a public **read-only** task API for:

- listing runs;
- listing active runs;
- retrieving one run's full data.

The document explicitly says that other `/api` management paths are internal console interfaces and are not part of the promised public API. A future OCGForge integration should therefore not depend on undocumented write/control endpoints without explicitly owning and pinning that integration.

---

# Relevance to future OCGForge deployment and evaluation

The two repositories together show three useful deployment patterns:

```text
1. YGOPro/EDOPro-style launcher compatibility
   -> tiny wrapper / process adapter

2. Remote bot creation
   -> HTTP launch service
   -> bot joins normal duel host

3. External automated evaluation
   -> experiment controller
   -> multiple bot endpoints
   -> duel server
   -> aggregate result store
```

For OCGForge, the preferred future separation remains:

```text
AUTHORITATIVE OCGForge
----------------------
PlayerObservation
DecisionRequest
complete ActionCandidate[]
semantic candidate identity
deterministic policy/checkpoint provenance
native replay/evaluation evidence

EXTERNAL DEPLOYMENT / EVALUATION
--------------------------------
EDOPro/YGOPro launch compatibility
network client
optional launch service
WindBot Arena-style orchestration
human-vs-bot UI
black-box opponent benchmarks
```

The external layer may request games and collect aggregate results. It may not redefine legality, expose hidden information, alter candidate domains, or become the source of semantic replay identity.

---

# Recommended future use

## After a first frozen neural checkpoint exists

The highest-value use of these references would be:

1. implement a standalone OCGForge EDOPro/YGOPro network client;
2. optionally accept WindBot-compatible launch arguments or provide a very small wrapper;
3. freeze one OCGForge checkpoint for external evaluation;
4. validate local OCGForge execution against network execution for observation/candidate/action/response equivalence;
5. only then consider WindBot Arena compatibility for repeated black-box matches.

Potential external experiments include:

```text
OCGForge checkpoint N vs WindBot
OCGForge checkpoint N vs checkpoint N-1
OCGForge teacher vs learned policy
deck-specific checkpoint regression
deployment smoke tests
```

These should remain separate from training-data acceptance. Human or Arena matches are evaluation/debug evidence unless an explicit, versioned trajectory-import contract later says otherwise.

---

# Acceptance requirements for any future adapter

A future OCGForge deployment or Arena adapter should not be considered accepted until it proves:

1. **same information boundary** — policy receives only `PlayerObservation` plus the current complete candidate domain;
2. **same legal decision** — selected semantic key exists in the exact current `DecisionRequest`;
3. **same response encoding** — network response bytes represent the same semantic action as local execution;
4. **hidden-information safety** — no server/client cache, room metadata, replay data, or remote mirror leaks private state;
5. **deterministic policy identity** — checkpoint and policy RNG provenance are explicit;
6. **rules/deck provenance** — external games record the exact intended rules/deck compatibility context;
7. **failure isolation** — protocol, launch, timeout, or disconnect failures are explicit and never converted into fabricated decisions;
8. **evaluation separation** — Arena aggregate statistics are external evidence, not authoritative OCGForge replay/acceptance evidence.

---

# Findings

## BLOCKER

**None for current OCGForge.**

Neither repository is part of the current runtime, and no current milestone depends on either one.

## MAJOR — external evaluation must not weaken OCGForge evidence semantics

WindBot Arena's aggregate result model is useful for black-box testing but is not sufficient for deterministic replay, trajectory provenance, or acceptance.

## MAJOR — remote launch is a control plane, not a policy interface

WindBot `ServerMode` demonstrates convenient orchestration, but an OCGForge equivalent must not bypass `PlayerObservation + DecisionRequest + ActionCandidate` or expose hidden engine state.

## MINOR — BotWrapper validates the thin-launcher approach

The historical wrapper supports the preferred architectural direction: keep client-specific process launching small and separate from the bot's semantic decision stack.

## NOTE — WindBot Arena may save substantial evaluation-tooling effort later

If compatibility can be achieved through a narrow adapter, the Arena can be evaluated as an external regression/benchmark harness instead of building an equivalent web console immediately.

---

# Recommendation

**Add both repositories to the OCGForge reference set, but only as secondary deployment/evaluation references.**

- `IceYGO/windbot`: use `BotWrapper` and `ServerMode` as examples of thin launch/control integration.
- `mercury233/windbot-arena`: use as a reference or potential future harness for automated external bot-vs-bot regression.
- Keep `ProjectIgnis/windbot` as the primary WindBot architecture/heuristic source for the original research.
- Keep EDOPro and pinned ocgcore as the relevant client/protocol and legality references.
- Do not start integration work before a frozen OCGForge bot/checkpoint and network-deployment contract exist.

This addendum does **not** change the original report's final recommendation B: targeted capability mining and carefully bounded future adapters are valuable; wholesale architecture adoption is not.

---

# Pinned source references

## IceYGO/windbot @ `8ad1601096f6c7aef4624f07874878d2a3f83e8f`

- README: https://github.com/IceYGO/windbot/blob/8ad1601096f6c7aef4624f07874878d2a3f83e8f/README.md
- BotWrapper implementation: https://github.com/IceYGO/windbot/blob/8ad1601096f6c7aef4624f07874878d2a3f83e8f/BotWrapper/BotWrapper.cs
- Bot catalog/config: https://github.com/IceYGO/windbot/blob/8ad1601096f6c7aef4624f07874878d2a3f83e8f/BotWrapper/bot.conf
- HTTP ServerMode: https://github.com/IceYGO/windbot/blob/8ad1601096f6c7aef4624f07874878d2a3f83e8f/Program.cs

## mercury233/windbot-arena @ `64e4df634bb3609ea10b7a2183a3affedea1b092`

- README: https://github.com/mercury233/windbot-arena/blob/64e4df634bb3609ea10b7a2183a3affedea1b092/README.md
- Project/architecture guidance: https://github.com/mercury233/windbot-arena/blob/64e4df634bb3609ea10b7a2183a3affedea1b092/AGENTS.md
- Read-only task API: https://github.com/mercury233/windbot-arena/blob/64e4df634bb3609ea10b7a2183a3affedea1b092/API.md

---

## Scope statement

This addendum changes research documentation only. It does not add a WindBot dependency, SRVPro dependency, launcher, HTTP control service, EDOPro integration, training pipeline, trajectory importer, or ML implementation. No M5 work is authorized by this document.
