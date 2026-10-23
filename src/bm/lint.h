/* The build lint of a .b16 (docs/spec/lint-rules.toml): warnings only, never an error.
 * The same rules run in scripts/mkbm.py (b16_lint) and in the SDK export (bm_build_b16). */
#ifndef BM_LINT_H
#define BM_LINT_H

#include <stddef.h>
#include <stdint.h>

/* One warning: `line` is the Lua line, or 0 for a fact of the header. */
typedef void (*bm_lint_fn)(void *ctx, int line, const char *msg);

/* Checks the cartridge `cart` (`len` bytes) as a .b16: the header facts, then the first hit of each rule of
 * the Lua code (comments and the insides of strings do not count). Calls `warn` once for each warning.
 * Returns the number of warnings, or -1 if the file is not a cartridge. */
int bm_b16_lint(const uint8_t *cart, size_t len, bm_lint_fn warn, void *ctx);

#endif
