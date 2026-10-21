/* SPDX-License-Identifier: GPL-3.0-only */
/* Main-only virtual panel. A claimed token, ordered frames and a short lease
 * bound input ownership; expiry cancels a pending tap instead of toggling it. */
#ifndef TERKINSTEIN_PANEL_INPUT_H
#define TERKINSTEIN_PANEL_INPUT_H
#include "terkinstein_protocol.h"
#define TK_PANEL_INPUT_LEASE_MS 350u
static struct {
    uint32_t token, next_token, serial, at, held, pressed, released, canceled;
    int32_t encoder[7];
} tk_panel_input;
static void tk_panel_input_cancel(void)
{
    uint32_t i;
    tk_panel_input.canceled |= tk_panel_input.held;
    tk_panel_input.held = tk_panel_input.pressed = tk_panel_input.released = 0u;
    tk_panel_input.token = 0u;
    for (i = 0; i < 7u; i++) tk_panel_input.encoder[i] = 0;
}
static void tk_panel_input_expire(uint32_t now)
{
    if (tk_panel_input.token && now - tk_panel_input.at >= TK_PANEL_INPUT_LEASE_MS)
        tk_panel_input_cancel();
}
static uint32_t tk_panel_input_command(const uint8_t *p, uint32_t n, uint8_t *out)
{
    uint32_t token, serial, advance, mask;
    out[0] = TK_REMOTE_BAD_ARGUMENT;
    if (!n) {
        out[0] = TK_REMOTE_OK; out[1] = 1u; out[2] = 14u; out[3] = 7u;
        out[4] = TK_PANEL_INPUT_LEASE_MS & 127u; out[5] = TK_PANEL_INPUT_LEASE_MS >> 7;
        return 6u;
    }
    if (n == 1u && !p[0]) {
        tk_panel_input_cancel();
        tk_panel_input.next_token = (tk_panel_input.next_token + 1u) & 16383u;
        if (!tk_panel_input.next_token) tk_panel_input.next_token = 1u;
        tk_panel_input.token = tk_panel_input.next_token; tk_panel_input.serial = 0u;
        tk_panel_input.at = fm1_ms;
        out[0] = TK_REMOTE_OK; out[1] = 1u;
        out[2] = (uint8_t)(tk_panel_input.token & 127u); out[3] = (uint8_t)(tk_panel_input.token >> 7);
        return 4u;
    }
    if ((n != 7u || (p[0] != 1u && p[0] != 2u)) && (n != 5u || p[0] != 3u)) return 1u;
    if (p[0] == 2u && (p[5] >= 7u || p[6] < 56u || p[6] > 72u || p[6] == 64u)) return 1u;
    token = p[1] | (uint32_t)p[2] << 7; serial = p[3] | (uint32_t)p[4] << 7;
    tk_panel_input_expire(fm1_ms);
    advance = (serial - tk_panel_input.serial) & 16383u;
    if (!token || token != tk_panel_input.token || !advance || advance >= 8192u) {
        out[0] = TK_REMOTE_BUSY; return 1u;
    }
    tk_panel_input.serial = serial; tk_panel_input.at = fm1_ms;
    if (p[0] == 1u) {
        mask = p[5] | (uint32_t)p[6] << 7;
        tk_panel_input.pressed |= mask & ~tk_panel_input.held;
        tk_panel_input.released |= tk_panel_input.held & ~mask;
        tk_panel_input.held = mask;
    } else if (p[0] == 2u) {
        int32_t next = tk_panel_input.encoder[p[5]] + (int32_t)p[6] - 64;
        tk_panel_input.encoder[p[5]] = next < -32 ? -32 : next > 32 ? 32 : next;
    } else tk_panel_input_cancel();
    out[0] = TK_REMOTE_OK; out[1] = 1u; out[2] = p[0]; out[3] = p[3]; out[4] = p[4];
    return 5u;
}
static int32_t tk_panel_input_encoder(uint32_t index)
{
    int32_t delta = tk_panel_input.encoder[index];
    tk_panel_input.encoder[index] = 0; return delta;
}
#endif
