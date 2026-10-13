# Page controls — TERKINSTEIN 0.22.0-dev

The FM-1's detented Knobs 1–4 have distinct jobs on each parameter page.
SELECT chooses the sequencer track, ALGORITHM chooses its A–D bank, and
PRESETS chooses the live sound. Captured track patches stay independent of
live sound edits. Selectable acceleration is available on eligible continuous
parameters. See [testing](TESTING.md) for the current validation scope.

| Page / instrument | Knob 1 | Knob 2 | Knob 3 | Knob 4 |
| --- | --- | --- | --- | --- |
| HOME / SOUND EDIT | SHADE | FRACTURE | DRIFT | BLOOM |
| HOME / SOUND EDIT, kit | BODY | WIRE | DECAY | BITE |
| MEMORY, pad | Afterimage amount | Dark filter | Corrosion | Bloom |
| ENV, pad / FM | Attack | Release | Bloom | Panel hold |
| ENV, metal | Attack | Tail | Alloy | Panel hold |
| ENV, kit | Decay | Body | Wire | Bite |
| LFO, pitched | Rate | Depth | Shape | Base filter |
| ARP | BPM | Note order | Rate | Gate |
| SEQ | BPM, including EDIT | Length: 16 / 32 / 64 | Step cursor in EDIT only | Ghost Mask |
| METRONOME | BPM | Click level | Beats per measure: 1–12 | Beat unit: 4 or 8 |
| CONTROL FEEL | LINEAR / ACCEL | Inactive | Inactive | Inactive |
| MIDI CONTROL | Target | CC number; bend range on bend target | Input port | MIDI channel |
| OP1–OP6, FM | Ratio | Level | Attack | Decay |
| PIT | FM pitch depth | FM pitch time | Bend range | Panel octave |
| SYNTH GLOBAL | FM feedback | Bend range | FM routing | Mono / poly |
| VOICE | Mono / poly | Bend range | Panel octave | Release; metal tail; kit decay |
| FX, pad / metal / FM | Corrosion | Dark filter | Afterimage / alloy / feedback | Filter motion |
| FX, kit | Bite | Wire | Decay | Body |

HOME retains the sound animation and its four performance macros. EDIT opens
the labeled SOUND EDIT screen. SELECT and ALGORITHM have only the following
additional roles; on the other parameter pages their redundant aliases are
inactive and do not edit hidden settings.

| Page | SELECT | ALGORITHM |
| --- | --- | --- |
| HOME, pad | Afterimage amount | Inactive |
| HOME, metal / kit / FM | Inactive | Inactive |
| SOUND EDIT, pitched | GRAFT amount | Compatible GRAFT target |
| SOUND EDIT, kit | Inactive | Inactive |
| SEQ | DRUM / MELODY / ATMOS / BASS track | A–D pattern bank |
| OP1–OP6, FM | Sustain | Release |
| FX, pitched | Filter-motion rate | Filter-motion shape |
| FX, kit | Inactive | Inactive |
| MEMORY / ENV / LFO / ARP / MIDI / PIT / SYNTH GLOBAL / VOICE / METRONOME / CONTROL FEEL / diagnostics | Inactive | Inactive |

Operator sustain/release need separate controls because each operator has six
parameters. FX rate/shape and SOUND EDIT GRAFT also remain distinct from the
four primary knob controls. HOME afterimages apply only to PAD; turning SELECT
on other instruments does not bake GRAFT or change inactive memory.

| Physical button | Short tap | Hold for 700 ms |
| --- | --- | --- |
| FX | Activate/bypass corrosion or kit BITE, without navigation | Open FX controls without toggling |
| LFO | Enable/bypass filter motion, without navigation | Open LFO controls without toggling |
| ARP | Start/stop held-note arpeggiation, without navigation | Open ARP controls without starting/stopping |
| SEQ | Open/close SEQ without changing playback | Open METRONOME without changing playback |
| ENV | Open/close envelope controls | No envelope-bypass function |

A recognized long hold consumes the release; it does not also trigger a short
tap. ENV opens directly because its amplitude envelope remains part of the
instrument's note behavior. HOME returns from a controls page.

Inactive features are explicit: MEMORY requires a pad; the kit has no filter
LFO; FM pitch/routing fields require an FM preset. Those inactive turns do not
change hidden sound macros. The bend target has no port/channel fields, so
MIDI Knobs 3–4 are inactive there. GLO diagnostics are read-only, including
SELECT/ALGORITHM and Knobs 1–4; EDIT opens MIDI CONTROL and SEL opens CONTROL FEEL.

## FM-1 encoder response

Press **GLO, then SEL** to open **CONTROL FEEL**. Knob 1 chooses **LINEAR**
(default) or **ACCEL**. SEL or EDIT returns to GLO; HOME returns home. Physical
SAVE stores this preference.

FM-1 encoders have detents: slow turning still produces individual clicks.
LINEAR retains the existing increment per click. ACCEL retains that precision
for a first click, a direction change, a pause longer than 180 ms or a control
context change. Repeated same-direction clicks use 2× steps at intervals of
60 ms or less and 4× at 25 ms or less, for eligible continuous sound fields on
Knobs 1–4, including Ghost Mask and metronome level. This changes traversal
speed, not the sound parameter's value curve.
Numeric limits still clamp safely.

Tempo, FM ratios, shapes, modes, octave, bend range, MIDI channels/bindings,
track lengths, steps, tracks, banks, presets and meter remain unaccelerated.
SELECT, ALGORITHM and PRESETS keep their existing steps. The FM-1 setting does
not alter incoming MIDI controller values.

## Sequencer and metronome

On SEQ, **SELECT chooses the track**, **ALGORITHM chooses its A–D bank**,
**Knob 1 always sets BPM**, **Knob 2 sets length**, **Knob 3 moves the cursor
after EDIT enters step entry**, and **Knob 4 sets Ghost Mask**. Knob 3 is
explicitly inactive outside step entry. Track navigation does not start playback
or silently select a live bass preset. Choose the desired live sound before
legacy REC captures it; navigation retains each track's captured sound.
Track, bank, length and cursor changes are locked during recording; BPM and
Mask remain adjustable. All SEQ encoders are locked during a staged song import.
Shrinking a pattern retains hidden steps.

ALGORITHM queues a bank change at that track's next loop boundary while SEQ
plays. Repeated turns advance the queued choice; returning to the active bank
cancels it. A stopped sequencer changes bank immediately. Other tracks and live
notes retain their ownership. The row shows active/queued banks, such as `A>B`.
The separate **P1/1** indicator means visible 16-step page / total pages:
`P1/4` is steps 1–16 of a 64-step bank, and `P4/4` is steps 49–64. It is **not
a time signature**; sequence steps remain sixteenth notes.

Hold SEQ to open METRONOME. Knobs 1–4 set BPM, level, beats per measure and
quarter/eighth-note beat unit. SEL enables/bypasses the click; HOME returns.
The default is **OFF, level 32, 4/4**. It follows sequencer PLAY/REC and accents
the first beat. Changing its meter changes click grouping, not pattern lengths
or the sequencer's sixteenth-note step timing. The click uses its own bounded
sound generator rather than another one of the six instrument voices.

The new parameter mappings stop at numeric limits without baking GRAFT or creating a
save change. Shape, ARP choices, track and cursor navigation wrap in either
direction. Panel HOLD affects the physical keyboard's sustain and sends its
existing CC64 message; it is transient and is not stored by SAVE. External
pedals retain their independent owners. Held panel notes retain their pitches
when octave changes, while new notes use the new octave.
If panel HOLD is already active when the kit is chosen, ENV shows
**EDIT: RELEASE PANEL HOLD**; EDIT releases that existing hold once.

The control remap adds no instrument voices or captured-patch fields. The
metronome adds a small click generator included in GLO's render timing and four
saved fields in **schema 11, 3,700 bytes**. Existing schema-10 settings migrate
with the click off at level 32 and 4/4. After a 0.19 SAVE, downgrading to 0.18
or earlier starts with factory settings. Physical SAVE retains four active
patterns. Save separate copies of important patterns before changing firmware.
CONTROL FEEL reuses a previously reserved byte without changing schema 11's
size or song protocol. Host tests and previews do not qualify physical encoder/hold response, device
audio timing or USB latency. See [testing](TESTING.md).
