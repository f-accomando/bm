/* Lua scripts built into the kernel image. */
    .section .rodata
    .balign 4
    .globl boot_lua, boot_lua_end
boot_lua:
    .incbin "src/script/boot.lua"
boot_lua_end:
    .byte 0

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

    .section .rodata
    .balign 4
    .globl bm_texroom_cart, bm_texroom_cart_end
bm_texroom_cart:
    .incbin "texroom.bm"
bm_texroom_cart_end:
