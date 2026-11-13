#ifndef HEAP_H
#define HEAP_H

#include <stdint.h>

/* The malloc() heap grows from the end of the kernel image up to limit. */
void      heap_init(uintptr_t limit);
uintptr_t heap_start(void);
uintptr_t heap_end(void);
uintptr_t heap_brk(void);   /* current top of the heap */

#endif
