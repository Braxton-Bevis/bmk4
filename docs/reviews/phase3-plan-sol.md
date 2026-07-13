# Adversarial review — Phase 3 headless `Com_Init` plan (Sol)

## CHALLENGES

1. **Claim under attack:** the existing staged boot can simply advance into
   `Com_Init`. **Failure scenario:** `kisak_boot_smoke` initializes the hunk,
   then `Com_Init` initializes it again and hits `iassert(!s_hunkData)`.
   **Cheapest test:** compare `BootSmoke.cpp:42`, `common.cpp:1348`, and
   `com_memory.cpp:385`.
2. **Claim under attack:** `FS_InitFilesystem` can succeed with no assets.
   **Failure scenario:** its unconditional `fileSysCheck.cfg` validation calls
   fatal `Com_Error`. **Cheapest test:** inspect `com_files.cpp:2198-2223` and
   confirm the fixture is absent from `git ls-files`.
3. **Claim under attack:** no-fastfile/headless behavior follows from an empty
   sandbox. **Failure scenario:** `useFastFile` defaults to 1 and `dedicated`
   to 0, entering database, client, renderer, and sound tails. **Cheapest
   test:** inspect `common.cpp:1547-1563` and probe both registered values after
   startup-variable processing.
4. **Claim under attack:** auditing only newly added TUs is sufficient.
   **Failure scenario:** already-linked `dvar.cpp` truncates enum string-list
   pointers through its 32-bit integer lane; `Com_InitDvars` immediately
   registers two enums. **Cheapest test:** register/read/dump an enum under the
   arm64 simulator and inspect `dvar.cpp` enum domain stores/readers.
5. **Claim under attack:** the already-census-green filesystem is LP64-ready
   at runtime. **Failure scenario:** `FS_AddGameDirectory` allocates the
   32-bit 28-byte `searchpath_s`, but arm64 needs 40 bytes, corrupting the heap
   even with no assets. **Cheapest test:** static-assert the arm64 size and
   inspect `com_files.cpp:1656,1786`.
6. **Claim under attack:** undefined linker symbols reveal the true closure.
   **Failure scenario:** every `ios/Stub/*.cpp` participates in the link and
   existing benign scaffolds suppress undefineds. **Cheapest test:** intersect
   `nm` undefineds from candidate real objects with definitions from every app
   scaffold object.
7. **Claim under attack:** adding a census TU makes it live in the app.
   **Failure scenario:** `ios/project.yml` links only `libkisakpmove` and
   `libkisaksmoke`; `libkisakcod.a` is never linked. **Cheapest test:** inspect
   both Xcode link lanes and archive-member construction.
8. **Claim under attack:** “event loop ran at least one frame” is an objective
   post-condition. **Failure scenario:** CADisplayLink advances only pmove and
   Metal, while real `Com_Frame` has a much larger closure. **Cheapest test:**
   source-grep the display callback and all `Com_Frame` call sites.
9. **Claim under attack:** the variable `<N>` and unnamed dvar floor are
   enforceable. **Failure scenario:** a regex or hardcoded low count can pass
   without the intended subsystems. **Cheapest test:** require one immutable
   full marker and enumerate every behavior contributing to its count.

## VERDICTS

1. **CONFIRMED-DEFECT.** The double-hunk call order is statically decisive.
   Interim waves may run after M13, but the final M15 path must be a fresh
   cold-start orchestrator that does not call `kisak_boot_smoke` first.
2. **CONFIRMED-DEFECT.** The asset-free filesystem target is impossible
   without a narrow iOS headless policy. Fabricating `fileSysCheck.cfg` would
   hide the actual product boundary and is rejected.
3. **CONFIRMED-DEFECT.** Headless/no-fastfile selection must be explicit and
   behaviorally verified; absence of files is not configuration.
4. **CONFIRMED-DEFECT.** Audit scope changes from “new TU” to “newly reached
   path.” A dvar enum/external-string preflight is mandatory before FS startup.
5. **CONFIRMED-DEFECT.** Both 28-byte search-node allocations and the remaining
   pointer-sized filesystem sites require iOS LP64 fixes with byte-identical
   Windows `#else` branches.
6. **CONFIRMED-DEFECT.** Linker output is authoritative only after aggregate
   scaffold-owner accounting. Each runtime wave needs a real-symbol allowlist
   and forbidden-scaffold reach/definition check.
7. **CONFIRMED-DEFECT.** Phase 3 needs a hard-required exact archive (named
   `libkisakcominit.a` in the corrected plan), linked in simulator and device
   lanes with required-member assertions.
8. **CONFIRMED-DEFECT.** The gate is frozen as one real `Com_EventLoop` pass,
   proved by a queued console event changing a dvar. A Swift frame or screenshot
   cannot satisfy it; full `Com_Frame` is not silently pulled into M15.
9. **CONFIRMED-DEFECT.** The final contract is frozen as
   `cominit=Com_Init OK — 4 subsystems up, no assets`, dvar count greater than
   24, behavioral `set`/`dvarlist` ownership, FS write/read/delete, and the real
   event-loop probe.

The supplied expected wave order remains a useful hypothesis, but these are
blocking method defects. `docs/NEXT_SESSION.md` is corrected before Wave 1 as
the work order requires.

## STANDING

**NEEDS-FIX.** Proceed only after the corrected cold-start/headless/provenance
contract is in `docs/NEXT_SESSION.md`. The review does not block the bounded
filesystem wave once those corrections are committed; it blocks any claim
that the old staged entry itself ran `Com_Init` or that a marker backed by a
benign scaffold is M15.
