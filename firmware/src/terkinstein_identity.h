/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TK_IDENTITY_H
#define TK_IDENTITY_H

/* Shared default for the application, update loader and package builder.
 * Checked against the public hub catalog 2026-10-26; recheck before release. */
#define TK_FIRMWARE_ID "FM-1_986"
#ifndef FELUCCA_ID
#ifdef FELUCCA_LOADER
#define FELUCCA_ID "ota-" TK_FIRMWARE_ID
#else
#define FELUCCA_ID TK_FIRMWARE_ID
#endif
#endif

#endif
