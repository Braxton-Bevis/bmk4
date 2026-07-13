# Review of `965d45f` — Claude

## CHALLENGES

1. **Claim under attack:** every real-minimal definition in
   `ios/Stub/BootScaffold.cpp` is narrow enough that a Phase 1–3 caller
   cannot silently consume fake state while a milestone marker remains green.
   **Concrete failure scenario:** a later boot stage resolves an engine symbol
   to a benign scaffold default, returns normally, and earns an FS or M15
   marker without the real subsystem semantics. **Cheapest settling test:**
   trace every real-minimal definition to its current caller, identify its real
   owner TU, and require that owner (or an explicitly equivalent implementation)
   before any marker whose contract depends on it.

## VERDICTS

### Challenge 1 response

The review artifact was not present at head `2573381`; the challenge above is
reconstructed verbatim in substance from the Phase 3 work order. The audit
below uses the implementation at `ios/Stub/BootScaffold.cpp`, its callers, and
the real owner definitions in `src/`.

| Definition(s) | Verdict and evidence | Marker-safety boundary |
|---|---|---|
| `FS_LoadStack` | **REFUTED with evidence for M13 only.** `Com_InitHunkMemory` requires a zero load stack at `src/universal/com_memory.cpp:387`; before the filesystem exists, zero is the true invariant. The real state accessor and `FS_InitFilesystem` are both in `com_files.cpp` (`:344`, `:2205`). | Wave 1 must delete this definition when it hard-requires the real `com_files` object. The FS marker is earned by real path and write/read/delete probes, never by this return value. |
| `Dvar_AddCommands` | **REFUTED with evidence for the scoped M13 claim; forbidden for M15.** It is reached by `Dvar_Init` (`dvar.cpp:2816-2820`) and intentionally omits the 15 console handlers in `dvar_cmds.cpp:507-524`. M13 proves registry write/read separately and claims no dvar-command behavior. | The final archive/provenance gate must require real `dvar_cmds.cpp`, remove this definition plus `info1`/`info2`, and behaviorally execute `set` and require `dvarlist`. A generic `cmdlist` lookup cannot earn M15. |
| `Com_LogFileOpen` | **REFUTED with evidence.** Before real common/log initialization no logfile exists, so false is accurate. Its only dvar-side guarded output is commented out at `dvar.cpp:1343-1347`. | Real `common.cpp:2232` replaces it in the common-spine wave. |
| `Sys_IsRenderThread` | **REFUTED with evidence for the staged main-queue path.** M13 runs on the main queue, and false makes main-or-render assertions stricter rather than bypassing them. | Real `threads.cpp:848-851` replaces it before any threaded Phase 3 claim. |
| `Com_PrintWarning` | **REFUTED with evidence.** It emits the supplied formatted warning to stderr and has no benign return or hidden state. | Real common owns the production sink later; no marker depends on warning capture. |
| `NET_Sleep` | **REFUTED with evidence.** The implementation really yields for zero and uses `nanosleep` for positive delays. | `win_net.cpp` must replace it in the networking wave; the network marker cannot be emitted while this is the selected owner. |
| `Sys_EnterCriticalSection`, `Sys_LeaveCriticalSection` | **REFUTED with evidence.** These are functional recursive pthread locks, validate the MP range of 22 sections, and abort on invalid indices. They are exercised by real command/dvar code. | The real synchronization owner replaces them when `win_common.cpp`/`threads.cpp` enters the exact Phase 3 archive. |
| `Sys_LockWrite`, `Sys_UnlockWrite` | **REFUTED with evidence.** They implement exclusive writer acquisition against `FastCriticalSection` read/write counts and abort on an unmatched unlock; dvar registration exercises the path. | The real `win_common.cpp:115-135` implementation replaces them in the platform closure. |
| `SL_GetString_`, `SL_ConvertToString` | **REFUTED with evidence for M13 only.** The bounded table genuinely interns strings, reference-counts reuse, returns stable IDs/pointers, and aborts on null, capacity, OOM, or invalid IDs. Real dvar string registration reaches it through `CopyString`. | Full `Com_Init` calls `SL_Init`; the M15 provenance gate must require `scr_stringlist.cpp` (which owns `SL_Init` and all four APIs) and reject these scaffold owners. No benign `SL_Init` stub is permitted. |
| `SL_FindString`, `SL_RemoveRefToString` | **CONFIRMED-DEFECT in one input lane.** Non-null find/not-found and validated remove behavior match the required subset, but `SL_FindString(nullptr)` returned zero while the real implementation immediately evaluates `strlen(str)` (`scr_stringlist.cpp:501-504`) and `FreeString` already asserts non-null (`com_memory.cpp:164-172`). | The null lane is converted to `BootScaffoldAbort("SL_FindString(null)")` in the protocol-housekeeping commit. Valid current M13/M14 calls are unaffected; simulator rerun is required. |
| `useFastFile`, `R_ReflectionProbeRegisterDvars`, `r_reflectionProbeGenerate` | **REFUTED with evidence only for explicit no-asset/headless hunk smoke.** False selects the intended no-fastfile hunk size and matches the real reflection-generate default. It does not constitute a registered renderer dvar. | M15 must use the real `useFastFile` registered by `common.cpp`, prove it is disabled, and exclude the synthetic reflection value from its dvar floor. Reflection registration graduates before renderer Phase 5 or any name lookup. |
| `track_*` bodies | **REFUTED with evidence.** These hooks maintain diagnostics only; allocation, bounds, readback, and failure behavior remain in real `com_memory.cpp`. `track_PrintAllInfo` is followed by fatal `Com_Error` on the relevant OOM paths, so a no-op cannot turn failure into success. | `meminfo`/`tempmeminfo` output is explicitly disallowed as M15's command post-condition. |
| `com_fileDataHashTable` | **REFUTED with evidence.** It is correctly sized zero-initialized storage; real `com_memory.cpp:437,483-484` owns the behavior operating on it. | The DB owner replaces the storage when `db_registry.cpp` graduates; no DB marker may rely on the scaffold owner. |
| `com_sv_running` | **REFUTED with evidence for M13.** The staged marker does not execute server-state logic. Unknown command forwarding reaches an abort-loud client scaffold rather than being silently consumed. | Real common registers and owns this dvar in the common-spine wave. |
| `info1`, `info2` | **REFUTED with evidence for M13.** They are exact-size scratch buffers used by dvar formatting, not fabricated subsystem state. | Real `dvar_cmds.cpp` owns them and is mandatory before M15. |

Challenge 1 therefore finds one immediate code defect, the null
`SL_FindString` lane. Every other survival is conditional on the explicit
owner-retirement boundaries above. `docs/NEXT_SESSION.md` now makes aggregate
symbol provenance and a scaffold denylist part of every Phase 3 runtime gate;
that prevents a later marker from silently broadening these M13-only
justifications.

## STANDING

**NEEDS-FIX** until the null lane is abort-loud, the M13 exact marker is
re-run, and the Phase 3 plan contains the owner/provenance gates above. After
those changes and hosted evidence, Challenge 1 is answered for the Phase 1
scope; M15 remains unproven until its real-owner gates pass.
