/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 TERKINSTEIN contributors */
#pragma once
#include <stdint.h>
/* Known 1 MiB flash parts; reject all other JEDEC IDs. */
static inline int fl_jedec_ok(uint32_t id) { return id == 0x856014u || id == 0xB36014u; }
