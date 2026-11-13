/* The Pi 1 B's Ethernet (net.c's other data path): not on the RGB30 */
#include "usb/smsc95xx.h"
static const unsigned char no_mac[6];
const unsigned char *eth_mac(void)          { return no_mac; }
int  eth_linked(void)                       { return 0; }
void eth_poll(void)                         { }
int  eth_recv(void *buf, int max)           { (void)buf; (void)max; return 0; }
int  eth_send(const void *frame, int len)   { (void)frame; (void)len; return -1; }

