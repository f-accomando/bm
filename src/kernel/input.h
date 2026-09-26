#ifndef INPUT_H
#define INPUT_H

/* Blocking read of one key from the serial console. While waiting it
 * refreshes the uptime in the status bar. */
char input_getc(void);

#endif
