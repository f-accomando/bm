/*
 * Tiny integer neural network (M30): the layers the assistant needs, INT8
 * weights and activations, INT32 sums. Bit for bit the same as the Python
 * reference (scripts/assistlib.py); on the ARM1176 the SIMD instructions of
 * ARMv6 (SXTB16, SADD16, SMLAD) do 2-4 operations at a time.
 */
#ifndef AI_NN_H
#define AI_NN_H

#include <stdint.h>

/* acc[j] = (sum over the features f of w[f][j]) * pool >> 16 + bias[j].
 * w: one row of `hid` weights per feature (hid a multiple of 4, rows
 * 4-byte aligned); at most 255 features (the sums fit 16 bits). */
void nn_embed_bag(const int8_t *w, int hid, const uint16_t *feats, int n, int32_t pool,
                  const int32_t *bias, int32_t *acc);

/* y[j] = clamp((max(acc[j], 0) * mult + 2^(shift-1)) >> shift, 0, 127) */
void nn_relu_q(const int32_t *acc, int n, int32_t mult, int shift, int8_t *y);

/* out[k] = bias[k] + sum_j w[k][j] * x[j]; w: one row of nin per output
 * (nin a multiple of 4, rows 4-byte aligned) */
void nn_dense(const int8_t *w, const int32_t *bias, const int8_t *x, int nin, int nout,
              int32_t *out);

#endif
