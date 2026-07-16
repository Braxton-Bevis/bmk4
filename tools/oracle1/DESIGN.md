# bmk4-oracle1 — engine-instrumented zone loader (Oracle 1)

Status: DESIGN (Lane C). Reviewed adversarially by the Sol pair before code;
review record in `docs/reviews/oracle1-lane-c-notes.md`.

## 1. Position in the oracle taxonomy

Per `docs/reviews/orchestrator-doctrine-claude.md`, Oracle 0
(`tools/ff_oracle`) is a container inspector: it parses bytes but executes no
loader semantics. Oracle 1 is the *engine-instrumented* loader: the zone is
loaded by the REAL Windows engine loader code and the tool records what that
code actually does. Runtime asset-graph semantics must come from Oracle 1,
not Oracle 0 (doctrine ruling 3), and every kernel wave must be
engine-qualified by Oracle-1-style evidence before it ships
(`ff-kernel-k01-claude-response.md`, challenge 10).

Evidence tier of every claim this tool produces: **RUNTIME (Windows x86,
real loader TUs)** — stronger than the static-read tier the fixture builder
and the layout manifests occupy.

## 2. Closure analysis — what links, what is scaffolded

### 2.1 Real engine TUs (linked whole, unmodified semantics)

The tool links the complete `DATABASE` module exactly as the shipping
targets do (`scripts/common_files.cmake`):

| TU | Role in the load | Link-time externals (beyond the database module / CRT / Win32) |
|---|---|---|
| `db_load.cpp` | All `Load_*` walkers, dispatch (`Load_XAsset(Header)`), all `AllocLoad_*` | `MyAssertHandler`, `SND_SetData`, `Com_GetServerDObj`, `Com_GetClientDObj`, `DObjArchive`, `DObjUnarchive` |
| `db_stream.cpp` | Block cursors: `DB_InitStreams/Push/Pop/SetStreamIndex/AllocStreamPos/IncStreamPos/InsertPointer` | none |
| `db_stream_load.cpp` | `Load_Stream`, `Load_DelayStream`, `DB_ConvertOffsetToPointer/Alias`, `Load_XStringCustom`, `Load_TempStringCustom` | `MyAssertHandler`, `SL_GetString` |
| `db_stringtable_load.cpp` | `Load_ScriptStringCustom` (index remap), `Mark_ScriptStringCustom` | `SL_AddUser` |
| `db_file_load.cpp` | `DB_LoadXFile(Internal)`, staged overlapped reads, inflate pump, `Load_XAssetListCustom`, `Load_XAssetArrayCustom` | `Com_Error`, `Com_Printf`, `va`, `I_stricmp`, `Sys_WaitDatabaseThread`, `R_DelayLoadImage`, `R_FinishStaticVertexBuffer/IndexBuffer`, `KISAK_NULLSUB`, Win32 (`ReadFileEx`, `SleepEx`, …) |
| `db_memory.cpp` | `DB_AllocXZoneMemory`, `DB_MemAlloc` | `Com_Error`, `PMem_Alloc`, `PMem_GetOverAllocatedSize`, `R_*StaticVertexBuffer/IndexBuffer` family |
| `db_auth.cpp` | `DB_AuthLoad_Inflate*` (zlib lane) | zlib only (`inflateInit_`, `inflate`, `inflateEnd`) |
| `db_assetnames.cpp` | `DB_GetXAssetName`, per-type name get/set, type sizes | `MyAssertHandler`, `va` |
| `db_registry.cpp` | `DB_AddXAsset`, `DB_LinkXAssetEntry`, `DB_AllocXAssetEntry`, pools, hash table, `Load_*Asset` for every type | large: `Sys_*` thread/database family, `SL_ConvertToString/GetString/ShutdownSystem/TransferSystem`, `PMem_*`, `FS_*`, `Cmd_*`, `Dvar_RegisterString`, `Com_*` print/error/sync, `NET_Sleep`, `R_*`/`RB_*`/`Material_*` render family, `CG_VisionSetMyChanges`, `BG_FillInAllWeaponItems`, `CM_Unload`, `Image_IsProg`, `IsFastFileLoad`, `IsUsingMods`, `Win_GetLanguage`, `Z_Free`, `track_static_alloc_internal`, `ProfLoad_*`/`Profile_*`, string utils (`I_*`), data globals `cm`, `comWorld`, `gameWorldMp`, `fs_gameDirVar`, `loc_warnings*`, `DB_AllocMaterial`, `DB_FreeMaterial` |

Nothing inside these TUs is reimplemented, wrapped-out, or forked. The
functions the K-series kernel needs qualified — `Load_Stream`,
`DB_AllocStreamPos`, push/pop, `Load_RawFile` (db_load.cpp:5643),
`Load_StringTablePtr` (db_load.cpp:5711), `Load_ScriptStringList`,
`Load_TempString*`, `Load_XString*`, `Load_XAnimParts*`,
`DB_ConvertOffsetToPointer/Alias`, `DB_InsertPointer`,
`DB_LinkXAssetEntry` — all execute as compiled from the real sources.

`ASSET_TYPE_RAWFILE = 31`, `ASSET_TYPE_STRINGTABLE = 32`,
`ASSET_TYPE_XANIMPARTS = 2` (xanim.h, MP numbering) — matching the fixture
manifests' `RAWFILE(31)` / `STRINGTABLE(32)` / `XANIMPARTS(2)` labels; the
tool compiles with `KISAK_MP` like the mp/dedi shipping targets.

### 2.2 Tool-owned code (four files, `tools/oracle1/`)

1. **`oracle1_main.cpp`** — CLI, allowlist enforcement (copied from Oracle
   0's refusal pattern: canonicalized containment check, exit 3, output
   path checked too), trace file management, and the zone-load driver.
   The driver replicates the *setup* half of `DB_TryLoadXFileInternal`
   (db_registry.cpp:2650–2716) rather than calling it, because the real
   function front-loads mod-dir probing, the `zone_reorder` dvar, and
   `$init` waits that belong to the full client. The driver performs, in
   order, exactly what the engine performs before `DB_LoadXFile`:
   `DB_Init()` (real), zone-slot claim in `g_zones[1]` + `g_zoneHandles` +
   `g_zoneCount`/`g_loadingZone`/`g_zoneIndex`, `CreateFileA(...,
   0x60000000, ...)` with the engine's exact flags
   (`FILE_FLAG_OVERLAPPED|FILE_FLAG_NO_BUFFERING`), `g_loadingAssets = 1`,
   `DB_ResetZoneSize(0)`, then hands off to the REAL `DB_LoadXFile` +
   `DB_LoadXFileInternal` with the real `g_fileBuf`. Every replicated line
   cites its db_registry.cpp source line in a comment.
2. **`oracle1_trace.cpp/.h`** — the event emitter (schema §4). Owns the
   output stream; every write is flushed line-wise so refusal paths keep
   their prefix.
3. **`oracle1_scaffold.cpp`** — the abort-loud link scaffold in the
   `ios/Stub/BootScaffold.cpp` discipline: every out-of-scope engine symbol
   referenced by the nine TUs gets a definition that prints its own name
   and exits with the scaffold code; a small documented subset is
   *functional* (§2.3). Declarations come from the same engine headers the
   database TUs use, so a signature drift is a compile/link error, not a
   silent ABI break.
4. **`check_trace.py`** — the qualification gates (§6).

### 2.3 Functional scaffold subset (documented allowances)

These are *platform/service boundaries*, not loader semantics. Each is
listed with its contract and why the trace stays honest:

| Symbol(s) | Tool-owned behavior | Honesty argument |
|---|---|---|
| `MyAssertHandler` | emit `ev=error kind=assert` + flush + `exit 4` | This is the engine's own refusal firing; the tool only reports it. Never returns, matching engine expectation. |
| `Com_Error`, `Com_ErrorAbort` | emit `ev=error kind=com_error` + flush + `exit 5` | Same; ERR_DROP never returns into the loader. |
| `Com_Printf/PrintWarning/PrintError`, `va`, `Com_sprintf` | stdout / `vsnprintf` | Formatting only; never enters the trace stream except as the engine-emitted text of an error event. |
| `PMem_Alloc`, `PMem_GetOverAllocatedSize`, `PMem_Begin/EndAlloc`, `PMem_Free`, `Z_Free` | 4096-aligned VirtualAlloc arena, **plus 64 KiB guard slack per block** | The engine's own `DB_IncStreamPos` fence (`db_stream.cpp:91`) stays the *observable* failure on an over-model walk instead of a heap AV that would truncate the trace. Block *sizes* given to `DB_AllocXZoneMemory` are the zone's own declared sizes — the fence itself is untouched engine code. |
| `Sys_IsMainThread/IsDatabaseThread/IsRenderThread` | `true/false/false` | Tool is single-threaded; the database thread is never spawned, so main-thread identity is the truthful answer. |
| `Sys_LockWrite/UnlockWrite`, `Sys_Enter/LeaveCriticalSection`, `Sys_WaitDatabaseThread`, `NET_Sleep`, remaining `Sys_*Database*` | uncontended single-thread implementations over the real `FastCriticalSection` fields / no-ops | Single-threaded execution is a *documented divergence* from the engine's worker/database threading. Consequence for evidence: event *order* is the deterministic single-thread walk order (which is also the order the engine's loader logic imposes on the byte stream — the walk itself is sequential even in the engine; only I/O staging overlaps). |
| `SL_GetString`, `SL_ConvertToString`, `SL_AddUser` | deterministic interning table: first-use order, handles 1,2,3…; emits `ev=sl_intern` | The SL subsystem is beyond scope. HANDLE VALUES are tool-defined and the trace must never be read as qualifying them; what IS engine-real is the remap *structure* (`Load_ScriptStringCustom` executes real code storing `strings[index]` into the slot). The trace records index→handle→string-hash triples so gates check structure, not values. Uses of the handle inside `Load_TempStringCustom` (db_stream_load.cpp:80, `SL_GetString(*str, 4u)`) run unmodified. |
| `I_stricmp/I_strncmp/I_strncat/I_strncpyz/I_strnicmp/I_stristr` | plain ASCII implementations | Case-insensitive compare/copy utilities; behavior-identical for the ASCII fixture names. |
| `Dvar_RegisterString` | returns a static zeroed dvar | Only reachable from `DB_TryLoadXFileInternal`/`DB_LoadZone_f` paths the driver does not call; exists to satisfy the Debug (`/OPT:NOREF`) link. |
| `track_static_alloc_internal`, `ProfLoad_Begin/End`, `Profile_*`, `KISAK_NULLSUB` | no-ops | Telemetry only. |
| Data globals `cm`, `comWorld`, `gameWorldMp`, `fs_gameDirVar`, `loc_warnings`, `loc_warningsAsErrors` | zero-initialized storage | Referenced by `DB_XAssetPool` (clipmap/comworld/gameworld singletons) and unreachable print paths. `cm` as the CLIPMAP pool target is real engine topology (`node1_` returns the pool pointer); the storage must merely exist. |
| Everything else (`R_*`, `RB_*`, `Material_*`, `FS_*`, `Cmd_*`, `CG_*`, `BG_*`, `CM_Unload`, `SND_SetData`, `DObj*`, `Com_GetServerDObj`, …) | **abort-loud**: print symbol, `exit 6` | Reached only if a fixture exercises a subsystem outside this wave's scope; a loud abort is the required behavior (never fake a tail). |

The complete scaffold is enumerated by a grep census of the nine TUs
(recorded in `oracle1_scaffold.cpp` comments); the Debug configuration
links with `/OPT:NOREF`, so CI proves the census complete — a missed
symbol is a red link step, not a silent gap.

### 2.4 Explicitly NOT in scope

- The database worker thread (`DB_Thread`, `Sys_SpawnDatabaseThread`) — the
  load runs synchronously on the main thread. Staged overlapped I/O still
  runs through the real `DB_ReadData`/`ReadFileEx`/`SleepEx` alertable-APC
  machinery, single-threaded.
- Geometry blocks 7/8 (`R_AllocStatic*Buffer`) — abort-loud; no fixture
  declares vertex/index block bytes.
- `IWff0100` (signed/secure) files — real `DB_AuthLoad_InflateInit` refuses
  via the real `iassert(!isSecure)` + `failureReason` path.
- Real retail zones in CI — allowlist-refused (exit 3) exactly like Oracle
  0; real-zone runs happen only on the owner's machine, locally.

## 3. Instrumentation — mechanism and byte-identity

### 3.1 Why some hooks must live inside engine sources

The observation points (`DB_PushStreamPos`, `DB_IncStreamPos`,
`Load_Stream`, `DB_ConvertOffsetToPointer`, …) are *leaf functions called
from inside other engine TUs*. MSVC's linker has no `--wrap`, so a
tool-owned wrapper can never interpose on engine-internal call edges.
Where a boundary IS tool-owned (the scaffold: `SL_GetString`,
`MyAssertHandler`, `Com_Error`, `PMem_Alloc`) the hook lives in the
scaffold, per the "prefer tool-owned wrappers" rule. The remaining hooks
are `#ifdef BMK4_ORACLE1` blocks inside six engine files.

### 3.2 The `#line` byte-identity discipline

`BMK4_ORACLE1` is defined ONLY for the `bmk4-oracle1` target — never for
KisakCOD-sp/mp/dedi. Preprocessor-inert guards keep shipping *code*
identical, but inserting source lines would still shift `__LINE__` inside
downstream `iassert`/`vassert` expansions and change Debug-build string
literals. Every guarded insertion is therefore followed by a `#line N`
directive restoring the original numbering of the next source line:

```cpp
#ifdef BMK4_ORACLE1
    Bmk4Or1_StreamPush(index);          // tool hook, compiled out for shipping
#endif
#line 29                                 // next line is original line 29
```

`#line` participates in both branches, so `__LINE__` (and therefore every
assert string) is identical whether or not the guard is active. `__FILE__`
is untouched. Shipping-target translation units preprocess to the same
token stream as before the edit.

### 3.3 Hook inventory (all guarded, all one-liners into `bmk4_oracle1_instr.h`)

| File | Function | Event(s) |
|---|---|---|
| db_stream.cpp | `DB_PushStreamPos` | `stream_push` (requested index, new stack depth) |
| db_stream.cpp | `DB_PopStreamPos` | `stream_pop` (restored index) |
| db_stream.cpp | `DB_AllocStreamPos` | `alloc` (block, align, offset after alignment) |
| db_stream.cpp | `DB_IncStreamPos` | `inc` (block, offset before, size) |
| db_stream.cpp | `DB_InsertPointer` | `alias_insert` (block-4 slot offset) |
| db_stream_load.cpp | `Load_Stream` (atStreamStart && size) | `fill` (block, offset, size, source = file / zerofill / delay_queue) |
| db_stream_load.cpp | `DB_ConvertOffsetToPointer` | `ptr_offset` (raw token, decoded block, decoded offset) |
| db_stream_load.cpp | `DB_ConvertOffsetToAlias` | `ptr_alias` (raw token, decoded block, decoded offset) |
| db_stream_load.cpp | `Load_DelayStream` | `delay_drain` (index, size, dest block+offset) |
| db_file_load.cpp | `DB_LoadXFileData` | `inflate` (request size; dest resolved to block+offset or `external`) |
| db_file_load.cpp | `DB_LoadXFileInternal` (post-XFile read) | `xfile` (size, externalSize, 9 block sizes) |
| db_file_load.cpp | `Load_XAssetListCustom` (post-header read) | `assetlist` (string count + token, asset count + token) |
| db_load.cpp | `Load_XAsset` (post 8-byte read) | `asset_dispatch` (running index, type id, type name) |
| db_stringtable_load.cpp | `Load_ScriptStringCustom` | `scriptstring_remap` (local index before, handle after) |
| db_registry.cpp | `DB_AddXAsset` (post-link) | `asset_insert` (type, FNV-1a64 of name incl. NUL, outcome) |

`-1` inline pointer tokens are not separately hooked: an inline token is
precisely "a pointer field followed by `alloc`+`fill` with no
`ptr_offset`/`ptr_alias`/`alias_insert` event", and the schema documents
that reading. `-2` is covered by `alias_insert`; every offset token is
covered by the two convert hooks.

All hook implementations live in `oracle1_trace.cpp`; hooks report
positions ONLY as (block index, offset from `blocks[i].data`) — never raw
pointers — which is what makes the trace ASLR-independent (§5).

## 4. Event schema `bmk4.oracle1.v1`

Line-oriented text, first line `schema=bmk4.oracle1.v1`, then one event per
line: `ev=<name> key=value key=value …` with a fixed key order per event
type, decimal offsets/sizes, `0x`-prefixed lowercase hex tokens, bare
16-digit lowercase hex FNV-1a64 values. Events:

```
zone_open        name=<basename> bytes=<n>
container        magic=IWffu100 version=5
xfile            size=N external=N b0=N .. b8=N
assetlist        strings=N strings_token=0x… assets=N assets_token=0x…
inflate          size=N dest=blockB+O | dest=external
stream_push      index=B depth=D
stream_pop       index=B depth=D
alloc            block=B align=A offset=O
inc              block=B offset=O size=N
fill             block=B offset=O size=N src=file|zerofill|delay_queue
ptr_offset       token=0x… block=B offset=O
ptr_alias        token=0x… block=B offset=O
alias_insert     block=4 offset=O
asset_dispatch   index=I type=T name=<typename>
sl_intern        handle=H hash=<fnv64 of bytes incl NUL> [text=<str>]
scriptstring_remap index=I handle=H
asset_insert     type=T typename=<name> namehash=<fnv64 incl NUL> outcome=new|existing|override [name=<str>]
delay_drain      index=I size=N block=B offset=O
zone_loaded      name=<basename>
error            kind=assert|com_error|scaffold detail=<text>
```

String hashing convention: FNV-1a64 over the UTF-8 bytes INCLUDING the
terminating NUL — the same `utf8_nul` convention as the fixture manifests
(`tools/zone_fixtures/README.md` §Manifest hashing), so gate checks are
direct equality against manifest fields.

Sanitization: `text=`/`name=` payloads appear ONLY when `--emit-names` is
passed. CI fixture runs pass it (synthetic labels only, per the fixture
charter); the flag defaults OFF so a local real-zone run leaks only hashes.
The `zone_open` name is the input file's basename (CI: fixture names;
local: zone name, which also appears in every retail filename — accepted).

## 5. Determinism argument (gate a)

Sources of nondeterminism examined:

- **Pointers/ASLR** — no raw pointer value is ever emitted; positions are
  block-relative offsets; `dest=external` replaces out-of-block pointers.
- **Threading** — single thread; APC completion via alertable `SleepEx`
  serializes I/O completion into program order.
- **Allocation** — `PMem_Alloc` scaffold offsets don't enter the trace;
  block offsets derive from `DB_InitStreams` + walk order only.
- **SL handles** — first-use sequential; walk order is deterministic.
- **Iteration order** — no hash-map iteration is traced; `db_hashTable`
  chains affect only lookup, and insertion outcomes are order-determined.
- **Time/size** — `g_trackLoadProgress=0` path skips `GetFileSize`
  accounting; no timestamps in the trace.
- **Buffers** — `g_fileBuf` and zone blocks are process-fresh each run
  (one process per trace); stale-bytes reads (e.g. `avail_in` over-credit
  at EOF) read zeroed memory identically each run.

CI enforces the gate mechanically: every fixture is loaded twice in
separate processes; SHA-256 of the two traces AND the two exit codes must
match, including for refusal traces.

## 6. Qualification gates (gate b, gate c)

`tools/oracle1/check_trace.py <trace> --manifest <MANIFEST.json> --gate <n>`

**Gate b — fixture 01 vs the shipped iOS kernel model.** Asserts the trace
contains, in order (kernel claims ↔ engine events):

1. `alloc block=4 align=3 offset=0` + `fill block=4 offset=0 size=8
   src=file` — asset array in block 4 (K1 model: virtual block).
2. `asset_dispatch index=0 type=31` — RAWFILE.
3. `stream_push index=0` then `fill block=0 offset=0 size=12 src=file` —
   RawFile struct in block 0 (`Load_RawFilePtr` pushes 0,
   db_load.cpp:5664).
4. `stream_push index=4` then name bytes: `alloc block=4 align=0 offset=8`
   + `inc block=4 offset=8 size=25` — name interned in block 4 under the
   RawFile's own `DB_PushStreamPos(4)` (db_load.cpp:5646).
5. buffer truthiness: `alloc block=4 align=0 offset=33` + `fill block=4
   offset=33 size=6 src=file` — buffer loaded because the stored token was
   nonzero (db_load.cpp:5649–5653), `len+1` bytes.
6. `asset_insert type=31 namehash=fc7845dd3a44c753` (manifest
   `rawfile[0].name` `utf8_nul` hash).
7. exit code 0 and `zone_loaded`.

**Gate c — fixture 02 StringTable block adjudication.** The dispute: the
fixture builder placed `stringtable[0]`, `.name`, `.values`, `.value[k]`
in block 0 (`MANIFEST.json` stream_events); static reading of
`Load_StringTablePtr` (db_load.cpp:5711–5728) shows NO
`DB_PushStreamPos` — allocations stay in the block active at dispatch
time, which is block 4 (pushed at db_file_load.cpp:281 before the asset
walk). The checker extracts the runtime events between
`asset_dispatch type=32` and the next dispatch/error and reports which
block received (i) the 16-byte StringTable struct alloc/fill and (ii) the
name bytes. The verdict is recorded in
`tools/oracle1/FIXTURE02_VERDICT.md`; the CI step fails if the trace lacks
the struct-placement events or contradicts the recorded verdict.

Desk-computed prediction (to be confirmed/refuted by the first CI run,
recorded either way): block-4 cursor walk = script-string region 0–31,
asset array 32–47, StringTable struct 48–63, name alloc at 64; the name's
20-byte `inc` trips the engine's own fence `g_streamPos + size <=
block.data + block.size` (64+20 > 72 declared) → `ev=error kind=assert`
and exit 4. That partial trace IS the adjudication: the engine put the
StringTable body in block 4 until its fixture-declared block-4 budget ran
out, and block 0 never received a StringTable byte. Fixture 02 as built
cannot complete a real-engine load; Lane A's regeneration must move the
StringTable body accounting to block 4.

**Gate can fail (doctrine rule 5):** the CI step also runs
`01_rawfile_inline/malformed_truncated_buffer.ff` and
`02…/malformed_bad_script_count.ff` and asserts a NONZERO, non-3 exit
(engine-native refusal via truncation assert / `Load_Stream` stream-start
assert with `RELEASE_ASSERTS` forced on for this target), plus the exit-3
allowlist refusals with the FF0a `ErrorActionPreference` guard pattern.

## 7. Exit codes

| code | meaning |
|---|---|
| 0 | zone loaded; trace complete (`zone_loaded`) |
| 2 | usage / input-not-a-file / trace-unwritable (tool-level) |
| 3 | fixture allowlist refusal (input or output outside root) — matches Oracle 0 |
| 4 | engine refusal via `MyAssertHandler` (assert text in trace + stderr) |
| 5 | engine refusal via `Com_Error` (message in trace + stderr) |
| 6 | abort-loud scaffold reached (out-of-scope subsystem; symbol on stderr) |

## 8. Build wiring

`tools/oracle1/CMakeLists.txt`, added beside `tools/ff_oracle` for
`KISAK_PLATFORM=win32`:

- target `bmk4-oracle1`: the nine database TUs + the three tool TUs.
- defines: `KISAK_MP;CINEMA;USE_SEPARATE_BLIT_TEXTURE;WIN32;_CONSOLE;_MBCS`
  (mp target parity) + `BMK4_ORACLE1` + `RELEASE_ASSERTS` (asserts stay
  observable in Release; tool-only, shipping flags untouched).
- includes: `SRC_DIR`, `DEPS_DIR`, `tools/oracle1`, DXSDK include (headers
  only — d3d9 types appear in engine headers; no d3d import lib is linked).
- links: `bmk4-ff-oracle-zlib` (same deps/zlib the engine embeds; provides
  `inflateInit_` for db_auth.cpp), kernel32/user32 defaults only.
- runtime: `MultiThreaded$<$<CONFIG:Debug>:Debug>` (engine parity).
- 32-bit only: the CI generator is `-A Win32`; the loader's pointer-token
  arithmetic (`(uint32_t)&block.data[…]`) requires ILP32. The CMake file
  hard-fails on a 64-bit platform.
- engine TUs compile with default /W3 (not /WX); tool TUs with /W4 /WX.

## 9. CI wiring (`build-kisarcod-win.yaml`, both configs)

1. `Build` step: add `--target "bmk4-oracle1"`.
2. New step `Oracle 1 engine loader gate` (id `oracle1_gate`, PowerShell,
   FF0a step style):
   - run fixtures 01–07 `valid.ff` twice each with
     `--fixture-allowlist-root $workspace --emit-names`; assert both runs
     byte-identical (SHA-256) with identical exit codes; record per-fixture
     accept/refuse in the step summary;
   - fixture 01 must exit 0; `check_trace.py --gate b` on its trace;
   - fixture 02: `check_trace.py --gate c` on its trace; verdict line into
     `$GITHUB_STEP_SUMMARY`;
   - malformed 01 + malformed 02 must refuse (nonzero, ≠3);
   - allowlist refusal probes (input outside root; output outside root)
     expect exit 3, wrapped in the FF0a `ErrorActionPreference` relax +
     trailing `exit 0` pattern.
3. Upload `oracle1-traces-<config>` artifact (traces + verdict output;
   synthetic fixture data only — no game assets can enter CI because the
   allowlist root confines inputs to the repo checkout).
4. Register the step in the staging `Verdict for coordinator` list.

## 10. Risks / expected CI surprises

- **Scaffold census misses** — Debug `/OPT:NOREF` guarantees discovery in
  the first link; budgeted for one fix round.
- **Signature drift** — scaffold includes engine headers instead of
  redeclaring, so drift is a compile error in the tool target only.
- **`#line` interactions with /MP or PCH** — the database TUs use no PCH;
  `/MP` is per-file and unaffected.
- **Engine headers under the tool target** — the nine TUs already compile
  under the mp target with the same defines/includes; the tool target
  reproduces those flags. Tracy is NOT defined for the tool
  (`TRACY_ENABLE` off ⇒ `Profile_*` are plain externs ⇒ scaffolded).
- **Fixture 02 prediction wrong** (e.g. the fence does not fire where
  desk-computed) — the gate records what the trace ACTUALLY says; the
  verdict document is written from the CI trace, not from the prediction.
