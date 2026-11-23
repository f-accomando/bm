/*
 * Shell SDK: the start file of a new project (see shell_sdk_tpl.h). Original code.
 */
#include "shell_sdk_tpl.h"

#include <stdint.h>

#include "shell_sdk_proj.h"

extern const uint8_t bm_sdk_tpl_nes[], bm_sdk_tpl_nes_end[];
extern const uint8_t bm_sdk_tpl_snes[], bm_sdk_tpl_snes_end[];

const char *sdk_template(int target, size_t *len)
{
    if (target == SDK_NES) {
        *len = (size_t)(bm_sdk_tpl_nes_end - bm_sdk_tpl_nes);
        return (const char *)bm_sdk_tpl_nes;
    }
    if (target == SDK_SNES) {
        *len = (size_t)(bm_sdk_tpl_snes_end - bm_sdk_tpl_snes);
        return (const char *)bm_sdk_tpl_snes;
    }
    *len = 0;
    return "";
}
