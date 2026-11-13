/* The RGB30's sound output (rk_audio.c): the I2S1 and the RK817's codec. */
#ifndef RK_AUDIO_H
#define RK_AUDIO_H

/* Before a restart or the power off: the amplifier off, the I2S stopped. */
void rk_audio_off(void);

#endif
