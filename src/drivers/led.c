/* ACT LED: GPIO 47 active low on the Pi Zero / Zero W, GPIO 16 active low
 * on the first Pi 1 A/B, GPIO 47 active high on the Pi 1 A+/B+, GPIO 29
 * active low on the Pi Zero 2 W (board.c). */
#include "led.h"
#include "board.h"
#include "gpio.h"
#include "timer.h"

static uint8_t led_pin, led_high;

/* At the very start: GPIO 47 active low (Pi Zero / Zero W; kernel7.img:
 * GPIO 29, the Zero 2 W), without asking the firmware (its first answers
 * are not the board's revision on a real Zero W). led_init_board() then
 * moves it where the board has it. */
static int led_ready;

static void led_use(uint8_t pin, uint8_t high)
{
    led_pin = pin;
    led_high = high;
    if (led_pin)
        gpio_set_function(led_pin, GPIO_OUTPUT);
}

void led_init(void)
{
    if (!led_ready)
        led_use(BOARD_LED_PIN, 0);
    led_set(0);
}

void led_init_board(void)
{
    const board_t *b = board();
    int on = led_pin ? (gpio_read(led_pin) ? led_high : !led_high) : 0;
    if (b->led_pin != led_pin || b->led_active_high != led_high) {
        if (led_pin)
            gpio_write(led_pin, !led_high);         /* the old pin off */
        led_use(b->led_pin, b->led_active_high);
    }
    led_ready = 1;
    led_set(on);
}

void led_set(int on)
{
    if (led_pin)
        gpio_write(led_pin, led_high ? on : !on);
}

void led_blink_forever(unsigned on_ms, unsigned off_ms)
{
    for (;;) {
        led_set(1);
        timer_delay_ms(on_ms);
        led_set(0);
        timer_delay_ms(off_ms);
    }
}

void led_blink_code(unsigned n)
{
    for (;;) {
        for (unsigned i = 0; i < n; i++) {
            led_set(1);
            timer_delay_ms(200);
            led_set(0);
            timer_delay_ms(300);
        }
        timer_delay_ms(1500);
    }
}
