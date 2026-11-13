/*
 * Minimal arena-allocated Lisp engine for symbolic code synthesis (M30+).
 * Designed for bare-metal Raspberry Pi Zero W (no malloc/free fragmentation,
 * bounded evaluation steps, structural pattern matching).
 */
#ifndef AI_LISP_H
#define AI_LISP_H

#include <stddef.h>
#include <stdint.h>

typedef struct bm_lisp bm_lisp_t;

/* Default arena size (512 KB) — plenty for AST synthesis & microKanren. */
#define BM_LISP_DEFAULT_ARENA (512u * 1024u)

/* Initialize a Lisp VM inside a caller-owned buffer. Returns NULL if too small. */
bm_lisp_t *bm_lisp_init(void *arena, size_t arena_bytes);

/* Reset the VM arena back to its initial loaded state (or empty). */
void bm_lisp_reset(bm_lisp_t *L);

/* Save current arena/env watermark as the reset point (after loading stdlib). */
void bm_lisp_freeze_base(bm_lisp_t *L);

/* Evaluate one or more S-expressions from `src`.
 * Writes the string representation of the last result into `out` (if non-NULL).
 * Returns 0 on success, -1 on syntax/eval/oom/step-limit error (message in `err`). */
int bm_lisp_eval(bm_lisp_t *L, const char *src, char *out, size_t out_cap,
                 char *err, size_t err_cap);

/* Bytes currently used in the arena. */
size_t bm_lisp_used(const bm_lisp_t *L);

#endif /* AI_LISP_H */
