/*
 * HCI over the UART (H4): commands and events. ACL data comes later.
 */
#ifndef HCI_H
#define HCI_H

#include <stdint.h>

#define HCI_INQUIRY             0x0401
#define HCI_RESET               0x0C03
#define HCI_READ_LOCAL_VERSION  0x1001
#define HCI_READ_BD_ADDR        0x1009
#define HCI_BCM_DOWNLOAD_MINI   0xFC2E
#define HCI_BCM_WRITE_RAM       0xFC4C
#define HCI_BCM_LAUNCH_RAM      0xFC4E

/* Sends a command and waits for its Command Complete (or Command Status).
 * Returns the status byte (0 = success) or -1 on timeout / bad answer.
 * ret receives the return parameters after the status (may be NULL). */
int hci_cmd(uint16_t opcode, const void *params, uint8_t len,
            uint8_t *ret, uint8_t ret_size, uint32_t timeout_us);

/* Next event (any): buf[0] code, buf[1] length, then the parameters.
 * Returns the total length, or -1 after timeout_us. ACL packets are
 * skipped for now. */
int hci_event(uint8_t *buf, uint32_t size, uint32_t timeout_us);

#endif
