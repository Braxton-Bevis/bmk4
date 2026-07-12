# FASTFILE_PLAN — loading COD4 fastfiles on 64-bit iOS (BMK4)

*Fulfills stream 4 of docs/NEXT_SESSION.md. Companion data: docs/fastfile-struct-catalog.md
(the machine-checkable struct inventory). Scoping was done by three readers over
(a) src/database/ zone-loader mechanics, (b) the KISAK_LAYOUT_ASSERT census,
(c) community precedent; this document is the synthesis. All file:line citations
verified against this tree at write time.*

---

## 1. Problem statement

A COD4 fastfile (.ff) is not an archive — it is a serialized image of the 32-bit
in-memory asset graph. The container level is trivial: 8-byte magic
`IWffu100`/`IWff0100` + LE u32 version==5 (db_file_load.cpp:226, :235), then one
continuous zlib stream, read in 0x40000-byte overlapped stages into the static
512KB ring buffer `g_fileBuf` (db_registry.cpp:364, db_file_load.cpp:151-168,
:363-364). The decompressed payload opens with the 44-byte `XFile` header
`{size, externalSize, blockSize[9]}` (xanim.h:1122-1128), whose nine u32 block
sizes are the **byte-exact totals of the 32-bit MSVC struct images** destined
for each of the 9 zone memory blocks (db_memory.cpp:75-120). The loader then
inflates **directly to final runtime addresses** — `DB_LoadXFileData` sets
`stream.next_out = pos` with no intermediate decompressed buffer
(db_file_load.cpp:93-138) — while the recursive `Load_*` walk (296 functions in
db_load.cpp, dispatched from the `Load_XAssetHeader` switch at
db_load.cpp:6746-6852) claims block cursors via `DB_AllocStreamPos`
(db_stream.cpp:81-86) and patches pointer slots in place. The on-disk struct
image IS the in-memory struct; there is no unmarshal step.

Every one of those assumptions is 32-bit-hard. Pointer slots are exactly 4
bytes on disk with four sentinel meanings (0 = NULL; -1 = inline-follows,
allocate at cursor; -2 = inline-follows plus a 4-byte alias slot reserved in
block 4 via `DB_InsertPointer`, db_stream.cpp:96-105; anything else =
`(blockIndex<<28)|offset`, +1 bias, decoded by
`DB_ConvertOffsetToPointer`/`DB_ConvertOffsetToAlias`, db_stream_load.cpp:45-57
— note :56 writes a truncated `(uint32_t)&...` address back into the 4-byte
slot). The byte counts fed to `Load_Stream` are hardcoded 32-bit struct-size
literals at each of its 279 call sites (e.g. 8 for XAsset at db_load.cpp:6856;
only 1 of 279 uses `sizeof`). Block sizes from the header would overflow the
moment any pointer-bearing struct grows under LP64 — and the repo already
knows this: `KISAK_LAYOUT_ASSERT` (universal/kisak_layout.h:9-12) relaxes the
x86-32 sizeof asserts under KISAK_IOS precisely because arm64 layouts do not
match. **Conclusion of the scoping: there is no single choke point.** Two
narrow gates exist (`Load_Stream`/`DB_LoadXFileData` for byte placement;
`DB_ConvertOffsetToPointer/Alias` + `DB_AllocStreamPos` for fixups), but
`Load_Stream` receives only `(ptr, byteCount)` and knows nothing of field
layout — per-struct 32→64 field maps are unavoidable. The saving grace: the
296 `Load_*` functions already visit every struct and every pointer slot in
exact stream order (that is how fixups work), so they collectively ARE a
complete executable schema. The work is mechanical per-function rewriting, not
reverse engineering.

## 2. Strategy decision: on-device translation vs offline repack

### Candidate A — on-device load-time translation

Keep the existing `Load_*` walker. Change `Load_Stream`
(db_stream_load.cpp:4-35, single body) to inflate the 32-bit image into a
staging buffer using the existing hardcoded literal (which is the *file* image
size), then expand into a 64-bit struct at the block cursor via a per-struct
field map. Maintain a file-offset → runtime-address table recorded at each
`DB_AllocStreamPos` so the offset/alias fixups resolve against translated
addresses. Widen the ~30 stereotyped `-1`/`-2` Ptr-loader branches (24 verbatim
`value == -1 || value == -2` matches in db_load.cpp, e.g. :994, :1148, :2887,
plus variants — an identical ~15-line pattern) mechanically. Over-allocate
blocks (first pass: 2x `file.blockSize`) in `DB_AllocXZoneMemory`.

### Candidate B — offline repack

A desktop (Mac) tool reads the user's stock .ff, walks the same asset graph,
and writes a new 64-bit-native container; the device loader stays dumb and
never parses the 32-bit format. This is the Ship of Harkinian OTR shape
(desktop extractor converts user-supplied data; the port ships zero game data
and only ever loads converted archives), and the iw3x-port → IW4x ZoneBuilder
pipeline proved it end-to-end for IW3 content specifically. OpenAssetTools
(GPL-3.0) is the strongest live precedent: a standalone, portable IW3 zone
loader/linker whose recent releases document exactly our problem — loading
x86 fastfiles from an x64 process — solved by deep-copying stream data into
native structs instead of in-place fixup.

### Decision: **A first — on-device translation — with a designed migration path to B.**

Rationale (from the loader verdict, weighed against the precedent research
which leaned B):

1. **The schema already lives in this tree as executable code.** The 296
   `Load_*` functions visit every pointer slot in stream order. An offline tool
   gets none of that for free: the walk is entangled with live engine state —
   `Load_MaterialTechniqueSetAsset` calls `Material_UploadShaders`
   (db_registry.cpp:715-720), `Load_TempStringCustom` interns via `SL_GetString`
   (db_stream_load.cpp:74-84), block alloc creates locked D3D vertex/index
   buffers mid-load (db_memory.cpp:112-119), and `DB_LoadXFileData` is welded to
   the overlapped-I/O ring buffer (db_file_load.cpp:93-138). Extracting a
   standalone parser means stubbing half the engine in a new tool.
2. **The invariant cost is identical either way.** The per-struct 32→64 field
   maps for the serialized closure (~150-165 layouts, ~110-120 pointer-bearing;
   see §4) must be written under both strategies. Offline repack does not
   remove that work — it relocates it into a new tool *plus* a new container
   format *plus* a new iOS loader: strictly more surface area before first
   pixel.
3. **A is incremental and machine-verifiable per asset type**, which fits this
   repo's census/CI/device-marker methodology (§3). B has a long dark period
   before anything on-device can be verified.
4. **A degrades gracefully into B.** Once translation works, cache the
   translated blocks + relocation list to disk on first load and the offline
   performance profile is obtained for free (a de-facto hybrid). If load times
   or staging memory pressure on device prove unacceptable, promote the same
   translation code into a Mac-side repack step — the field maps transfer 1:1.
   The decision to promote is an explicit gate at FF5, not a rewrite.

Precedent usage under this decision: read OAT's ZoneLoading/ZoneCommon (IW3)
as the model for the staging/deep-copy pattern and as a second opinion on
struct layouts (GPL-3.0, compatible with this repo's KisakCOD/LWSS GPL-3.0
lineage — see §5 before adapting any code verbatim). Use CoD4x db_load.cpp as
a *read-only* cross-check (AGPL-3.0 — do not copy). Use OAT's **Linker** as an
independent synthetic-zone generator for cross-validation (§6).

## 3. Phased milestones with verification gates

House rules apply to every phase (docs/NEXT_SESSION.md "House rules"): every
engine edit `#ifdef KISAK_IOS` with the original in `#else`; the Windows
byte-identity regression build must stay green; every claim machine-verified
(census, device marker files, CI); a PORT_JOURNAL.md entry per landed
milestone. Phases are labeled FF0-FF5 here; they take M-numbers when they land
in the journal.

### FF0 — Synthetic zone generator + ground-truth harness
- Build a generator (proposed: `scripts/tools/make_test_zone.py`) that emits
  minimal valid `IWffu100` v5 zones: XFile header with hand-computed 32-bit
  blockSize[9], zlib stream, XAssetList → ScriptStringList → XAsset array,
  starting with `rawfile` (asserted sizeof 12, one name + one buffer pointer)
  and `stringtable` (asserted 16) payloads. All 32-bit layout knowledge in the
  generator comes from the headers cited in the catalog, not invented.
- Validate the generator against ground truth: the **Windows build** of this
  tree loads the synthetic zone through the stock 32-bit path and the asset
  contents round-trip. This proves the generator before any iOS code exists.
- Gate: CI job "synthetic-zone round-trip (win32)" green; generator + expected
  outputs committed; census stays 26/26.

### FF1 — 64-bit load spine (container level only)
- Port the pipeline stages 1-4 under KISAK_IOS: replace the `ReadFileEx`
  overlapped ring (db_file_load.cpp:151-168) with a POSIX read loop feeding the
  same `stream.avail_in` contract; header parse; zlib init
  (`DB_AuthLoad_InflateInit`, db_file_load.cpp:253); `DB_AllocXZoneMemory` with
  over-allocation (first pass 2x `file.blockSize`; keep the overflow assert at
  db_stream.cpp:91 — it is the detector, not the enemy); `DB_InitStreams`.
  Blocks 7/8 (vertex/index) allocate plain memory for now — D3D buffer locking
  (db_memory.cpp:112-119) is deferred to renderer content-readiness.
- Gate: db_file_load.cpp / db_memory.cpp / db_stream.cpp graduate into the
  census (census count grows, stays green); simulator/device run parses the
  FF0 synthetic zone header and writes marker
  `Documents/ff_header_ok.txt` (contents: magic, version, the 9 block sizes);
  Windows regression byte-identical.

### FF2 — Translation core + first end-to-end asset
- The heart of the plan. Under KISAK_IOS, `Load_Stream` inflates the 32-bit
  image into a staging buffer (size = the existing call-site literal), then a
  per-struct field map expands it into the 64-bit struct at the cursor.
  Mechanisms required (names proposed, to be defined in src/database/ under
  KISAK_IOS):
  - **Field maps**: per-struct tables (32-bit offset/size → 64-bit offset/size,
    pointer-slot flags) for the closure. Start with the containers: XAssetList
    (asserted 16), ScriptStringList (8), XAsset (8), then RawFile (12),
    StringTable (16), LocalizeEntry (8).
  - **Offset translation table**: every `DB_AllocStreamPos` records
    (32-bit block:offset as the file would compute it, 64-bit runtime address,
    field map) so `DB_ConvertOffsetToPointer`/`Alias` (db_stream_load.cpp:45-57)
    can resolve block-tagged references — including **interior** offsets that
    point mid-struct, which must be translated through the owning allocation's
    field map, not just its base.
  - **Widened fixup targets**: the 64-bit image has 8-byte pointer slots; the
    `-2` path's `DB_InsertPointer` block-4 slots (db_stream.cpp:96-105) widen
    to 8 bytes; the alias read at db_stream_load.cpp:51 reads the translated
    8-byte value at the translated address.
  - **Block semantics preserved**: block 1 memset-zeroed at the **64-bit**
    expanded size; blocks 2-3 still deferred via `g_streamDelayArray` and
    translated when `Load_DelayStream` drains them (db_stream_load.cpp:9-27,
    :37-43; invoked db_file_load.cpp:291).
- Gate: FF0 synthetic zone containing rawfile + stringtable + localize loads
  end-to-end on device; assets present in `db_hashTable` via
  `DB_LinkXAssetEntry` (db_registry.cpp:1837); payload bytes verified against
  generator expectations; marker `Documents/ff_rawfile_ok.txt`; census green;
  Windows byte-identical.

### FF3 — Asset-type waves (census-style graduation)
Convert the 25 top-level handlers (Load_XAssetHeader switch,
db_load.cpp:6746-6852) in dependency-ordered waves, each with a synthetic zone
exercising that type and field-level spot checks:
- **Wave A — flat/simple**: PhysPreset, SndCurve, MapEnts, GameWorldMp,
  ComWorld, GfxLightDef, MenuList(shallow), SndDriverGlobals(dead on PC),
  FxImpactTable shell.
- **Wave B — sound tree**: snd_alias_list_t → snd_alias_t (asserted 92) →
  SoundFile/SpeakerMap; LoadedSound (44). `Load_TempStringCustom`/`SL_GetString`
  interning must be live.
- **Wave C — render assets**: Material (80), MaterialTechniqueSet (148),
  shaders, GfxImage (36 — pointer mid-struct shifts all later fields). Note
  `Load_MaterialTechniqueSetAsset` → `Material_UploadShaders`
  (db_registry.cpp:718-719) touches the renderer: on iOS gate behind the DXVK
  bring-up or stub the upload with a recorded TODO.
- **Wave D — model/anim**: XModel (220, 12+ pointers), XSurface (56),
  XModelSurfs, XAnimParts (88) incl. the XAnimIndices pointer-union and the
  XAnimDeltaPart family.
- **Wave E — worlds**: clipMap_t (0x11C, ~20 array pointers + internals),
  GfxWorld (0x2DC, ~30 pointers, GfxWorldDpvsStatic embedded — the largest
  single job), GameWorldSp/PathData, com/game world types, DynEntity* set.
- **Wave F — UI + gameplay**: menuDef_t (0x11C, statement/expression trees —
  deepest pointer graph), Font_s, WeaponDef (2168 bytes, dozens of interleaved
  pointers), FxEffectDef/FxElemDef tree.
- Gate per wave: synthetic zone for the wave loads on device with field-level
  assertions; a checklist table in this doc (or PORT_JOURNAL) ticks types off;
  census green; Windows byte-identical. 7 types have no PC handler
  (0x00, 0x12, 0x18 partial, 0x1B-0x1E — see catalog) and are explicitly
  out of scope.

### FF4 — Real fastfile gate (needs the user's COD4 files, §6)
- Order: Mac host harness first (arm64 macOS build of the loader spine as a
  test target — same LP64 layouts as iOS, seconds-fast iteration), then device.
- Load stock `code_post_gfx.ff`, `common_mp.ff`, then a map zone.
- Gate: asset counts and script-string counts match values dumped from the
  Windows build loading the same files (ground-truth comparison harness); no
  block-overflow asserts; device marker `Documents/ff_realzone_ok.txt` naming
  the zone and asset count; journal entry.

### FF5 — Performance + cache decision point
- Measure device load time and peak staging memory on real zones.
- Implement translated-output caching: on first load, write translated blocks
  + relocation list to `Documents/`; subsequent loads skip translation (the
  de-facto hybrid from §2).
- **Explicit gate**: if first-load times or memory pressure are unacceptable,
  promote the translation code into a Mac-side repack tool (strategy B) using
  the same field maps. Either way this is where the strategy question is
  re-litigated with data, not before.

## 4. The struct-translation workload, quantified

From the KISAK_LAYOUT_ASSERT census (254 grep hits; 251 real assert sites
after excluding the 2 macro-definition lines in universal/kisak_layout.h and
the q_shared.h:9 include comment; NEXT_SESSION's "249" additionally excluded
the two non-struct oddballs at actor_aim.cpp:1103 and
r_staticmodelcache.cpp:248):

| Bucket | Count | Meaning |
|---|---|---|
| Assert-covered structs on the fastfile path | **55** | The machine-checkable core (full table: docs/fastfile-struct-catalog.md) |
| — of which pointer-bearing (need field maps) | **45** | Offsets shift, 4-byte slots become 8 |
| — of which pure-data (memcpy from stream unchanged) | **10** | XFile, XBoneInfo, XModelCollTri_s, GfxPackedVertex, DObjAnimMat, pathlink_s, pathbasenode_t, DynEntityPose, DynEntityClient, DynEntityColl |
| Remaining assert sites (runtime-only, never serialized) | ~196 | Script VM, playerState/bgs, aim assist, vehicles, FX editor, devgui, etc. — no fastfile work needed |
| Full transitive serialized closure (incl. ~100+ unasserted types) | **~150-165 layouts** | FxElemDef tree, menu/itemDef/statement graph, clipMap_t internals (cplane_s/cbrush_t/cLeaf_t/CollisionPartition), GfxWorld internals (GfxCell/GfxLightGrid/GfxSurface/GfxAabbTree), XAnimDeltaPart family, SoundFile/SpeakerMap, pathnode_constant_t, XModelCollSurf_s, PhysGeomList/BrushWrapper |
| — of which pointer-bearing | ~110-120 | The real field-map workload |

Mechanical-rewrite inventory in src/database/: 296 `Load_*` functions; 279
`Load_Stream` call sites (byte counts are hardcoded 32-bit literals — these
literals stay, as staging sizes); 25 top-level switch cases (26 types via the
shared clipmap handler); 24 verbatim `value == -1 || value == -2` dispatch
sites in db_load.cpp plus variants (~30 total), all instances of one ~15-line
pattern; 2 fixup functions + 2 allocator functions (db_stream_load.cpp:45-57,
db_stream.cpp:81-105).

The KISAK_LAYOUT_ASSERT sites double as the verification spec: each asserted
sizeof is the authoritative 32-bit staging size for that struct's field map,
checkable in CI against the generator and the field-map tables.

## 5. Risks and unknowns

- **License compatibility of adapted code.** This repo's lineage is
  KisakCOD/LWSS, GPL-3.0-credited (house rules). OAT is GPL-3.0 — compatible
  if code is adapted with attribution; still prefer imitating its *pattern*
  over pasting, since this tree's Load_ walkers already exist. CoD4x is
  **AGPL-3.0**: read for cross-checking the zone grammar only; do not adapt
  code. iw3x-port has **no license** (GitHub license=null): pipeline shape
  only, no code. The decompiled loader itself is derivative of Activision
  code — nothing here changes that posture, but it argues against publishing
  translated-zone caches or converted containers with any game data inside.
- **IW3-vs-IW4 format drift in precedent.** OAT's documented x86-in-x64 zone
  loading work and most zonetool-family activity centers on IW4+. IW3 details
  (version 5, no signed-zone support on the PC path — db_file_load.cpp:252
  errors on `IWff0100` secure files; block roles; asset table) must be
  verified against **this tree's** decompiled loader, which is ground truth,
  not against IW4 writeups.
- **2x block over-allocation is a heuristic, not a proof.** Pointer expansion
  at most doubles pointer bytes, but LP64 alignment padding can add more in
  pathological structs. Mitigation: the overflow assert (db_stream.cpp:91)
  stays armed; FF5's cache pass can compute exact 64-bit totals per zone.
  Note the 28-bit offset window (256MB/block) applies to *file-image* offsets
  and is unaffected by 64-bit expansion.
- **Interior offsets and aliases.** The offset form can reference any byte in
  a block, not just struct starts, and the alias form reads a pointer *value*
  stored at that location (db_stream_load.cpp:51). The translation table must
  map arbitrary interior file offsets through the owning allocation's field
  map. Getting this wrong corrupts silently; synthetic zones must include
  deliberate offset/alias/-2 cross-references to regression-test it.
- **Pointer unions.** XAnimIndices (asserted 4 → 8 on arm64) and
  pathnode_tree unions require knowing the live arm, which can depend on
  runtime data (e.g. bone counts). These are per-case, not mechanical; flag
  each in the field-map tables.
- **Live-engine side effects during load.** `Material_UploadShaders`
  (db_registry.cpp:718-719), `SL_GetString` interning
  (db_stream_load.cpp:80), locked D3D vertex/index buffers for blocks 7/8
  (db_memory.cpp:112-119) and their unlock in post-load
  (db_file_load.cpp:190-202), plus `DB_LoadDelayedImages`. Each couples the
  loader to subsystems that come up in other streams (renderer, SL). Sequence:
  stub loudly under KISAK_IOS, record in the wave checklist, close during
  renderer content-readiness.
- **Endianness/platform variants**: PC zones are LE and arm64 is LE — fine.
  Console (signed, big-endian) fastfiles are out of scope; the PC loader
  rejects secure files anyway (db_file_load.cpp:252, "authenticated file not
  supported").
- **externalSize / streamed data**: XFile.externalSize and delayed-image
  streaming interact with .iwd content outside the zone; scope of what real
  maps need beyond the .ff is an unknown until FF4.
- **Memory pressure on device**: staging buffer + 2x blocks concurrently; the
  iPad M5 has headroom, but measure at FF4/FF5 rather than assume.

## 6. What needs the user's real COD4 files vs synthetic zones

**Synthetic-first is the plan's backbone.** Everything through FF3 is built
and machine-verified without any Activision data:

- The FF0 generator emits zones whose exact expected contents are known, so
  every gate is a byte-level assertion, not an eyeball — matching the house
  rule that every claim is machine-verified.
- The Windows build of this tree is the ground-truth oracle: any synthetic
  zone must load identically through the stock 32-bit path before it is used
  to judge the 64-bit path. Divergence then isolates to the translation layer
  by construction.
- OAT's Linker can build IW3 zones independently, giving a *second* generator
  to cross-validate ours (catches shared misconceptions in ours); it runs fine
  on the Mac toolchain.
- Synthetic zones can deliberately exercise edge cases real zones hit rarely:
  -2 alias chains, interior-offset references, block 2/3 delay-stream
  ordering, empty script-string tables, per-type minimal assets per FF3 wave.

**Real files are required only at FF4+**, for:

- Final verification at scale: real block sizes (MBs, not KBs), real asset
  counts, the full transitive closure actually exercised (union arms, rare
  fields no synthetic zone thought to include).
- Ground-truth diffing: asset/string counts dumped by the Windows build
  loading the same .ff.
- Load-time and memory measurements that drive the FF5 cache/repack decision.
- Anything touching externalSize/.iwd-adjacent streaming.

The user's files never enter the repo (they are user-supplied on the Mac and
device, mirroring the Ship of Harkinian posture); CI runs entirely on
synthetic zones.
