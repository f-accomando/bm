#ifndef SYSINFO_H
#define SYSINFO_H

void sysinfo_print(void);
void sysinfo_print_heap(void);
void sysinfo_print_short(void);
/* "ARM1176", "Cortex-A53" (Pi Zero 2 W), from the CPU's ID register. */
const char *sysinfo_cpu(void);

#endif
