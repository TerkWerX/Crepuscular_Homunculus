# Install, return and recover

TERKINSTEIN runs on the **M-VAVE FM-1**. The release `.fwsc` is the firmware package. This guide is for the hardware firmware only.

## Before you change firmware

Stop playback, release keys and pedals, press the FM-1's physical **SAVE**, and wait for any unsaved indicator to clear. A physical SAVE stores on-device settings and active patterns. Save important songs and presets separately with a compatible backup tool; other firmware may not understand TERKINSTEIN's saved-data format. Keep a copy of the `.fwsc` for the firmware you currently use if you may return to it. **The normal USB-MIDI updater cannot extract the installed firmware image**, so a firmware package must be obtained from its author or an existing download.

## Install TERKINSTEIN

Download the `.fwsc` attached to the matching [GitHub Release](https://github.com/TerkWerX/Crepuscular_Homunculus/releases). On a computer, connect the FM-1 by a USB data cable, close other apps that have its MIDI ports open, then use the browser installer on [TERKINSTEIN.COM](https://terkinstein.com/) when that page is published. It requires a browser with Web MIDI and SysEx access, typically desktop Chrome or Edge, on HTTPS. The page checks the package and FM-1 identity before asking you to start.

You can also use the included Python installer:

```powershell
py -m pip install mido python-rtmidi
py tools/fm1_install.py --info
py tools/fm1_install.py .\TERKINSTEIN-0.22.0-dev.fwsc
```

The installer asks before writing. Do not unplug the FM-1 during an update. After restart, dismiss the startup splash with a key or button; HOME or GLO displays the running version.

## Returning or recovering

To return to **M-VAVE's official firmware**, download the current FM-1 package from [M-VAVE's download center](https://www.m-vave.com/download) under **PC Firmware → FM-1**, then follow its installer instructions. TERKINSTEIN.COM's browser tool accepts the verified official V15 `FM-1.fwsc` file when available. Do not use an unverified file renamed to look official.

To return to another custom firmware, obtain that project's `.fwsc` and follow its own update instructions. The TERKINSTEIN browser tool can attempt a validated FM-1 package, but a different update loader may require its project's installer. Saved songs and sounds may be incompatible after switching.

If an update is interrupted while TERKINSTEIN's loader is still present, reconnect USB and retry the **same package**. If the running app cannot start, hold **OCT−** while powering on the FM-1 to enter its USB rescue screen, then reinstall a compatible package. If the device cannot enumerate over USB, follow hardware recovery guidance from the relevant firmware project.
