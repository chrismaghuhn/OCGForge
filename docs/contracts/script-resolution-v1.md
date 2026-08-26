# Script resolution and required closure v1

## Contract IDs

```text
script resolution contract: ocgforge.script_resolution.v1
closure schema:             ocgforge.required_script_closure.v1
closure hash domain:        ocgforge.required_script_closure_identity.v1
```

## Scope

This contract records the deterministic script-resolution environment needed
by the certified fixed-deck matchup. It does not redesign or restrict the
runtime loader.

The current `CoreHost` bootstrap sequence loads these logical names, in this
order, through `ScriptStore`:

```text
constant.lua
utility.lua
proc_normal.lua
```

The current `ScriptStore` behavior is part of `ocgforge.script_resolution.v1`:

1. remove repeated leading `./` prefixes from the requested logical name;
2. try the requested path under the CardScripts root;
3. for a basename without a slash or backslash, try `official/<name>`;
4. for the same basename, try `unofficial/<name>`;
5. load any found script through the existing OCG callback;
6. when a missing basename is a `c<decimal>.lua` card script, return the
   existing success/failure behavior; a code in the required-card seed set
   receives the existing fail-closed missing-script diagnostic;
7. preserve the existing global-script calls and precedence.

The test-only `CoreHost::load_fixture_script` path is not part of this
closure identity. It is fixture setup infrastructure, not runtime resolution.

## Meaning of required closure identity

`required_script_closure_identity` is a **resolution-environment closure
identity**, not an enumerated transitive runtime-request closure. It binds:

| Field | Type and canonical source | Reason |
| --- | --- | --- |
| CardScripts source commit | length-prefixed UTF-8 string | binds the pinned source revision |
| resolved CardScripts tree hash | length-prefixed lowercase SHA-256 hex string | binds resolved tree contents without a host path |
| ScriptStore resolution contract | length-prefixed UTF-8 string | binds lookup, normalization, and missing-script semantics |
| required global-script names | ordered vector of logical relative names | binds the CoreHost bootstrap names and order; the tree hash already binds their bytes |
| required-card seed set | sorted unique `u32` card codes | binds the certified deck-derived expected-card inputs used for fail-closed diagnostics |

The required-card seed set is constructed from the two locked fixture decks as

```text
sort_unique(
    deck_0.main
  + deck_0.extra
  + deck_1.main
  + deck_1.extra
)
```

The input deck vectors are concatenated in the shown semantic order, then
sorted numerically as `u32`; duplicates are removed. This set is not a
runtime allowlist. A script found through the existing ScriptStore resolution
still loads even when its card code is not in this set. The set only supplies
the expected-card missing-script diagnostic.

Listing the global names is intentionally explicit semantic domain
information even though the resolved tree hash binds their bytes. It binds
which bootstrap names and order CoreHost uses without binding an absolute
path or filesystem traversal result.

## Canonical bytes

The closure digest is lowercase SHA-256 over these exact fields, in this exact
order:

```text
1. closure hash domain = "ocgforge.required_script_closure_identity.v1"
2. closure schema ID  = "ocgforge.required_script_closure.v1"
3. CardScripts source commit
4. resolved CardScripts tree SHA-256
5. ScriptStore resolution contract ID
6. ordered required global-script-name vector
7. sorted-unique required-card-code vector
```

Primitive encoding is the accepted Episodic V1 encoding:

- every string is `u32be byte_length || UTF-8 bytes`;
- every vector is `u32be element_count || encoded elements`;
- every card code is `u32be`;
- integers are unsigned and big-endian;
- there are no field labels, terminators, paths, timestamps, or implicit
  platform encodings.

The complete golden input below is independently encoded by the executable
test. Its canonical payload is hashed to the stated identity:

```text
cardscripts_commit = 0123456789abcdef0123456789abcdef01234567
tree_sha256 = abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789
resolution = ocgforge.script_resolution.v1
globals = [constant.lua, utility.lua, proc_normal.lua]
required_codes = [9, 3, 9, 1]   // canonical bytes contain [1, 3, 9]
identity = 397107b0f9493076372d0df7a360c8a7b12251dfe64530e0fcdfad0d4d567372
```

Reordering the input code vector produces the same identity after
canonicalization. Changing a code, tree hash, commit, resolution contract,
global-name/order vector, closure schema, or hash domain changes the identity.

## Determinism, privacy, and compatibility

The identity excludes filesystem traversal order, absolute paths, drive
letters, timestamps, inode/file IDs, PID, thread/worker identity, compiler,
load timing, unordered iteration, hidden gameplay information, private
observations, response bytes, and raw engine state. It is a rules/script
environment identity, not an observation identity.

Changing these field meanings or their byte order requires a new closure
schema/hash-domain version. This definition does not change
`ScriptStore::load`, `CoreHost` bootstrap behavior, required-script
diagnostics, fixture setup, the locked rules bundle, or any gameplay result.
