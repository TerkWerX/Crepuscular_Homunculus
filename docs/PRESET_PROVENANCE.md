# Preset authorship and review

This document records the earlier36-to64 expansion. Current0.26.18 adds two separately approved revisions at stable factory positions25/52; original definitions are immutable. The factory now includes14 FM choices. Historical catalog hashes/counts below refer to the earlier bank; see CATALOG_REVISION_2.md and TESTING.md.

The 28 sounds added to expand TERKINSTEIN from 36 to 64 presets are authored
for its own PAD, METAL, KIT and six-operator FM engines. Their settings are
chosen to complement the existing bank. No stock FM-1 voice, DX7 cartridge,
Dexed sound bank, third-party preset table or recorded sample is imported for
this expansion. The inherited Dinghy/Felucca/SLOOP hardware foundation and
its credits remain separate from the authorship of these new sound settings.

## Local source record

The [authored catalog](../assets/presets/terkinstein-presets.json) records every
factory macro, FX flag, envelope, LFO and complete six-operator patch, along
with each addition's musical role and curation note. The
[generator](../tools/generate_presets.py) reads this local JSON and produces
the firmware constants. It has no bank
download or external-patch import path. Its `--check` mode verifies generated
files, preserves the fingerprint of the original 36 slots and rejects duplicate
complete sound settings.

A separate catalog comparison found **zero identical new full sound tuples**
against any earlier slot, including the other additions. The comparison uses
instrument, four macros, MEMORY, FX, attack/release, four LFO fields and the
entire FM patch; it excludes names, slot IDs and FM table indices. The nine new
FM operator patches also differ from each other and the four original FM
patches when compared independently of the surrounding macros. There are
13 complete FM voices in the resulting 64-sound bank. Local evidence is in
[catalog-check.json](data/catalog-check.json), with the exact catalog hash.
These checks establish internal settings diversity; they do not establish
globally unique sound or guarantee that every different setting is audible.

Review also checked the values against TERKINSTEIN's own engine semantics:
FM pitch depth is centered at 24, PAD key sounds follow their held gate,
FM operator sustain can allow a sound to decay while held, and kit settings
use the existing twelve recipes. No new oscillator, effect algorithm, sample
library or additional voice was added for these presets. Rendered auditions
and hardware checks belong in [TESTING.md](TESTING.md).

## Focused comparison, October 13, 2026

The [FM-1 Firmware Hub](https://fm1.designburgapps.com/) was reviewed first,
then five related projects' public editor name tables were checked at the
versions below. These tables include some legacy or hidden entries; their
counts are extracted names, not a claim about each project's current preset
menu. The comparison retains names and source hashes only, not external
patch parameters.

| Project | Source reviewed | Extracted distinct names | Exact matches to the new 28 names |
| --- | --- | ---: | ---: |
| Felucca | [v1.5.1 editor](https://raw.githubusercontent.com/hugelton/Felucca/v1.5.1/web/editor.html) | 63 | 0 |
| SLOOP | [v2.6 editor](https://raw.githubusercontent.com/isod89/sloop-fm1/v2.6/web/editor.html) | 115 | 0 |
| Jangada | [v0.9.9 editor](https://raw.githubusercontent.com/zednaked/jangada/v0.9.9/web/editor.html) | 157 | 0 |
| GHOULBOX | [v2.1 editor](https://raw.githubusercontent.com/jasonpersinger/ghoulbox-fm1-dungeon-synth/ghoulbox-v2.1/web/editor.html) | 108 | 0 |
| JIANT | [fff7369 editor](https://raw.githubusercontent.com/juanjiant-bit/JIANT/fff7369/web/editor.html) | 54 | 0 |

There are 497 names across these tables, with 291 distinct names after
case-insensitive deduplication. Each proposed TERKINSTEIN name was compared
case-insensitively against that set. The local evidence is
`build/preset-originality/source-names.json`, including the URLs, SHA-256 hashes,
extracted names and empty exact-match lists. The extraction reads the editor's
`P("NAME", ...)` strings. It does not inspect binary cartridges or user banks.

Seven grouped web searches also checked all 28 full names as quoted phrases
with a synthesizer/preset context. The returned results did not identify an
existing synthesizer preset using one of these full names. Results included
unrelated dictionaries, fiction and music pages; those were not used as sound
design references. The groups were:

| Group | Names checked |
| --- | --- |
| 1 | TAR MARROW, COIL BURROW, MOSS CAPACITOR, CINDER PISTON |
| 2 | HUSK CURRENT, ROOT TERMINAL, FANG RELAY, ION STITCH |
| 3 | VELVET FAULT, SCAR VOLTAGE, MOTH CIRCUIT, NIGHT SUTURE |
| 4 | BONE SPARKS, VELLUM PRONG, OXIDE CHIMES, WICKED FELT |
| 5 | GLASS VERTEBRA, LANTERN RATTLE, RIVET WAKE, BOILER TEETH |
| 6 | DUST CASKET, CHROME FUNERAL, VEIN WEATHER, NOCTILUCE VAT |
| 7 | LICHEN VECTOR, SUTURE FOG, HOLLOW EMBER, PHANTOM LOOM |

## Design separation

Jangada's [published sound direction](https://github.com/zednaked/jangada/tree/v0.9.9)
already spans industrial basses, machine percussion and evolving drones.
GHOULBOX's [published direction](https://github.com/jasonpersinger/ghoulbox-fm1-dungeon-synth/tree/ghoulbox-v2.1)
emphasizes dungeon choirs, organs and bowed instruments. Those broad musical
spaces are shared; dark or metallic sounds alone cannot establish novelty.
TERKINSTEIN's added bank instead fills gaps in its own collection: restrained
low-end anchors, short articulated melodies, contrasting key attacks, four
SCRAP FORGE configurations and slow textures that leave room for those parts.
The individual roles and pairing suggestions are in [PRESET_GUIDE.md](PRESET_GUIDE.md).

The [stock FM-1 provenance project](https://github.com/KingParamount/fm1-factory-presets/blob/main/docs/protocol-and-provenance.md)
reports that most recovered stock voices trace to older DX7 collections.
That makes independent settings authorship more useful here than merely
renaming a familiar imported sound. TERKINSTEIN's FM engine has its own eight
routings and 40-byte operator format; this expansion supplies its own operator
values instead of translating a DX7 voice. Familiar ratios, bell-like attacks
and bass timbres remain ordinary sound-design tools.

This is a focused overlap check and an authorship record. It cannot prove that
no musician has ever made a similar sound, or that no unpublished or unindexed
preset has the same name. A generic name alone also does not demonstrate copied
settings. No universal "unheard before" or worldwide exclusivity claim is made.
Software checks and hardware qualification are recorded separately in
[TESTING.md](TESTING.md).
