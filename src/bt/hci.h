/*
 * HCI over the UART (H4): commands, events and ACL data. Packets that
 * arrive while hci_cmd() waits for its answer are queued for hci_recv().
 */
#ifndef HCI_H
#define HCI_H

#include <stdint.h>

#define HCI_INQUIRY             0x0401
#define HCI_CREATE_CONNECTION   0x0405
#define HCI_ACCEPT_CONNECTION   0x0409
#define HCI_LINK_KEY_REPLY      0x040B
#define HCI_LINK_KEY_NEG_REPLY  0x040C
#define HCI_PIN_CODE_REPLY      0x040D
#define HCI_AUTH_REQUESTED      0x0411
#define HCI_SET_CONN_ENCRYPTION 0x0413
#define HCI_IO_CAP_REPLY        0x042B
#define HCI_USER_CONFIRM_REPLY  0x042C
#define HCI_SET_EVENT_MASK      0x0C01
#define HCI_RESET               0x0C03
#define HCI_WRITE_LOCAL_NAME    0x0C13
#define HCI_WRITE_SCAN_ENABLE   0x0C1A
#define HCI_WRITE_CLASS         0x0C24
#define HCI_WRITE_SSP_MODE      0x0C56
#define HCI_READ_LOCAL_VERSION  0x1001
#define HCI_READ_BD_ADDR        0x1009
#define HCI_BCM_UPDATE_BAUD     0xFC18
#define HCI_BCM_DOWNLOAD_MINI   0xFC2E
#define HCI_BCM_WRITE_RAM       0xFC4C
#define HCI_BCM_LAUNCH_RAM      0xFC4E

#define HCI_EVENT   0x04
#define HCI_ACL     0x02

typedef struct {
    uint8_t type;               /* HCI_EVENT or HCI_ACL */
    uint16_t len;               /* bytes in data */
    uint8_t data[1028];         /* event: code, length, params; ACL: 4-byte header, payload */
} hci_pkt_t;

/* Sends a command and waits for its Command Complete (or Command Status).
 * Returns the status byte (0 = success) or -1 on timeout / bad answer.
 * ret receives the return parameters after the status (may be NULL). */
int hci_cmd(uint16_t opcode, const void *params, uint8_t len,
            uint8_t *ret, uint8_t ret_size, uint32_t timeout_us);

/* Sends a command without waiting (its answer is dropped when it comes). */
void hci_send(uint16_t opcode, const void *params, uint8_t len);

/* Next event or ACL packet (queued ones first); 0, or -1 after timeout_us. */
int hci_recv(hci_pkt_t *p, uint32_t timeout_us);

/* 1 if a packet is queued or bytes are waiting on the UART. */
int hci_pending(void);

/* Sends ACL data (one L2CAP frame, start of packet). */
void hci_acl_send(uint16_t handle, const void *data, uint16_t len);

/* ACL data with this packet boundary flag: LE links send a frame as a
 * first piece (0) and continuations (1), each at most the chip's size. */
void hci_acl_send_pb(uint16_t handle, int pb, const void *data, uint16_t len);

/* Drops the queue (after a reset). */
void hci_flush(void);

#endif
