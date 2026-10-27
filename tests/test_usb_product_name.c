/* SPDX-License-Identifier: GPL-3.0-only */
/* Pure USB product string fixture; no HAL, MIDI or device calls. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/src/terkinstein_usb_identity.h"
int main(int argc,char **argv) {
    unsigned i,n;
    if(argc!=2)return 2;
    n=(unsigned)strlen(argv[1]);
    if(sizeof STR2!=2u+2u*n || STR2[0]!=sizeof STR2 || STR2[1]!=3u || sizeof STR2>64u)return 3;
    for(i=0;i<n;i++)if(STR2[2u+2u*i]!=(uint8_t)argv[1][i] || STR2[3u+2u*i])return 4;
    if(TK_USB_PRODUCT_GENERATION!=4u)return 5;
    puts("PASS exact UTF-16LE product name, descriptor length/type, EP0 bound and revision");
    return 0;
}
