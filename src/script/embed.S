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
    .globl bm_sound_cart, bm_sound_cart_end
bm_sound_cart:
    .incbin "sound.bm"
bm_sound_cart_end:

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

/* the word completion (M30): its Lua library (require "predict") and its
 * dictionaries, built by scripts/mkwords.py (require "words") */
    .section .rodata
    .balign 4
    .globl bm_lib_predict, bm_lib_predict_end
bm_lib_predict:
    .incbin "src/ai/predict.lua"
bm_lib_predict_end:

    .section .rodata
    .balign 4
    .globl bm_lib_words, bm_lib_words_end
bm_lib_words:
    .incbin "words.lua"
bm_lib_words_end:

/* typing with the pad: compose and the on-screen keyboard (require "padtype") */
    .section .rodata
    .balign 4
    .globl bm_lib_padtype, bm_lib_padtype_end
bm_lib_padtype:
    .incbin "src/ai/padtype.lua"
bm_lib_padtype_end:

    .section .rodata
    .balign 4
    .globl bm_assistant_cart, bm_assistant_cart_end
bm_assistant_cart:
    .incbin "assistant.bm"
bm_assistant_cart_end:

/* bm Code, the code editor (Dev tab) */
    .section .rodata
    .balign 4
    .globl bm_code_cart, bm_code_cart_end
bm_code_cart:
    .incbin "code.bm"
bm_code_cart_end:

/* the public key that checks the releases (M19, src/net/release.c): PEM
 * text, NUL-terminated; a placeholder until scripts/release-key.sh */
    .section .rodata
    .balign 4
    .globl bm_release_key
bm_release_key:
    .incbin "keys/release-pub.pem"
    .byte 0

/* the public key that checks the Market's catalog (M25, src/net/catalog.c):
 * PEM text, NUL-terminated; a placeholder until scripts/market-key.sh */
    .section .rodata
    .balign 4
    .globl bm_market_key
bm_market_key:
    .incbin "keys/market-pub.pem"
    .byte 0

    .section .rodata
    .balign 4
    .globl bm_studio_cart, bm_studio_cart_end
bm_studio_cart:
    .incbin "studio.bm"
bm_studio_cart_end:

    .section .rodata
    .balign 4
    .globl bm_animator_cart, bm_animator_cart_end
bm_animator_cart:
    .incbin "animator.bm"
bm_animator_cart_end:

/* what bm Studio and bm Animator share (require "bm3d") */
    .section .rodata
    .balign 4
    .globl bm_lib_bm3d, bm_lib_bm3d_end
bm_lib_bm3d:
    .incbin "src/script/bm3d.lua"
bm_lib_bm3d_end:

/* what the games share (require "bmlib", R10) */
    .section .rodata
    .balign 4
    .globl bm_lib_bmlib, bm_lib_bmlib_end
bm_lib_bmlib:
    .incbin "src/script/bmlib.lua"
bm_lib_bmlib_end:

/* games over the network (require "bmnet") */
    .section .rodata
    .balign 4
    .globl bm_lib_bmnet, bm_lib_bmnet_end
bm_lib_bmnet:
    .incbin "src/script/bmnet.lua"
bm_lib_bmnet_end:

/* the language of patterns for music (require "riff") */
    .section .rodata
    .balign 4
    .globl bm_lib_riff, bm_lib_riff_end
bm_lib_riff:
    .incbin "src/script/riff.lua"
bm_lib_riff_end:

/* the mouse in bm's tools (require "bmui") */
    .section .rodata
    .balign 4
    .globl bm_lib_bmui, bm_lib_bmui_end
bm_lib_bmui:
    .incbin "src/script/bmui.lua"
bm_lib_bmui_end:

    .section .rodata
    .balign 4
    .globl bm_mesh_cart, bm_mesh_cart_end
bm_mesh_cart:
    .incbin "mesh.bm"
bm_mesh_cart_end:

    .section .rodata
    .balign 4
    .globl bm_pixel_cart, bm_pixel_cart_end
bm_pixel_cart:
    .incbin "pixel.bm"
bm_pixel_cart_end:
    .section .rodata
    .balign 4
    .globl bm_texroom_cart, bm_texroom_cart_end
bm_texroom_cart:
    .incbin "texroom.bm"
bm_texroom_cart_end:
