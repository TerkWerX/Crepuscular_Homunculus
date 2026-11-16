# 0.26.24-usb1 — M-Flasher USB discovery preview

Normal USB MIDI name: **FM-1 Crepuscular Homunculus**. Update-loader name: **FM-1 Homunculus Update**.
The normal name now contains the FM-1 spelling recognized by M-Flasher.
The update name is shorter to keep both UTF-16 USB descriptors within the
existing 64-byte control-transfer limit. USB product generation changes from
4 to 5 so hosts can distinguish the new descriptor revision.

The application name remains Crepuscular Homunculus. Package/application ID
FM-1_986 and loader ID ota-FM-1_986 remain unchanged. The existing download-mode
SysEx handler is retained. This release does not install or replace m-bootloader.

This is based on 0.26.23-ns2: both 0x856014 and 0xB36014 flash IDs remain
recognized; unknown chips remain rejected. Sound, saves, updater protocol,
flash write ranges and recovery guards are unchanged apart from version and
USB friendly-name metadata. Keep your previous firmware and a verified backup.

M-Flasher detection is checked against its source name-matching function;
both normal and loader descriptor tests passed (56 and 46 bytes), all 21 host groups and the native preview passed, and the target build passed. These offline checks do not
prove detection or flashing on a physical FM-1. No device was flashed to prepare
this release. Newer-chip physical save, power-cycle, update and recovery testing
remains pending, as in ns2. Older-chip ns2 hardware results do not qualify this
new binary. See [flash compatibility](FLASH_COMPATIBILITY_NS2.md).

After installing, select the new MIDI name again in applications if needed.
Other MIDI clients can also prevent an updater from opening a port.
Existing installations retain their old name until this firmware is installed.

M-Flasher: https://github.com/jvitkauskas/m-flasher . Its recognition rule is in
src/midi.rs. M-Flasher is separate software; this release does not modify it.
