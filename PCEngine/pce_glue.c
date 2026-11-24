/* ============================================================================
 *  pce-go と PCEngine.ino のつなぎ(説明は pce_glue.h)
 *  pce-go が呼ぶ osd_* と psg_*(psg.c の代わり。音は pce_sound.cpp が作る)もここに置く
 * ============================================================================ */
#pragma GCC optimize("O3")

#include <stdlib.h>
#include <string.h>
#include "pce-go.h"
#include "pce.h"
#include "pce_glue.h"
#ifdef ARDUINO
#include <esp_heap_caps.h>
#endif

volatile uint32_t pce_line_abs = 0;
uint8_t pce_vga_sync = 0;
volatile uint8_t pce_pad = 0;

void *pce_alloc(size_t size, int internal_first)
{
	void *p = NULL;
#ifdef ARDUINO
	int internal = 0;
	if (internal_first) {
		p = heap_caps_calloc(1, size, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
		internal = p != NULL;
	}
	if (!p)
		p = heap_caps_calloc(1, size, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
	printf("PCE: %u bytes in %s\n", (unsigned)size, internal ? "internal RAM" : "PSRAM");
#else
	(void)internal_first;
	p = calloc(1, size);
#endif
	return p;
}

void pce_psg_queue(uint8_t reg, uint8_t val)
{
	int cpl = PCE.Timer.cycles_per_line > 0 ? PCE.Timer.cycles_per_line : 455;
	int c = PCE.Cycles < 0 ? 0 : PCE.Cycles;
	uint32_t sub = (uint32_t)c * PCE_T_PER_LINE / (uint32_t)cpl;
	if (sub >= PCE_T_PER_LINE) sub = PCE_T_PER_LINE - 1;
	pce_snd_push((pce_line_abs << 10) + sub, reg, val);
}

void pce_psg_restore(void)
{
	pce_psg_queue(1, PCE.PSG.volume);
	for (int c = 0; c < 6; c++) {
		const psg_chan_t *h = &PCE.PSG.chan[c];
		pce_psg_queue(0, (uint8_t)c);
		pce_psg_queue(4, 0x40);
		pce_psg_queue(4, 0x00);
		for (int i = 0; i < 32; i++) pce_psg_queue(6, h->wave_data[i]);
		for (int i = 0; i < (h->wave_index & 31); i++) pce_psg_queue(6, h->wave_data[i]);
		pce_psg_queue(2, h->freq_lsb);
		pce_psg_queue(3, h->freq_msb);
		pce_psg_queue(5, h->balance);
		pce_psg_queue(7, h->noise_ctrl);
		pce_psg_queue(4, h->control);
	}
	pce_psg_queue(0, PCE.PSG.ch);
}

int pce_screen_width(void) { return (int)PCE.VDC.screen_width; }
int pce_screen_height(void) { return (int)PCE.VDC.screen_height; }
uint8_t pce_bg_pix(void) { return PCE.Palette[0]; }

void pce_frame(void)
{
	PCE.Joypad.regs[0] = pce_pad;
	pce_run();
}

void osd_input_read(uint8_t joypads[8])
{
	joypads[0] = pce_pad;
	for (int i = 1; i < 8; i++) joypads[i] = 0;
}

void osd_vsync(void) {}

int psg_init(int samplerate, bool stereo) { (void)samplerate; (void)stereo; return 0; }
void psg_term(void) {}
void psg_update(int16_t *output, size_t length, uint32_t channels) { (void)output; (void)length; (void)channels; }
