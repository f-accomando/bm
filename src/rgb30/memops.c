/*
 * memcpy, memmove, memset and memcmp for the RGB30 kernel. The libc of this build (picolibc) has byte loops for them
 * (G3, 26 Oct: the draw descriptor's memset cost 3.4 us a draw). These move 8 or 32 bytes a step. The destination is
 * aligned first; the source may be unaligned (Normal memory takes that). Device memory is reached through volatile
 * accessors, not through these.
 * Each function says "no-tree-loop-distribute-patterns": the compiler must not turn the loops into calls of themselves.
 */
#include <stddef.h>
#include <stdint.h>

#define NOPAT __attribute__((optimize("no-tree-loop-distribute-patterns", "no-tree-loop-vectorize")))

typedef uint64_t u64;
typedef u64 __attribute__((aligned(1))) u64u;           /* a word that may be unaligned */

NOPAT void *memcpy(void *restrict dst, const void *restrict src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    if (n >= 16) {
        while ((uintptr_t)d & 7) {
            *d++ = *s++;
            n--;
        }
        u64 *dw = (u64 *)d;
        const u64u *sw = (const u64u *)s;
        while (n >= 32) {
            const u64 a = sw[0], b = sw[1], c = sw[2], e = sw[3];
            dw[0] = a;
            dw[1] = b;
            dw[2] = c;
            dw[3] = e;
            dw += 4;
            sw += 4;
            n -= 32;
        }
        while (n >= 8) {
            *dw++ = *sw++;
            n -= 8;
        }
        d = (uint8_t *)dw;
        s = (const uint8_t *)sw;
    }
    while (n--)
        *d++ = *s++;
    return dst;
}

NOPAT void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    if (d == s || n == 0)
        return dst;
    if (d < s || d >= s + n)
        return memcpy(dst, src, n);                     /* a forward copy is safe */
    d += n;                                             /* overlap with dst above src: from the end */
    s += n;
    while (n >= 8 && ((uintptr_t)d & 7) == 0) {
        d -= 8;
        s -= 8;
        n -= 8;
        *(u64 *)d = *(const u64u *)s;
    }
    while (n--)
        *--d = *--s;
    return dst;
}

NOPAT void *memset(void *dst, int c, size_t n)
{
    uint8_t *d = dst;
    const uint8_t b = (uint8_t)c;
    if (n >= 16) {
        while ((uintptr_t)d & 7) {
            *d++ = b;
            n--;
        }
        const u64 w = 0x0101010101010101ull * b;
        u64 *dw = (u64 *)d;
        while (n >= 32) {
            dw[0] = w;
            dw[1] = w;
            dw[2] = w;
            dw[3] = w;
            dw += 4;
            n -= 32;
        }
        while (n >= 8) {
            *dw++ = w;
            n -= 8;
        }
        d = (uint8_t *)dw;
    }
    while (n--)
        *d++ = b;
    return dst;
}

NOPAT int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    while (n >= 8 && *(const u64u *)x == *(const u64u *)y) {
        x += 8;
        y += 8;
        n -= 8;
    }
    while (n--) {
        if (*x != *y)
            return *x - *y;
        x++;
        y++;
    }
    return 0;
}
