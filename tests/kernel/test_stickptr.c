/*
 * The stick as a pointer (src/kernel/stickptr.c): the option parsed, the
 * start in the middle, the speed, the clamping and the buttons.
 */
#include "kernel/stickptr.h"

#include <stdio.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    stickptr_t s;
    int i;

    CHECK(stickptr_parse(NULL) == 0);
    CHECK(stickptr_parse("off") == 0);
    CHECK(stickptr_parse("on") == 1);
    CHECK(stickptr_parse("ON") == 0);

    stickptr_reset(&s, 640, 360);
    CHECK(s.x == 320 && s.y == 180 && s.buttons == 0 && s.pressed == 0);

    for (i = 0; i < 10; i++) stickptr_step(&s, 1, 0, 0, 640, 360);
    CHECK(s.x > 320 + 40);
    CHECK(s.y == 180);

    for (i = 0; i < 1000; i++) stickptr_step(&s, 1, 1, 0, 640, 360);
    CHECK(s.x == 639 && s.y == 359);
    for (i = 0; i < 1000; i++) stickptr_step(&s, -1, -1, 0, 640, 360);
    CHECK(s.x == 0 && s.y == 0);

    stickptr_step(&s, 0, 0, 32768, 640, 360);
    CHECK(s.buttons == 1 && s.pressed == 1);
    stickptr_step(&s, 0, 0, 32768, 640, 360);
    CHECK(s.buttons == 1 && s.pressed == 0);
    stickptr_step(&s, 0, 0, 32768 | 16384, 640, 360);
    CHECK(s.buttons == 3 && s.pressed == 2);
    stickptr_step(&s, 0, 0, 0, 640, 360);
    CHECK(s.buttons == 0 && s.pressed == 0);

    if (fails) { printf("test_stickptr: %d failed\n", fails); return 1; }
    printf("test_stickptr: ok\n");
    return 0;
}
