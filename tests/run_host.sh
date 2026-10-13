#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Run TERKINSTEIN on this computer: WAV, PNG screen previews, and optional DSP checks.
set -e
cd "$(dirname "$0")/.."
exec "${PYTHON:-python3}" tools/preview.py "$@"
