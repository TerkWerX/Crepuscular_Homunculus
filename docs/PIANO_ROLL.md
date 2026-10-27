# Piano roll — Crepuscular Homunculus 0.26.19

The piano roll edits notes directly on the FM-1.

See [the illustrated route through ARRANGE and STEP EDIT](ILLUSTRATED_GUIDE.md#6-hold-sel-reach-the-piano-roll) for pictures of every screen on the way here.

## Open and leave the editor

Open **SEQ** and choose the track with SELECT and its A–D bank with ALGORITHM.
Hold **SEL** for 700 ms and release it to open **ARRANGEMENT**. Tap **SEL** to
open **STEP EDIT**. Hold **SEL** for 700 ms and release it to open **PIANO ROLL**.

The full path is **SEQ → hold SEL → ARRANGEMENT → tap SEL → STEP EDIT → hold SEL → PIANO ROLL**.
STEP EDIT is the arrangement note editor. It is different from SEQ EDIT, the
step-entry mode opened by pressing EDIT on the sequencer overview.

In the roll, EDIT or held SEL returns to STEP EDIT. HOME opens the selected
track's sound workbench; pressing HOME again returns to the independent LIVE sound.
Navigation does not start playback or insert notes.

## Controls

| Control | Action |
| --- | --- |
| SELECT encoder | Move the step cursor across the bank |
| ALGORITHM encoder | Move the pitch cursor |
| Physical note key | Audition the captured track and select that pitch |
| SEL button, short tap | Add the cursor note; an existing onset keeps its fields |
| REC | Delete only the note at the cursor |
| Knob 1 | Change that note's length, or the next inserted note's length |
| Knob 2 | Change that note's velocity, or the next inserted note's velocity |
| Knob 3 | Select track, preserving its captured sound |
| Knob 4 | Select bank; immediate stopped, queued at its loop boundary playing |
| PLAY | Start/stop normal sequence playback with the roll visible |
| PRESETS | Inactive in the roll; use HOME to edit the track's sound |
| SAVE | Save after playback stops and all notes/tails are silent |

Four notes fit at one step. A fifth displays STEP FULL: FOUR NOTES without
replacing a chord member. Recording and staged song transfers lock note edits.
Selecting a pitch or step does not add or sound a note; SEL inserts explicitly.
The cursor selects an onset at that step, not a tail continuing from earlier.

Length uses quarter-step units, with familiar musical values shown where exact.
Kit sounds retain their one-shot envelopes regardless of the drawn gate length.
Purple bars show held gates; green marks note onsets, cursor and playhead. The
view follows the editing cursor rather than scrolling away during playback.
The screen is not touch-sensitive. The roll adds no synthesis voices: eight
voices remain shared, with at most four FM voices.

See [full controls](PANEL_CONTROLS.md), [SAVE and compatibility](INSTALL.md) and
[validation scope](TESTING.md).
