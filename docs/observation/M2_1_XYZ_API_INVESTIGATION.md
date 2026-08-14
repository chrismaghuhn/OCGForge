# M2.1 Xyz Material Observation Closure

## Authority

This investigation uses the unchanged pinned rules bundle:

| Input | Pinned value |
| --- | --- |
| bundle | `6fbbd212ae4be2df36170dcbfcdf5c46aaaa0e3091cf815c2d0261fd01640ea4` |
| OCG core | `9a0c558c2d686542f7914a6d529fd7aa57746aed` |
| API | `11.0` |
| CardScripts | `f337c87018ca723c1aded5143e616bb649555273` |
| BabelCDB | `89ad6837b0766a52984d8c715a7d5d4f8447946b` |

No core, CardScripts, or rules-bundle source was modified.

## Public API inspected

The pinned public declarations in
`.cache/rules_bundle/ocgcore/ocgapi.h` expose:

- `OCG_DuelQueryCount`
- `OCG_DuelQuery`
- `OCG_DuelQueryLocation`
- `OCG_DuelQueryField`

`.cache/rules_bundle/ocgcore/ocgapi_types.h` defines `OCG_QueryInfo` with:

```text
flags, con, loc, seq, overlay_seq
```

The public constants define `LOCATION_OVERLAY = 0x80` and
`QUERY_OVERLAY_CARD = 0x10000`.

The strongest possible public individual-material address is therefore:

```text
con          = controller of the parent field card
loc          = parent field location | LOCATION_OVERLAY
seq          = parent field sequence
overlay_seq  = material index
```

This is also consistent with the message-side locator convention used by
EDOPro, where an overlay location is paired with the parent sequence and the
overlay index is carried in the location record's position field. The client
source was inspected only as a protocol reference, not used as a runtime
dependency: EDOPro `gframe/duelclient.cpp` at commit
`1d127c2a49d6a01229cd37f0302d0d26e31a080a`.

Reference:
<https://github.com/edo9300/edopro/blob/1d127c2a49d6a01229cd37f0302d0d26e31a080a/gframe/duelclient.cpp#L1977-L1987>

## Pinned implementation semantics

The pinned implementation was inspected only to establish the behavior of
the public ABI; OCGForge does not include or call these internal types.

1. `ocgapi.cpp:140-165` implements `OCG_DuelQueryCount` for Hand, Grave,
   Banished, Extra, Deck, Monster, and Spell/Trap zones. There is no
   `LOCATION_OVERLAY` branch, so `OCG_DuelQueryCount(team,
   LOCATION_OVERLAY)` returns `0` even when a parent has materials.
2. `ocgapi.cpp:180-204` contains an apparent individual overlay path. It
   validates a parent location bit, then calls:

   ```text
   get_field_card(con, info.loc & LOCATION_OVERLAY, info.seq)
   ```

   before indexing `xyz_materials[overlay_seq]`.
3. `field.cpp:521-590` contains the complete pinned `get_field_card`
   location switch. It has cases for field, deck, hand, Grave, Banished, and
   Extra locations, but no `LOCATION_OVERLAY` case. The function consequently
   returns `nullptr` for the argument used by the apparent overlay path.
4. `ocgapi.cpp:207-248` implements `OCG_DuelQueryLocation`. For an overlay
   location it inserts a single zero marker instead of enumerating material
   records.
5. `card.cpp:175-181` implements `QUERY_OVERLAY_CARD` on the parent card. It
   returns the aggregate material count and an ordered vector of raw material
   passcodes. This is the only positive material-identity-like data returned
   by the public query surface; it is not a per-material `OCG_QueryInfo`
   record and carries no material owner, controller, position, feature set, or
   per-material visibility result.

## Real pinned-core probe

`tests/observation/m2_1_xyz_api_test.cpp` runs against
`fixtures/m2_setup.lua`, with a real pinned Xyz card (`359563`) and two
attached materials (`32864`, `549481`). It uses only `CoreHost::query`,
`query_location`, `query_count`, and `query_field`, which call the public ABI.

Observed output:

```text
parent_overlay_count=2
field_overlay_count=2
overlay_query_count=0
overlay_seq=0 individual_query_bytes=0 location_query_bytes=6
overlay_seq=1 individual_query_bytes=0 location_query_bytes=6
```

The six-byte location result is the public length prefix plus one null slot
marker. Both distinct overlay sequence values therefore fail to produce an
individual card record through the strongest valid public address.

## Classification

**Outcome B — aggregate-only public overlay information confirmed.**

This is a pinned public OCG API limitation, not an OCGForge implementation
gap. The parent query and field query expose correct aggregate count and
ordered raw codes, but the public API does not provide a usable individual
material record or an individual visibility/ownership state. The public query
also has no observer-perspective parameter; joining the raw parent code list
to identity-known `ObservedCard` entities would therefore create an
unproven hidden-information side channel.

## Conservative observation behavior

OCGForge retains:

- the correct Xyz parent identity and Rank;
- the aggregate material count;
- one observation-local redacted overlay slot per aggregate material;
- deterministic overlay locators ordered by `overlay_sequence`; and
- typed `XyzMaterial` relationships from each redacted material slot to the
  parent Xyz entity.

It does not emit the raw parent material passcodes, per-material printed or
current features, owner/controller claims, or persistent engine identity.
Each material entity remains `identity_known=false` with absent passcode,
printed state, and current state. This preserves the M2 anti-tracking and
privacy guarantees.

## Paired-world privacy result

`m2_1_setup_xyz_a.lua` and `m2_1_setup_xyz_b.lua` retain the same parent and
material count but use different material codes. Their perspective-0
canonical observations are byte-identical and produce:

```text
08527198cefca948d2510caf6f08a268b7c657e447e0246b175c175971d0864a
```

The test also verifies that both material slots lack identity-derived
metadata. This is the correct result for an aggregate-only, visibility-
unproven API: changing hidden omniscient material identity does not change
the player observation or hash.

## Detach transition

**DEFERRED.** A deterministic detach transition is not required to establish
the API limitation. The public query surface cannot produce the individual
material record before or after detach, so a detach fixture would validate
event plumbing rather than close the missing identity capability. Existing
M2 event redaction remains authoritative for hidden transitions.

## Final conclusion

The material-identity PENDING row is closed as an API limitation. No private
core pointer, internal object ID, core patch, or CardScripts change is needed.
The appropriate recommendation is:

```text
M2 PASS WITH DOCUMENTED API LIMITATION
```

Hosted Windows CI remains a separate unrun finalization gate because no push
or PR was authorized.
