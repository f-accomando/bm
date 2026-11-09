/*
 * Shell Code: Hybrid Vim / bm Code editor, compiler, and runner for bm shell.
 *
 * Supports editing .b, .c, .lua, .txt files with syntax highlighting,
 * line numbering, multi-file tabs, modal Vim navigation and editing,
 * direct .b compilation feedback, native ARMv6 runner with microsecond
 * execution profiling and graphics, and an ARMv6 disassembly viewer.
 */
#ifndef SHELL_CODE_H
#define SHELL_CODE_H

#include <stddef.h>
#include <stdint.h>

/* Opens the Shell Code interactive editor.
 * If path is NULL or empty, opens a new/scratch buffer.
 * Returns 0 on normal exit. */
int shell_code_open(const char *path);

/* Host unit test suite for shell code editor, syntax, compiler, and runner. */
int shell_code_test_run(void);

#endif /* SHELL_CODE_H */
