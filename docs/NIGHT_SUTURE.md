# Crepuscular Homunculus 0.26.23-ns1 — fuller NIGHT SUTURE

NIGHT SUTURE's factory browser entry now selects the fuller retained PAD47 used
in the approved Iron Undertow audition. Exact macros104/34/43/46, FX on,
Attack12/Release53, triangle LFO24/depth9, velocity response on, Afterimages0.
The VST 0.2.7 uses the same authored definition. No DSP algorithm was added.
Other 63 factory sounds and ALL127 stored definitions remain unchanged.
Previous FM112 remains available to saved LIVE/captured songs and displays OLD.
No existing song is silently remixed. Catalog3 and journal3 are unchanged.

Offline preview; no FM1 was connected, flashed or CPU/listening-tested for this
update. Eight shared voices/four FM voices remain. Image224016/581564,
static RAM86208/98304, pool319876/344064; ram_text909/no calls and register guards pass.
22 native ASan suites pass, including real save/reload coexistence of PAD47 and
FM112. All127 raw stored app_audio reference scenes are unchanged. Actual VST and
firmware definitions match all58 fields and 6291456 raw stereo engine frames.
Computer and FM1 output stages differ; no DAC/USB/loudness equivalence is claimed.

## Before installing
Only connect the intended FM1; close other MIDI applications. Stop PLAY/REC and
step editing; release all keys/pedals and allow tails to finish. Press physical
SAVE and verify SAVED/dirty clear. Use the matching website installer: obtain the
complete four-track/sixteen-bank/1024-step/captured-patch backup, download it,
then reselect that same file for verification. Backup cannot query every LIVE
setting; completed SAVE remains required. Update is separate and user initiated.
Do not force, interrupt power or bypass identity guards. Verify the preserved
song after reboot with Check my song. Keep older firmware and verified backups.
Old catalog2 firmware cannot read catalog3 saves; downgrade is not conversion.
0.26.22-vst1 catalog127 backups remain supported by this installer.

## Source/build/license
The source ZIP includes firmware, HAL, loader, graphics, inherited GPL credits,
portable VST reference and native tests. Follow firmware/docs/BUILD.md, using the
external AC79 SDK/pi32v2 toolchain. Target command:
python tools/build.py --release 0.26.23-ns1
Root native CMake: configure/build then ctest; CHECK_ASAN=ON enables MSVC ASan.
Build does not install. GPL-3.0-only; preserve upstream notices and licenses.
No private songs, diagnostics, recordings or FireLab binaries are included.
