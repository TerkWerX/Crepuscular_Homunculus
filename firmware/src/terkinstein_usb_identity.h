/* SPDX-License-Identifier: GPL-3.0-only */
/* Friendly USB labels only; updater IDs, endpoints and save format are unchanged. */
#ifndef TERKINSTEIN_USB_IDENTITY_H
#define TERKINSTEIN_USB_IDENTITY_H
#define TK_USB_PRODUCT_GENERATION 5u
#define TK_USB_PRODUCT_NAME "FM-1 Crepuscular Homunculus"
#define TK_USB_UPDATE_NAME "FM-1 Homunculus Update"
#ifdef FELUCCA_LOADER
static const uint8_t STR2[] = {46,3,70,0,77,0,45,0,49,0,32,0,72,0,111,0,109,0,117,0,110,0,99,0,117,0,108,0,117,0,115,0,32,0,85,0,112,0,100,0,97,0,116,0,101,0};
#else
static const uint8_t STR2[] = {56,3,70,0,77,0,45,0,49,0,32,0,67,0,114,0,101,0,112,0,117,0,115,0,99,0,117,0,108,0,97,0,114,0,32,0,72,0,111,0,109,0,117,0,110,0,99,0,117,0,108,0,117,0,115,0};
#endif
#endif
