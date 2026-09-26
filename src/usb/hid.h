#ifndef HID_H
#define HID_H

#include <stdint.h>

/* Game buttons, same order as the .b33 btn() numbers */
#define HID_LEFT    (1u << 0)
#define HID_RIGHT   (1u << 1)
#define HID_UP      (1u << 2)
#define HID_DOWN    (1u << 3)
#define HID_A       (1u << 4)
#define HID_B       (1u << 5)
#define HID_START   (1u << 6)
#define HID_SELECT  (1u << 7)

void hid_keyboard_attach(void);
int  hid_gamepad_attach(const uint8_t *report_desc, uint32_t len);
void hid_xbox360_attach(void);
void hid_report(int kind, const uint8_t *data, uint32_t len);

/* Text input from the keyboard (layout applied): next byte or -1. */
int      hid_getc(void);
/* Buttons held now, from keyboard or gamepad. */
uint32_t hid_buttons(void);
/* 1 once per press of Esc (keyboard) or Start+Select (gamepad). */
int      hid_quit_pressed(void);

/* "it" (default) or "us" */
void hid_set_layout(const char *name);
const char *hid_layout(void);

#endif
