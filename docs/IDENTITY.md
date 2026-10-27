# Updater identity — 0.26.19

The package and normal application report **FM-1_986**. The update loader reports
**ota-FM-1_986**. USB display names remain Crepuscular Homunculus and Crepuscular
Homunculus Update. The creator logo, audio engine, presets, controls, song format,
USB VID/PID, endpoint layout and app MIDI protocol remain unchanged.

The shared default is `TK_FIRMWARE_ID` in `firmware/src/terkinstein_identity.h`.
The builder reads it for package labeling, passes it to the app and passes the
`ota-` form to the loader. An explicit `--product FM-1_9NN` override applies to
all three. Direct compilation uses the same header defaults. Recovery continues
to advertise FM-1_000; the inherited SPL update-record metadata and flash guards
are preserved. Do not replace those boot fields with the normal app identity.

Why a new build: historical18 used FM-1_960, shared by SID-1 and Makohai. Its loader
also retained the inherited ota-FM-1_900 constant. This revision corrects both
running identities and the package together. No historical binary is patched or
renamed. The new ID retains the FM-1_9NN form supported by the inherited resume path.

Availability was checked against https://thefirmwarehub.com/catalog.json on
October 26,2026, including both `product` and `ident` fields and IDs beginning with
FM-1_986 (version extensions). No matching entry was present. This does not reserve
the ID or establish absence from private/unlisted projects. Recheck before release.
The [hub identity guide](https://thefirmwarehub.com/kb/#identity) describes this scope.

Offline acceptance includes real updater handshake code over memory-only SysEx,
normal/recovery/loader replies, descriptor names, package CRC/loader validation,
target binary comparison with18, native save/recovery and silent audio regression.
The preceding 0.26.19-dev target app bytes differ from 0.26.18 only in identity/version strings; loader bytes
differ only in its identity string. These are offline results; the following paragraph records separate hardware acceptance.

Exact 0.26.19-dev hardware checks are complete: custom 0.26.18 and Sloop entry, official V15
entry, outgoing V15 update, normal/loader identity readback and a controlled
host-transfer interruption followed by ordinary same 0.26.19 completion. Full song
and queryable LIVE comparisons matched after completed SAVE. Power/USB stayed
connected during the interruption. See [validation](TESTING.md). This does not
qualify a power cut or every unqueryable setting across unrelated firmware.

The final 0.26.19 rebuild changes only APP_VER from 0.26.19-dev to 0.26.19.
Identity, USB descriptors, updater and recovery sources are unchanged. Its
new hashes and offline results are recorded in TESTING.md; it was not installed
during release preparation. Historical development-build hashes are retained.
