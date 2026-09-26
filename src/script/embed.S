/* Lua scripts built into the kernel image. */
    .section .rodata
    .balign 4
    .globl boot_lua, boot_lua_end
boot_lua:
    .incbin "src/script/boot.lua"
boot_lua_end:
    .byte 0

/* s32 cartridges built into the kernel (spec/s32, from lua32) */
    .section .rodata
    .balign 4
    .globl s32_demo_cart, s32_demo_cart_end
s32_demo_cart:
    .incbin "spec/s32/conformance/demo.cart"
s32_demo_cart_end:

/* native bm33 demo cartridge, packed by scripts/mkb33.py (see Makefile) */
    .section .rodata
    .balign 4
    .globl b33_demo_cart, b33_demo_cart_end
b33_demo_cart:
    .incbin "demo.b33"          /* found through -Wa,-I$(BUILD) */
b33_demo_cart_end:

    .section .rodata
    .balign 4
    .globl b33_stress_cart, b33_stress_cart_end
b33_stress_cart:
    .incbin "stress.b33"
b33_stress_cart_end:
