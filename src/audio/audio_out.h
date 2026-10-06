/*
 * The way out of the sound, under audio.c (the voices, the player, the
 * API): the Pi's HDMI (hdmi_audio.c), the RGB30's I2S and the RK817's
 * codec (src/rgb30/rk_audio.c), QEMU virt's sink (src/rgb30/plat_virt.c).
 * Each starts its output and, from its interrupt, asks audio_render() for
 * the samples it needs.
 */
#ifndef AUDIO_OUT_H
#define AUDIO_OUT_H

#include <stdint.h>

/* Starts the output (silence first). 0 = running, *status says what (e.g.
 * "HDMI 48 kHz"); otherwise *status says why not. */
int  audio_out_start(const char **status);
/* The output's own lines for audio_print() (clocks, counters). */
void audio_out_print(void);
/* From audio_idle(), once a frame while the output runs: what the output
 * has to say outside its interrupt (QEMU's sink: what it heard). */
void audio_out_idle(void);

/* n mono samples at AUDIO_RATE: the player and the voices move by n. For
 * the outputs' interrupts (it is IRQ-safe with the rest of audio.h). */
void audio_render(int16_t *out, unsigned n);

#endif
