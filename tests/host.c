/* SPDX-License-Identifier: GPL-3.0-only */
/* A fake FM-1 on a computer: firmware/src/terkinstein.c runs unchanged on it. platform.c includes
 * this file instead of the hardware when TERKINSTEIN_HOST is set. A script presses keys over time; out come
 * the screen (PPM, at the times SHOT asks) and the sound (WAV, as the FM-1 sends it over USB).
 *
 *   mkdir -p build/host
 *   cc -O2 -Wall -DTERKINSTEIN_HOST -Ifirmware/src -Ifirmware/gen -o build/host/terkinstein firmware/src/terkinstein.c
 *   build/host/terkinstein build/host/terkinstein  (-> WAV and PPM screens)
 *   build/host/terkinstein OUT script.txt [FPS] (with FPS: OUT-NNNNN.ppm, a frame every 1/FPS s:
 *                                             a film of your firmware, e.g. ffmpeg -r 30 -i OUT-%05d.ppm ...)
 *
 * Change SCRIPT below to try your own app. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifdef _MSC_VER
/* The host framebuffer does not need the target's GCC alignment attribute. */
#define __attribute__(x)
#endif

#ifndef APP_NAME
#define APP_NAME "TERKINSTEIN"
#endif
#ifndef APP_VER
#define APP_VER "0.0"
#endif
#define FS 44100u
#define HALF_FRAMES 256u

static volatile uint32_t fm1_ms;
static struct { uint32_t notes, buttons; } fm1_in;
static uint32_t host_edges, host_released;
static int32_t host_enc[7];
static int host_master_activity;
static int platform_master_activity(void)
{ int changed = host_master_activity; host_master_activity = 0; return changed; }
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t pressed = host_edges;
    if (released) *released = host_released;
    host_edges = host_released = 0;
    return pressed;
}
static uint32_t fm1_input_note_edges(void) { return 0; }   /* (the script sets fm1_in.notes) */
static int32_t fm1_enc_take(uint32_t e)
{
    int32_t n;
    if (e >= 7u) return 0;
    n = host_enc[e]; host_enc[e] = 0;
    return n;
}
static void fm1_led_key(uint32_t id, int on) { (void)id; (void)on; }

/* the screen: 240 x 240 RGB565, as the ST7789 gets it (gfx.c sends the bytes swapped) */
#ifdef TERKINSTEIN_SCREEN_MIRROR
#include "terkinstein_screen.h"
#endif
static uint16_t fb[240 * 240];
static uint32_t host_lcd_pixels;
#ifdef TK_TEST_LCD_HOOK
static void (*host_lcd_transfer_hook)(uint32_t);
#endif
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
#ifdef TERKINSTEIN_SCREEN_MIRROR
    tk_screen_capture_fill(x, y, w, h, c);
#endif
#ifdef TERKINSTEIN_LCD_COOPERATIVE
    if (tk_lcd_queue.active) return;
#endif
    host_lcd_pixels += w * h;
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = c;
#ifdef TK_TEST_LCD_HOOK
    if (host_lcd_transfer_hook) host_lcd_transfer_hook(w * h);
#endif
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
#ifdef TERKINSTEIN_SCREEN_MIRROR
    tk_screen_capture_blit(x, y, w, h, px);
#endif
#ifdef TERKINSTEIN_LCD_COOPERATIVE
    if (tk_lcd_queue.active) return;
#endif
    host_lcd_pixels += w * h;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u) {
                uint16_t c = px[j * w + i];
                fb[(y + j) * 240u + x + i] = (uint16_t)(c >> 8 | c << 8);
            }
#ifdef TK_TEST_LCD_HOOK
    if (host_lcd_transfer_hook) host_lcd_transfer_hook(w * h);
#endif
}
static void lcd_cooperative_begin(void)
{
#ifdef TERKINSTEIN_LCD_COOPERATIVE
    tk_lcd_queue.active = 1u;
#endif
}
static void lcd_cooperative_immediate(void)
{
#ifdef TERKINSTEIN_LCD_COOPERATIVE
    uint32_t tile;
    for (tile = 0; tile < TK_SCREEN_TILES; ++tile)
        if (tk_lcd_queue.dirty[tile]) tk_screen.dirty[tile] = 1u;
    tk_lcd_queue_reset();
#endif
}
static uint32_t lcd_cooperative_upload(void)
{
    uint32_t count = 0u;
#ifdef TERKINSTEIN_LCD_COOPERATIVE
    for (; count < TK_LCD_BATCH_TILES; ++count) {
        uint32_t tile = tk_lcd_queue_take(tk_screen_pool.pixels), row, col, x, y;
        if (tile == TK_SCREEN_TILES) break;
        x = tile % TK_SCREEN_COLUMNS * TK_SCREEN_TILE;
        y = tile / TK_SCREEN_COLUMNS * TK_SCREEN_TILE;
        for (row = 0; row < TK_SCREEN_TILE; ++row)
            for (col = 0; col < TK_SCREEN_TILE; ++col) {
                uint16_t c = tk_lcd_queue.pixels[row * TK_SCREEN_TILE + col];
                fb[(y + row) * TK_SCREEN_WIDTH + x + col] = (uint16_t)(c >> 8 | c << 8);
            }
        host_lcd_pixels += TK_SCREEN_TILE * TK_SCREEN_TILE;
#ifdef TK_TEST_LCD_HOOK
        if (host_lcd_transfer_hook) host_lcd_transfer_hook(TK_SCREEN_TILE * TK_SCREEN_TILE);
#endif
        tk_screen.dirty[tile] = 1u;
    }
#endif
    return count;
}
#define CV_MAX (240u * 64u)
#include "gfx.c"

static uint8_t host_store[3840 + 4];
static uint32_t host_store_len;
static int host_store_imported;
/* Opt-in persistence lets a fresh host process exercise the real settings
 * load path. Ordinary previews still start with their in-memory fake flash. */
static void host_store_import(void)
{
    const char *path = getenv("TK_HOST_SETTINGS_PATH");
    FILE *f;
    uint32_t n;
    if (host_store_imported) return;
    host_store_imported = 1;
    if (!path || !*path || !(f = fopen(path, "rb"))) return;
    if (fread(&n, sizeof n, 1, f) == 1 && n <= 3840u &&
        fread(host_store + 4, 1, n, f) == n && fgetc(f) == EOF) {
        host_store_len = n;
        host_store[0] = 1;
    }
    fclose(f);
}
static int settings_load(void *p, uint32_t n)
{
    host_store_import();
    if (host_store[0] != 1 || n > 3840u || n != host_store_len) return -1;
    memcpy(p, host_store + 4, n);
    return 0;
}
static int settings_save(const void *p, uint32_t n)
{
    const char *path = getenv("TK_HOST_SETTINGS_PATH");
    if (n > 3840u) return -1;
    if (path && *path) {
        FILE *f = fopen(path, "wb");
        int ok;
        if (!f) { perror(path); return -1; }
        ok = fwrite(&n, sizeof n, 1, f) == 1 && fwrite(p, 1, n, f) == n;
        if (fclose(f) != 0) ok = 0;
        if (!ok) { perror(path); return -1; }
    }
    memcpy(host_store + 4, p, n);
    host_store_len = n;
    host_store[0] = 1;
    printf("%6u ms  settings saved (%u B)\n", fm1_ms, n);
    return 0;
}

static uint8_t host_journal[TK_STORE_END - TK_STORE_BASE];
static int host_journal_imported;
static void host_journal_import(void)
{
    const char *path = getenv("TK_HOST_JOURNAL_PATH"); FILE *f;
    if (host_journal_imported) return;
    host_journal_imported = 1; memset(host_journal, 255, sizeof host_journal);
    if (path && *path && (f = fopen(path, "rb"))) {
        if (fread(host_journal, 1, sizeof host_journal, f) != sizeof host_journal || fgetc(f) != EOF)
            memset(host_journal, 255, sizeof host_journal);
        fclose(f);
    }
}
static int host_journal_flush(void)
{
    const char *path = getenv("TK_HOST_JOURNAL_PATH"); FILE *f; int ok;
    if (!path || !*path) return 0;
    if (!(f = fopen(path, "wb"))) return -1;
    ok = fwrite(host_journal, 1, sizeof host_journal, f) == sizeof host_journal;
    if (fclose(f)) ok = 0;
    return ok ? 0 : -1;
}
static int host_journal_range(uint32_t off, uint32_t n)
{ return off >= TK_STORE_BASE && off <= TK_STORE_END && n <= TK_STORE_END - off; }
static int tk_store_flash_read(uint32_t off, void *p, uint32_t n)
{
    host_journal_import();
    if (!host_journal_range(off, n)) return -1;
    memcpy(p, host_journal + off - TK_STORE_BASE, n); return 0;
}
static int tk_store_flash_write(uint32_t off, const uint8_t *p, uint32_t n)
{
    uint32_t i; host_journal_import();
    if (!host_journal_range(off, n)) return -1;
    for (i = 0; i < n; i++) if ((host_journal[off - TK_STORE_BASE + i] & p[i]) != p[i]) return -1;
    for (i = 0; i < n; i++) host_journal[off - TK_STORE_BASE + i] &= p[i];
    return host_journal_flush();
}
static int tk_store_flash_erase(uint32_t off)
{
    host_journal_import();
    if ((off & 0xFFFu) || !host_journal_range(off, 0x1000u)) return -1;
    memset(host_journal + off - TK_STORE_BASE, 255, 0x1000u); return host_journal_flush();
}
static void tk_store_idle(void) {}

static uint8_t mq[64][4];
static uint32_t mq_w, mq_r;
static uint32_t host_midi_overflow;
static int midi_get_from(uint8_t *port, uint8_t *st, uint8_t *d1, uint8_t *d2)
{
    if (mq_r == mq_w) return 0;
    *st = mq[mq_r % 64u][0]; *d1 = mq[mq_r % 64u][1]; *d2 = mq[mq_r % 64u][2];
    *port = mq[mq_r % 64u][3];
    mq_r++;
    return 1;
}
static int midi_get(uint8_t *st, uint8_t *d1, uint8_t *d2)
{
    uint8_t port;
    return midi_get_from(&port, st, d1, d2);
}
static uint32_t midi_take_overflow(void)
{
    uint32_t ports = host_midi_overflow;
    uint32_t read, write = mq_r;
    host_midi_overflow = 0;
    /* Match the real rear-input acknowledgement: pending messages from a
     * lost port are stale and cannot repopulate the notes being released. */
    if (ports) {
        for (read = mq_r; read < mq_w; ++read) {
            uint8_t *event = mq[read % 64u];
            if (!(ports & (1u << event[3]))) {
                memmove(mq[write % 64u], event, 4u);
                ++write;
            }
        }
        mq_w = write;
    }
    return ports;
}
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2) { printf("%6u ms  MIDI out %02X %u %u\n", fm1_ms, st, d1, d2); }

/* Use the actual bounded USB SysEx collector against fake register/ring state.
 * Each sysex script event supplies a complete hex F0..F7 frame. */
#include "terkinstein_protocol.h"
#define FELUCCA_OTA 1
#define FELUCCA_REMOTE 1
#define RING_PUBLISH() ((void)0)
static struct { uint8_t config, sysex[16], sx_len, sx_on, uboot_req, ota_req; } usb = {1,{0},0,0,0,0};
static uint8_t sx_frame[640], usb_remote_frame[TK_REMOTE_DATA_MAX];
static uint32_t sx_pos, sx_frame_len, usb_remote_length, usb_remote_generation;
static uint8_t sx_ready, sx_collect, sx_busy, sx_open, usb_remote_ready;
#define SXQ 64u
static uint32_t sx_out_q[SXQ], so_w, so_r, mo_w, mo_r;
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t flags) { (void)flags; }
#include "../firmware/src/midi_sysex.c"
static int midi_remote_get(const uint8_t **p, uint32_t *n) { return usb_remote_get(p,n); }
static void midi_remote_done(void) { usb_remote_done(); }
static uint32_t midi_remote_generation(void) { return usb_remote_generation; }
static int host_remote_complete(const uint8_t *p, uint32_t n)
{
    uint32_t i;
    printf("%6u ms  REMOTE out ",fm1_ms);
    for (i = 0; i < n; i++) printf("%02X",p[i]);
    printf("\n");
    so_r = so_w; /* fake USB host immediately consumes the complete TX frame */
    return 1;
}
static int midi_remote_send(const uint8_t *p, uint32_t n)
{
    return usb_remote_send(p,n) ? host_remote_complete(p,n) : 0;
}
static int midi_remote_screen_send(const uint8_t *p, uint32_t n)
{
    return usb_remote_screen_send(p,n) ? host_remote_complete(p,n) : 0;
}
static uint8_t host_sysex_event[4096][TK_REMOTE_WIRE_MAX];

static void app_audio(int32_t *out, uint32_t n);
static void app_main(void);

/* Events change only their own input. In particular a knob or MIDI event preserves held keys.
 * Files must be in time order: T keys MASK, T btn ID, T hold ID 0|1,
 * T enc ID DELTA, T midi ST D1 D2 (USB), T din ST D1 D2 (rear TRS),
 * T drop PORT (0 USB, 1 TRS, simulates input queue loss), T end.
 * `btn` is a one-shot press edge;
 * `hold` changes the debounced held state and latches press/release edges.
 * MASK/status accept hexadecimal; button IDs are 0..13, encoder IDs 0..6,
 * and delta is signed detents. */
enum { HOST_KEYS, HOST_BTN, HOST_MIDI, HOST_ENC, HOST_HOLD, HOST_DIN, HOST_DROP, HOST_SYSEX, HOST_USB_RESET };
typedef struct { uint32_t t, type, a, b, c; int32_t delta; } host_ev_t;
static const host_ev_t SCRIPT0[] = {
    {300, HOST_KEYS, 1u << 7 | 1u << 10 | 1u << 14, 0, 0, 0}, /* C3 Eb3 G3 */
    {2200, HOST_ENC, ENC_KNOB2, 0, 0, 22},                    /* fracture */
    {3400, HOST_ENC, ENC_KNOB3, 0, 0, 12},                    /* drift */
    {4600, HOST_ENC, ENC_KNOB1, 0, 0, -18},                   /* shade */
    {5200, HOST_MIDI, 0x91, 70, 90, 0},                      /* Bb4, MIDI channel 2 */
    {6600, HOST_MIDI, 0x81, 70, 0, 0},
    {7000, HOST_ENC, ENC_KNOB4, 0, 0, 16},                    /* bloom */
    {8000, HOST_KEYS, 0, 0, 0, 0},                           /* let the release breathe */
};
static const host_ev_t *script = SCRIPT0;
static uint32_t script_n = sizeof SCRIPT0 / sizeof SCRIPT0[0], end_ms = 12000, frame_fps;
static const uint32_t SHOT[] = {1000, 4200, 8200};

static void read_script(const char *path)
{
    static host_ev_t ev[4096];
    char line[256], what[16], first[32], value[32], extra[2];
    FILE *f = fopen(path, "r");
    uint32_t n = 0, line_no = 0, last_t = 0;
    if (!f) { perror(path); exit(1); }
    while (fgets(line, sizeof line, f)) {
        host_ev_t e = {0, 0, 0, 0, 0, 0};
        char *end, *comment = strchr(line, '#');
        unsigned long t, a;
        unsigned b = 0, c = 0;
        int count;
        line_no++;
        if (comment) *comment = 0;
        count = sscanf(line, "%31s %15s", first, what);
        if (count == EOF) continue;
        if (count != 2 || first[0] == '-') goto bad_line;
        t = strtoul(first, &end, 10);
        if (*end || t > 600000ul || t < last_t) goto bad_line;
        e.t = (uint32_t)t;
        last_t = e.t;
        if (!strcmp(what, "end")) {
            if (sscanf(line, "%*s %*s %1s", extra) != EOF || !e.t) goto bad_line;
            end_ms = e.t;
            continue;
        }
        if (!strcmp(what, "sysex")) {
            char hex[TK_REMOTE_WIRE_MAX * 2u + 1u];
            uint32_t length, at;
            if (n == 4096u || sscanf(line,"%*s %*s %182s %1s",hex,extra) != 1) goto bad_line;
            length = (uint32_t)strlen(hex);
            if (!length || length % 2u || length > TK_REMOTE_WIRE_MAX * 2u) goto bad_line;
            e.type = HOST_SYSEX; e.a = n; e.b = length / 2u;
            for (at = 0; at < length; at += 2u) {
                char byte[3] = {hex[at],hex[at + 1u],0};
                unsigned long decoded = strtoul(byte,&end,16);
                if (*end || decoded > 255u) goto bad_line;
                host_sysex_event[n][at / 2u] = (uint8_t)decoded;
            }
            ev[n++] = e;
            continue;
        }
        if (n == 4096u || sscanf(line, "%*s %*s %31s", value) != 1 || value[0] == '-') goto bad_line;
        a = strtoul(value, &end, 0);
        if (*end || a > UINT32_MAX) goto bad_line;
        e.a = (uint32_t)a;
        if (!strcmp(what, "keys")) {
            e.type = HOST_KEYS;
            if (a >= (1u << 27) || sscanf(line, "%*s %*s %*s %1s", extra) != EOF) goto bad_line;
        } else if (!strcmp(what, "drop")) {
            e.type = HOST_DROP;
            if (a > 1u || sscanf(line, "%*s %*s %*s %1s", extra) != EOF) goto bad_line;
        } else if (!strcmp(what, "usbreset")) {
            e.type = HOST_USB_RESET;
            if (a != 0u || sscanf(line, "%*s %*s %*s %1s", extra) != EOF) goto bad_line;
        } else if (!strcmp(what, "btn")) {
            e.type = HOST_BTN;
            if (a >= 14u || sscanf(line, "%*s %*s %*s %1s", extra) != EOF) goto bad_line;
        } else if (!strcmp(what, "hold")) {
            e.type = HOST_HOLD;
            if (a >= 14u || sscanf(line, "%*s %*s %*s %u %1s", &b, extra) != 1 || b > 1u)
                goto bad_line;
            e.b = b;
        } else if (!strcmp(what, "enc")) {
            long delta;
            e.type = HOST_ENC;
            if (a >= 7u || sscanf(line, "%*s %*s %*s %31s %1s", value, extra) != 1) goto bad_line;
            delta = strtol(value, &end, 10);
            if (*end || delta < -32767 || delta > 32767) goto bad_line;
            e.delta = (int32_t)delta;
        } else if (!strcmp(what, "midi") || !strcmp(what, "din")) {
            e.type = !strcmp(what, "din") ? HOST_DIN : HOST_MIDI;
            if (a < 0x80u || a > 0xFFu || sscanf(line, "%*s %*s %*s %u %u %1s", &b, &c, extra) != 2 || b > 127u || c > 127u) goto bad_line;
            e.b = b; e.c = c;
        } else goto bad_line;
        ev[n++] = e;
        continue;
bad_line:
        fprintf(stderr, "%s:%u: invalid or out-of-order host event\n", path, line_no);
        fclose(f);
        exit(1);
    }
    if (ferror(f) || last_t > end_ms) { fprintf(stderr, "%s: script exceeds end time or read failed\n", path); fclose(f); exit(1); }
    fclose(f);
    script = ev;
    script_n = n;
}

static const char *host_out = "build/host/terkinstein";
static int16_t *wav;
static uint32_t wav_n, wav_cap;

static void write_ppm_as(const char *name)
{
    FILE *f;
    uint32_t i;
    if (!(f = fopen(name, "wb"))) { perror(name); exit(1); }
    fprintf(f, "P6 240 240 255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = fb[i];
        fputc((c >> 11) * 255 / 31, f);
        fputc((c >> 5 & 63) * 255 / 63, f);
        fputc((c & 31) * 255 / 31, f);
    }
    fclose(f);
}
static void write_ppm(uint32_t t)
{
    char name[512];
    snprintf(name, sizeof name, "%s-%u.ppm", host_out, t);
    write_ppm_as(name);
    printf("%6u ms  screen -> %s\n", t, name);
}

static void write_wav(void)
{
    char name[512];
    FILE *f;
    uint32_t bytes = wav_n * 4u, v[] = {16, 0x00020001u, FS, FS * 4u, 0x00100004u};
    snprintf(name, sizeof name, "%s.wav", host_out);
    if (!(f = fopen(name, "wb"))) { perror(name); exit(1); }
    fwrite("RIFF", 1, 4, f); bytes += 36; fwrite(&bytes, 4, 1, f); bytes -= 36;
    fwrite("WAVEfmt ", 1, 8, f); fwrite(v, 4, 5, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    fwrite(wav, 4, wav_n, f);
    fclose(f);
    printf("sound  -> %s (%u frames)\n", name, wav_n);
}

static void platform_poll(void)                 /* one call = one millisecond */
{
    static uint32_t step, shot, owed, frame;
    static int32_t blk[2u * HALF_FRAMES];
    fm1_ms++;
#ifdef TERKINSTEIN_SCREEN_MIRROR
    if (getenv("TK_SCREEN_VALIDATE")) {
        uint32_t at;
        for (at = 0; at < 240u * 240u; ++at) {
#ifdef TERKINSTEIN_LCD_COOPERATIVE
            if (tk_lcd_queue.active && tk_lcd_queue.dirty[(at / 240u / 16u) * 15u + at % 240u / 16u]) continue;
#endif
            if (fb[at] != tk_screen_pool.pixels[at]) {
        fprintf(stderr, "LCD capture differs from actual host framebuffer at %u ms\n", fm1_ms);
        exit(1);
            }
        }
    }
#endif
    if (fm1_ms % 1000u == 0u && getenv("TK_TRACE_LCD")) {
        printf("%6u ms  LCD pixels %u\n", fm1_ms, host_lcd_pixels);
        host_lcd_pixels = 0;
    }
    while (step < script_n && script[step].t <= fm1_ms) {
        const host_ev_t *e = &script[step];
        if (e->type == HOST_MIDI || e->type == HOST_DIN) {
            if (mq_w - mq_r >= 64u) { fprintf(stderr, "host MIDI queue full at %u ms\n", fm1_ms); exit(1); }
            mq[mq_w % 64u][0] = (uint8_t)e->a; mq[mq_w % 64u][1] = (uint8_t)e->b; mq[mq_w % 64u][2] = (uint8_t)e->c;
            mq[mq_w % 64u][3] = (uint8_t)(e->type == HOST_DIN);
            mq_w++;
        } else if (e->type == HOST_DROP) {
            host_midi_overflow |= 1u << e->a;
        } else if (e->type == HOST_SYSEX) {
            uint32_t byte;
            for (byte = 0; byte < e->b; byte++) sysex_byte(host_sysex_event[e->a][byte]);
        } else if (e->type == HOST_USB_RESET) {
            usb.sx_on = 0; sx_collect = 0; so_r = so_w; usb_remote_reset();
        } else if (e->type == HOST_BTN)
            host_edges |= 1u << e->a;
        else if (e->type == HOST_HOLD) {
            uint32_t bit = 1u << e->a;
            if (e->b && !(fm1_in.buttons & bit)) {
                fm1_in.buttons |= bit;
                host_edges |= bit;
            } else if (!e->b && (fm1_in.buttons & bit)) {
                fm1_in.buttons &= ~bit;
                host_released |= bit;
            }
        }
        else if (e->type == HOST_ENC) {
            int64_t sum = (int64_t)host_enc[e->a] + e->delta;
            host_enc[e->a] = (int32_t)(sum > INT32_MAX ? INT32_MAX : sum < INT32_MIN ? INT32_MIN : sum);
        } else fm1_in.notes = e->a;
        step++;
    }
    owed += FS;                                 /* the frames this millisecond is worth, in blocks as the ISR renders */
    while (owed >= 1000u * HALF_FRAMES) {
        uint32_t i;
        owed -= 1000u * HALF_FRAMES;
        memset(blk, 0, sizeof blk);
        app_audio(blk, HALF_FRAMES);
        if (wav_n + HALF_FRAMES > wav_cap) {
            int16_t *next;
            wav_cap = wav_cap ? wav_cap * 2u : FS;
            next = realloc(wav, (size_t)wav_cap * 4u);
            if (!next) { fprintf(stderr, "not enough memory for host WAV\n"); exit(1); }
            wav = next;
        }
        for (i = 0; i < 2u * HALF_FRAMES; i++) {
            int32_t v = blk[i];                 /* (USB audio at the MASTER knob all the way up) */
            wav[2u * wav_n + i] = (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
        }
        wav_n += HALF_FRAMES;
    }
    if (frame_fps) {                            /* a film: a frame every 1/fps s, as the screen is then */
        if ((uint64_t)frame * 1000u <= (uint64_t)fm1_ms * frame_fps) {
            char name[512];
            snprintf(name, sizeof name, "%s-%05u.ppm", host_out, frame++);
            write_ppm_as(name);
        }
    } else if (shot < sizeof SHOT / sizeof SHOT[0] && fm1_ms == SHOT[shot])
        write_ppm(SHOT[shot++]);
    if (fm1_ms >= end_ms) {
        write_wav();
        exit(0);
    }
}

int main(int argc, char **argv)
{
    if (argc > 1) host_out = argv[1];
    if (argc > 2) read_script(argv[2]);
    if (argc > 3) {
        char *end;
        unsigned long fps = strtoul(argv[3], &end, 10);
        if (*end || fps > 120u) { fprintf(stderr, "FPS must be 0..120\n"); return 1; }
        frame_fps = (uint32_t)fps;
    }
    app_main();
}
