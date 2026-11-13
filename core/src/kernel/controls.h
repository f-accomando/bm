/* The player's remap of the game buttons (CONTROLS-1; no Settings page since 28 Oct 2026, the player writes controls= in bm/config.txt).
 *
 * One key of bm/config.txt, controls=, holds the changed actions as
 * space-separated code:button pairs (e.g. "c1:x s2:l1 mc:y"); a missing action
 * keeps its default (the game's own binding). Codes: c1..c4 the scroller
 * scheme's act1..act4, t1..t4 the top-down's, s1..s8 the stick's act1..act8,
 * pa the pause, mo and mc the menus' OK and cancel. Buttons: a b x y l1 r1 l2 r2
 * st se. The 19 codes fit the 127 characters of a config value (19 x 6 - 1 =
 * 113). Movement and the shoulders of the scroller and top-down schemes are not
 * remappable. */
#ifndef KERNEL_CONTROLS_H
#define KERNEL_CONTROLS_H

#define CONTROLS_COUNT 19

int controls_count(void);
const char *controls_code(int i);       /* "c1" */
const char *controls_scheme(int i);     /* "scroller", "topdown", "stick" or "system" */
const char *controls_label(int i);      /* for the menu: "Action 1", "Pause", "OK" ... */
const char *controls_default(int i);    /* the RGB30 button of the default: "b", "y", "a", "x", "r2", "st", "mo", "mc" */

/* The button set for action i, or NULL when it is on its default. */
const char *controls_get(int i);
/* button NULL (or "default") puts the action back on its default; returns 0,
 * or -1 for a button that is not valid, with nothing changed. Saves bm/config.txt. */
int controls_set(int i, const char *button);
/* Every action back on its default; saves. */
void controls_reset(void);
int controls_valid_button(const char *button);
/* The button the player set for the keymap action `action` of `scheme`, or NULL. */
const char *controls_lookup(const char *scheme, const char *action);

#endif
