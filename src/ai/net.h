/*
 * Small integer networks for the cartridges (M38.4): a stack of dense
 * layers with INT8 weights and activations (the layers of nn.h), from a
 * blob that a training script writes, run from Lua:
 *
 *   net = nnet(blob)            the network (an error if the blob is bad)
 *   out = net:run(inputs, [out]) the outputs (numbers) for a table of inputs
 *   k, v = net:pick(inputs, [mask]) the index (1-based) of the largest
 *                               output and its value; mask: a table of
 *                               booleans, false = that output is left out
 *   n_in, n_out = net:size()
 *
 * Blob ("BMNN", little endian):
 *   "BMNN" u8 version (1) u8 layers u16 inputs  f32 input scale  f32 output scale
 *   per layer: u16 outputs u8 relu (0/1) u8 shift  i32 mult
 *              outputs x i32 bias, outputs x pad4(inputs) x i8 weights
 * An input x becomes clamp(round(x * input scale), -127, 127); a relu
 * layer gives clamp((max(acc, 0) * mult + 2^(shift-1)) >> shift, 0, 127);
 * the last layer's sums times output scale are the outputs.
 */
#ifndef AI_NET_H
#define AI_NET_H

#include <stdint.h>

#include "lua.h"

void nnet_lua_open(lua_State *L);

#endif
