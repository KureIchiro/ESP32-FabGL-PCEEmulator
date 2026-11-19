// gfx.c - VDC/VCE Emulation
//
/* 2026-11-12 (GPLv3 section 5a) PCEngine for TTGO VGA32: speed
 *  - This file is compiled with O3 (the Arduino core for the ESP32 uses -Os).
 *  - The four bit planes of a pixel row are merged with a 256-entry table (plane_lut, 1KB of internal RAM)
 *    instead of 16 mask-and-shift steps per byte. The result (L) has the same layout as before.
 *  - The sprites that can be on the screen are decoded once after each SATB transfer (spr_list), so the
 *    64 entries are not decoded again at every call of draw_sprites.
 *  - PCE_BAND_RENDER: the lines waiting to be drawn are flushed every 16 lines (on a tile row boundary)
 *    instead of all at once at line 256. The frame buffer is in PSRAM behind a 32KB cache: a whole
 *    frame does not fit, so each pass (clear, sprites, tiles, sprites) loaded and wrote back every line
 *    again. With a band of 16 lines all passes stay in the cache. Nothing is drawn differently as long as
 *    the game does not change VRAM or the palette in the middle of the frame (then the band shows the
 *    state at its own time, as the real machine does, instead of the state at line 256). */
#pragma GCC optimize("O3")

#include <stdlib.h>
#include <string.h>
#include "pce.h"
#include "gfx.h"

#define PAL(nibble) (PAL[(L >> ((nibble) * 4)) & 15])

#define V_FLIP  0x8000
#define H_FLIP  0x0800

static int last_line_counter = 0;
static int line_counter = 0;

static struct {
	int scroll_x;
	int scroll_y;
	int control;
	int latched;
} gfx_context;

static uint8_t *framebuffer_top, *framebuffer_bottom;

#ifndef PCE_BAND_RENDER
#define PCE_BAND_RENDER 1
#endif

static uint32_t plane_lut[256];

typedef struct {
	int16_t y, x;
	uint16_t no;
	uint16_t attr;
} spr_ent_t;
static spr_ent_t spr_list[64];
static uint8_t spr_count[2];
static uint8_t spr_dirty = 1;

/* 2026-11-12 (GPLv3 section 5a) PCEngine for TTGO VGA32: the drawing functions run from internal RAM (IRAM) */
#ifdef ARDUINO
#include <esp_attr.h>
#define PCE_GFX_FAST IRAM_ATTR
#else
#define PCE_GFX_FAST
#endif

/* 2026-11-13 (GPLv3 section 5a) PCEngine for TTGO VGA32: the lines can be drawn on the other CPU core.
 * render_lines() (the emulation, core 0) does not draw by itself any more: it fills rjob with what the drawing
 * reads besides VRAM and the sprite list (the lines, the scroll / control registers latched as before, the
 * screen width, MWR, the palette), then either draws it at once (pce_render_async = 0: the same as before) or
 * leaves it to gfx_render_service() on the other core (pce_render_async = 1).
 * One job at a time: a new job waits until the other core has finished the previous one, and the sketch calls
 * gfx_render_wait() before a frame starts (the frame buffer is copied to the VGA lines during the next frame).
 * The sprite list is built from SPRAM when the job is made, so the SATB DMA may change SPRAM while the other
 * core draws. VRAM is read when the job is drawn: a VRAM write made during the next lines can show in the lines
 * drawn just before (upstream draws the waiting lines only when the registers change, so it already reads VRAM
 * later than the lines it draws).
 */
typedef struct {
	int min_line, max_line;
	int scroll_x, scroll_y, control;
	int screen_width;
	int mwr;
	int fb_height;
	const uint8_t *pal;
	uint8_t pal_copy[512];
} render_job_t;
static render_job_t rjob;

int pce_render_async = 0;
volatile int pce_render_pending = 0;

/*
	Draw background tiles between two lines
*/
static void PCE_GFX_FAST
draw_tiles(uint8_t *screen_buffer, int Y1, int Y2, int scroll_x, int scroll_y)
{
	TRACE_GFX("Rendering tiles on lines %3d - %3d\tScroll: (%3d,%3d)\n", Y1, Y2, scroll_x, scroll_y);

	uint32_t _bg_w[] = { 32, 64, 128, 128 };
	uint32_t _bg_h[] = { 32, 64 };
	const uint16_t *const vram = PCE.VRAM;
	const uint32_t *const lut = plane_lut;
	uint8_t *const fb_top = framebuffer_top, *const fb_bottom = framebuffer_bottom;

	const uint8_t *const pal = rjob.pal;

	uint32_t bg_w = _bg_w[(rjob.mwr >> 4) & 3]; // Bits 5-4 select the width
	uint32_t bg_h = _bg_h[(rjob.mwr >> 6) & 1]; // Bit 6 selects the height

	int num_tiles = rjob.screen_width / 8 + 1;
	int x;
	int y = Y1 + scroll_y;
	int offset = y & 7;
	int h = MIN(8 - offset, Y2 - Y1);

	y >>= 3;

	uint8_t *PP = (screen_buffer + XBUF_WIDTH * Y1) - (scroll_x & 7);

	for (int line = Y1; line < Y2; y++) {
		x = scroll_x / 8;
		y &= bg_h - 1;
		for (int n = 0; n < num_tiles; n++, x++, PP += 8) {
			x &= bg_w - 1;

			int no = vram[x + y * bg_w];

			const uint8_t *PAL = &pal[(no >> 8) & 0x1F0];
			const uint8_t *C = (const uint8_t*)(vram + (no & 0x7FF) * 16 + offset);
			uint8_t *P = PP;

			for (int i = 0; i < h; i++, P += XBUF_WIDTH, C += 2) {
				uint32_t J, L;

				J = C[0] | C[1] | C[16] | C[17];

				if (!J)
					continue;

				if (P + 8 >= fb_bottom) {
					MESSAGE_DEBUG("tile overflow!\n");
					break;
				} else if (P < fb_top) {
					MESSAGE_DEBUG("tile underflow!\n");
					continue;
				}

				L = lut[C[0]] | (lut[C[1]] << 1) | (lut[C[16]] << 2) | (lut[C[17]] << 3);

				if (J & 0x80) P[0] = PAL(1);
				if (J & 0x40) P[1] = PAL(3);
				if (J & 0x20) P[2] = PAL(5);
				if (J & 0x10) P[3] = PAL(7);
				if (J & 0x08) P[4] = PAL(0);
				if (J & 0x04) P[5] = PAL(2);
				if (J & 0x02) P[6] = PAL(4);
				if (J & 0x01) P[7] = PAL(6);
			}
		}
		line += h;
		PP += XBUF_WIDTH * h - num_tiles * 8;
		offset = 0;
		h = MIN(8, Y2 - line);
	}
}


/*
	Draw sprite C to framebuffer P
*/
static void PCE_GFX_FAST
draw_sprite(uint8_t *P, const uint16_t *C, int height, uint32_t attr)
{
	const uint8_t *PAL = &rjob.pal[256 + ((attr & 0xF) << 4)];
	const uint32_t *const lut = plane_lut;
	uint8_t *const fb_top = framebuffer_top, *const fb_bottom = framebuffer_bottom;

	bool hflip = attr & H_FLIP;
	int inc = 1; //(attr & V_FLIP) ? -1 : 1;

	if (attr & V_FLIP) {
		inc = -1;
		C = C + height - 1;
	}

	for (int i = 0; i < height; i++, C += inc, P += XBUF_WIDTH) {

		uint32_t J = C[0] | C[16] | C[32] | C[48];
		uint32_t L1, L2, L;

		if (!J)
			continue;

		// This will also need to be handled in draw_sprites... (it could adjust simply constrain the height)
		if (P + 16 >= fb_bottom) {
			MESSAGE_DEBUG("sprite overflow %d!\n", i);
			break;
		} else if (P < fb_top) {
			MESSAGE_DEBUG("sprite underflow %d!\n", i);
			continue;
		}

		{
			const uint32_t M0 = C[0], M1 = C[16], M2 = C[32], M3 = C[48];
			L1 = lut[M0 & 0xFF] | (lut[M1 & 0xFF] << 1) | (lut[M2 & 0xFF] << 2) | (lut[M3 & 0xFF] << 3);
			L2 = lut[M0 >> 8] | (lut[M1 >> 8] << 1) | (lut[M2 >> 8] << 2) | (lut[M3 >> 8] << 3);
		}

		if (hflip) {
			L = L2;
			if ((J & 0x8000)) P[15] = PAL(1);
			if ((J & 0x4000)) P[14] = PAL(3);
			if ((J & 0x2000)) P[13] = PAL(5);
			if ((J & 0x1000)) P[12] = PAL(7);
			if ((J & 0x0800)) P[11] = PAL(0);
			if ((J & 0x0400)) P[10] = PAL(2);
			if ((J & 0x0200)) P[9]  = PAL(4);
			if ((J & 0x0100)) P[8]  = PAL(6);

			L = L1;
			if ((J & 0x80)) P[7] = PAL(1);
			if ((J & 0x40)) P[6] = PAL(3);
			if ((J & 0x20)) P[5] = PAL(5);
			if ((J & 0x10)) P[4] = PAL(7);
			if ((J & 0x08)) P[3] = PAL(0);
			if ((J & 0x04)) P[2] = PAL(2);
			if ((J & 0x02)) P[1] = PAL(4);
			if ((J & 0x01)) P[0] = PAL(6);
		} else {
			L = L2;
			if ((J & 0x8000)) P[0] = PAL(1);
			if ((J & 0x4000)) P[1] = PAL(3);
			if ((J & 0x2000)) P[2] = PAL(5);
			if ((J & 0x1000)) P[3] = PAL(7);
			if ((J & 0x0800)) P[4] = PAL(0);
			if ((J & 0x0400)) P[5] = PAL(2);
			if ((J & 0x0200)) P[6] = PAL(4);
			if ((J & 0x0100)) P[7] = PAL(6);

			L = L1;
			if ((J & 0x80)) P[8]  = PAL(1);
			if ((J & 0x40)) P[9]  = PAL(3);
			if ((J & 0x20)) P[10] = PAL(5);
			if ((J & 0x10)) P[11] = PAL(7);
			if ((J & 0x08)) P[12] = PAL(0);
			if ((J & 0x04)) P[13] = PAL(2);
			if ((J & 0x02)) P[14] = PAL(4);
			if ((J & 0x01)) P[15] = PAL(6);
		}
	}
}


static void
build_sprite_list(void)
{
	int n0 = 0, n1 = 0;

	for (int n = 63; n >= 0; n--) {
		const sprite_t *spr = &PCE.SPRAM[n];
		uint32_t attr = spr->attr;
		int y = (spr->y & 0x3FF) - 64;
		int x = (spr->x & 0x3FF) - 32;
		int cgx = (attr >> 8) & 1;
		int cgy = (attr >> 12) & 3;
		int no = (spr->no & 0x7FF);

		cgy |= cgy >> 1;

		no = (no >> 1) & ~(cgy * 2 + cgx);
		no &= 0x1FF; // PCE has max of 512 sprites

		if (y >= 256 || y + (cgy + 1) * 16 < 0 || x + (cgx + 1) * 16 < 0) {
			continue;
		}

		spr_ent_t *e = ((attr >> 7) & 1) ? &spr_list[63 - n1++] : &spr_list[n0++];
		e->y = y;
		e->x = x;
		e->no = no;
		e->attr = attr;
	}
	spr_count[0] = n0;
	spr_count[1] = n1;
	spr_dirty = 0;
}


/*
	Draw sprites between two lines
*/
static void PCE_GFX_FAST // Do not inline
draw_sprites(uint8_t *screen_buffer, int Y1, int Y2, int priority)
{
	TRACE_GFX("Rendering sprites on lines %3d - %3d\tPriority: %d\n", Y1, Y2, priority);

	// NOTE: At this time we do not respect bg sprites priority over top sprites.
	// Example: Assume that sprite #2 is priority=0 and sprite #5 is priority=1. If they
	// overlap then sprite #5 shouldn't be drawn because #2 > #5. But currently it will.

	// We iterate sprites in reverse order because earlier sprites have
	// higher priority and therefore must overwrite later sprites.

	const int screen_width = rjob.screen_width;
	const uint16_t *const vram = PCE.VRAM;
	const spr_ent_t *e = priority ? &spr_list[63] : &spr_list[0];
	const int step = priority ? -1 : 1;

	for (int count = spr_count[priority]; count > 0; count--, e += step) {
		uint32_t attr = e->attr;
		int y = e->y;
		int x = e->x;
		int cgx = (attr >> 8) & 1;
		int cgy = (attr >> 12) & 3;
		int no = e->no;

		cgy |= cgy >> 1;

		TRACE_SPR("Sprite : X = %d, Y = %d, attr = %d, no = %d\n", x, y, attr, no);

		// Sprite is completely outside our window, skip it
		if (y >= Y2 || y + (cgy + 1) * 16 < Y1 || x >= screen_width || x + (cgx + 1) * 16 < 0) {
			continue;
		}

		cgy *= 16;

		uint8_t *P = screen_buffer + (y * XBUF_WIDTH) + x;
		const uint16_t *C = vram + (no * 64);

		for (int yy = 0; yy <= cgy; yy += 16, P += 16 * XBUF_WIDTH) {
			int top = y + yy;
			int t = Y1 - top;
			if (t < 0) {
				t = 0;
			}
			int height = MIN(16, Y2 - top) - t;

			if (height > 0) {
				const uint16_t *cell;
				if (attr & V_FLIP) {
					cell = C + (cgy - yy) * 8 + (16 - t - height);
				} else {
					cell = C + yy * 8 + t;
				}
				uint8_t *row = P + t * XBUF_WIDTH;
				for (int j = 0; j <= cgx; j++) {
					draw_sprite(row + (attr & H_FLIP ? cgx - j : j) * 16, cell + j * 64, height, attr);
				}
			}
		}
	}
}


/*
	Hit Check Sprite#0 and others
*/
static inline bool
sprite_hit_check(void)
{
	const sprite_t *spr = &PCE.SPRAM[0];
	int x0 = spr->x;
	int y0 = spr->y;
	int w0 = (((spr->attr >> 8) & 1) + 1) * 16;
	int h0 = (((spr->attr >> 12) & 3) + 1) * 16;

	spr++;

	for (int i = 1; i < 64; i++, spr++) {
		int x = spr->x;
		int y = spr->y;
		int w = (((spr->attr >> 8) & 1) + 1) * 16;
		int h = (((spr->attr >> 12) & 3) + 1) * 16;
		if ((x < x0 + w0) && (x + w > x0) && (y < y0 + h0) && (y + h > y0))
			return 1;
	}
	return 0;
}


void
gfx_latch_context(int force)
{
	if (!gfx_context.latched || force) { // Context is already saved + we haven't render the line using it
		gfx_context.scroll_x = IO_VDC_REG[BXR].W;
		gfx_context.scroll_y = IO_VDC_REG[BYR].W - PCE.ScrollYDiff;
		gfx_context.control = IO_VDC_REG[CR].W;
		gfx_context.latched = 1;
	}
}


/*
	Render lines into the buffer from min_line to max_line (inclusive)
*/
/* 2026-11-12 (GPLv3 section 5a) PCEngine for TTGO VGA32: pce_render_skip = 1 skips drawing for a
   frame that started late (PCEngine.ino); the VDC state is updated the same way */
int pce_render_skip = 0;

static inline __attribute__((always_inline)) void
render_range(const render_job_t *j, uint8_t *screen_buffer, int min_line, int max_line)
{
	// We must fill the region with color 0 first.
	size_t screen_width = j->screen_width;
	for (int y = min_line; y <= max_line; y++) {
		memset(screen_buffer + (y * XBUF_WIDTH), j->pal[0], screen_width);
	}

	// Sprites with priority 0 are drawn behind the tiles
	if (j->control & 0x40) {
		draw_sprites(screen_buffer, min_line, max_line, 0);
	}

	// Draw the background tiles
	if (j->control & 0x80) {
		draw_tiles(screen_buffer, min_line, max_line, j->scroll_x, j->scroll_y);
	}

	// Draw regular sprites
	if (j->control & 0x40) {
		draw_sprites(screen_buffer, min_line, max_line, 1);
	}
}

/* 2026-11-17 (GPLv3 section 5a) PCEngine for TTGO VGA32: the band buffer in internal RAM (2026-11-19: the only way
   of drawing; the frame buffer in PSRAM, copied to the VGA lines one frame later, was removed).
   PCE_BAND_LINES lines and the line after them (render_range clears it), with 16 bytes of scratch before and
   after (tiles and sprites may start left of the line). A longer band is drawn in pieces of PCE_BAND_LINES. */
#define PCE_BAND_LINES 24
static uint8_t band_buf[16 + (PCE_BAND_LINES + 1) * XBUF_WIDTH + 16] __attribute__((aligned(4)));

static void PCE_GFX_FAST
render_job_exec(const render_job_t *j)
{
	for (int a = j->min_line; a < j->max_line; a += PCE_BAND_LINES) {
		const int b = MIN(a + PCE_BAND_LINES, j->max_line);
		uint8_t *screen_buffer = band_buf + 16 - a * XBUF_WIDTH;
		framebuffer_top = band_buf;
		framebuffer_bottom = band_buf + 16 + (b - a + 1) * XBUF_WIDTH;
		render_range(j, screen_buffer, a, b);
		pce_band_done(screen_buffer, a, b, j->screen_width, j->fb_height);
	}
}

void
gfx_render_wait(void)
{
#ifdef PCE_RENDER_SIM
	gfx_render_service();
#endif
	while (pce_render_pending) {
	}
}

void PCE_GFX_FAST
gfx_render_service(void)
{
	if (pce_render_pending) {
		render_job_exec(&rjob);
		pce_render_pending = 0;
	}
}

static inline void
render_lines(int min_line, int max_line)
{
	gfx_context.latched = 0;
	if (pce_render_skip)
		return;

	render_job_t *j = &rjob;
	gfx_render_wait();
	if (spr_dirty && (gfx_context.control & 0x40)) {
		build_sprite_list();
	}
	j->min_line = min_line;
	j->max_line = max_line;
	j->scroll_x = gfx_context.scroll_x;
	j->scroll_y = gfx_context.scroll_y;
	j->control = gfx_context.control;
	j->screen_width = IO_VDC_SCREEN_WIDTH;
	j->mwr = IO_VDC_REG[MWR].W;
	j->fb_height = PCE.VDC.screen_height;
	if (pce_render_async) {
		memcpy(j->pal_copy, PCE.Palette, sizeof(j->pal_copy));
		j->pal = j->pal_copy;
		pce_render_pending = 1;
	} else {
		j->pal = PCE.Palette;
		render_job_exec(j);
	}
}


int
gfx_init(void)
{
	for (int b = 0; b < 256; b++) {
		plane_lut[b] = ((b & 0x88) >> 3) | ((b & 0x44) << 6) | ((b & 0x22) << 15) | ((b & 0x11) << 24);
	}
	gfx_reset(true);
	return 0;
}


void
gfx_reset(bool hard)
{
	gfx_render_wait();
	last_line_counter = 0;
	line_counter = 0;
	spr_dirty = 1;
}


void
gfx_term(void)
{
	//
}


/*
	Raises a VDC IRQ and/or process pending VDC IRQs.
	More than one interrupt can happen in a single line on real hardware and the cpu
	would usually receive them one by one. We use an uint32 as a 8 slot buffer.
*/
/* 2026-11-17 (GPLv3 section 5a) PCEngine for TTGO VGA32: gfx_irq and gfx_run (once per line) run from IRAM */
void PCE_GFX_FAST
gfx_irq(int type)
{
	/* If IRQ, push it on the stack */
	if (type >= 0) {
		PCE.VDC.pending_irqs <<= 4;
		PCE.VDC.pending_irqs |= type & 0xF;
	}

	/* Pop the first pending vdc interrupt only if CPU.irq_lines is clear */
	int pos = 28;
	while (!(CPU.irq_lines & INT_IRQ1) && PCE.VDC.pending_irqs) {
		if (PCE.VDC.pending_irqs >> pos) {
			PCE.VDC.status |= 1 << (PCE.VDC.pending_irqs >> pos);
			PCE.VDC.pending_irqs &= ~(0xF << pos);
			CPU.irq_lines |= INT_IRQ1; // Notify the CPU
		}
		pos -= 4;
	}
}


/*
	Process one scanline
*/
void PCE_GFX_FAST
gfx_run(void)
{
	int scanline = PCE.Scanline;

	/* DMA Transfer in "progress" */
	if (PCE.VDC.satb > DMA_TRANSFER_COUNTER) {
		if (--PCE.VDC.satb == DMA_TRANSFER_COUNTER) {
			if (SATBIntON) {
				gfx_irq(VDC_STAT_DS);
			}
		}
	}

	/* Test raster hit */
	if (RasHitON) {
		if (IO_VDC_REG[RCR].W >= 0x40 && (IO_VDC_REG[RCR].W <= 0x146)) {
			uint16_t temp_rcr = (uint16_t)(IO_VDC_REG[RCR].W - 0x40);
			if (scanline == (temp_rcr + IO_VDC_MINLINE) % 263) {
				TRACE_GFX("\n-----------------RASTER HIT (%d)------------------\n", scanline);
				gfx_irq(VDC_STAT_RR);
			}
		}
	}

	/* Visible area */
	if (scanline >= 14 && scanline <= 255) {
		if (scanline == IO_VDC_MINLINE) {
			gfx_latch_context(1);
		}

		if (scanline >= IO_VDC_MINLINE && scanline <= IO_VDC_MAXLINE) {
			if (gfx_context.latched) {
				render_lines(last_line_counter, line_counter);
				last_line_counter = line_counter;
			}
#if PCE_BAND_RENDER
			else {
				int waiting = line_counter - last_line_counter;
				if (waiting >= 8 && (waiting >= 24
						|| ((line_counter + IO_VDC_REG[BYR].W - PCE.ScrollYDiff) & 15) == 0)) {
					gfx_latch_context(0);
					render_lines(last_line_counter, line_counter);
					last_line_counter = line_counter;
				}
			}
#endif
			line_counter++;
		}
	}
	/* V Blank trigger line */
	else if (scanline == 256) {

		// Draw any lines left in the context
		gfx_latch_context(0);
		render_lines(last_line_counter, line_counter);

		// Trigger interrupts
		if (SpHitON && sprite_hit_check()) {
			gfx_irq(VDC_STAT_CR);
		}
		if (VBlankON) {
			gfx_irq(VDC_STAT_VD);
		}

		/* VRAM to SATB DMA */
		if (PCE.VDC.satb == DMA_TRANSFER_PENDING || AutoSATBON) {
				memcpy(PCE.SPRAM, PCE.VRAM + IO_VDC_REG[SATB].W, 512);
			spr_dirty = 1;
			PCE.VDC.satb = DMA_TRANSFER_COUNTER + 4;
		}
	}
	/* V Blank area */
	else {
		gfx_context.latched = 0;
		last_line_counter = 0;
		line_counter = 0;
		PCE.ScrollYDiff = 0;
	}

	/* Always call at least once (to handle pending IRQs) */
	gfx_irq(-1);
}
