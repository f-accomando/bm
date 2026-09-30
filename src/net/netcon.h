/*
 * The network console (M18.7): the monitor over TCP, port 3333, one
 * client at a time, after a password. Everything printed goes to the
 * client too; what it types is read like the keyboard. Plain text: for
 * the home network only (TLS comes with M19).
 */
#ifndef NETCON_H
#define NETCON_H

#define NETCON_PORT 3333

/* Listens (once lwIP is up). The password is net_password in
 * bm/config.txt; without one a 6-digit PIN is made and saved. */
int  netcon_start(void);
/* Sends what was printed; from net_poll, outside lwIP's callbacks. */
void netcon_poll(void);
/* A byte typed by the client, -1 if none. */
int  netcon_getc(void);
int  netcon_pending(void);
/* The password, for the screen. */
const char *netcon_password(void);
/* A client is logged in. */
int  netcon_active(void);

#endif
