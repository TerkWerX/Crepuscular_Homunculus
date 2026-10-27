# Install, return and recover

This guide is for the M-VAVE FM-1 running Crepuscular Homunculus. Use the matching
0.26.19 package and release tag; see RELEASE_NOTES.md.
Play, sequence and save directly on the FM-1.

## Preserve music before changing firmware

Stop PLAY, release keys and pedals and let the tails finish. Press physical SAVE
and wait for SAVED/the unsaved indicator to clear. SAVE stores all sixteen banks,
captured sounds and local settings. Pressing SAVE alone does not prove completion
while notes, holds or tails remain active. Use a compatible backup tool for a
separate complete song copy when the music matters. Keep the previous firmware's
.fwsc too: the normal USB-MIDI updater cannot extract the installed image.

Different firmware may overwrite or be unable to read saved projects. This release
migrates supported earlier Homunculus saves, not arbitrary third-party projects or
stock samples. Revised captures use stored IDs 64/65 and journal format 3. An older
firmware needs its own compatible backup; no automatic downgrade is promised.

## Install an approved package

Use the .fwsc from the matching release, and verify its SHA-256 against SHA256SUMS.txt.
Connect a reliable USB data cable and close apps using the FM-1's MIDI ports.
Use the project or hub installer only after the intended release is published.
For the included Python installer, extract the source and install its dependencies:

```sh
python -m pip install mido python-rtmidi
python tools/fm1_install.py --info
python tools/fm1_install.py path/to/Crepuscular-Homunculus-0.26.19.fwsc
```

Replace path/to with the file's actual location. The first command installs Python
dependencies, the second only reads identity, and the third asks before writing.
Never use --force or an unknown package to work around an identity/recovery error.
Do not interrupt power or USB during writing. After normal boot, GLO shows the version.

## Return or recover

To return to another firmware, use that project's verified package and instructions.
The [hub's official-firmware section](https://thefirmwarehub.com/) and
[M-VAVE download center](https://www.m-vave.com/download) provide stock recovery routes.
Do not assume returning firmware restores overwritten songs automatically.

If a transfer stops while its loader is present, keep power and USB connected,
close the failed updater session and retry the same verified package. The19
loader recovered this way in the controlled transfer test; do not confuse this
with power-loss or mid-program qualification. If the app will not start, hold OCT- while powering on to enter USB
rescue, then use a compatible verified installer/package. If USB rescue cannot
enumerate, follow the [hub recovery guide](https://thefirmwarehub.com/kb/#rescue).
Inherited loader, boot and flash guards remain in this source and package.
