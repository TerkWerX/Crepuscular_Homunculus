/* SPDX-License-Identifier: GPL-3.0-only */
/* Render label-only crash/rescue fixtures; never enter actual recovery or fault. */
#define TERKINSTEIN_HOST 1
#define main tk_original_main
#include "../firmware/src/terkinstein.c"
#undef main
static void require(int value,const char *reason)
{ if(!value) { fprintf(stderr,"FAIL product label: %s\n",reason);exit(1); } }
int main(int argc,char **argv)
{
    char path[1024];
    require(argc==2,"output path");
    require(!strcmp(APP_NAME,"Crepuscular Homunculus"),"product name");
    require(text_w(&FONT_S,"CREPUSCULAR")<=224 && text_w(&FONT_S,"HOMUNCULUS")<=224,
            "both name rows fit without clipping");
    require(text_w(&FONT_S,"OPEN THE INSTALLER")<=240,"recovery instruction fits");
    tk_product_recovery_screen(1);
    snprintf(path,sizeof path,"%s/rescue-known.ppm",argv[1]);write_ppm_as(path);
    tk_product_recovery_screen(0);
    snprintf(path,sizeof path,"%s/rescue-unknown.ppm",argv[1]);write_ppm_as(path);
    tk_product_fault_screen(0x12u,0x01020304u,0xAABBCCDDu);
    snprintf(path,sizeof path,"%s/fault-label.ppm",argv[1]);write_ppm_as(path);
    puts("PASS product labels: full name fits; known/unknown flash and crash numbers rendered; drawing only");
    return 0;
}
