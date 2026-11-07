#ifndef SYSINFO_H
#define SYSINFO_H

void sysinfo_print(void);
void sysinfo_print_heap(void);
void sysinfo_print_short(void);
/* "ARM1176", "Cortex-A53" (Pi Zero 2 W), from the CPU's ID register. */
const char *sysinfo_cpu(void);

/* The main facts for the bm shell's box: label (13 characters) and value rows; returns how many. */
int sysinfo_summary(char label[][14], char value[][40], int max);

#endif
