/* SPDX-License-Identifier: GPL-3.0-only */
/* Main-only labeled-key editing. Holding physical EDIT turns only newly
 * pressed label keys into shortcuts; existing held notes keep their owners. */
#ifndef TERKINSTEIN_PANEL_H
#define TERKINSTEIN_PANEL_H
static const uint8_t TK_LABEL_KEYS[10] = {1u,3u,5u,8u,10u,13u,15u,17u,20u,22u};
static uint8_t tk_operator_focus;
static int tk_label_action(tk_settings_t *s, uint32_t key, uint32_t *page)
{
    uint32_t i;
    for (i=0; i<10u; i++) if (TK_LABEL_KEYS[i] == key) {
        if (i<6u) { tk_operator_focus=(uint8_t)i; *page=TK_PAGE_OPERATOR; }
        else if (i==6u) *page=TK_PAGE_PITCH;
        else if (i==7u) *page=TK_PAGE_GLOBAL;
        else { s->mono=(uint8_t)(i==8u); tk_set_mono(s->mono); *page=TK_PAGE_VOICE; }
        return i>=8u ? 3 : 1; /* navigation bit 0, settings bit 1 */
    }
    return 0;
}
static void tk_default_bend(tk_settings_t *s, uint8_t value)
{
    uint32_t source;
    s->midi_bend_range=value;
    for (source=0; source<TK_SOURCE_SEQUENCE; source++) tk_set_bend_range(source,value);
}
static int tk_panel_value(tk_settings_t *s, uint8_t *value, int32_t delta, uint32_t max)
{
    uint8_t next=adjust_u8(*value,delta,0,max);
    if (next==*value) return 1;
    graft_bake(s); *value=next;
    return 3;
}
/* Mode, controller sensitivity and panel transposition are performance
 * settings, not timbre changes: they leave the current GRAFT intact. */
static int tk_panel_mode(tk_settings_t *s, int32_t delta)
{
    uint8_t next=adjust_u8(s->mono,delta,0,1);
    if (next==s->mono) return 1;
    s->mono=next; tk_set_mono(next); return 3;
}
static int tk_panel_bend(tk_settings_t *s, int32_t delta)
{
    uint8_t next=adjust_u8(s->midi_bend_range,delta,0,24);
    if (next==s->midi_bend_range) return 1;
    tk_default_bend(s,next); return 3;
}
static int tk_panel_octave(tk_settings_t *s, int32_t delta)
{
    uint8_t current=(uint8_t)(s->octave+2), next=adjust_u8(current,delta,0,4);
    if (next==current) return 1;
    /* Held panel keys retain their original panel_note ownership. New panel
     * presses use this same -2..+2 setting as the physical OCT buttons. */
    s->octave=(int8_t)((int32_t)next-2); return 3;
}
static int tk_panel_encoder(tk_settings_t *s, uint32_t page, uint32_t encoder, int32_t delta)
{
    uint32_t fm=PRESETS[s->preset].instrument==TK_INSTRUMENT_FM;
    if (encoder>1u && page>=TK_PAGE_OPERATOR && page<=TK_PAGE_VOICE) return 1;
    if (page==TK_PAGE_OPERATOR) {
        tk_fm_operator_t *op=&s->fm[0].op[tk_operator_focus];
        return fm ? tk_panel_value(s,encoder ? &op->release : &op->sustain,delta,127u) : 1;
    }
    /* These fields already have Knobs 1-4. Only OP needs two extra controls. */
    if (page==TK_PAGE_PITCH || page==TK_PAGE_GLOBAL || page==TK_PAGE_VOICE) return 1;
    return 0;
}
static int tk_panel_knob(tk_settings_t *s, uint32_t page, uint32_t knob, int32_t delta)
{
    uint32_t fm=PRESETS[s->preset].instrument==TK_INSTRUMENT_FM;
    if (knob>3u && page>=TK_PAGE_OPERATOR && page<=TK_PAGE_VOICE) return 1;
    if (page==TK_PAGE_OPERATOR) {
        tk_fm_operator_t *op=&s->fm[0].op[tk_operator_focus];
        uint8_t *value=knob==0u ? &op->ratio : knob==1u ? &op->level :
            knob==2u ? &op->attack : &op->decay;
        return fm ? tk_panel_value(s,value,delta,knob==0u ? 31u : 127u) : 1;
    }
    if (page==TK_PAGE_PITCH || page==TK_PAGE_GLOBAL) {
        if ((page==TK_PAGE_PITCH && knob==2u) || (page==TK_PAGE_GLOBAL && knob==1u))
            return tk_panel_bend(s,delta);
        if (page==TK_PAGE_PITCH && knob==3u) return tk_panel_octave(s,delta);
        if (page==TK_PAGE_GLOBAL && knob==3u) return tk_panel_mode(s,delta);
        if (!fm) return 1;
        if (page==TK_PAGE_GLOBAL) return knob==0u ? tk_panel_value(s,&s->fm[0].feedback,delta,127u)
            : tk_panel_value(s,&s->fm[0].algorithm,delta,7u);
        return tk_panel_value(s,knob ? &s->fm[0].pitch_time : &s->fm[0].pitch_depth,delta,knob ? 127u : 48u);
    }
    if (page==TK_PAGE_VOICE) {
        if (!knob) return tk_panel_mode(s,delta);
        if (knob==1u) return tk_panel_bend(s,delta);
        if (knob==2u) return tk_panel_octave(s,delta);
        /* Use the actual tail control of the current engine. The metallic
         * and kit engines do not consume the global release scalar. */
        return tk_panel_value(s,PRESETS[s->preset].instrument==TK_INSTRUMENT_METAL ? &s->p.bloom
            : PRESETS[s->preset].instrument==TK_INSTRUMENT_DRUM_KIT ? &s->p.drift : &s->release,delta,127u);
    }
    return 0;
}
static void tk_panel_row(uint32_t y,const char *name,uint32_t value)
{
    char number[4];
    draw_text_box(8,y,144,&FONT_S,name,TK_DIM,0);
    draw_number(number,value);
    draw_text_box(168,y,64,&FONT_S,number,TK_GREEN,2);
}
static void tk_panel_octave_row(uint32_t y,const char *name,int32_t octave)
{
    char number[3]={octave<0 ? '-' : '+',(char)('0'+(octave<0 ? -octave : octave)),0};
    draw_text_box(8,y,152,&FONT_S,name,TK_DIM,0);
    draw_text_box(168,y,64,&FONT_S,number,TK_GREEN,2);
}
static void tk_panel_mode_row(uint32_t y,const char *name,uint32_t mono)
{
    draw_text_box(8,y,152,&FONT_S,name,TK_DIM,0);
    draw_text_box(168,y,64,&FONT_S,mono ? "MONO" : "POLY",TK_GREEN,2);
}
static void tk_panel_disabled_row(uint32_t y,const char *name)
{
    draw_text_box(8,y,152,&FONT_S,name,TK_DIM,0);
    draw_text_box(168,y,64,&FONT_S,"FM ONLY",TK_DIM,2);
}
static int tk_draw_fm_required(const tk_settings_t *s)
{
    if (PRESETS[s->preset].instrument==TK_INSTRUMENT_FM) return 0;
    draw_text_box(8,74,224,&FONT_S,"SELECT AN FM PRESET",TK_GREEN,0);
    draw_text_box(8,101,224,&FONT_S,"33-36: SIX REAL OPERATORS",TK_DIM,0);
    draw_text_box(8,130,224,&FONT_S,"PRESETS CHOOSES THE SOUND",TK_DIM,0);
    return 1;
}
static void tk_draw_operator(const tk_settings_t *s)
{
    const tk_fm_operator_t *op=&s->fm[0].op[tk_operator_focus];
    char title[]="OP1 / SIX-OP FM";
    title[2]=(char)('1'+tk_operator_focus);
    draw_text_box(8,45,224,&FONT_S,title,C_ACC,0);
    if (!tk_draw_fm_required(s)) {
        tk_panel_row(67,"K1 RATIO",op->ratio);
        if (!op->ratio) draw_text_box(168,67,64,&FONT_S,"0.5X",TK_GREEN,2);
        tk_panel_row(89,"K2 LEVEL",op->level);
        tk_panel_row(111,"K3 ATTACK",op->attack);
        tk_panel_row(133,"K4 DECAY",op->decay);
        tk_panel_row(155,"SELECT SUSTAIN",op->sustain);
        tk_panel_row(177,"ALGO RELEASE",op->release);
    }
    draw_text_box(8,204,224,&FONT_S,"HOLD EDIT + OP1-OP6",C_ACC,0);
    draw_text_box(8,223,224,&FONT_S,"EDIT OR HOME: BACK",TK_DIM,0);
}
static void tk_draw_pitch(const tk_settings_t *s)
{
    char number[4],depth[]="+00 ST";
    int32_t value=(int32_t)s->fm[0].pitch_depth-24;
    draw_text_box(8,47,224,&FONT_S,"PITCH ENVELOPE",C_ACC,0);
    if (PRESETS[s->preset].instrument==TK_INSTRUMENT_FM) {
        depth[0]=value<0 ? '-' : '+'; if (value<0) value=-value;
        depth[1]=(char)('0'+value/10); depth[2]=(char)('0'+value%10);
        draw_text_box(8,74,112,&FONT_S,"K1 DEPTH",TK_DIM,0);
        draw_text_box(120,74,112,&FONT_S,depth,TK_GREEN,2);
        tk_panel_row(99,"K2 TIME",s->fm[0].pitch_time);
        cv_begin(224,45,C_BLACK);
        cv_line(0,23,223,23,C_BAR);
        cv_line(0,23-((int32_t)s->fm[0].pitch_depth-24)*20/24,48,23,C_ACC);
        cv_line(48,23,223,23,TK_GREEN);
        cv_blit(8,125);
    } else {
        tk_panel_disabled_row(74,"K1 DEPTH");
        tk_panel_disabled_row(99,"K2 TIME");
        draw_text_box(8,140,224,&FONT_S,"FM PRESETS 33-36",TK_DIM,0);
    }
    draw_number(number,s->midi_bend_range);
    draw_text_box(8,178,160,&FONT_S,"K3 BEND RANGE",TK_DIM,0);
    draw_text_box(168,178,64,&FONT_S,number,TK_GREEN,2);
    tk_panel_octave_row(200,"K4 PANEL OCT",s->octave);
    draw_text_box(8,219,224,&FONT_S,"EDIT OR HOME: BACK",TK_DIM,0);
}
static void tk_draw_fm_routing(uint32_t algorithm)
{
    static const int32_t x[6]={22,112,202,22,112,202}, y[6]={46,46,46,13,13,13};
    uint32_t i,j;
    const tk_fm_algorithm_t *route=&TK_FM_ROUTING[algorithm];
    cv_begin(224,64,C_BLACK);
    for (i=0; i<6u; i++) for (j=i+1u; j<6u; j++)
        if (route->input[i] & (1u<<j)) cv_line(x[j],y[j],x[i],y[i],TK_DIM);
    for (i=0; i<6u; i++) {
        char number[2]={(char)('1'+i),0};
        uint16_t color=(route->carriers & (1u<<i)) ? TK_GREEN : C_ACC;
        cv_rect(x[i]-10,y[i]-10,20,20,C_BAR);
        cv_line(x[i]-10,y[i]-10,x[i]+9,y[i]-10,color);
        cv_line(x[i]-10,y[i]+9,x[i]+9,y[i]+9,color);
        cv_text(x[i]-4,y[i]-8,&FONT_S,number,color);
        if (route->carriers & (1u<<i)) cv_line(x[i],y[i]+10,x[i],63,TK_GREEN);
    }
    cv_blit(8,86);
}
static void tk_draw_global(const tk_settings_t *s)
{
    uint32_t fm=PRESETS[s->preset].instrument==TK_INSTRUMENT_FM;
    draw_text_box(8,47,224,&FONT_S,fm ? "K3: SIX-OP ROUTING" : "LIVE SYNTH GLOBALS",C_ACC,0);
    if (fm) {
        tk_panel_row(67,TK_FM_ALGORITHM_NAMES[s->fm[0].algorithm],s->fm[0].algorithm);
        tk_draw_fm_routing(s->fm[0].algorithm);
        tk_panel_row(157,"K1 FEEDBACK",s->fm[0].feedback);
    } else {
        tk_panel_disabled_row(67,"K3 ROUTING");
        draw_text_box(8,111,224,&FONT_S,"FM PRESETS 33-36",TK_DIM,0);
        tk_panel_disabled_row(157,"K1 FEEDBACK");
    }
    tk_panel_row(180,"K2 BEND RANGE",s->midi_bend_range);
    tk_panel_mode_row(203,"K4 VOICE MODE",s->mono);
    draw_text_box(8,223,224,&FONT_S,"EDIT OR HOME: BACK",TK_DIM,0);
}
static void tk_draw_voice(const tk_settings_t *s)
{
    uint32_t instrument=PRESETS[s->preset].instrument;
    draw_text_box(8,47,224,&FONT_S,"VOICE / LIVE INPUTS",C_ACC,0);
    tk_panel_mode_row(74,"K1 VOICE MODE",s->mono);
    tk_panel_row(101,"K2 BEND RANGE",s->midi_bend_range);
    tk_panel_octave_row(128,"K3 PANEL OCT",s->octave);
    tk_panel_row(155,instrument==TK_INSTRUMENT_METAL ? "K4 METAL TAIL" :
        instrument==TK_INSTRUMENT_DRUM_KIT ? "K4 KIT DECAY" : "K4 RELEASE",
        instrument==TK_INSTRUMENT_METAL ? s->p.bloom :
        instrument==TK_INSTRUMENT_DRUM_KIT ? s->p.drift : s->release);
    draw_text_box(8,182,224,&FONT_S,s->mono ? "LAST HELD NOTE WINS" :
        PRESETS[s->preset].instrument==TK_INSTRUMENT_FM ? "FOUR FM / SIX SHARED" : "SIX SHARED VOICE SLOTS",C_ACC,0);
    draw_text_box(8,202,224,&FONT_S,"RECORDED CHORDS STAY POLY",TK_DIM,0);
    draw_text_box(8,223,224,&FONT_S,"EDIT OR HOME: BACK",TK_DIM,0);
}
#endif
