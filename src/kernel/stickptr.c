#include "stickptr.h"

#include <string.h>

#define PAD_R3 32768u
#define PAD_L3 16384u

int stickptr_parse(const char *v)
{
    return v && strcmp(v, "on") == 0;
}

void stickptr_reset(stickptr_t *s, int w, int h)
{
    s->x = (float)w / 2;
    s->y = (float)h / 2;
    s->buttons = 0;
    s->pressed = 0;
}

void stickptr_step(stickptr_t *s, float sx, float sy, uint32_t pad, int w, int h)
{
    uint8_t b = (uint8_t)((pad & PAD_R3 ? 1 : 0) | (pad & PAD_L3 ? 2 : 0));

    /* both axes scale with w: the same speed right and down */
    s->x += sx * (float)w / 150;
    s->y += sy * (float)w / 150;
    if (s->x < 0) s->x = 0;
    if (s->x > (float)(w - 1)) s->x = (float)(w - 1);
    if (s->y < 0) s->y = 0;
    if (s->y > (float)(h - 1)) s->y = (float)(h - 1);
    s->pressed = (uint8_t)(b & ~s->buttons);
    s->buttons = b;
}
