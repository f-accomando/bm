/*
 * Shell SDK: the skeleton of the IDE. Row 0 shows the tab bar (as Shell Code does), the rest of the
 * screen shows the active tab. F1 to F5 select a tab; Esc or Ctrl+Q leaves.
 */
#include "shell_sdk.h"

#include <stdio.h>
#include <string.h>

#include "gfx/console.h"
#include "input.h"
#include "usb/hid.h"

static const char *const tab_names[SDK_TAB_COUNT] = { "Project", "Code", "Sprite", "Map", "Sounds" };

const char *shell_sdk_tab_name(int tab)
{
    return tab >= 0 && tab < SDK_TAB_COUNT ? tab_names[tab] : "";
}

int shell_sdk_tab_for_key(int key)
{
    return key >= HID_KEY_F1 && key < HID_KEY_F1 + SDK_TAB_COUNT ? key - HID_KEY_F1 : -1;
}

static void draw(int tab)
{
    char left[96];
    size_t pos = 0;
    for (int i = 0; i < SDK_TAB_COUNT; i++)
        pos += (size_t)snprintf(left + pos, sizeof left - pos, i == tab ? "[%s] " : "%s ", tab_names[i]);
    console_set_status(left, "F1-F5 tab  Esc exit");

    uint32_t cols, rows;
    console_size(&cols, &rows);
    char b[48];
    console_write("\x1b[0m\x1b[2J");
    snprintf(b, sizeof b, "\x1b[%d;%dH", (int)rows / 2, 3);
    console_write(b);
    console_write(tab_names[tab]);
    console_write(": not built yet");
}

int shell_sdk_open(void)
{
    int tab = SDK_TAB_PROJECT;
    hid_text_mode(1);
    console_clear();
    for (;;) {
        draw(tab);
        const int c = (unsigned char)input_getc();
        const int t = shell_sdk_tab_for_key(c);
        if (t >= 0)
            tab = t;
        else if (c == 0x1B || c == 0x11)
            break;
    }
    hid_text_mode(0);
    console_clear();
    return 0;
}
