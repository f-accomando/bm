#ifndef SELFTEST_H
#define SELFTEST_H

/* Quick checks of newlib (stdio, malloc, libm) on the target. Returns the
 * number of failures and prints a summary line. */
int libc_selftest(void);

#endif
