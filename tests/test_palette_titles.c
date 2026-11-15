/* SPDX-License-Identifier: GPL-3.0-only */
#define TERKINSTEIN_HOST 1
#define main original_main
#include "../firmware/src/terkinstein.c"
#undef main
int main(int argc,char **argv){static tk_settings_t s;char title[24];unsigned legacy;if(argc!=3)return 2;legacy=(unsigned)atoi(argv[2]);settings_defaults(&s);if(legacy)preset_apply(&s,0);preset_title(title,&s);if(strcmp(title,legacy?"01 ASH CHOIR OLD":"01 ASH CHOIR"))return 3;lcd_fill(0,0,240,240,C_BLACK);cv_oy=0;draw_header(TK_PAGE_PAD);draw_home(&s,0,0,0);write_ppm_as(argv[1]);return 0;}
