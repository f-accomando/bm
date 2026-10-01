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

/* the development assistant (M30): knowledge base and network, built by
 * scripts/mkassist.py; its panel, a Lua library (require "assist") */
    .section .rodata
    .balign 4
    .globl bm_assist_bin, bm_assist_bin_end
bm_assist_bin:
    .incbin "assist.bin"
bm_assist_bin_end:

    .section .rodata
    .balign 4
    .globl bm_lib_assist, bm_lib_assist_end
bm_lib_assist:
    .incbin "src/ai/assist.lua"
bm_lib_assist_end:

    .section .rodata
    .balign 4
    .globl bm_assistant_cart, bm_assistant_cart_end
bm_assistant_cart:
    .incbin "assistant.bm"
bm_assistant_cart_end:

/* the public key that checks the releases (M19, src/net/release.c): PEM
 * text, NUL-terminated; a placeholder until scripts/release-key.sh */
    .section .rodata
    .balign 4
    .globl bm_release_key
bm_release_key:
    .incbin "keys/release-pub.pem"
    .byte 0
