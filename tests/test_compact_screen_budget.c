/* SPDX-License-Identifier: GPL-3.0-only */
/* Emit the real codec on stored native HOME LCD snapshots, not timing claims. */
#define main tk_screen_previous_main
#include "test_screen.c"
#undef main
static uint32_t source_ms;
static FILE *wire_log;
static int budget_send(const uint8_t *wire, uint32_t n)
{
    uint32_t i;
    fprintf(wire_log, "%u ms  REMOTE out ", source_ms);
    for (i = 0; i < n; i++) fprintf(wire_log, "%02X", wire[i]);
    fputc('\n', wire_log);
    return 1;
}
int main(int argc, char **argv)
{
    static uint16_t swapped[240u * 240u];
    uint32_t mode, frame, pixel, clock;
    if (argc != 3) { fprintf(stderr, "Usage: compact-budget SOURCE_FOLDER OUTPUT_PREFIX\n"); return 2; }
    for (mode = 0; mode < 2u; mode++) {
        char name[1024];
        reset(); tk_screen_set_home(1u); clock = 0u;
        snprintf(name, sizeof name, "%s-%s.log", argv[2], mode ? "compact" : "lossless");
        wire_log = fopen(name, "w"); if (!wire_log) return 3;
        tk_screen_subscribe_profile(3u, (uint8_t)mode, clock, 0u);
        for (frame = 0; frame < 200u; frame++) {
            unsigned w, h, max;
            char magic[8]; FILE *image;
            snprintf(name, sizeof name, "%s/native-home-budget-%05u.ppm", argv[1], frame);
            image = fopen(name, "rb"); if (!image) break;
            if (fscanf(image, "%7s %u %u %u", magic, &w, &h, &max) != 4 ||
                strcmp(magic, "P6") || w != 240u || h != 240u || max != 255u || fgetc(image) != '\n') return 4;
            for (pixel = 0; pixel < 240u * 240u; pixel++) {
                int r = fgetc(image), g = fgetc(image), b = fgetc(image);
                uint16_t color;
                if (r < 0 || g < 0 || b < 0) return 5;
                color = (uint16_t)(((uint32_t)r >> 3) << 11 | ((uint32_t)g >> 2) << 5 | ((uint32_t)b >> 3));
                swapped[pixel] = (uint16_t)(color >> 8 | color << 8);
            }
            fclose(image); source_ms = frame * 100u;
            tk_screen_capture_blit(0u, 0u, 240u, 240u, swapped);
            tk_screen_subscribe_profile(4u, (uint8_t)mode, clock, 0u);
            for (;;) {
                uint32_t remaining = tk_screen.transmitting, tile;
                for (tile = 0; tile < 225u; tile++) remaining |= tk_screen.dirty[tile];
                if (!remaining) break;
                tk_screen_pump(clock, 0u, 1, budget_send); clock++;
            }
        }
        fclose(wire_log);
        if (frame < 53u) return 6;
    }
    puts("PASS actual emitted compact/lossless codec budgets over native HOME snapshots; not hardware timing");
    return 0;
}
