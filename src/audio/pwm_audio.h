/*
 * BCM2835 PWM analog stereo audio (3.5mm jack on Raspberry Pi 1 Model B/B+).
 * Driven by DMA paced by DREQ 5 (PWM DREQ) into PWM FIFO at 48 kHz.
 * Pinout: GPIO 40 (PWM0, Left) and GPIO 45 (PWM1, Right) in ALT0 mode.
 * Pins 0-27 on the GPIO header are untouched (no conflict with LCD hats).
 */
#ifndef PWM_AUDIO_H
#define PWM_AUDIO_H

int  pwm_audio_start(const char **status);
void pwm_audio_print(void);
void pwm_audio_idle(void);

#endif
