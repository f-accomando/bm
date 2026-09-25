#ifndef LED_H
#define LED_H

void led_init(void);
void led_set(int on);
void led_blink_forever(unsigned on_ms, unsigned off_ms) __attribute__((noreturn));
/* Repeats n short blinks followed by a pause, forever. */
void led_blink_code(unsigned n) __attribute__((noreturn));

#endif
