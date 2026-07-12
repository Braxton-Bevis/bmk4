# HANDOFF → Mac session: project rename to BMK4

*From the Windows coordination session, 2026-07-11, at Braxton's request. This file is
a one-shot task note — execute it, then delete it in the same commit.*

## The decision

The project is now named **BMK4**:
**B**raxton · **M**etal · **K**isak · **4** (Call of Duty 4).

Suggested README rendering — title `BMK4`, tagline directly under it:

> **B**raxton · **M**etal · **K**isak · **4** — Call of Duty 4, carried over to Apple silicon.
> A Bevis Metalworks project, forged from [KisakCOD](https://github.com/SwagSoftware/KisakCOD) by LWSS.

## Task list

1. `gh repo rename bmk4` (run inside the repo — updates the local remote too; old
   `kisakcod-ios-port` URLs auto-redirect).
2. README.md: retitle to BMK4 + tagline above; update the three CI badge URLs and any
   `git clone` commands that still say `kisakcod-ios-port` (also check ios/README.md,
   FRONTIER_REPORT.md header, and PORT_JOURNAL.md header for the old slug).
3. `gh repo edit` — refresh the description to lead with BMK4; KEEP the credit tail:
   "based on KisakCOD by LWSS, GPL-3.0, no game assets."
4. Do NOT rename PORT_JOURNAL.md / FRONTIER_REPORT.md / DEPENDENCY_MAP.md, the bundle
   ID, or any code identifiers — branding only.
5. Courtesy (optional, Braxton's call): a short note in the upstream KisakCOD Discord
   that a GPL iOS-port fork named BMK4 exists, crediting LWSS.
6. Single commit: `Branding: project is now BMK4 (Braxton · Metal · Kisak · COD4)` —
   and delete this HANDOFF.md in it.
