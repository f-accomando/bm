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

/* native bm demo cartridge, packed by scripts/mkbm.py (see Makefile) */
    .section .rodata
    .balign 4
    .globl bm_demo_cart, bm_demo_cart_end
bm_demo_cart:
    .incbin "demo.bm"          /* found through -Wa,-I$(BUILD) */
bm_demo_cart_end:

    .section .rodata
    .balign 4
    .globl bm_editor_cart, bm_editor_cart_end
bm_editor_cart:
    .incbin "editor.bm"
bm_editor_cart_end:

    .section .rodata
    .balign 4
    .globl bm_stress_cart, bm_stress_cart_end
bm_stress_cart:
    .incbin "stress.bm"
bm_stress_cart_end:
