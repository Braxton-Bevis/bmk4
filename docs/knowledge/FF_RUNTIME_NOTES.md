# COD4 (IW3) Fastfile Runtime-Loading Knowledge Pack — BMK4 Stage C

*Sonnet research agent under the coordination seat, 2026-07-13. Sources:
this tree (file:line cited), the OAT clone (independent reimplementation),
CoD4x_Server (AGPL — read-only cross-check), web. Unverified items are
flagged, not guessed. Sol: adversarial-review alongside OAT_EVALUATION.md.*

## 1. The runtime pipeline, function by function

### Container level (once per zone)
- Entry: DB_LoadXAssets (src/database/db_registry.cpp:2171) → unload
  colliding zones → DB_LoadXZone (:2261) queues g_zoneInfo[] and wakes the
  database thread (Sys_WakeDatabase/2, :2287-2300).
- DB_Thread (:2319) → DB_TryLoadXFile (:2344) → per zone DB_LoadXFile
  (db_file_load.cpp:340) inits g_load, then DB_LoadXFileInternal
  (db_file_load.cpp:204):
  1. Validate 8-byte magic IWff0100/IWffu100 (:226-230).
  2. Version must be exactly 5 (:235-251).
  3. fileIsSecure = (magic != "IWffu100") (:252); secure path fails with
     "authenticated file not supported" (:255-264; db_auth.cpp:9
     iassert(!isSecure)) — this PC loader only supports unsigned zones.
  4. DB_LoadXFileData(&file, sizeof(XFile)) (:266) reads the 44-byte XFile
     {size, externalSize, blockSize[9]} (src/xanim/xanim.h:1122-1128)
     directly to its final address — inflate targets final addresses, no
     staging buffer anywhere in the stock design.
  5. DB_AllocXZoneMemory(file.blockSize,…) (:278) allocates the 9 blocks.
  6. DB_InitStreams (:279; db_stream.cpp:14) resets g_streamPosArray[9].
  7. Load_XAssetListCustom (:280,:305): 16-byte XAssetList header, then
     Load_ScriptStringList (db_load.cpp:641) inside block 4
     (DB_PushStreamPos(4), db_file_load.cpp:310).
  8. Load_XAssetArrayCustom(assetCount) (:316): per entry Load_Stream(1,
     ptr, 8) then Load_XAsset → Load_XAssetHeader (db_load.cpp:6854,:6746)
     — 25-case switch setting var<Type>Ptr and recursing.
  9. Then DB_FinishGeometryBlocks (D3D buffer unlock, :190,:289),
     Load_DelayStream() drains blocks 2/3 (:291), DB_LoadDelayedImages
     (:292,:178), DB_CancelLoadXFile (:297,:38).

### The recursive walk (the ~30-site stereotyped shape)
Example Load_XModelPtr (db_load.cpp:2877-2899; same shape at :984-1013):
read 4-byte slot via Load_Stream; if value is -1/-2 the body follows
in-stream: claim cursor via DB_AllocStreamPos, -2 additionally reserves an
alias slot in block 4 via DB_InsertPointer, recurse, back-patch the alias;
otherwise DB_ConvertOffsetToPointer resolves a cross-block reference.
Load_XString/Load_XStringPtr (db_load.cpp:590-605,623-638) are the
single-sentinel (-1 only) variant.

### Script-string patching (two mechanisms)
- Interning: Load_TempStringCustom (db_stream_load.cpp:74-84) →
  Load_XStringCustom (:59-72) → SL_GetString(*str, 4u); driven for the
  zone's whole string array by Load_ScriptStringList (db_load.cpp:641-652).
- Index remap: Load_ScriptStringCustom (db_stringtable_load.cpp:3-6):
  *var = (uint16_t)varXAssetList->stringList.strings[*var] — an array
  lookup into the already-interned list, not a dictionary.
- Mark_ScriptStringCustom (:8-12) is the unload-side SL_AddUser refcount.

### Offset scheme (db_stream_load.cpp:45-57)
DB_ConvertOffsetToPointer: *data = &blocks[(*data-1)>>28].data[(*data-1)
& 0xFFFFFFF] (address). DB_ConvertOffsetToAlias additionally dereferences
(reads a pointer previously written via DB_InsertPointer). Top 4 bits =
block, -1 bias, 28-bit offset. DB_InsertPointer (db_stream.cpp:96-105)
always reserves 4 bytes in block 4.

### XZone blocks 0-8 (db_memory.cpp:7-21; XBlock at xanim.h:1079-1092)
| # | name | behavior |
|---|------|----------|
| 0 | temp | immediate read |
| 1 | runtime | ZERO-FILLED, never read from file (db_stream_load.cpp:11-14) |
| 2 | large_runtime | DELAY-streamed |
| 3 | physical_runtime | DELAY-streamed |
| 4 | virtual | immediate; also the DB_InsertPointer alias block |
| 5 | large | immediate |
| 6 | physical | immediate |
| 7 | vertex | backs a LOCKED D3D vertex buffer (db_memory.cpp:112-113) |
| 8 | index | backs a LOCKED D3D index buffer (:118-119) |
DB_AllocXZoneMemory (db_memory.cpp:75-120) allocates via PMem_Alloc and
locks blocks 7/8 D3D buffers immediately.

### Blocks 2/3 delay stream, concretely (db_stream_load.cpp:4-43)
Load_Stream branches on g_streamPosIndex: 0/≥4 → immediate read; 1 →
memset 0; 2/3 → record (ptr,size) into g_streamDelayArray (cap 4096) and
advance the logical cursor WITHOUT reading. Load_DelayStream (invoked once,
db_file_load.cpp:291, after the whole asset array) drains the records in
recorded order — the compressed bytes for blocks 2/3 sit later in the one
continuous zlib stream. (WHY is inferred from mechanics — verify against
FF0/FF4 dumps; flagged.)

### Linking (db_registry.cpp:1837-1967)
DB_LinkXAssetEntry hashes into db_hashTable[0x8000] via DB_HashForName,
chain-compares via DB_GetXAssetName/I_stricmp, inserts or applies
override-priority (DB_OverrideAsset :2065-2072, zone flags) — the layering
mechanism for localized/mod zones. DB_AddXAsset (:1823) is the public
single-asset path.

## 2. useFastFile=0 — settled: cannot boot a menu
- The dvar says it: "Only tools can run without fast files"
  (src/qcommon/common.cpp:1559-1563). IsFastFileLoad() =
  q_shared.h:885-891.
- LoadObj enumeration knows only FOUR types: XMODEL (com_fileDataHashTable),
  MATERIAL, TECHNIQUE_SET, IMAGE (db_registry.cpp:1083-1159); everything a
  menu needs (Font, MenuList, menuDef, LocalizeEntry, StringTable) has NO
  loose path.
- DB_FindXAssetHeader asserts iassert(IsFastFileLoad()) at entry
  (db_registry.cpp:1183): by-name lookup is impossible with fastfiles off.
- FS_IsBasePathValid requires fileSysCheck.cfg (com_files.cpp:2248-2257);
  the only bypass is BMK4's own FS_iOS_HeadlessNoAssetsActive (:71-74,
  :2253) — a Stage-B scaffold, not a loose-file boot path.

## 3. Container format triangulation
Three independent sources agree on magic/version-5/44-byte XFile/9 blocks/
block<<28|offset with -1 bias and -1/-2 sentinels: this tree's decompile,
OAT (ZoneConstantsIW3.h:15-18,41-42 — also proves version 5 = PC and
Xenon = version 1 big-endian; ZoneInputStream.cpp block-type semantics
match), and CoD4x_Server src/db_load.cpp (AGPL, read-only). zeroy.com's
wiki page is stale — do not use. No iw3xo binary-format writeup exists.
NOTE: OAT deep-copies into native-width structs, so it never solves the
in-place 32→64 problem — only the container/offset mechanics and the
per-struct layout knowledge transfer (consistent with OAT_EVALUATION's
adapt-the-generator recommendation, not adopt-the-loader).

## 4. Boot zones: load order (SETTLED by this tree) and the menu-minimal set
CL_SetFastFileNames (src/client_mp/cl_main_mp.cpp:2439-2449) +
R_LoadGraphicsAssets (src/gfx_d3d/r_init.cpp:3717-3764) load, in one
DB_LoadXAssets call, in this fixed order:
1. code_post_gfx_mp (allocFlags=2)
2. localized_code_post_gfx_mp (0)
3. ui_mp (8; skipped for dedicated)
4. common_mp (4)
5. localized_common_mp (1)
6. mod (16; only if present)
Zone path: <install>\zone\<language>\<name>.ff (DB_BuildOSPath,
db_registry.cpp:2726-2734). DB_IsMinimumFastFileLoaded gates on
localized_code_post_gfx_mp (db_file_load.cpp:295-303). The
g_defaultAssetName fallback table (db_registry.cpp:228-267: "$default"
Material, "default" TechniqueSet, "$white" Image, "fonts/consolefont",
"ui/default.menu", "default_menu", "mp/defaultStringTable.csv") must
resolve from the earliest zones.

Menu-only milestone asset-type set: MENU, MENULIST, FONT, MATERIAL,
TECHNIQUE_SET, IMAGE, STRINGTABLE, LOCALIZE_ENTRY (+ RAWFILE unconfirmed;
SOUND referenced via itemDef_s.focusSound at ui_shared.h:485 but plausibly
stubbable while audio is deferred). Struct evidence: windowDef_t.background
is Material* (ui_shared.h:373); menuDef_t embeds windowDef_t + font +
items (ui_shared.h:504+); Font_s → Material/glowMaterial (r_font.h:21).

## Flagged gaps (not settled)
1. Why blocks 2/3 delay — mechanical inference; verify with FF0/FF4 dumps.
2. Exact per-zone contents of ui_mp/common_mp — naming+order inference;
   FF0a oracle dump settles it.
3. "PC rejects signed zones" — code-verified here; no prose source.
4. IW3-vs-IW4 block-model drift unchecked; distrust IW4-based writeups.
5. RawFile's necessity for menu path — guess.

Key refs: db_file_load.cpp:93-368; db_memory.cpp:7-128; db_stream.cpp:
14-105; db_stream_load.cpp:4-85; db_stringtable_load.cpp:1-13;
db_registry.cpp:78-267,1020-1183,1780-2340; db_load.cpp:590-652,975-1013,
2860-2899,6746-6863; q_shared.h:860-891; com_files.cpp:62-75,2248-2257;
common.cpp:1558-1563; r_init.cpp:3717-3764; r_init.h:37-51;
cl_main_mp.cpp:2439-2449; ui_shared.h:355-374,450-514; xanim.h:905-944,
1079-1128.
