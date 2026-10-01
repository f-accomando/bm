/*
 * The RGB30's WiFi, the parts without hardware (host-tested in
 * tests/rgb30/rtw_frame_test.c): the RTL8821C's RX buffer (descriptors,
 * aggregated packets, PHY status), and the 802.11 frames a station reads
 * and writes (beacons, probe requests and responses).
 */
#ifndef RTW_FRAME_H
#define RTW_FRAME_H

#include <stdint.h>

/* --- RX: one read of the RX FIFO holds one or more packets --- */
typedef struct {
    const uint8_t *data;        /* the 802.11 frame, or the C2H message */
    uint32_t len;               /* without the FCS */
    int rssi;                   /* dBm, from the PHY status (-127 if none) */
    uint8_t rate;               /* rtw rate code: 0..3 CCK, 4..11 OFDM, 12+ HT */
    uint8_t c2h;                /* a message from the firmware, not a frame */
    uint8_t crc_err, icv_err;
    uint8_t decrypted;          /* by the chip (security CAM) */
    uint8_t macid;              /* the CAM entry that matched */
} rtw_rxpkt_t;

typedef void (*rtw_rx_cb)(const rtw_rxpkt_t *p, void *ctx);

/* Walks a buffer read from the RX FIFO (len: what SDIO_RX0_REQ_LEN said)
 * and calls cb for each packet; rfe: the efuse RFE type (CCK RSSI table).
 * Returns the packets seen, or -1 - n if the n+1-th descriptor is bad
 * (the rest of the buffer is dropped, as rtw88 does). */
int rtw_rx_walk(const uint8_t *buf, uint32_t len, unsigned rfe, rtw_rx_cb cb, void *ctx);

/* --- 802.11 --- */
#define WL_FC_PROBE_REQ     0x0040
#define WL_FC_PROBE_RESP    0x0050
#define WL_FC_BEACON        0x0080
#define WL_FC_AUTH          0x00b0
#define WL_FC_DEAUTH        0x00c0
#define WL_FC_DISASSOC      0x00a0
#define WL_FC_ASSOC_REQ     0x0000
#define WL_FC_ASSOC_RESP    0x0010

static inline uint16_t wl_fc(const uint8_t *f) { return (uint16_t)(f[0] | f[1] << 8); }
/* type and subtype only (the FC's flags left out) */
static inline uint16_t wl_kind(const uint8_t *f) { return (uint16_t)(f[0] & 0xfc); }

typedef struct {
    char ssid[33];              /* printable: other bytes shown as '?' */
    uint8_t ssid_raw[32], ssid_len;
    uint8_t bssid[6];
    int rssi;
    uint8_t channel;            /* from the DS parameter IE (0 if none) */
    uint16_t capab, interval;
    const char *security;       /* "WPA3", "WPA2", "WPA", "WEP" or "open" */
    uint8_t rsn, rsn_psk, rsn_sae, rsn_ccmp, rsn_group;  /* group: cipher suite type */
    uint8_t wpa, ht;
    uint8_t rates[16], nrates;  /* supported + extended, as in the IEs */
} wl_bss_t;

/* A beacon or probe response (len without the FCS) into b; 0, or -1 if
 * it is not one or is too short. */
int wl_parse_bss(const uint8_t *f, uint32_t len, int rssi, wl_bss_t *b);

/* A probe request from mac, for every network (ssid NULL or "") or one
 * (hidden networks); out holds 128 bytes. Returns the frame's length. */
unsigned wl_probe_req(uint8_t *out, const uint8_t mac[6], const char *ssid);

#endif
