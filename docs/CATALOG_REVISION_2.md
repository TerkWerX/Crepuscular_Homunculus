# Catalog revision 2 — sound and save compatibility

Introduced in 0.26.18 and retained unchanged in 0.26.19. The approved redesigns passed
listening, capture, SAVE and restart checks. See [validation](TESTING.md).

## Factory selection and existing songs

PRESETS still browses 64 choices in the same order. Positions 25 and 52 select
the redesigns, with their existing names. WICKED uses the existing four-FM cap
within eight shared voices, as heard in the accepted chord/overlap auditions.
No new effect processor or voice limit is introduced.

Stored and Program Change IDs remain separate from the displayed positions:

| Definition | Stored ID (zero-based) | Displayed position |
| --- | --- | --- |
| Original SULFUR TONGUE | 24 | 25, OLD suffix |
| Revised SULFUR TONGUE | 64 | 25 |
| Original WICKED FELT PAD | 51 | 52, OLD suffix |
| Revised WICKED FELT FM | 65 | 52 |

All original 64 definitions remain immutable. Two internal additions and a
fixed browser map preserve existing songs, LIVE settings, captures and GRAFT
targets. Old captures are not silently replaced. Selecting a new factory sound
through the existing controls deliberately captures that revision.

The generator locks the complete original 64-definition prefix and retains the
original 36-definition lock. Duplicate names are allowed only for these two
explicit revision pairs. HELLO/STATE reports 66 stored definitions. Four tracks,
four banks per track and 64 steps per bank remain unchanged.

## SAVE and interchange

Settings schema 13 remains 3,704 bytes, the song stage 19,836 bytes, and the full
journal payload 23,556 bytes. Journal format 3 stamps catalog revision 2 in a
formerly reserved metadata word. Formats 1 and 2 explicitly migrate through
the unchanged original definitions; pre-swing format 1 remains straight.
Unknown catalog stamps or invalid old references fall back to the previous
valid full-song slot.

Alternate slots, header-last writes, CRC, flash guards and silence guards are
unchanged. A revised song preserves the last valid schema 12 legacy projection
instead of writing IDs that older firmware cannot represent. Older firmware
does not understand format 3 or the new IDs. Downgrading requires the verified
older backup; automatic downgrade of revised songs is not supported.

External song tools must negotiate the 66-definition catalog and explicit swing
contract, refusing unsupported layouts before import. The firmware is fully
usable standalone; no companion implementation is part of this source tree.

## Verification

Optimized and ASan checks cover migration, interrupted saves, independent
captures, live ownership and complete tails. Actual app_main fake-USB tests
transfer all 16 banks / 1,024 steps, captures/FM and song settings, then verify
full readback after a simulated native SAVE and boot. These are host operations.

All 296 original-sound PCM probes match the sealed reference; eight redesign
probes exactly match the approved auditions. Optimized/ASan PCM is identical,
unclipped and finishes with no voices. Native PNGs show both new and OLD labels
without overlap. The target build adds 400 image bytes, with unchanged static
RAM, pool and flash-code allocation. Engine, allocation, USB and artwork source
remain unchanged. Host checks do not establish device timing or listening.

Hardware listening, capture, SAVE, mixed-FM playback,48 kHz musical recording
and normal restart acceptance passed on 0.26.18;19 retains the same musical/save code.
See [validation](TESTING.md) for exact scope and limitations.

USB descriptors already say Crepuscular Homunculus (and Crepuscular Homunculus
Update). Windows may retain a former friendly audio-device name. Correcting
that local cache does not require flashing or changing the USB identity.
