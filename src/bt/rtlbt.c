/*
 * Realtek Bluetooth set-up (as Linux's btrtl.c does it for the RTL8821CS on
 * a UART): the firmware file holds one patch per ROM version ("Realtech"
 * epatch v1); the one for the chip's ROM gets the file's firmware version
 * in its last 4 bytes and the config file appended, and goes to the chip
 * with vendor command 0xFC20 in pieces of 252 bytes. The config file also
 * says how fast the UART runs afterwards.
 */
#include "rtlbt.h"
#include "hci.h"

#include <stdlib.h>
#include <string.h>

#define EPATCH_SIG      "Realtech"
#define EXT_SIG         0x77fd0451u
#define CONFIG_SIG      0x8723ab55u
#define FRAG            252

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

int rtlbt_patch(const uint8_t *fw, size_t fwlen, const uint8_t *cfg, size_t cfglen,
                unsigned rom_version, uint8_t **out, size_t *outlen, const char **err)
{
    *out = NULL;
    *outlen = 0;
    if (fwlen < 14 + 4 || memcmp(fw, EPATCH_SIG, 8) != 0) {
        *err = "not a Realtek firmware file";
        return -1;
    }
    if (le32(fw + fwlen - 4) != EXT_SIG) {
        *err = "firmware file without its extension signature";
        return -1;
    }
    /* project id: records read backwards from before the signature, as
     * btrtl.c walks them (opcode, length, data byte, then length more) */
    int project = -1;
    for (size_t p = fwlen - 4; p >= 14 + 3; ) {
        uint8_t op = fw[p - 1], len = fw[p - 2], data = fw[p - 3];
        if (op == 0xff || len == 0)
            break;
        if (op == 0 && len == 1) {
            project = data;
            break;
        }
        if (p < 3u + len + 14 + 3)
            break;
        p -= 3u + len;
    }
    if (project != 10) {                    /* 10: the 8821C */
        *err = "firmware for another chip";
        return -1;
    }
    uint32_t fw_version = le32(fw + 8);
    unsigned n = le16(fw + 12);
    if (14 + n * 8u > fwlen) {
        *err = "truncated firmware header";
        return -1;
    }
    const uint8_t *ids = fw + 14, *lens = ids + 2 * n, *offs = lens + 2 * n;
    for (unsigned i = 0; i < n; i++) {
        if (le16(ids + 2 * i) != rom_version + 1)
            continue;
        uint32_t len = le16(lens + 2 * i), off = le32(offs + 4 * i);
        if (len < 4 || off + len > fwlen) {
            *err = "patch outside the file";
            return -1;
        }
        uint8_t *img = malloc(len + cfglen);
        if (!img) {
            *err = "out of memory";
            return -1;
        }
        memcpy(img, fw + off, len - 4);
        img[len - 4] = (uint8_t)fw_version;
        img[len - 3] = (uint8_t)(fw_version >> 8);
        img[len - 2] = (uint8_t)(fw_version >> 16);
        img[len - 1] = (uint8_t)(fw_version >> 24);
        if (cfglen)
            memcpy(img + len, cfg, cfglen);
        *out = img;
        *outlen = len + cfglen;
        return 0;
    }
    *err = "no patch for this ROM version";
    return -1;
}

int rtlbt_uart_config(const uint8_t *cfg, size_t cfglen, uint32_t *word, uint32_t *baud, int *flow)
{
    if (cfglen < 6 || le32(cfg) != CONFIG_SIG || 6u + le16(cfg + 4) > cfglen)
        return -1;
    size_t end = 6u + le16(cfg + 4);
    for (size_t i = 6; i + 3 <= end; ) {
        unsigned off = le16(cfg + i), len = cfg[i + 2];
        const uint8_t *d = cfg + i + 3;
        if (i + 3 + len > end)
            break;
        if (off == 0x000c && len >= 4) {
            *word = le32(d);
            *flow = len >= 13 && (d[12] & 4);
            switch (*word) {
            case 0x0252a00au: *baud = 230400; break;
            case 0x05f75004u: *baud = 921600; break;
            case 0x00005004u: *baud = 1000000; break;
            case 0x04928002u: case 0x01128002u: *baud = 1500000; break;
            case 0x00005002u: *baud = 2000000; break;
            case 0x0000b001u: *baud = 2500000; break;
            case 0x04928001u: *baud = 3000000; break;
            case 0x052a6001u: *baud = 3500000; break;
            case 0x00005001u: *baud = 4000000; break;
            default: *baud = 115200; break;
            }
            return 0;
        }
        i += 3u + len;
    }
    return -1;
}

int rtlbt_download(const uint8_t *img, size_t len, unsigned *fragments)
{
    unsigned frags = (unsigned)(len / FRAG + 1), j = 0;
    uint8_t buf[1 + FRAG];
    for (unsigned i = 0; i < frags; i++) {
        uint8_t index = (uint8_t)j++;
        if (index == 0x7f)
            j = 1;
        size_t n = FRAG;
        if (i == frags - 1) {
            index |= 0x80;
            n = len % FRAG;
        }
        buf[0] = index;
        memcpy(buf + 1, img + (size_t)i * FRAG, n);
        uint8_t ret[2];
        int st = hci_cmd(0xfc20, buf, (uint8_t)(1 + n), ret, sizeof ret, 1000000);
        if (st != 0) {
            if (fragments)
                *fragments = i;
            return st < 0 ? -1 : -2;
        }
    }
    if (fragments)
        *fragments = frags;
    return 0;
}
