# Song-wide swing

Included since 0.26.15, retained in 0.26.19. Native listening0/50/100, live changes,
legacy import and restart passed; see [validation](TESTING.md).

## Control and timing

On the **SEQ overview, Knob3 edits SWING0â€“100**. In **SEQ EDIT, Knob3 still
selects steps**. Knob1 BPM, Knob2 length, Knob4 mask, SELECT track and ALGORITHM
bank retain their roles. REC/import lock swing edits. Physical SAVE retains it
only after the existing full silence/transport/import guards complete.

One amount applies to all four sequencer tracks and all Aâ€“D banks. Zero is the
old straight timing, including its integer rounding. At100, pairs approach
75:25;50 gives approximately62.5:37.5. With base periodP, the long interval is
P+floor(P*amount/200), the short P-minus-that; the pair always lasts2P. Changes
latch on the next even global step, completing the current pair first. Live
panel/MIDI notes and ARP remain immediate/unchanged; this increment swings SEQ.
Track lengths, scene/bank boundaries and ownership stay independent.

Explicit note values and default75% pulses follow the warped grid, so quarter
and longer whole-pair values keep their overall musical length. REC converts
elapsed time back through that grid; zero uses the original capture arithmetic.
Tempo edits preserve pending phase; already-started gates retain their stored
deadlines except for the existing BPM scaling. Metronome quarter/eighth beats
fall on even steps and retain the underlying tempo. No timing work enters audio.

Leased external24PPQN clock anchors every even grid event; alternate grid
events wait the measured step's bounded swing delay. A held/pending swing pair
is canceled on Stop, including its consumed grid position. Continue keeps phase
and last clock rate, excluding the stopped gap. Fresh Start resets. Clock loss,
backlog or a pending offbeat overtaken by the next grid event stops rather than
firing a catch-up burst. Timing remains bounded by main-loop service; see the measured scope in TESTING.md.

## Save, migration and readback

Settings schema 13 appends a validated32-bit amount at byte3700:3704 bytes total.
Schema12's3700-byte prefix and every earlier field offset are unchanged.
Schema12 active settings and the exact format1/schema 12 complete journal migrate
to swing 0. Earlier active-settings upgrade paths remain; no interpretation of
old padding as a musical setting. All1,024 steps/four full captured patches
read identically on old0.26.14 and new startup in the tested old journal.

The complete stage is 19,836 bytes and the full journal payload 23,556 bytes.
Current journal format 3 stamps catalog revision 2; supported formats 1/2 migrate
explicitly. Core/stage swing must match and be0–100. Invalid records fall back
to the previous valid slot. Two-slot CRC/header-last writes remain in place.
The schema 12 legacy projection is not a full downgrade backup and cannot
represent revised sound IDs; see [catalog compatibility](CATALOG_REVISION_2.md).

## Protocol and portable song support

GET/SET **0x42**: empty query or one amount 0â€“100; reply including status is
`[OK,1,amount,100,16]`. Absolute writes mark dirty, no-op writes do not. Writes
are blocked during REC/import. GET_STATE layout remains unchanged and its
settings revision includes swing. GET_BANK_STATE changes its contract byte to2
with the same lengths/fields; older complete-song readers refuse rather than
silently exporting a song without swing.

SONG_BEGIN requires`[1]` before any Stop/stage mutation. Empty/unknown BEGIN is
UNSUPPORTED. SONG_GLOBAL is exactly19 bytes: prior18 fields plus swing. COMMIT
requires a valid explicit swing stage; missing/malformed fields cannot rescue
a poisoned import. The catalog/namespaces/wire checksum remain unchanged.

External tools must negotiate the current contract before importing and retain
explicit swing. Missing swing in a supported legacy song becomes 0. The native
firmware includes no companion implementation. Its host tests cover the full
native command code, all 16 banks and complete captured/FM patches.

Optimized/ASan checks cover zero-swing parity, bounds, grid/gates, REC, time wrap,
tempo, Stop/Continue, native SAVE/reload and legacy migration. Owner listening,
live changes, legacy import and restart passed on 0.26.15; revised saved songs also
survived restart on 0.26.18. Current memory figures and timing limits are in
[performance](PERFORMANCE.md); results remain attributed to their tested builds.
