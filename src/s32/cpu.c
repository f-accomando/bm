/*
 * s32 CPU interpreter (spec 3-4). Mirrors lua32's cpu.lua exactly,
 * including its quirks: 8-bit reads of input ports, port writes that do not
 * reach memory, flags left untouched by some instructions, 16-bit return
 * addresses on the stack.
 */
#include "s32.h"

#include <stdlib.h>
#include <string.h>

typedef s32_machine_t M;

static inline uint32_t mask(uint32_t a) { return a & S32_ADDR_MASK; }

static inline int is_input_port(uint32_t a)
{
    return a == S32_PORT_INPUT || (a >= S32_PORT_INPUT2 && a <= S32_PORT_INPUT2 + 6);
}

static inline uint16_t read16(const M *m, uint32_t a)
{
    a = mask(a);
    return (uint16_t)(m->mem[a] | m->mem[mask(a + 1)] << 8);
}

static inline uint16_t read_mem(const M *m, uint32_t a)
{
    return is_input_port(a) ? m->mem[a] : read16(m, a);
}

static void write16(M *m, uint32_t a, uint16_t v)
{
    a = mask(a);
    switch (a) {
    case S32_PORT_STAGE:
        if (m->cart && v < m->cart->stage_count)
            memcpy(m->mem + S32_TILEMAP_BASE, m->cart->stages + (size_t)v * S32_TILEMAP_BYTES,
                   S32_TILEMAP_BYTES);
        return;
    case S32_PORT_GFX_BANK:
        if (m->cart && v < m->cart->gfx_count)
            memcpy(m->mem + S32_DIR_BASE, m->cart->gfx + (size_t)v * S32_GFX_BANK_BYTES,
                   S32_GFX_BANK_BYTES);
        return;
    case S32_PORT_SCROLL_X:
        m->scroll_x = v;
        return;
    case S32_PORT_SCROLL_Y:
        m->scroll_y = v;
        return;
    }
    m->mem[a] = (uint8_t)v;
    m->mem[mask(a + 1)] = (uint8_t)(v >> 8);
}

static inline void set_flag(M *m, uint8_t f, int cond)
{
    if (cond) m->flags |= f;
    else m->flags &= (uint8_t)~f;
}

static inline void zn(M *m, uint16_t v)
{
    set_flag(m, S32_FLAG_Z, v == 0);
    set_flag(m, S32_FLAG_N, v & 0x8000);
}

static inline uint16_t imm16(const M *m) { return (uint16_t)(m->mem[mask(m->pc + 1)] | m->mem[mask(m->pc + 2)] << 8); }
static inline uint32_t addr24(const M *m) { return m->mem[mask(m->pc + 1)] | m->mem[mask(m->pc + 2)] << 8 | (uint32_t)m->mem[mask(m->pc + 3)] << 16; }

static void op_add(M *m, uint16_t op)
{
    uint32_t raw = (uint32_t)m->a + op;
    uint16_t r = (uint16_t)raw;
    set_flag(m, S32_FLAG_C, raw > 0xFFFF);
    set_flag(m, S32_FLAG_V, ((m->a ^ op) & 0x8000) == 0 && ((r ^ m->a) & 0x8000));
    zn(m, r);
    m->a = r;
}

static void op_sub(M *m, uint16_t op)
{
    uint16_t r = (uint16_t)(m->a - op);
    set_flag(m, S32_FLAG_C, m->a >= op);
    set_flag(m, S32_FLAG_V, ((m->a ^ op) & 0x8000) && ((r ^ m->a) & 0x8000));
    zn(m, r);
    m->a = r;
}

static void op_cmp(M *m, uint16_t op)
{
    set_flag(m, S32_FLAG_C, m->a >= op);
    zn(m, (uint16_t)(m->a - op));
}

static int push(M *m, uint16_t v)
{
    write16(m, (uint32_t)m->sp, v);
    m->sp -= 2;
    return m->sp < 0 ? -1 : 0;
}

static int pop(M *m, uint16_t *v)
{
    m->sp += 2;
    if (m->sp > S32_SP_INIT)
        return -1;
    *v = read16(m, (uint32_t)m->sp);
    return 0;
}

int s32_init(s32_machine_t *m, uint8_t *mem)
{
    memset(m, 0, sizeof *m);
    m->mem = mem ? mem : malloc(S32_MEM_SIZE);
    if (!m->mem)
        return -1;
    memset(m->mem, 0, S32_MEM_SIZE);
    m->sp = S32_SP_INIT;
    return 0;
}

void s32_install(s32_machine_t *m, const s32_cart_t *c)
{
    m->cart = c;
    memcpy(m->mem + S32_LOAD_ADDR, c->code, c->code_size);
    if (c->cgram)
        memcpy(m->mem + S32_CGRAM_BASE, c->cgram, S32_CGRAM_BYTES);
    if (c->stage_count)
        write16(m, S32_PORT_STAGE, 0);
    if (c->gfx_count)
        write16(m, S32_PORT_GFX_BANK, 0);
}

const char *s32_status_str(enum s32_status st)
{
    switch (st) {
    case S32_OK:                    return "ok";
    case S32_CRASH_OPCODE:          return "invalid opcode";
    case S32_CRASH_STACK_OVERFLOW:  return "stack overflow";
    case S32_CRASH_STACK_UNDERFLOW: return "stack underflow";
    case S32_CRASH_STEP_LIMIT:      return "step limit exceeded";
    }
    return "?";
}

/* Operand-address helpers for the ,X / ,Y forms */
#define AX  mask(addr24(m) + m->x)
#define AY  mask(addr24(m) + m->y)

enum s32_status s32_tick(s32_machine_t *m, const uint8_t input[8])
{
    m->mem[S32_PORT_INPUT] = input[0];
    for (int i = 1; i < 8; i++)
        m->mem[S32_PORT_INPUT2 + i - 1] = input[i];
    m->pc = S32_LOAD_ADDR;

    for (m->steps = 1; m->steps <= S32_MAX_STEPS; m->steps++) {
        uint8_t op = m->mem[m->pc];
        uint32_t a;
        uint16_t v;

        switch (op) {
        case 0x00: m->pc += 1; break;                                           /* NOP */
        case 0x01: return S32_OK;                                               /* HALT */

        case 0x10: m->a = imm16(m); zn(m, m->a); m->pc += 3; break;             /* LDA # */
        case 0x11: m->a = read_mem(m, addr24(m)); zn(m, m->a); m->pc += 4; break;
        case 0x12: write16(m, addr24(m), m->a); m->pc += 4; break;              /* STA */
        case 0x13: m->x = imm16(m); zn(m, m->x); m->pc += 3; break;
        case 0x14: m->x = read_mem(m, addr24(m)); zn(m, m->x); m->pc += 4; break;
        case 0x15: write16(m, addr24(m), m->x); m->pc += 4; break;
        case 0x16: m->y = imm16(m); zn(m, m->y); m->pc += 3; break;
        case 0x17: m->y = read_mem(m, addr24(m)); zn(m, m->y); m->pc += 4; break;
        case 0x18: write16(m, addr24(m), m->y); m->pc += 4; break;

        case 0x20: m->x = m->a; zn(m, m->x); m->pc += 1; break;                 /* TAX */
        case 0x21: m->a = m->x; zn(m, m->a); m->pc += 1; break;                 /* TXA */
        case 0x22: m->y = m->a; zn(m, m->y); m->pc += 1; break;                 /* TAY */
        case 0x23: m->a = m->y; zn(m, m->a); m->pc += 1; break;                 /* TYA */
        case 0x24: m->y = m->x; zn(m, m->y); m->pc += 1; break;                 /* TXY */
        case 0x25: m->x = m->y; zn(m, m->x); m->pc += 1; break;                 /* TYX */

        case 0x30: op_add(m, imm16(m)); m->pc += 3; break;
        case 0x31: op_sub(m, imm16(m)); m->pc += 3; break;
        case 0x32: m->a &= imm16(m); zn(m, m->a); m->pc += 3; break;
        case 0x33: m->a |= imm16(m); zn(m, m->a); m->pc += 3; break;
        case 0x34: m->a ^= imm16(m); zn(m, m->a); m->pc += 3; break;
        case 0x35: op_cmp(m, imm16(m)); m->pc += 3; break;

        case 0x40: op_add(m, read_mem(m, addr24(m))); m->pc += 4; break;
        case 0x41: op_sub(m, read_mem(m, addr24(m))); m->pc += 4; break;
        case 0x42: m->a &= read_mem(m, addr24(m)); zn(m, m->a); m->pc += 4; break;
        case 0x43: m->a |= read_mem(m, addr24(m)); zn(m, m->a); m->pc += 4; break;
        case 0x44: m->a ^= read_mem(m, addr24(m)); zn(m, m->a); m->pc += 4; break;
        case 0x45: op_cmp(m, read_mem(m, addr24(m))); m->pc += 4; break;

        case 0x50:                                                              /* ASL */
            set_flag(m, S32_FLAG_C, m->a & 0x8000);
            m->a = (uint16_t)(m->a << 1); zn(m, m->a); m->pc += 1; break;
        case 0x51:                                                              /* LSR */
            set_flag(m, S32_FLAG_C, m->a & 1);
            m->a >>= 1; zn(m, m->a); m->pc += 1; break;
        case 0x52: a = addr24(m); v = (uint16_t)(read_mem(m, a) + 1); write16(m, a, v); zn(m, v); m->pc += 4; break;
        case 0x53: a = addr24(m); v = (uint16_t)(read_mem(m, a) - 1); write16(m, a, v); zn(m, v); m->pc += 4; break;
        case 0x54: m->x++; zn(m, m->x); m->pc += 1; break;
        case 0x55: m->y++; zn(m, m->y); m->pc += 1; break;
        case 0x56: m->x--; zn(m, m->x); m->pc += 1; break;
        case 0x57: m->y--; zn(m, m->y); m->pc += 1; break;

        case 0x60: m->pc = addr24(m); break;                                    /* JMP */
        case 0x61: m->pc = (m->flags & S32_FLAG_Z) ? addr24(m) : m->pc + 4; break;
        case 0x62: m->pc = !(m->flags & S32_FLAG_Z) ? addr24(m) : m->pc + 4; break;
        case 0x63: m->pc = (m->flags & S32_FLAG_N) ? addr24(m) : m->pc + 4; break;
        case 0x64: m->pc = !(m->flags & S32_FLAG_N) ? addr24(m) : m->pc + 4; break;
        case 0x65: m->pc = (m->flags & S32_FLAG_C) ? addr24(m) : m->pc + 4; break;
        case 0x66: m->pc = !(m->flags & S32_FLAG_C) ? addr24(m) : m->pc + 4; break;
        case 0x67:                                                              /* JSR */
            a = addr24(m);
            if (push(m, (uint16_t)(m->pc + 4)))     /* only 16 bits are saved */
                goto overflow;
            m->pc = a;
            break;
        case 0x68:                                                              /* RTS */
            if (pop(m, &v))
                goto underflow;
            m->pc = v;
            break;

        case 0x70: if (push(m, m->a)) goto overflow; m->pc += 1; break;         /* PHA */
        case 0x71: if (pop(m, &m->a)) goto underflow; zn(m, m->a); m->pc += 1; break;
        case 0x72: if (push(m, m->x)) goto overflow; m->pc += 1; break;
        case 0x73: if (pop(m, &m->x)) goto underflow; zn(m, m->x); m->pc += 1; break;
        case 0x74: if (push(m, m->y)) goto overflow; m->pc += 1; break;
        case 0x75: if (pop(m, &m->y)) goto underflow; zn(m, m->y); m->pc += 1; break;

        case 0x80: m->a = m->mem[S32_PORT_INPUT]; m->pc += 1; break;            /* IN */

        case 0x90: case 0x91: {                                                 /* CLAMPX/Y */
            uint16_t lo = imm16(m);
            uint16_t hi = (uint16_t)(m->mem[mask(m->pc + 3)] | m->mem[mask(m->pc + 4)] << 8);
            uint16_t *r = op == 0x90 ? &m->x : &m->y;
            if (*r < lo) *r = lo;
            if (*r > hi) *r = hi;
            m->pc += 5;
            break;
        }

        case 0xA0: m->a = read_mem(m, AX); zn(m, m->a); m->pc += 4; break;
        case 0xA1: m->a = read_mem(m, AY); zn(m, m->a); m->pc += 4; break;
        case 0xA2: write16(m, AX, m->a); m->pc += 4; break;
        case 0xA3: write16(m, AY, m->a); m->pc += 4; break;
        case 0xA4: m->x = read_mem(m, AX); zn(m, m->x); m->pc += 4; break;
        case 0xA5: m->x = read_mem(m, AY); zn(m, m->x); m->pc += 4; break;
        case 0xA6: write16(m, AX, m->x); m->pc += 4; break;
        case 0xA7: write16(m, AY, m->x); m->pc += 4; break;
        case 0xA8: m->y = read_mem(m, AX); zn(m, m->y); m->pc += 4; break;
        case 0xA9: m->y = read_mem(m, AY); zn(m, m->y); m->pc += 4; break;
        case 0xAA: write16(m, AX, m->y); m->pc += 4; break;
        case 0xAB: write16(m, AY, m->y); m->pc += 4; break;

        case 0xB0: op_add(m, read_mem(m, AX)); m->pc += 4; break;
        case 0xB1: op_add(m, read_mem(m, AY)); m->pc += 4; break;
        case 0xB2: op_sub(m, read_mem(m, AX)); m->pc += 4; break;
        case 0xB3: op_sub(m, read_mem(m, AY)); m->pc += 4; break;
        case 0xB4: m->a &= read_mem(m, AX); zn(m, m->a); m->pc += 4; break;
        case 0xB5: m->a &= read_mem(m, AY); zn(m, m->a); m->pc += 4; break;
        case 0xB6: m->a |= read_mem(m, AX); zn(m, m->a); m->pc += 4; break;
        case 0xB7: m->a |= read_mem(m, AY); zn(m, m->a); m->pc += 4; break;
        case 0xB8: m->a ^= read_mem(m, AX); zn(m, m->a); m->pc += 4; break;
        case 0xB9: m->a ^= read_mem(m, AY); zn(m, m->a); m->pc += 4; break;
        case 0xBA: op_cmp(m, read_mem(m, AX)); m->pc += 4; break;
        case 0xBB: op_cmp(m, read_mem(m, AY)); m->pc += 4; break;
        case 0xBC: a = AX; v = (uint16_t)(read_mem(m, a) + 1); write16(m, a, v); zn(m, v); m->pc += 4; break;
        case 0xBD: a = AY; v = (uint16_t)(read_mem(m, a) + 1); write16(m, a, v); zn(m, v); m->pc += 4; break;
        case 0xBE: a = AX; v = (uint16_t)(read_mem(m, a) - 1); write16(m, a, v); zn(m, v); m->pc += 4; break;
        case 0xBF: a = AY; v = (uint16_t)(read_mem(m, a) - 1); write16(m, a, v); zn(m, v); m->pc += 4; break;

        default:
            m->crash_pc = m->pc;
            return S32_CRASH_OPCODE;
        }
        m->pc = mask(m->pc);
    }
    m->crash_pc = m->pc;
    return S32_CRASH_STEP_LIMIT;

overflow:
    m->crash_pc = m->pc;
    return S32_CRASH_STACK_OVERFLOW;
underflow:
    m->crash_pc = m->pc;
    return S32_CRASH_STACK_UNDERFLOW;
}
