/* ACT LED on Pi Zero / Zero W: GPIO 47, active low. */
#include "led.h"
#include "mmio.h"
#include "timer.h"

#define GPIO_BASE   (PERIPHERAL_BASE + 0x200000)
#define GPFSEL4     (GPIO_BASE + 0x10)
#define GPSET1      (GPIO_BASE + 0x20)
#define GPCLR1      (GPIO_BASE + 0x2C)

#define LED_PIN     47
#define LED_BIT     (1u << (LED_PIN - 32))

void led_init(void)
{
    uint32_t sel = mmio_read(GPFSEL4);
    sel &= ~(7u << 21);             /* GPIO47 -> FSEL4 bits 23:21 */
    sel |=  (1u << 21);             /* output */
    mmio_write(GPFSEL4, sel);
    led_set(0);
}

void led_set(int on)
{
    mmio_write(on ? GPCLR1 : GPSET1, LED_BIT);
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
