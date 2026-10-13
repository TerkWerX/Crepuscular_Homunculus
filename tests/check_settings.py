#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile the actual platform settings functions against a guarded fake sector.

The platform otherwise needs target hardware. Extract only its unchanged CRC,
load and save functions, so this smoke test exercises the production code.
Run from a compiler environment: py tests/check_settings.py --cc cl
"""
from pathlib import Path
import argparse
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from preview import build, compiler_command

PREFIX = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SETTINGS_AT 0xFC000u
#define SETTINGS_MAGIC 0x4E494B54u
static uint8_t sector[4096], input[3585], output[3585];
static uint32_t flash_ok = 1, writes, erases, fail_payload, fail_header, checks;
static void expect(int ok, const char *why) {
    checks++; if (!ok) { fprintf(stderr, "FAIL: %s\n", why); exit(1); }
}
static int st_read(uint32_t off, void *p, uint32_t n) {
    expect(off >= SETTINGS_AT && off - SETTINGS_AT + n <= sizeof sector, "read inside guarded sector");
    memcpy(p, sector + off - SETTINGS_AT, n); return 0;
}
static int fl_erase4k(uint32_t off, uint32_t *took) {
    expect(off == SETTINGS_AT, "erase only the settings sector");
    memset(sector, 255, sizeof sector); erases++; writes = 0; *took = 0; return 0;
}
static int fl_write(uint32_t off, const void *p, uint32_t n) {
    expect(off >= SETTINGS_AT && off - SETTINGS_AT + n <= sizeof sector, "write inside guarded sector");
    if (off == SETTINGS_AT + 12u) {
        expect(writes == 0, "payload writes first"); if (fail_payload) return -1;
    } else {
        expect(off == SETTINGS_AT && n == 12u && writes == 1, "header commits last");
        if (fail_header) return -1;
    }
    memcpy(sector + off - SETTINGS_AT, p, n); writes++; return 0;
}
'''

SUFFIX = r'''
int main(void) {
    uint32_t i, before;
    for (i = 0; i < sizeof input; i++) input[i] = (uint8_t)(i * 37u + 11u);
    expect(!settings_save(input, 3584), "maximum payload saves");
    expect(!settings_load(output, 3584) && !memcmp(input, output, 3584), "maximum payload round trips");
    before = erases;
    expect(settings_save(input, 3585) != 0 && erases == before, "oversize save rejected before erase");
    memset(output, 0x5A, sizeof output);
    expect(settings_load(output, 3585) != 0 && output[0] == 0x5A, "oversize load does not copy");
    expect(settings_load(output, 688) != 0 && output[0] == 0x5A, "load requires exact saved length");
    sector[100] ^= 1u;
    expect(settings_load(output, 3584) != 0 && output[0] == 0x5A, "CRC failure leaves destination untouched");
    expect(!settings_save(input, 688) && !settings_load(output, 688), "old migration payload remains readable");
    fail_payload = 1;
    expect(settings_save(input, 3412) != 0 && settings_load(output, 3412) != 0, "interrupted payload has no valid header");
    fail_payload = 0; fail_header = 1;
    expect(settings_save(input, 3412) != 0 && settings_load(output, 3412) != 0, "interrupted header stays invalid");
    fail_header = 0;
    expect(!settings_save(input, 3412) && !settings_load(output, 3412), "schema-7 size round trips");
    flash_ok = 0; before = erases;
    expect(settings_save(input, 3412) != 0 && erases == before, "unavailable flash is not erased");
    expect(settings_load(output, 3412) != 0, "unavailable flash is not read");
    printf("settings: %u checks passed; CRC, bounds, exact length and header-last commit\n", checks);
    return 0;
}
'''


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc")
    args = parser.parse_args()
    platform = (ROOT / "firmware/src/platform.c").read_text()
    start = platform.index("static uint32_t crc32(")
    end = platform.index("/* ---- MIDI over USB", start)
    source = ROOT / "build/host/settings-smoke.c"
    source.parent.mkdir(parents=True, exist_ok=True)
    source.write_text(PREFIX + platform[start:end] + SUFFIX)
    executable = source.with_suffix(".exe" if sys.platform == "win32" else "")
    build(compiler_command(args.cc), source, executable, host=False, sanitize=False)
    subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
