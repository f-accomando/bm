/* A fuzz of the bmvm machine (make test-vmfuzz): the blobs made by bmtvm.py are changed at random and loaded; what loads is run
 * with arrays and random numbers. With -fsanitize=address,undefined a wrong read or write, or an undefined operation, stops it.
 * A blob that does not load, or a run that stops with an error, is fine: it must never crash. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bmvm.h"

static uint32_t rng = 12345;
static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

int main(int argc, char **argv)
{
    static uint8_t seed[1 << 16], blob[1 << 16];
    static int32_t ia[64];
    static double da[64];
    int iterations = argc > 2 ? atoi(argv[2]) : 100000, nseed = 0, loaded = 0, ran = 0, errors = 0;
    FILE *f = fopen(argv[1], "rb");
    if (!f)
        return 2;
    size_t n = fread(seed, 1, sizeof seed, f);
    fclose(f);
    (void)nseed;
    bmvm_arr arrs[4] = { { ia, 64, 0 }, { da, 64, 1 }, { ia, 64, 0 }, { da, 64, 1 } };
    for (int it = 0; it < iterations; it++) {
        memcpy(blob, seed, n);
        size_t len = n;
        const int kind = (int)(rnd() % 4);
        const int changes = 1 + (int)(rnd() % 6);
        for (int c = 0; c < changes; c++) {
            const size_t at = rnd() % len;
            if (kind == 0)
                blob[at] ^= (uint8_t)(1u << (rnd() % 8));       /* a bit */
            else if (kind == 1)
                blob[at] = (uint8_t)rnd();                      /* a byte */
            else if (kind == 2 && at > 12)
                blob[at] = (uint8_t)(rnd() % 80);               /* a small number: an operation, a register */
            else if (len > 40)
                len -= rnd() % 8;                               /* a blob that ends too soon */
        }
        bmvm_mod *m;
        const char *why;
        if (bmvm_load(&m, blob, len, &why))
            continue;
        loaded++;
        for (int fn = 0; fn < m->nfn; fn++) {
            const bmvm_fn *fp = &m->fn[fn];
            bmvm_val args[16], ret;
            for (int i = 0; i < 16; i++)
                args[i].d = (double)(int)(rnd() % 200) - 50.0 + (rnd() % 100) / 100.0;
            for (int i = 0; i < fp->nparams; i++) {
                if (fp->ptype[i] == BMVM_I32)
                    args[i].i = (int32_t)(rnd() % 300) - 20;
                else if (fp->ptype[i] >= BMVM_ARR_I32)
                    args[i].i = (int32_t)(rnd() % 6);           /* in range, or not */
            }
            const int e = bmvm_call(m, fn, args, fp->nparams, arrs, 4, &ret, 1500);
            ran++;
            errors += e != 0;
        }
        bmvm_free(m);
    }
    printf("vmfuzz: %d blobs, %d loaded, %d functions run, %d stopped by an error, no crash\n", iterations, loaded, ran, errors);
    return 0;
}
