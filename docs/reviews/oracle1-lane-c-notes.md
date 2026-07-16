# Oracle 1 (Lane C) — Sol adversarial review record

Protocol: the Sol pair (ChatGPT 5.6, ULTRA reasoning, read-only sandbox)
attacks (a) tools/oracle1/DESIGN.md before code and (b) the implementation
before CI. Challenges and rulings are recorded here; the final report
summarizes them.

## Round 1 — DESIGN.md (pre-code)

Brief: attack closure feasibility (nine-TU link plan + scaffold census),
byte-identity risk (#ifdef BMK4_ORACLE1 + #line restoration), sanitization
holes (CI trace/artifact leakage), the fixture-02 desk prediction (block-4
walk recomputation), and determinism.

**Sol STANDING: NEEDS-FIX — 19 findings.** Rulings and actions (Lane C):

1. `useFastFile` closure (CONFIRMED-DEFECT) — **ACCEPTED; was already
   fixed in the implementation** before the verdict landed (Lane C hit the
   same inline during scaffold construction): scaffold defines a backing
   dvar with `current.enabled = true`.
2. Census incomplete/unclean (CONFIRMED-DEFECT) — **FIXED.** Added
   `com_missingAssetOpenFailed`, `com_sv_running`,
   `fs_numServerReferencedFFs`, `fs_serverReferencedFFNames[32]`; removed
   the `KISAK_NULLSUB` definition (inline in qcommon.h:1557 — defining it
   again is an ODR break); DESIGN table corrected: `DB_AllocMaterial`/
   `DB_FreeMaterial` are db_registry-internal, `MyAssertHandler` rows
   added for db_stream/db_auth. Census remains CI-proven by the Debug
   `/OPT:NOREF` link.
3. Nine-TU plan linkable (OK-AS-DESIGNED) — noted.
4. `g_fileBuf` unbuffered-I/O alignment (RISK) — **FIXED**: driver passes
   a VirtualAlloc'd 0x80000 ring (sector-aligned, OS-zeroed), documented
   divergence; the engine contract (ring size, 4-byte check) is kept.
5. Refusal semantics scaffold-assisted (RISK) — **ACCEPTED**: all runtime
   claims are now labeled "real loader walk under Oracle assert/scaffold
   policy" (DESIGN §6, verdict doc); fixture-02's 12-byte overrun sits
   inside even the engine's own `size+15`, so the verdict is
   slack-independent.
6. `#line` is not whole-PE/PDB identity (CONFIRMED-DEFECT) — **ACCEPTED;
   claim narrowed** to preprocessor token-stream + logical-location
   identity (DESIGN §3.2), explicitly excluding PDB/debug-directory bytes
   and noting the pre-existing buildnumber/`__DATE__` nondeterminism.
7. Manual `#line` fragility (RISK) — **FIXED with a mechanized gate**:
   `tools/oracle1/check_line_discipline.py` (baseline-free structural
   check) runs first inside the CI oracle1 gate; desk mode additionally
   byte-compares the shipping view against the pre-edit git baseline.
8. `error detail=` leaks names (CONFIRMED-DEFECT) — **FIXED**: without
   `--emit-names` the assert/Com_Error channel carries only
   engine-source literals (file:line, unformatted fmt, error code) in
   both trace and stderr.
9. `Com_Print*` unsanitized channel (CONFIRMED-DEFECT) — **FIXED**:
   suppressed entirely unless `--emit-names`.
10. Schema escaping (CONFIRMED-DEFECT) — **FIXED**: normative
    percent-encoding for all free-text fields (bytes outside
    `[0-9A-Za-z_./:-]` → `%XX`).
11. Containment is not a content allowlist (CONFIRMED-DEFECT) —
    **FIXED**: the CI gate pins every fixture it feeds to the tool
    against the reviewed `tools/zone_fixtures/SHA256SUMS` before any run.
12. FNV hashes are pseudonyms (RISK) — **ACCEPTED**: promise re-worded to
    "no plaintext payload fields by default" (DESIGN §4).
13. Fixture-02 walk (OK-AS-DESIGNED) — Sol independently recomputed the
    block-4 cursor and confirmed the desk prediction, refining it: the
    final `inc` is an attempt, the committed cursor stays 64, and values/
    insertion/XAnimParts dispatch are never reached. Folded into DESIGN
    §6 and FIXTURE02_VERDICT.md.
14. `[0,31)` wording NIT — **FIXED**.
15. Gate C attempt-vs-commit (RISK) — **ACCEPTED/ALIGNED**: the
    `DB_IncStreamPos` hook fires at entry, so `ev=inc` is documented as
    an attempt (committed unless followed by `ev=error`); gate c keys on
    the struct alloc/fill, the name alloc, and the assert — not on a
    20-byte fill (none exists on the `Load_XStringCustom` path) nor a
    committed cursor.
16. Determinism overstated (RISK) — **ACCEPTED**: DESIGN §5 now scopes
    the zero-tail argument to sub-ring, process-fresh runs and records
    the ring-wrap caveat and the ignored `SleepEx` return (no in-process
    user APC source).
17. Hook input discipline / outcome tri-state (RISK) — **ACCEPTED**:
    hooks read only explicitly-initialized fields; the unknowable
    `new|existing|override` was replaced by the observable
    `redirected=0|1` (pool-clone redirect — itself an engine truth the
    kernel must model; Lane C had independently hit this while
    desk-walking DB_LinkXAssetEntry).
18. EAP wrapper scope (CONFIRMED-DEFECT vs the DESIGN text) — **FIXED in
    DESIGN**; the implemented step already relaxed EAP for the whole
    step. Every oracle invocation is wrapped; trailing `exit 0` kept.
19. CI closure not proven for 03–07 (CONFIRMED-DEFECT) — **FIXED**: every
    fixture outcome must be engine-native {0,4,5} (scaffold 6 / tool 2/3
    are red); malformed twins are double-run for determinism; stdout/
    stderr are documented as outside the determinism comparison.

## Round 2 — implementation (pre-CI)

*Pending round 1 closure.*

## Lane C desk evidence gathered independently of Sol

- `#line` discipline mechanically verified: a script strips every
  `#ifdef BMK4_ORACLE1` block + `#line` directive from the six edited
  engine files and byte-compares against `git show HEAD:` — all six equal,
  and every `#line N` value equals the original number of the next line
  (scratchpad `check_line_discipline.py`, run green 2026-07-16).
- FNV-1a64 `utf8_nul` convention validated against the fixture manifests:
  `synthetic/raw_inline.txt\0` = `fc7845dd3a44c753` (manifest
  rawfile[0].name), `script_zero\0script_one\0` = `dbdbd34c08d4b111`
  (fixture-02 script_strings content hash).
- check_trace.py gate b and gate c dry-run GREEN on desk-simulated traces
  derived by hand-walking the engine code, and demonstrably RED on
  mutated traces (struct block flipped to 0; asset_insert removed) —
  doctrine rule 5 (gates must be able to fail) holds at the checker level.
- Malformed-fixture refusal paths desk-traced: malformed 01 refuses via
  the `err == Z_OK` assert in DB_LoadXFileData (db_file_load.cpp:402);
  malformed 02 refuses via the Load_Stream stream-start iassert
  (db_stream_load.cpp:6) after the 4*count size wrap — both exit 4,
  engine-native, distinct from allowlist exit 3.
