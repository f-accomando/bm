/*
 * Integer layers of the assistant's network (see nn.h). The plain C loops
 * are the reference; on ARMv6 the same sums go through the SIMD
 * instructions, which give exactly the same integers.
 */
#include "nn.h"

#include <string.h>

#if defined(__ARM_FEATURE_SIMD32) && __ARM_FEATURE_SIMD32
#include <arm_acle.h>
#define NN_SIMD 1
#else
#define NN_SIMD 0
#endif

#define MAX_HID 256

#if NN_SIMD
static inline uint32_t ror8(uint32_t v) { return v >> 8 | v << 24; }    /* SXTB16 ..., ROR #8 */
#endif

void nn_embed_bag(const int8_t *w, int hid, const uint16_t *feats, int n, int32_t pool,
                  const int32_t *bias, int32_t *acc)
{
#if NN_SIMD
    /* two 16-bit sums per word: bytes 0 and 2 in sum[k], 1 and 3 in odd[k].
     * 255 features x 128 fit 16 bits. */
    uint32_t even[MAX_HID / 4], odd[MAX_HID / 4];
    int words = hid / 4;
    memset(even, 0, (size_t)words * 4);
    memset(odd, 0, (size_t)words * 4);
    for (int i = 0; i < n; i++) {
        const uint32_t *row = (const uint32_t *)(const void *)(w + (uint32_t)feats[i] * (uint32_t)hid);
        for (int k = 0; k < words; k++) {
            uint32_t v = row[k];
            even[k] = (uint32_t)__sadd16((int16x2_t)even[k], __sxtb16(v));
            odd[k] = (uint32_t)__sadd16((int16x2_t)odd[k], __sxtb16(ror8(v)));
        }
    }
    for (int k = 0; k < words; k++) {
        acc[4 * k + 0] = (int16_t)(even[k] & 0xFFFF);
        acc[4 * k + 1] = (int16_t)(odd[k] & 0xFFFF);
        acc[4 * k + 2] = (int16_t)(even[k] >> 16);
        acc[4 * k + 3] = (int16_t)(odd[k] >> 16);
    }
#else
    for (int j = 0; j < hid; j++)
        acc[j] = 0;
    for (int i = 0; i < n; i++) {
        const int8_t *row = w + (uint32_t)feats[i] * (uint32_t)hid;
        for (int j = 0; j < hid; j++)
            acc[j] += row[j];
    }
#endif
    for (int j = 0; j < hid; j++) {
        int64_t v = (int64_t)acc[j] * pool;
        acc[j] = (int32_t)(v >> 16) + bias[j];    /* arithmetic shift: floor, as Python */
    }
}

void nn_relu_q(const int32_t *acc, int n, int32_t mult, int shift, int8_t *y)
{
    int64_t half = (int64_t)1 << (shift - 1);
    for (int j = 0; j < n; j++) {
        if (acc[j] <= 0) {
            y[j] = 0;
            continue;
        }
        int64_t v = ((int64_t)acc[j] * mult + half) >> shift;
        y[j] = (int8_t)(v > 127 ? 127 : v);
    }
}

void nn_dense(const int8_t *w, const int32_t *bias, const int8_t *x, int nin, int nout,
              int32_t *out)
{
#if NN_SIMD
    const uint32_t *xw = (const uint32_t *)(const void *)x;
    int words = nin / 4;
    for (int k = 0; k < nout; k++) {
        const uint32_t *row = (const uint32_t *)(const void *)(w + (uint32_t)k * (uint32_t)nin);
        int32_t s = bias[k];
        for (int i = 0; i < words; i++) {
            uint32_t a = row[i], b = xw[i];
            s = __smlad(__sxtb16(a), __sxtb16(b), s);
            s = __smlad(__sxtb16(ror8(a)), __sxtb16(ror8(b)), s);
        }
        out[k] = s;
    }
#else
    for (int k = 0; k < nout; k++) {
        const int8_t *row = w + (uint32_t)k * (uint32_t)nin;
        int32_t s = bias[k];
        for (int i = 0; i < nin; i++)
            s += row[i] * x[i];
        out[k] = s;
    }
#endif
}
