/* SPDX-License-Identifier: GPL-3.0-only */
/* Cosmetic product labels; IDs, endpoints, protocols and updater markers stay unchanged. */
#ifndef TERKINSTEIN_USB_IDENTITY_H
#define TERKINSTEIN_USB_IDENTITY_H
#define TK_USB_PRODUCT_GENERATION 4u
#define TK_USB_PRODUCT_NAME "Crepuscular Homunculus"
#define TK_USB_UPDATE_NAME "Crepuscular Homunculus Update"
#ifdef FELUCCA_LOADER
static const uint8_t STR2[] = {60,3,67,0,114,0,101,0,112,0,117,0,115,0,99,0,117,0,108,0,97,0,114,0,32,0,72,0,111,0,109,0,117,0,110,0,99,0,117,0,108,0,117,0,115,0,32,0,85,0,112,0,100,0,97,0,116,0,101,0};
#else
static const uint8_t STR2[] = {46,3,67,0,114,0,101,0,112,0,117,0,115,0,99,0,117,0,108,0,97,0,114,0,32,0,72,0,111,0,109,0,117,0,110,0,99,0,117,0,108,0,117,0,115,0};
#endif
#endif
