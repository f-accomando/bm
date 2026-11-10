/* Host tests that link src/net/net.c do not link the breadcrumbs of the kernel.
 * These two functions give net.c what it calls. */
#include <stdint.h>

const char *crumbs_what(void) { return ""; }
uint32_t crumbs_uptime_ms(void) { return 0; }
