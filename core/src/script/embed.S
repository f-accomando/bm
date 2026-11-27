/* the development assistant (M30): knowledge base and network, built by
 * core/scripts/mkassist.py (a clean kernel has none: the Assistant app carries it, core/src/kernel/appdata.c); its panel, a Lua library (require "assist") */
    .section .rodata
    .balign 4
    .globl bm_assist_bin, bm_assist_bin_end
bm_assist_bin:
bm_assist_bin_end:

    .section .rodata
    .balign 4
    .globl bm_lib_assist, bm_lib_assist_end
bm_lib_assist:
    .incbin "core/src/ai/assist.lua"
bm_lib_assist_end:

/* the word completion (M30): its Lua library (require "predict") and its
 * dictionaries, built by core/scripts/mkwords.py (require "words") */
    .section .rodata
    .balign 4
    .globl bm_lib_predict, bm_lib_predict_end
bm_lib_predict:
    .incbin "core/src/ai/predict.lua"
bm_lib_predict_end:

    .section .rodata
    .balign 4
    .globl bm_lib_words, bm_lib_words_end
bm_lib_words:
bm_lib_words_end:

/* typing with the pad: compose and the on-screen keyboard (require "padtype") */
    .section .rodata
    .balign 4
    .globl bm_lib_padtype, bm_lib_padtype_end
bm_lib_padtype:
    .incbin "core/src/ai/padtype.lua"
bm_lib_padtype_end:

/* the public key that checks the releases (M19, core/src/net/release.c): PEM
 * text, NUL-terminated; a placeholder until core/scripts/release-key.sh */
    .section .rodata
    .balign 4
    .globl bm_release_key
bm_release_key:
    .incbin "core/keys/release-pub.pem"
    .byte 0

/* the public key that checks the Market's catalog (M25, core/src/net/catalog.c):
 * PEM text, NUL-terminated; a placeholder until core/scripts/market-key.sh */
    .section .rodata
    .balign 4
    .globl bm_market_key
bm_market_key:
    .incbin "core/keys/market-pub.pem"
    .byte 0

/* what bm Studio and bm Animator share (require "bm3d") */
    .section .rodata
    .balign 4
    .globl bm_lib_bm3d, bm_lib_bm3d_end
bm_lib_bm3d:
    .incbin "core/src/script/bm3d.lua"
bm_lib_bm3d_end:

/* what the games share (require "bmlib", R10) */
    .section .rodata
    .balign 4
    .globl bm_lib_bmlib, bm_lib_bmlib_end
bm_lib_bmlib:
    .incbin "core/src/script/bmlib.lua"
bm_lib_bmlib_end:

/* games over the network (require "bmnet") */
    .section .rodata
    .balign 4
    .globl bm_lib_bmnet, bm_lib_bmnet_end
bm_lib_bmnet:
    .incbin "core/src/script/bmnet.lua"
bm_lib_bmnet_end:

/* the language of patterns for music (require "riff") */
    .section .rodata
    .balign 4
    .globl bm_lib_riff, bm_lib_riff_end
bm_lib_riff:
    .incbin "core/src/script/riff.lua"
bm_lib_riff_end:

/* the mouse in bm's tools (require "bmui") */
    .section .rodata
    .balign 4
    .globl bm_lib_bmui, bm_lib_bmui_end
bm_lib_bmui:
    .incbin "core/src/script/bmui.lua"
bm_lib_bmui_end:

/* the 65C816 assembler and the SNES ROM builder (require "snes_asm") */
    .section .rodata
    .balign 4
    .globl bm_lib_snes_asm, bm_lib_snes_asm_end
bm_lib_snes_asm:
    .incbin "core/src/script/snes_asm.lua"
bm_lib_snes_asm_end:

/* the ASM listing of a ROM in the Shell SDK: the 65C816 disassembler and the driver */
    .section .rodata
    .balign 4
    .globl bm_lib_snesrom, bm_lib_snesrom_end
bm_lib_snesrom:
    .incbin "core/lua/snesrom.lua"
bm_lib_snesrom_end:
    .globl bm_lib_sdk_dis, bm_lib_sdk_dis_end
    .balign 4
bm_lib_sdk_dis:
    .incbin "core/src/script/sdk_dis.lua"
bm_lib_sdk_dis_end:

/* the start files of the Shell SDK: a NES and a SNES program that shows a blue screen (New in the Code tab) */
    .section .rodata
    .balign 4
    .globl bm_sdk_tpl_nes, bm_sdk_tpl_nes_end, bm_sdk_tpl_snes, bm_sdk_tpl_snes_end
    .globl bm_sdk_tpl_nes_c, bm_sdk_tpl_nes_c_end, bm_sdk_tpl_snes_c, bm_sdk_tpl_snes_c_end
bm_sdk_tpl_snes_c:
    .incbin "core/src/script/sdk_tpl_snes_c.txt"
bm_sdk_tpl_snes_c_end:
bm_sdk_tpl_nes_c:
    .incbin "core/src/script/sdk_tpl_nes_c.txt"
bm_sdk_tpl_nes_c_end:
bm_sdk_tpl_nes:
    .incbin "core/src/script/sdk_tpl_nes.s"
bm_sdk_tpl_nes_end:
bm_sdk_tpl_snes:
    .incbin "core/src/script/sdk_tpl_snes.s"
bm_sdk_tpl_snes_end:

