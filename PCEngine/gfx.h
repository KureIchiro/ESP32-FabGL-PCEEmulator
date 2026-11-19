#pragma once

#include <stdbool.h>
#include <stdint.h>

int gfx_init(void);
void gfx_run(void);
void gfx_term(void);
void gfx_irq(int type);
void gfx_reset(bool hard);
void gfx_latch_context(int force);

extern int pce_render_async;
extern volatile int pce_render_pending;
void gfx_render_wait(void);
void gfx_render_service(void);

/* 2026-11-17 (GPLv3 section 5a) PCEngine for TTGO VGA32: every band is drawn into a small buffer in internal RAM
   and handed at once to pce_band_done(), which writes it to the VGA lines (the picture is shown in the same frame).
   Lines y0..y1-1 of the frame are drawn: line y is at buf + y * XBUF_WIDTH (width, height: the screen size) */
void pce_band_done(const uint8_t *buf, int y0, int y1, int width, int height);
