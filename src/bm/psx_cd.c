/*
 * psx_cd.c: the CD-ROM controller of the PlayStation: the four registers with their index, the parameter, response and
 * data FIFOs, the interrupt flags, and the commands a BIOS and a game use (GetStat, Setloc, ReadN/ReadS, Pause, Init,
 * Setmode, GetID, GetTN/GetTD, SeekL/SeekP, Test, Mute/Demute, Stop, ...). The sectors come from a callback (psx.h,
 * psx_disc_t), so the disc can be streamed from the SD card. Not done: CD-DA and XA audio, the sub-channel Q data
 * (GetlocP gives the position only). Original code.
 */
#include "psx_int.h"

#define CD_STAT_MOTOR 0x02
#define CD_STAT_READ  0x20
#define CD_STAT_SHELL 0x10

typedef struct { int32_t due; uint8_t intn, len, kind; uint8_t d[10]; } cd_evt_t;

static psx_disc_t disc;
static int have_disc;
static char region = 'A';
static int index_, irq_en, irq_flag;
static uint8_t param[16], resp[16];
static int nparam, nresp, resp_pos;
static uint8_t sec[2352];
static uint8_t databuf[2352];
static int data_len, data_pos, want_data, sec_len;
static uint32_t cd_mode, cur_lba, loc_lba;
static int reading;
static cd_evt_t evq[12];
static int nev;
uint32_t psx_cd_stat_cmds, psx_cd_stat_sectors;

static uint8_t bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static int unbcd(uint8_t b) { return (b >> 4) * 10 + (b & 15); }

static uint8_t stat(void)
{
    if (!have_disc) return CD_STAT_SHELL;
    return (uint8_t)(CD_STAT_MOTOR | (reading ? CD_STAT_READ : 0));
}

static void push(int due, int intn, int kind, int len, const uint8_t *d)
{
    if (nev >= 12) return;
    cd_evt_t *e = &evq[nev++];
    e->due = due; e->intn = (uint8_t)intn; e->kind = (uint8_t)kind; e->len = (uint8_t)len;
    memcpy(e->d, d, (size_t)len);
}

static void push_stat(int due, int intn)
{
    const uint8_t s = stat();
    push(due, intn, 0, 1, &s);
}

void psx_cd_reset(void)
{
    index_ = 0; irq_en = 0; irq_flag = 0;
    nparam = nresp = resp_pos = 0;
    data_len = data_pos = 0; want_data = 0;
    cd_mode = 0; cur_lba = loc_lba = 0; reading = 0; nev = 0;
}

void psx_cd_set_disc(const psx_disc_t *d)
{
    if (!d) { have_disc = 0; memset(&disc, 0, sizeof disc); return; }
    disc = *d;
    have_disc = 1;
    region = 'I';
    uint8_t buf[2352];
    if (disc.read(disc.user, 4, buf) == 0) {
        const uint8_t *p = buf + (disc.raw ? 24 : 0);
        for (int i = 0; i < 2048 - 4; i++) {
            if (!memcmp(p + i, "Euro", 4)) { region = 'E'; break; }
            if (!memcmp(p + i, "Amer", 4)) { region = 'A'; break; }
        }
    }
}

static void deliver(cd_evt_t *e)
{
    nresp = e->len;
    memcpy(resp, e->d, e->len);
    resp_pos = 0;
    irq_flag = (irq_flag & ~7) | e->intn;
    if (irq_en & e->intn) psx_irq_raise(IRQ_CD);
}

static int load_sector(void)
{
    if (!have_disc || cur_lba >= disc.sectors) return -1;
    if (disc.raw) {
        if (disc.read(disc.user, cur_lba, sec) != 0) return -1;
    } else {
        memset(sec, 0, sizeof sec);
        const uint32_t abs = cur_lba + 150;
        sec[12] = bcd((int)(abs / 75 / 60)); sec[13] = bcd((int)(abs / 75 % 60)); sec[14] = bcd((int)(abs % 75)); sec[15] = 2;
        if (disc.read(disc.user, cur_lba, sec + 24) != 0) return -1;
    }
    psx_cd_stat_sectors++;
    return 0;
}

static void start_data(void)
{
    if (cd_mode & 0x20) { memcpy(databuf, sec + 12, 0x924); sec_len = 0x924; }
    else { memcpy(databuf, sec + 24, 0x800); sec_len = 0x800; }
}

void psx_cd_run(uint32_t cycles)
{
    for (int i = 0; i < nev; i++) evq[i].due -= (int32_t)cycles;
    while (nev && evq[0].due <= 0 && (irq_flag & 7) == 0) {
        cd_evt_t e = evq[0];
        memmove(&evq[0], &evq[1], (size_t)(nev - 1) * sizeof evq[0]);
        nev--;
        if (e.kind == 1) {                                         /* a sector of a read */
            if (!reading) continue;
            if (load_sector() != 0) {
                reading = 0;
                const uint8_t err[2] = { (uint8_t)(stat() | 1), 0x04 };
                cd_evt_t x; x.intn = 5; x.len = 2; memcpy(x.d, err, 2);
                deliver(&x);
                continue;
            }
            const uint32_t period = (cd_mode & 0x80) ? 225792u : 451584u;
            cur_lba++;
            /* an XA audio sector with XA on is not given to the CPU */
            if ((cd_mode & 0x40) && (sec[18] & 4)) { push((int)period, 1, 1, 0, NULL); continue; }
            start_data();
            cd_evt_t x; x.intn = 1; x.len = 1; x.d[0] = stat();
            deliver(&x);
            push((int)period, 1, 1, 0, NULL);
            continue;
        }
        deliver(&e);
    }
}

static void remove_sector_events(void)
{
    int k = 0;
    for (int i = 0; i < nev; i++) if (evq[i].kind != 1) evq[k++] = evq[i];
    nev = k;
}

static void command(uint32_t cmd)
{
    psx_cd_stat_cmds++;
    const uint8_t s = stat();
    const int d1 = 50401;
    uint8_t buf[10];
    switch (cmd) {
    case 0x01: push_stat(d1, 3); break;                                    /* GetStat */
    case 0x02:                                                              /* Setloc */
        loc_lba = (uint32_t)((unbcd(param[0]) * 60 + unbcd(param[1])) * 75 + unbcd(param[2])) - 150;
        push_stat(d1, 3);
        break;
    case 0x06: case 0x1B:                                                   /* ReadN, ReadS */
        if (!have_disc) { const uint8_t e[2] = { (uint8_t)(s | 1), 0x80 }; push(d1, 5, 0, 2, e); break; }
        cur_lba = loc_lba;
        reading = 1;
        remove_sector_events();
        push_stat(d1, 3);
        push(d1 + 100000, 1, 1, 0, NULL);
        break;
    case 0x09:                                                              /* Pause */
        push_stat(d1, 3);
        reading = 0;
        remove_sector_events();
        push_stat(d1 + 80000, 2);
        break;
    case 0x08: case 0x07:                                                   /* Stop, Standby */
        push_stat(d1, 3);
        reading = 0;
        remove_sector_events();
        push_stat(d1 + 80000, 2);
        break;
    case 0x0A:                                                              /* Init */
        cd_mode = 0; reading = 0;
        remove_sector_events();
        push_stat(d1, 3);
        push_stat(d1 + 120000, 2);
        break;
    case 0x0B: case 0x0C: case 0x0D: push_stat(d1, 3); break;               /* Mute, Demute, Setfilter */
    case 0x0E: cd_mode = param[0]; push_stat(d1, 3); break;                 /* Setmode */
    case 0x0F: buf[0] = s; buf[1] = (uint8_t)cd_mode; buf[2] = buf[3] = buf[4] = 0; push(d1, 3, 0, 5, buf); break;   /* Getparam */
    case 0x10:                                                              /* GetlocL */
        memcpy(buf, sec + 12, 8);
        push(d1, 3, 0, 8, buf);
        break;
    case 0x11: {                                                            /* GetlocP */
        const uint32_t abs = cur_lba + 150, rel = cur_lba;
        buf[0] = 1; buf[1] = 1;
        buf[2] = bcd((int)(rel / 75 / 60)); buf[3] = bcd((int)(rel / 75 % 60)); buf[4] = bcd((int)(rel % 75));
        buf[5] = bcd((int)(abs / 75 / 60)); buf[6] = bcd((int)(abs / 75 % 60)); buf[7] = bcd((int)(abs % 75));
        push(d1, 3, 0, 8, buf);
        break;
    }
    case 0x13: buf[0] = s; buf[1] = 1; buf[2] = 1; push(d1, 3, 0, 3, buf); break;      /* GetTN: one track */
    case 0x14: {                                                            /* GetTD */
        const int tr = unbcd(param[0]);
        uint32_t abs = 150;
        if (tr == 0) abs = disc.sectors + 150;
        buf[0] = s; buf[1] = bcd((int)(abs / 75 / 60)); buf[2] = bcd((int)(abs / 75 % 60));
        push(d1, 3, 0, 3, buf);
        break;
    }
    case 0x15: case 0x16:                                                   /* SeekL, SeekP */
        cur_lba = loc_lba;
        push_stat(d1, 3);
        push_stat(d1 + 100000, 2);
        break;
    case 0x19:                                                              /* Test */
        if (param[0] == 0x20) { buf[0] = 0x94; buf[1] = 0x09; buf[2] = 0x19; buf[3] = 0xC0; push(d1, 3, 0, 4, buf); }
        else push_stat(d1, 3);
        break;
    case 0x1A:                                                              /* GetID */
        push_stat(d1, 3);
        if (have_disc) {
            buf[0] = 0x02; buf[1] = 0; buf[2] = 0x20; buf[3] = 0; buf[4] = 'S'; buf[5] = 'C'; buf[6] = 'E'; buf[7] = (uint8_t)region;
            push(d1 + 33000, 2, 0, 8, buf);
        } else {
            buf[0] = 0x08; buf[1] = 0x40; memset(buf + 2, 0, 6);
            push(d1 + 33000, 5, 0, 8, buf);
        }
        break;
    case 0x1E: push_stat(d1, 3); push_stat(d1 + 200000, 2); break;          /* ReadTOC */
    case 0x03: case 0x04: case 0x05: push_stat(d1, 3); break;               /* Play, Forward, Backward: no audio */
    default: {
        const uint8_t e[2] = { (uint8_t)(s | 1), 0x40 };
        push(d1, 5, 0, 2, e);
        break;
    }
    }
    nparam = 0;
}

uint32_t psx_cd_read(uint32_t reg)
{
    switch (reg) {
    case 0:
        return (uint32_t)index_ | (nparam == 0 ? 8u : 0u) | 0x10u | (resp_pos < nresp ? 0x20u : 0u) |
               (data_pos < data_len ? 0x40u : 0u);
    case 1: return resp_pos < nresp ? resp[resp_pos++] : 0;
    case 2: return (data_pos < data_len) ? databuf[data_pos++] : 0;
    default:
        if (index_ & 1) return (uint32_t)irq_flag | 0xE0u;
        return (uint32_t)irq_en | 0xE0u;
    }
}

void psx_cd_write(uint32_t reg, uint32_t v)
{
    v &= 0xFF;
    if (reg == 0) { index_ = (int)(v & 3); return; }
    switch (index_ * 4 + (int)reg) {
    case 1: command(v); break;                          /* index 0, reg 1: the command */
    case 2: if (nparam < 16) param[nparam++] = (uint8_t)v; break;
    case 3:                                             /* the request register */
        if (v & 0x80) { data_len = sec_len; data_pos = 0; }
        else data_len = data_pos = 0;
        break;
    case 6: irq_en = (int)(v & 0x1F); break;            /* index 1, reg 2 */
    case 7:                                             /* index 1, reg 3: acknowledge */
        irq_flag &= ~(int)(v & 0x1F);
        if (v & 0x40) nparam = 0;
        break;
    default: break;
    }
}

int psx_cd_dma_read(uint8_t *dst, int n)
{
    int i = 0;
    for (; i < n && data_pos < data_len; i++) dst[i] = databuf[data_pos++];
    for (; i < n; i++) dst[i] = 0;
    return n;
}
