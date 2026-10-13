/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual-app 64-slot selection, captured sound and fixed-point PCM checks.
 * LEGACY_PCM was captured from the unmodified 0.19.2 app before this expansion:
 * USB-source C3/G3/C4 open fifth, velocity 100/92/85; 240 held + 160 release
 * blocks, 256 frames each. These hashes are independent compatibility fixtures,
 * not proof of physical listening, target deadlines or universal originality. */
#define TERKINSTEIN_HOST 1
#define main tk_preset_host_main
#include "../firmware/src/terkinstein.c"
#undef main
#include <stddef.h>

static const uint32_t LEGACY_PCM[36] = {
    2366421268u,125382115u,1108821870u,288569822u,2183232393u,1975491610u,
    2722767230u,3303089060u,432685268u,2273565378u,3804904848u,3195510429u,
    1854757u,1227171344u,2150201070u,2894116875u,2791468165u,2941409085u,
    395103175u,55672628u,1206860246u,1712851315u,635923993u,2271533177u,
    1443528319u,2415439927u,2369276077u,3514535954u,1804348032u,418963179u,
    2334873906u,1458525277u,3647233821u,754351477u,3159201725u,1192964425u
};
static uint32_t checks, reply_length;
static uint8_t reply[TK_REMOTE_PAYLOAD_MAX], live_shade, track_shade[TK_TRACKS];
static tk_settings_t fixture, before;
static tk_transport_t transport;
static tk_lfo_t live_lfo, track_lfo[TK_TRACKS];

static void check(int condition, const char *why)
{
    checks++;
    if (!condition) { fprintf(stderr,"FAIL preset bank %u: %s\n",checks,why); exit(1); }
}
static void reset_fixture(void)
{
    uint32_t track;
    tk_init(); tk_metronome_init(); tk_visual_init(); tk_ready=1u;
    settings_defaults(&fixture); memset(&transport,0,sizeof transport);
    tk_banks_init(&fixture,&transport); tk_lfo_init(&live_lfo,0u);
    remote_observed_valid=0u; remote_revision=remote_pattern_revision=0u;
    for(track=0;track<TK_TRACKS;++track) {
        tk_lfo_init(&track_lfo[track],0u); track_shade[track]=fixture.track[track].p.shade;
    }
    live_shade=fixture.p.shade; fm1_ms=0u;
}
static uint32_t command(uint32_t which,const uint8_t *payload,uint32_t count)
{
    tk_remote_message_t request;
    request.direction=TK_REMOTE_REQUEST; request.command=(uint8_t)which;
    request.id=1u; request.length=(uint8_t)count; request.payload=payload;
    return tk_remote_command(&request,&fixture,&transport,&live_shade,
        &live_lfo,track_lfo,track_shade,TK_PAGE_PAD,0u,reply,&reply_length);
}
static uint32_t pcm_hash(uint32_t slot,uint32_t *peak_out)
{
    uint32_t block,i,hash=2166136261u,peak=0u;
    int32_t pcm[512];
    reset_fixture(); preset_apply(&fixture,slot);
    publish_params(&fixture,fixture.p.shade);
    tk_note_on(48u,100u,1u); tk_note_on(55u,92u,1u); tk_note_on(60u,85u,1u);
    for(block=0;block<400u;++block) {
        if(block==240u) { tk_note_off(48u,1u); tk_note_off(55u,1u); tk_note_off(60u,1u); }
        memset(pcm,0,sizeof pcm); app_audio(pcm,256u);
        for(i=0;i<512u;++i) {
            uint32_t absolute=(uint32_t)(pcm[i]<0?-pcm[i]:pcm[i]);
            hash=(hash^(uint32_t)pcm[i])*16777619u;
            if(absolute>peak) peak=absolute;
        }
    }
    *peak_out=peak; return hash;
}
static void sound_and_metadata_checks(void)
{
    uint32_t slot,other,peak,hash[64],new_fm=0u,new_kit=0u,new_pad=0u,new_metal=0u;
    tk_fm_patch_t patch,expected,prior;
    check(TK_PRESET_COUNT==64u,"exact 64-slot bank");
    check(TK_VOICES==6u && TK_FM_VOICE_LIMIT==4u,"unchanged six total/four FM voice budget");
    for(slot=0;slot<64u;++slot) {
        reset_fixture(); preset_apply(&fixture,slot);
        check(settings_valid(&fixture),"factory settings validate across mixed engines");
        check(fixture.preset==slot,"factory selection retains requested index");
        fm_for_preset(&patch,slot);
        check(tk_fm_valid(&patch),"each slot publishes a complete valid FM patch or neutral default");
        if(PRESETS[slot].instrument==TK_INSTRUMENT_FM) {
            if(slot<36u) {
                tk_fm_factory(&expected,slot-32u);
                check(!memcmp(&patch,&expected,sizeof patch),"original FM patches unchanged");
            } else {
                new_fm++;
                for(other=0;other<slot;++other) if(PRESETS[other].instrument==TK_INSTRUMENT_FM) {
                    fm_for_preset(&prior,other);
                    check(memcmp(&patch,&prior,sizeof patch)!=0,"new FM factory is not a cloned previous patch");
                }
            }
        } else {
            tk_fm_defaults(&expected);
            check(!memcmp(&patch,&expected,sizeof patch),"non-FM slot above32 gets neutral FM metadata");
            if(slot>=36u) {
                new_kit+=PRESETS[slot].instrument==TK_INSTRUMENT_DRUM_KIT;
                new_pad+=PRESETS[slot].instrument==TK_INSTRUMENT_PAD;
                new_metal+=PRESETS[slot].instrument==TK_INSTRUMENT_METAL;
            }
        }
        for(other=0;other<4u;++other) {
            track_factory_apply(&fixture,other,slot);
            check(track_patch_valid(&fixture.track[other]),"all four tracks accept every factory engine");
            check(!memcmp(&fixture.fm[other+1u],&patch,sizeof patch),"captured track has selected full factory FM patch");
        }
        hash[slot]=pcm_hash(slot,&peak);
        check(peak>0u && peak<32767u,"reference chord is non-silent and unclipped");
        if(slot<36u) check(hash[slot]==LEGACY_PCM[slot],"all original36 preset PCM hashes unchanged");
        for(other=0;other<slot;++other) check(hash[slot]!=hash[other],"every factory reference produces distinct PCM");
        printf("PRESET %02u %-16s instrument=%u peak=%u pcm=%08X\n",slot+1u,PRESETS[slot].name,
            PRESETS[slot].instrument,peak,hash[slot]);
    }
    check(new_fm && new_kit && new_pad && new_metal,"expansion includes all four established engines");
    fm_for_preset(&patch,64u); tk_fm_defaults(&expected);
    check(!memcmp(&patch,&expected,sizeof patch),"out-of-range FM request yields safe neutral metadata");
    fm_for_preset(0,0u);
}
static void selection_and_graft_checks(void)
{
    uint32_t slot,track,i;
    uint8_t payload[2];
    tk_track_patch_t target;
    tk_fm_patch_t fm;
    reset_fixture(); command(TK_REMOTE_HELLO,0,0u);
    check(reply[0]==TK_REMOTE_OK && reply[8]==64u,"HELLO reports MIDI-safe64 count");
    for(slot=0;slot<64u;++slot) {
        payload[0]=(uint8_t)slot; command(TK_REMOTE_LIVE_PRESET,payload,1u);
        check(reply[0]==TK_REMOTE_OK && fixture.preset==slot,"remote live preset selects every factory slot");
        before=fixture; preset_apply(&fixture,64u);
        check(!memcmp(&fixture,&before,sizeof fixture),"invalid factory selection is an exact no-op");
        check(fixture.graft_base.instrument==PRESETS[slot].instrument,"GRAFT base retains selected engine");
        for(i=0;i<64u;++i) {
            fixture.graft_target=graft_next_target(&fixture,1);
            check(PRESETS[fixture.graft_target].instrument==PRESETS[slot].instrument,"GRAFT never crosses engine boundaries");
        }
        if(PRESETS[slot].instrument==TK_INSTRUMENT_DRUM_KIT)
            check(fixture.graft_target==slot,"kit keeps its existing non-GRAFT behavior");
        else {
            track_patch_factory(&target,fixture.graft_target); fm_for_preset(&fm,fixture.graft_target);
            fixture.graft_amount=127u; graft_apply(&fixture);
            check(!memcmp(&fixture.p,&target.p,sizeof target.p),"GRAFT endpoint reaches exact target macros");
            if(PRESETS[slot].instrument==TK_INSTRUMENT_FM)
                check(!memcmp(&fixture.fm[0],&fm,sizeof fm),"FM GRAFT reaches new factory's six operators exactly");
            fixture.graft_amount=0u; graft_apply(&fixture);
            check(!memcmp(&fixture.p,&before.p,sizeof before.p),"zero GRAFT restores source macros");
            check(!memcmp(&fixture.fm[0],&before.fm[0],sizeof fm),"zero GRAFT restores complete FM patch");
        }
        for(track=0;track<4u;++track) {
            payload[0]=(uint8_t)track; payload[1]=(uint8_t)slot;
            command(TK_REMOTE_TRACK_PRESET,payload,2u);
            check(reply[0]==TK_REMOTE_OK && fixture.track[track].preset==slot,"remote track selection reaches all64 factories");
        }
    }
    for(slot=64u;slot<128u;++slot) {
        before=fixture; payload[0]=(uint8_t)slot;
        command(TK_REMOTE_LIVE_PRESET,payload,1u);
        check(reply[0]==TK_REMOTE_BAD_ARGUMENT && !memcmp(&fixture,&before,sizeof fixture),"MIDI-safe invalid remote index rejected atomically");
        payload[0]=2u; payload[1]=(uint8_t)slot;
        command(TK_REMOTE_TRACK_PRESET,payload,2u);
        check(reply[0]==TK_REMOTE_BAD_ARGUMENT && !memcmp(&fixture,&before,sizeof fixture),"invalid track index rejected atomically");
    }
}
static void kit_checks(void)
{
    uint32_t slot,key,block,i,peak,hash,seen_count;
    uint32_t seen[64][12];
    int32_t pcm[512];
    seen_count=0u;
    for(slot=0;slot<64u;++slot) if(PRESETS[slot].instrument==TK_INSTRUMENT_DRUM_KIT) {
        for(key=0;key<12u;++key) {
            reset_fixture(); preset_apply(&fixture,slot); publish_params(&fixture,fixture.p.shade);
            tk_note_on(36u+key,110u,1u); peak=0u; hash=2166136261u;
            for(block=0;block<80u;++block) {
                memset(pcm,0,sizeof pcm); app_audio(pcm,256u);
                for(i=0;i<512u;++i) {
                    uint32_t absolute=(uint32_t)(pcm[i]<0?-pcm[i]:pcm[i]);
                    if(absolute>peak) peak=absolute;
                    hash=(hash^(uint32_t)pcm[i])*16777619u;
                }
            }
            check(peak>0u && peak<32767u,"every new kit pitch class retains an audible unclipped drum");
            for(i=0;i<seen_count;++i)
                check(seen[i][key]!=hash,"each kit's same drum family has a distinct forged timbre");
            seen[seen_count][key]=hash;
        }
        seen_count++;
    }
    check(seen_count==5u,"original plus four forged drum kits");
}
static void layered_checks(void)
{
    uint32_t slot,block,i,peak;
    int32_t pcm[512];
    for(slot=0;slot<64u;++slot) if(PRESETS[slot].instrument==TK_INSTRUMENT_DRUM_KIT) {
        reset_fixture(); preset_apply(&fixture,slot); publish_params(&fixture,fixture.p.shade);
        /* Use the selected kit on its generated source, as captured playback does. */
        track_factory_apply(&fixture,TK_TRACK_DRUM,slot); publish_track(&fixture,TK_TRACK_DRUM);
        for(i=0;i<6u;++i) tk_note_on(36u+i,127u,TK_SOURCE_DRUM);
        peak=0u;
        for(block=0;block<300u;++block) {
            memset(pcm,0,sizeof pcm); app_audio(pcm,256u);
            for(i=0;i<512u;++i) {
                uint32_t absolute=(uint32_t)(pcm[i]<0?-pcm[i]:pcm[i]);
                if(absolute>peak) peak=absolute;
            }
        }
        check(peak>0u && peak<24384u,"six simultaneous captured kit hits retain limiter headroom");
        printf("LAYERED_KIT %02u %-16s six_hits_peak=%u\n",slot+1u,PRESETS[slot].name,peak);
    }
}
static void drum_velocity_checks(void)
{
    static const uint8_t VELOCITY[] = {32u,64u,100u,127u};
    uint32_t slot,level,track,block,i,peak,previous_peak;
    uint64_t energy,previous_energy;
    uint8_t payload[7];
    int32_t pcm[512];
    for(slot=0;slot<64u;++slot) if(PRESETS[slot].instrument==TK_INSTRUMENT_DRUM_KIT) {
        previous_peak=0u; previous_energy=0u;
        for(level=0;level<sizeof VELOCITY;++level) {
            reset_fixture();
            for(track=0;track<TK_TRACKS;++track) tk_pattern_clear(&fixture.pattern[track]);
            payload[0]=TK_TRACK_DRUM; payload[1]=(uint8_t)slot;
            command(TK_REMOTE_TRACK_PRESET,payload,2u);
            check(reply[0]==TK_REMOTE_OK,"controller selects the captured drum kit");
            /* The controller's step edit traverses the real command parser,
             * stored pattern and sequencer playback rather than replacing a
             * voice's gain directly. Isolate one C kick at each velocity. */
            payload[0]=TK_TRACK_DRUM; payload[1]=0u; payload[2]=1u;
            payload[3]=36u; payload[4]=VELOCITY[level]; payload[5]=payload[6]=0u;
            command(TK_REMOTE_SET_STEP,payload,7u);
            check(reply[0]==TK_REMOTE_OK && fixture.pattern[TK_TRACK_DRUM].step[0].note[0].velocity==VELOCITY[level],
                "controller step velocity is retained exactly");
            publish_params(&fixture,fixture.p.shade);
            transport.mode=TK_MODE_SEQ; transport_start(&transport,0u);
            check(transport_tick(&transport,&fixture,0u),"sequencer emits the isolated controller-authored hit");
            peak=0u; energy=0u;
            for(block=0;block<80u;++block) {
                memset(pcm,0,sizeof pcm); app_audio(pcm,256u);
                for(i=0;i<512u;++i) {
                    uint32_t absolute=(uint32_t)(pcm[i]<0?-pcm[i]:pcm[i]);
                    if(absolute>peak) peak=absolute;
                    energy+=(uint64_t)absolute*absolute;
                }
            }
            check(peak>previous_peak && energy>previous_energy,
                "32,64,100,127 velocity produce strictly louder peak and energy");
            check(peak<24384u,"maximum drum velocity remains below the engine limiter");
            printf("DRUM_VELOCITY %02u %-16s velocity=%u peak=%u energy=%llu\n",
                slot+1u,PRESETS[slot].name,VELOCITY[level],peak,(unsigned long long)energy);
            previous_peak=peak; previous_energy=energy;
        }
    }
}
static void save_checks(void)
{
    uint32_t i;
    tk_settings_v10_t legacy;
    tk_settings_t migrated;
    reset_fixture(); preset_apply(&fixture,35u);
    for(i=0;i<6u;++i) {
        fixture.fm[i].op[0].level=(uint8_t)(11u+i*9u);
        fixture.fm[i].op[5].ratio=(uint8_t)(21u+i);
    }
    check(settings_valid(&fixture),"nonfactory six-bank FM fields validate");
    before=fixture;
    check(sizeof fixture==3700u && TK_SCHEMA==11u && offsetof(tk_settings_t,fm)==3452u,
        "schema11 size and saved FM offsets remain exact");
    memcpy(&legacy,&fixture,sizeof legacy); legacy.schema=10u; legacy.midi_pad=0u;
    check(settings_upgrade_v10(&migrated,&legacy),"legacy edited FM record migrates");
    check(!memcmp(migrated.fm,before.fm,sizeof before.fm),"migration retains all six edited FM banks");
    preset_apply(&fixture,63u); track_capture_apply(&fixture,3u);
    check(settings_valid(&fixture),"new lastslot captured settings fit unchanged schema");
    before=fixture;
    check(settings_valid(&fixture) && !memcmp(&fixture,&before,sizeof fixture),"validation never replaces saved FM with factory data");
    fixture.preset=64u; check(!settings_valid(&fixture),"future factory index is rejected on save load");
}
int main(void)
{
    sound_and_metadata_checks(); kit_checks(); layered_checks(); drum_velocity_checks(); selection_and_graft_checks(); save_checks();
    printf("PASS: %u actual-app preset bank checks (host only)\n",checks); return 0;
}
