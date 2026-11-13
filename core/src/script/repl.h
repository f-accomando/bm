#ifndef REPL_H
#define REPL_H

/* Interactive Lua on the serial console (output also on screen).
 * Returns on Ctrl-D (empty line) or exit(). */
void repl_run(void);

#endif
