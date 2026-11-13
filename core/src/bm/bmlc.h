/* Stage 9 on-console compiler for the language of bm (.b -> BMN1 ARMv6 A32 machine code).
 *
 * Compiles a .b source string in RAM (Lexer -> Parser -> TypeChecker -> Stack Bytecode -> ARMv6 A32 -> BMN1 container)
 * directly on the console or host without Python or external tools.
 */
#ifndef BMLC_H
#define BMLC_H

#include <stddef.h>
#include <stdint.h>

/* Compiles a .b source string into a heap-allocated BMN1 binary blob (*out_blob, *out_len).
 * Returns 0 on success (caller frees *out_blob), or non-zero on error with a human-readable message in err_buf. */
int bml_compile_native(const char *src, size_t src_len, uint8_t **out_blob, size_t *out_len, char *err_buf, size_t err_cap);

#endif
