/*
 * nano8 sound: the carts' sound effects and music, played from the
 * machine's RAM (music patterns at 0x3100, 64 sound effects of 68 bytes at
 * 0x3200) on 4 channels: 8 waveforms (triangle, tilted saw, saw, square,
 * pulse, organ, noise, phaser), sound effects as instruments, the effects
 * of each note (slide, vibrato, drop, fade in and out, arpeggios), loops.
 * Portable C: audio.c mixes it into the HDMI output in the audio
 * interrupt; the host tests render it to memory.
 */
#ifndef N8SND_H
#define N8SND_H

#include <stdint.h>

/* The RAM to play from (NULL: silent, everything stopped). */
void n8snd_attach(const uint8_t *ram, uint32_t rate);
/* sfx(n, channel, offset, length): n -1 stops the channel (or all with
 * channel -1), -2 releases its loop; channel -1 picks a free one. */
void n8snd_sfx(int n, int ch, int offset, int length);
/* music(n, fade_ms, channel_mask): n -1 stops (fading out over fade_ms). */
void n8snd_music(int n, int fade_ms, int mask);
/* stat(16..26, 46..57) */
int  n8snd_stat(int n);
/* Adds n samples to out (mono, 16 bits), scaled by gain (0..1). */
void n8snd_mix(int16_t *out, unsigned n, float gain);
/* A paused game: no sound, positions kept. */
void n8snd_pause(int on);

#endif
