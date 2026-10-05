/*
 * QEMU's virt machine has no sound device: this sink is the output of
 * audio.c there (audio_out.h), so the tests play the RGB30's sound path
 * whole. It takes the samples at 48 kHz as the I2S of rk_audio.c does,
 * paced by the EL1 virtual timer (PPI 27): a chunk from audio_render() in
 * its interrupt every 5.3 ms. It measures what it hears, one second at a
 * time: the pitch (rising zero crossings), the loudest sample and the
 * samples it took in that second of real time, and while there is sound
 * says it once a second from audio_idle() ("audio: heard 440 Hz ...").
 */
#ifdef PLAT_VIRT
#include "audio/audio.h"
#include "audio/audio_out.h"
#include "drivers/timer.h"
#include "kernel/irq.h"
#include "lib/printf.h"
#include "a64.h"

#define IRQ_VTIMER  27
#define LOUD        1000                /* below it, silence */

static int16_t pcm[AUDIO_CHUNK];
static uint64_t period, next;
static volatile uint32_t chunks, late;

/* the second being measured, and the last one with sound */
static uint32_t frames, crossings, peak, win_t0;
static int16_t prev;
static volatile int heard;
static volatile uint32_t heard_hz, heard_peak, heard_rate;

static void measure(const int16_t *s, unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        if (prev < 0 && s[i] >= 0)
            crossings++;
        prev = s[i];
        uint32_t a = (uint32_t)(s[i] < 0 ? -s[i] : s[i]);
        if (a > peak)
            peak = a;
    }
    frames += n;
    if (frames < AUDIO_RATE)
        return;
    uint32_t now = timer_ticks();
    if (peak >= LOUD && !heard) {
        heard_hz = crossings;
        heard_peak = peak;
        heard_rate = (uint32_t)((uint64_t)frames * 1000000u / (now - win_t0 ? now - win_t0 : 1));
        heard = 1;
    }
    frames = crossings = peak = 0;
    win_t0 = now;
}

static void vtimer_irq(void *arg)
{
    (void)arg;
    uint64_t now = read_sysreg(cntvct_el0);
    int n = 0;
    do {                                /* a chunk for each period gone by, a few at most */
        audio_render(pcm, AUDIO_CHUNK);
        measure(pcm, AUDIO_CHUNK);
        chunks++;
        next += period;
    } while (now >= next && ++n < 4);
    if (now >= next) {
        late++;
        next = now + period;            /* far behind: not caught up */
    }
    write_sysreg(cntv_cval_el0, next);
}

int audio_out_start(const char **status)
{
    uint64_t hz = read_sysreg(cntfrq_el0);
    period = hz * AUDIO_CHUNK / AUDIO_RATE;
    win_t0 = timer_ticks();
    irq_register(IRQ_VTIMER, vtimer_irq, 0);
    next = read_sysreg(cntvct_el0) + period;
    write_sysreg(cntv_cval_el0, next);
    write_sysreg(cntv_ctl_el0, 1);      /* enabled, not masked */
    irq_enable(IRQ_VTIMER);
    uint32_t t0 = timer_ticks();
    while (chunks < 2 && timer_ticks() - t0 < 100000)
        ;
    if (chunks < 2) {
        irq_disable(IRQ_VTIMER);
        write_sysreg(cntv_ctl_el0, 0);
        *status = "no interrupt from the virtual timer";
        return -1;
    }
    *status = "QEMU sink, 48 kHz";
    return 0;
}

void audio_out_print(void)
{
    kprintf("       sink: %lu chunks, %lu late\n", chunks, late);
}

void audio_out_idle(void)
{
    if (!heard)
        return;
    kprintf("audio: heard %lu Hz, peak %lu, %lu samples/s\n", heard_hz, heard_peak, heard_rate);
    heard = 0;
}

#endif
