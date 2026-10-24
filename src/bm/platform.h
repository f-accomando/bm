/*
 * platform.h: the services that the runtime takes from the platform.
 *
 * The files of src/bm that need the kernel, the USB stack, the network or
 * the AI code include this header and no other platform header. The Pi
 * kernel, the RGB30 port and bmhost (tests/host) link by symbol name, so
 * this file holds plain declarations and no table of function pointers.
 * The shared types (prompt_t, pointer_t, HID_*, img3d_provider_t) still
 * come from the kernel headers that this file includes.
 */
#ifndef BM_PLATFORM_H
#define BM_PLATFORM_H

/* Settings and config.txt */
#include "kernel/config.h"
#include "kernel/crt.h"

/* Input: pads, keys, pointer, prompts, key combinations */
#include "kernel/controls.h"
#include "kernel/input.h"
#include "kernel/pointer.h"
#include "kernel/prompts.h"
#include "kernel/syskeys.h"
#include "usb/hid.h"

/* Reports, crash crumbs and the names of the tools */
#include "kernel/reports.h"
#include "kernel/crumbs.h"
#include "kernel/toolnames.h"

/* Fibers: a download waits without blocking the frame */
#include "kernel/fiber.h"

/* Network: cartridge UDP, pictures, HTTP, the stack */
#include "net/cartnet.h"
#include "net/img3d.h"
#include "net/http.h"
#include "net/net.h"

/* AI: the assistant, its Lua API and its network client */
#include "ai/assist.h"
#include "ai/lua_ai.h"
#include "ai/net.h"

#endif
