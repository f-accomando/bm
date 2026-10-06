/*
 * The dev kit's profiler of functions (R14): where a cartridge's frame
 * goes, function by function. Two kinds of events cut the time into
 * pieces: the count hook of the runtime (every 1000 Lua instructions) and
 * every call of a C function (spr, map, the 3D..., through a test in
 * Lua's ldo.c: luai_cprof). Each piece of time since the last event goes
 * to the function running at that event ("self") and to every function
 * on the stack ("total", once each when recursive): the C functions
 * exactly, the Lua ones as close as the events are. Off, it costs one
 * test per C call.
 *
 * The rows of the last whole second (60 frames) are kept for the dev
 * kit's "functions" page (F11 three times) and profile() of the games.
 */
#ifndef BM_PROFILE_H
#define BM_PROFILE_H

#include <stdint.h>
#include "lua.h"

typedef struct {
    char name[24];      /* "draw_map", "spr", "update" (a method), "?" */
    char where[24];     /* "main.lua:120", "[C]" */
    int c;              /* a C function (of the console or of Lua's libraries) */
    uint32_t self_us;   /* per frame: its own time */
    uint32_t total_us;  /* per frame: with the functions it calls */
    uint32_t calls;     /* per frame, C functions only (0 for Lua) */
} prof_row_t;

/* Collecting on or off; clock() gives microseconds (timer_ticks). Off
 * forgets the rows. */
void prof_enable(int on, uint32_t (*clock)(void));
int  prof_enabled(void);

/* A callback of the cartridge starts ("_update": its name, as the runtime
 * calls it; the time before it is not counted) and ends (the time since
 * the last event goes where the last one went). */
void prof_begin(const char *name);
void prof_end(void);

/* From the runtime's count hook. */
void prof_sample(lua_State *L);

/* The end of a frame: after 60 frames the rows of that second are kept. */
void prof_frame(void);

/* The rows of the last whole second, the costliest first (self time), up
 * to n; returns how many, and in *frames the frames they are the mean of
 * (0: none yet). */
int  prof_rows(prof_row_t *out, int n, uint32_t *frames);

#endif
