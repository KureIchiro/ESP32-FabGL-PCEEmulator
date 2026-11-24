#pragma once
#include <stdint.h>
#include <stddef.h>

#define PCE_LINES_PER_FRAME 263
#define PCE_T_PER_LINE 1024u

#ifdef __cplusplus
extern "C" {
#endif
extern volatile uint32_t pce_line_abs;
extern uint8_t pce_vga_sync;
extern volatile uint8_t pce_pad;
void *pce_alloc(size_t size, int internal_first);
void pce_psg_queue(uint8_t reg, uint8_t val);
void pce_psg_restore(void);
int  pce_screen_width(void);
int  pce_screen_height(void);
uint8_t pce_bg_pix(void);
void pce_frame(void);

void pce_line_done(int scanline);

extern volatile uint32_t pce_emu_time;
extern volatile bool pce_sound_mute;
void pce_snd_push(uint32_t t, uint8_t reg, uint8_t val);
void pceSoundBegin(int rate, uint32_t frameUs);
void pceSoundFill(void);
int  pceSoundGetSample(void);
#ifdef __cplusplus
}
#endif
