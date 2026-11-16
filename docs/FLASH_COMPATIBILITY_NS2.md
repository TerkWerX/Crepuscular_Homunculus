# 0.26.23-ns2 — flash chip compatibility preview

Main firmware, USB rescue and the updater recognize the older Puya flash ID 0x856014 and the newer UC25HQ80 ID 0xB36014. Unknown chips remain rejected. The save format, flash ranges and sound engine are unchanged from 0.26.23-ns1.

The target build and host checks passed. On one older-chip FM-1, updating from ns1 to ns2 completed and all 1,024 song steps, captured patches, FM slots, macros and queryable state matched before and after; idle diagnostics reported zero late blocks. This is not full playing, CPU or USB qualification.

**Newer-chip hardware testing is pending.** Save, full power-cycle persistence, firmware update and recovery have not been verified on an FM-1 with 0xB36014. Keep a verified backup of your music and previous firmware. Stop playback, release notes and pedals, press SAVE and wait for completion before installing.

Firmware SHA-256: `32d13d52250720a86fb765314c3ff3eb2d7a80f2b5e4fe8aaf28383afb12fd93`. Product identity: FM-1_986. GPL-3.0-only; see LICENSE and LICENSING.md.
