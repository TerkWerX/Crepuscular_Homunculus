# The 64-sound TERKINSTEIN bank

Factory positions25/52 now select revised SULFUR TONGUE METAL and WICKED FELT FM. Original definitions remain in older captures as OLD. There are64 browser choices and66 stored definitions; see CATALOG_REVISION_2.md. The following catalog expansion history retains its original context.

The local **0.20.0-dev** bank extends the existing machinery, ritual and decay vocabulary
with parts that can form a complete four-track piece. The original 36 slots
keep their order and factory settings. Slots 37–64 add six basses, six leads,
six keys/plucks, four kit configurations and six atmospheres. Track roles
below are suggestions: any track can capture any instrument.
The complete bank contains 24 PAD, 22 METAL, 5 KIT and 13 FM sounds. The
source of their settings is the [curated catalog](../assets/presets/terkinstein-presets.json).

## What the existing bank already supplies

| Slots | Existing character | What the expansion adds beside it |
| --- | --- | --- |
| 01–11 | ASH CHOIR through CATHODE MOTH: dark pads, long gestures and moving textures | More defined bass, melody and key attacks underneath or above them |
| 12–21 | IRON PSALM through QUAKE ENGINE: metallic strikes, bowed entrances and rough pulses | Quieter melodic contrast and low-end anchors |
| 22–30 | HOLLOW ROOT through MIRROR INK: bass, keys, bells and additional pads | Several distinct choices for each of those jobs |
| 31 | SCRAP ALTAR: the neutral SCRAP FORGE kit | Four contrasting BODY/WIRE/DECAY/BITE configurations |
| 32 | RIVEN SIGNAL: moving metallic lead | Six leads with different attack and sustain priorities |
| 33–36 | NERVE CHOIR, CRUCIBLE, BONE WIRE and SPECTRAL GRAFT: the first four FM voices | Additional independent FM settings for bass, melody, keys and atmosphere |

## Added basses

These are intended for low-register lines that leave the upper register to
the existing pads and metals. Begin around C2–C3 and use one note at a time
when building a dense sequence.

| Slot | Name | Engine | Musical role |
| ---: | --- | --- | --- |
| 37 | TAR MARROW | METAL | Immediate corroded low note with zero DRIFT and a short released tail |
| 38 | COIL BURROW | FM | Brief branching growl that settles into a held fundamental |
| 39 | MOSS CAPACITOR | PAD | Rounded, nearly undetuned bass with corrosion bypassed and a quick release |
| 40 | CINDER PISTON | METAL | Harder corroded bass strike with a controlled tail |
| 41 | HUSK CURRENT | FM | Three muted pairs decay while held, for syncopated bass plucks |
| 42 | ROOT TERMINAL | PAD | Soft, nearly clean bass support for harsher drums and metal parts |

HUSK CURRENT intentionally decays even if you hold the key. Use COIL BURROW,
MOSS CAPACITOR or ROOT TERMINAL when the bass must sustain through a long gate.

## Added leads

The new leads favor a recognizable melodic line instead of a large chord
wash. Try them against ASH CHOIR, VEIL ENGINE, PALLID CHOIR or MIRROR INK.

| Slot | Name | Engine | Musical role |
| ---: | --- | --- | --- |
| 43 | FANG RELAY | METAL | Sharp entrance for angular melodic phrases |
| 44 | ION STITCH | FM | Forked wiry line with a brief downward pitch bite and sustained body |
| 45 | VELVET FAULT | PAD | Softer lead with restrained detune and corrosion bypassed |
| 46 | SCAR VOLTAGE | METAL | Abrasive foreground line for sparse arrangements |
| 47 | MOTH CIRCUIT | FM | Gently arriving hollow line with quiet upper modulation |
| 48 | NIGHT SUTURE | PAD | Soft lead with gentle filter motion above a dry rhythm |

## Added keys and plucks

These fill the gap between OBSIDIAN KEYS and TERKINSTEIN's ringing metal
strikes. PAD and METAL keys follow the held gate: VELLUM PRONG's short release
begins when the key is released, rather than creating an automatic pluck.
The original WICKED FELT PAD behaved similarly; the revised factory choice is a dry FM key with its own operator envelopes. The FM keys have independent operator decay;
BONE SPARKS and OXIDE CHIMES fade while held, while GLASS VERTEBRA leaves a
small sustained remainder. They are synthesized textures rather than recordings
of the instruments suggested by their names.

| Slot | Name | Engine | Musical role |
| ---: | --- | --- | --- |
| 49 | BONE SPARKS | FM | Three short pairs create bright ceramic-like strikes for arpeggios |
| 50 | VELLUM PRONG | PAD | Dry papery key, following note length with a short released tail |
| 51 | OXIDE CHIMES | FM | Branching chime whose upper modulation fades before its carrier |
| 52 | WICKED FELT | FM | Revised dry, muted FM key with distinct attack and decay; four-FM voice limit |
| 53 | GLASS VERTEBRA | FM | Twin chains with long glassy decay and only a small held remainder |
| 54 | LANTERN RATTLE | METAL | Moderately corroded metallic key with a ringing released tail |

## Added SCRAP FORGE kits

All five kits use the same twelve synthesized key roles, so a drum pattern
can be auditioned with contrasting factory macro settings. These are four
new configurations of TERKINSTEIN's existing recipes, not imported samples
or emulations of a named drum machine.

| Slot | Name | Curation role |
| ---: | --- | --- |
| 31 | SCRAP ALTAR | The original centered reference |
| 55 | RIVET WAKE | Tight industrial rhythm with room for longer atmosphere notes |
| 56 | BOILER TEETH | Weight and grit for harder bass-led arrangements |
| 57 | DUST CASKET | Shorter, softer contrast to aggressive melodic parts |
| 58 | CHROME FUNERAL | Ringing metallic accents for slower, more open pieces |

Their factory macro values make the contrast explicit. Higher BODY raises
the recipes' pitch; higher DECAY lengthens their tails. WIRE changes tonal/noise
balance. BITE below 64 uses the existing SAND shaping and above 64 the BURN
corrosion; 64 is the neutral reference, not a separate processor.

| Kit | BODY | WIRE | DECAY | BITE |
| --- | ---: | ---: | ---: | ---: |
| SCRAP ALTAR | 64 | 64 | 64 | 64 |
| RIVET WAKE | 78 | 40 | 26 | 28 |
| BOILER TEETH | 94 | 101 | 69 | 103 |
| DUST CASKET | 56 | 18 | 32 | 15 |
| CHROME FUNERAL | 60 | 114 | 92 | 52 |

The mapping repeats in every octave:

| Key | Drum | Key | Drum |
| --- | --- | --- | --- |
| C | Kick | F♯ | Alloy hat |
| C♯ | Sub boom | G | Low tom |
| D | Snare | G♯ | High tom |
| D♯ | Clap | A | Rim |
| E | Closed hat | A♯ | Struck bell |
| F | Open hat | B | Broken-clock click |

For rapid double-bass parts, start with the C kick in **RIVET WAKE** or
**DUST CASKET**. Their factory DECAY settings (26 and 32) are shorter than
the neutral kit's 64. Turn DECAY down further if repeated kicks blur together;
use the longer C♯ sub boom for occasional low accents. All parts share six
voices, so listen for missing hits when a dense melody and atmosphere are
playing at the same time.

Choose the desired kit before recording DRUM. DRUM REC keeps the live kit
and its sculpted settings when a kit is already selected. The four sound
knobs are BODY, WIRE, DECAY and BITE. Several different drums can share a
step within the existing four-note step limit.

For louder or more expressive hits, the kit responds to MIDI note velocity.
The kit macro knobs shape its sound; they are not a separate output-level
mixer.

## Added atmospheres

The bank already has long, full pads. These additions are intended to leave
holes for the newly added bass and melody parts: different entrances,
degrees of motion and pitched versus metallic texture.

| Slot | Name | Engine | Musical role |
| ---: | --- | --- | --- |
| 59 | VEIN WEATHER | PAD | Shaded slow entrance with moderate corrosion and gentle filter motion |
| 60 | NOCTILUCE VAT | FM | Softly arriving parallel harmonics for a dusky organ-like bed |
| 61 | LICHEN VECTOR | PAD | Dim, detuned filtered texture behind clean bass and short percussion |
| 62 | SUTURE FOG | METAL | Bowed metallic tension with less corrosion than BLADE REQUIEM |
| 63 | HOLLOW EMBER | PAD | Broad warm corrosion and mild drift, without periodic LFO motion |
| 64 | PHANTOM LOOM | FM | Slowly opening chain with later upper modulation and ramped filter motion |

Hold NOCTILUCE VAT and PHANTOM LOOM for several seconds to hear their build.
Their operator attacks are deliberately different; PHANTOM LOOM's carrier
reaches its full attack at roughly seven seconds under the factory settings,
with some modulation continuing to open later. That estimate follows the
existing fixed-point envelope code; it is not a device performance measurement.

## Four-track starting combinations

These combine new and existing sounds rather than placing every new sound
in competition with another. They are starting points, not supplied patterns.

| Piece direction | DRUM | MELODY | ATMOS | BASS |
| --- | --- | --- | --- | --- |
| Mechanical lullaby | 55 RIVET WAKE | 49 BONE SPARKS | 04 VEIL ENGINE | 37 TAR MARROW |
| Furnace signal | 56 BOILER TEETH | 43 FANG RELAY | 62 SUTURE FOG | 38 COIL BURROW |
| Worn miniature | 57 DUST CASKET | 52 WICKED FELT | 11 CATHODE MOTH | 39 MOSS CAPACITOR |
| Open metal chamber | 58 CHROME FUNERAL | 51 OXIDE CHIMES | 61 LICHEN VECTOR | 22 HOLLOW ROOT |
| Haunted wiring | 31 SCRAP ALTAR | 24 OBSIDIAN KEYS | 64 PHANTOM LOOM | 41 HUSK CURRENT |

Start with a simple drum pattern, a single bass line, a sparse melody and one
or two held atmosphere notes. Extend ATMOS to 32 or 64 steps when the buildup
needs more time. REC captures held-note duration; existing loops retain their
captured sound while a new live preset is selected. Use SEQ -> select track -> HOME/TRACK SOUND to change that capture independently; HOME again returns to LIVE.

The eight shared voices and four-FM-voice limit apply.
Storing 64 presets does not add voices or processing stages. The selected
engine, active notes, overlapping releases and operator routing determine
the actual render work. Dense combinations need an attended listening and
GLO timing check; host previews do not establish FM-1 CPU timing.

The factory bank is stored as constants. The save/song layout does not grow
to store those 28 descriptors, but older firmware does not recognize their
preset indices. Keep a full current song backup before downgrading.

For the focused comparison against other published preset names and the
independent authorship record, see [PRESET_PROVENANCE.md](PRESET_PROVENANCE.md).
