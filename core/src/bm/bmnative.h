/* Native ARMv6 A32 loader and runner for the language of bm (Stage 5, core/tools/bml/bml.py --native / --armv6).
 *
 * Loads a verified BMN1 module (or raw 32-bit ARMv6 A32 words) into 32-byte-aligned executable RAM, cleans the D-cache,
 * invalidates the I-cache and branch target cache before the first call, and calls the function using the AAPCS ABI.
 */
#ifndef BMNATIVE_H
#define BMNATIVE_H

#include <stddef.h>
#include <stdint.h>

#define BMN_MAGIC       0x314E4D42u     /* "BMN1" */
#define BMN_MAX_FN      512
#define BMN_MAX_STRUCT  64
#define BMN_MAX_FIELDS  16
#define BMN_MAX_CODE    65536
#define BMN_MAX_ARGS    16
#define BMN_VARARGS     0xFFu           /* raw A32 word blob without BMN1 header */
#define BMN_BUDGET      20000000u       /* step budget on non-ARM hosts */
#define BMN_STRUCT_BASE 0x80u           /* ptype >= 0x80 refers to struct (ptype & 0x7F) */

enum {
    BMN_NONE     = 0,
    BMN_I32      = 1,
    BMN_BOOL     = 2,
    BMN_F32      = 3,
    BMN_ARR_I32  = 4,
    BMN_ARR_BOOL = 5,
    BMN_ARR_F32  = 6,
    BMN_STR      = 7
};

enum {
    BMN_OK = 0,
    BMN_ERR_BAD,
    BMN_ERR_OOM,
    BMN_ERR_ARGS,
    BMN_ERR_BUDGET,
    BMN_ERR_TRAP
};

typedef struct {
    char     name[32];
    uint8_t  nparams;                   /* 0..16, or BMN_VARARGS for a raw A32 blob */
    uint8_t  ret;                       /* BMN_NONE, BMN_I32, BMN_BOOL, BMN_F32 */
    uint8_t  ptype[BMN_MAX_ARGS];       /* BMN_I32..BMN_ARR_F32, or BMN_STRUCT_BASE | idx */
    uint32_t word_off;                  /* entry offset in 32-bit words from m->code */
} bmnative_fn;

typedef struct {
    char     name[32];
    uint32_t nfields;                   /* 1..BMN_MAX_FIELDS */
    uint8_t  ftype[BMN_MAX_FIELDS];     /* BMN_I32, BMN_BOOL, or BMN_F32 */
    char     fname[BMN_MAX_FIELDS][16];
} bmnative_struct;

typedef struct {
    uint32_t *data;                     /* 32-bit words (int32_t or IEEE-754 binary32 bits) */
    uint32_t  nwords;                   /* element count in 32-bit words */
} bmnative_buf;

enum {
    BMN_HOST_CLS      = 0,
    BMN_HOST_PIX      = 1,
    BMN_HOST_RECTFILL = 2,
    BMN_HOST_SPR      = 3,
    BMN_HOST_BTN      = 4,
    BMN_HOST_BTNP     = 5,
    BMN_HOST_LINE     = 6,
    BMN_HOST_CIRC     = 7,
    BMN_HOST_CIRCFILL = 8,
    BMN_HOST_RECT     = 9,
    BMN_HOST_SFX      = 10,
    BMN_HOST_MUSIC    = 11,
    BMN_HOST_TIME     = 12,
    BMN_HOST_PRINT    = 13,
    BMN_HOST_SIN      = 14,
    BMN_HOST_COS      = 15,
    BMN_HOST_ATAN2    = 16,
    BMN_HOST_S16_BASE = 17,
    BMN_HOST_S16_MODE = 17,
    BMN_HOST_S16_SCROLL = 18,
    BMN_HOST_S16_OAM  = 19,
    BMN_HOST_S16_OAM_CLR = 20,
    BMN_HOST_S16_M7   = 21,
    BMN_HOST_S16_HDMA_GRAD = 22,
    BMN_HOST_S16_HDMA_BAND = 23,
    BMN_HOST_S16_HDMA_M7 = 24,
    BMN_HOST_S16_RENDER = 25,
    BMN_HOST_S16_SFX  = 26
};

typedef int32_t (*bmnative_host_fn)(int32_t host_id, const int32_t *args, int32_t nargs);

/* Register the fast C host dispatcher for BMN_HOST_S16_* (api_vm.c). */
void api_vm_set_fast_host(bmnative_host_fn fn);

typedef struct {
    int              nfn;
    int              nstruct;
    int              has_host_stub;     /* 1 if code ends with 6-word __bml_call_host stub */
    uint32_t         ncode;
    bmnative_fn     *fn;
    bmnative_struct *structs;
    uint32_t        *code;              /* 32-byte-aligned executable A32 machine words */
    void            *code_Alloc;        /* underlying heap allocation to free */
    uint32_t         nrodata;           /* byte size of read-only data segment (.rodata) */
    uint8_t         *rodata;            /* contiguous read-only data (strings, constants) */
} bmnative_mod;

/* Clean D-cache and invalidate I-cache + branch predictor over [code, code + bytes). */
void bmnative_sync_cache(const void *code, size_t bytes);

/* Register/get the kernel host function dispatcher called by __bml_call_host. */
void bmnative_set_host_fn(bmnative_host_fn fn);
bmnative_host_fn bmnative_get_host_fn(void);

int  bmnative_load(bmnative_mod **out, const uint8_t *blob, size_t len, const char **why);
void bmnative_free(bmnative_mod *m);
int  bmnative_find(const bmnative_mod *m, const char *name);
int  bmnative_call(const bmnative_mod *m, int fn, const int32_t *args, int nargs, int32_t *ret);
int  bmnative_call_buf(const bmnative_mod *m, int fn, const int32_t *args, const bmnative_buf *bufs, int nargs, int32_t *ret);
const char *bmnative_get_string(int32_t arg0);
uint32_t bmnative_last_insns(void);
void bmnative_disasm_word(uint32_t w, char *buf, size_t cap);

#endif
