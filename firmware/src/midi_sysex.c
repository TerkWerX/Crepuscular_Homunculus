/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Huegelton Instruments */
/* USB SysEx collector from usb.c, kept portable for updater/remote isolation tests.
 * Uses the enclosing USB driver state and IRQ/ring helpers; no DSP callbacks. */
/* SysEx assembly: only short commands matter here */
static void sysex_byte(uint8_t b)
{
    static const uint8_t UBOOT_KEY[6] = {0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7};
    if (b >= 0xF8u)
        return;                                        /* realtime may occur anywhere in SysEx */
    if ((b & 0x80u) && b != 0xF0u && b != 0xF7u) {
        usb.sx_on = 0;
#if FELUCCA_OTA
        sx_collect = 0;
#endif
        return;                                        /* another status aborts the unfinished frame */
    }
    if (b == 0xF0) {
        usb.sx_on = 1;
        usb.sx_len = 0;
#if FELUCCA_OTA
        sx_collect = !sx_ready;
        sx_pos = 0;
#endif
    }
    if (!usb.sx_on)
        return;
#if FELUCCA_OTA
    if (sx_collect && b != 0xF0 && b != 0xF7) {
        if (sx_pos < sizeof sx_frame)
            sx_frame[sx_pos++] = b;
        else
            sx_collect = 0;                             /* too long: not ours */
    }
#endif
    if (usb.sx_len < sizeof usb.sysex)
        usb.sysex[usb.sx_len++] = b;
    if (b == 0xF7) {
        uint32_t i, ok = usb.sx_len == 6u;
        for (i = 0; ok && i < 6u; i++)
            ok = usb.sysex[i] == UBOOT_KEY[i];
        if (ok)
            usb.uboot_req = 1;
#if FELUCCA_OTA
        else if (usb.sx_len == 6u && usb.sysex[1] == 0x22 && usb.sysex[2] == 0x24 && usb.sysex[3] == 0x35 &&
                 usb.sysex[4] == 0x7F)
            usb.ota_req = 1;
        else if (sx_collect && sx_pos) {
#if FELUCCA_REMOTE && !defined(FELUCCA_LOADER)
            if (tk_remote_namespace(sx_frame, sx_pos)) {
                /* This namespace can never enter ota_take(), even with a bad
                 * version/checksum. One pending request; the bridge retries. */
                if (!usb_remote_ready && sx_pos <= sizeof usb_remote_frame) {
                    for (i = 0; i < sx_pos; i++) usb_remote_frame[i] = sx_frame[i];
                    usb_remote_length = sx_pos;
                    RING_PUBLISH();
                    usb_remote_ready = 1;
                }
            } else
#endif
            {
            sx_frame_len = sx_pos;
            RING_PUBLISH();
            sx_ready = 1;                               /* main loop: ota_take() / ed_service() */
            }
        }
        sx_collect = 0;
#endif
        usb.sx_on = 0;
    }
}

#if FELUCCA_REMOTE && !defined(FELUCCA_LOADER)
static void usb_remote_reset(void)
{
    usb_remote_ready = 0; usb_remote_length = 0; usb_remote_generation++;
}
static int usb_remote_get(const uint8_t **p, uint32_t *n)
{
    if (!usb_remote_ready) return 0;
    RING_PUBLISH();
    *p = usb_remote_frame; *n = usb_remote_length;
    return 1;
}
static void usb_remote_done(void)
{
    RING_PUBLISH();
    usb_remote_ready = 0;
}
/* Nonblocking, all-or-nothing enqueue. Only main produces SysEx output; the
 * USB ISR consumes it. A reset during preparation cancels publication. */
static int usb_remote_send(const uint8_t *p, uint32_t n)
{
    uint32_t count = tk_remote_packet_count(n), write = so_w, generation = usb_remote_generation;
    uint32_t i, flags;
    if (!count || !p || p[0] != 0xF0u || p[n - 1u] != 0xF7u || !usb.config
        || sx_busy || sx_open || SXQ - (write - so_r) < count) return 0;
    for (i = 0; i < count; i++) sx_out_q[(write + i) % SXQ] = tk_remote_packet(p, n, i * 3u);
    flags = irq_save();
    if (!usb.config || generation != usb_remote_generation) {
        irq_restore(flags);
        return 0;
    }
    RING_PUBLISH();
    so_w = write + count;
    irq_restore(flags);
    return 1;
}
/* Display traffic must not keep the SysEx-first TX path occupied indefinitely.
 * Admit one frame only after prior SysEx and generated musical events drain.
 * A note arriving during admission waits for at most this one bounded frame. */
static int usb_remote_screen_send(const uint8_t *p, uint32_t n)
{
    if (so_w != so_r || mo_w != mo_r) return 0;
    return usb_remote_send(p, n);
}
#endif
