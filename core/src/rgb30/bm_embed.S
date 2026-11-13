/* The Lua libraries the cartridges can require (core/src/bm/require.c), as the
 * Pi's core/src/script/embed.S has them; the word dictionaries and the Texture
 * Room benchmark are built files of the Pi's build: empty here. */
    .section .rodata
    .balign 4
    .globl bm_lib_assist, bm_lib_assist_end
bm_lib_assist:
    .incbin "core/src/ai/assist.lua"
bm_lib_assist_end:
    .byte 0

    .balign 4
    .globl bm_lib_predict, bm_lib_predict_end
bm_lib_predict:
    .incbin "core/src/ai/predict.lua"
bm_lib_predict_end:
    .byte 0

    .balign 4
    .globl bm_lib_padtype, bm_lib_padtype_end
bm_lib_padtype:
    .incbin "core/src/ai/padtype.lua"
bm_lib_padtype_end:
    .byte 0

    .balign 4
    .globl bm_lib_bm3d, bm_lib_bm3d_end
bm_lib_bm3d:
    .incbin "core/src/script/bm3d.lua"
bm_lib_bm3d_end:
    .byte 0

    .balign 4
    .globl bm_lib_bmlib, bm_lib_bmlib_end
bm_lib_bmlib:
    .incbin "core/src/script/bmlib.lua"
bm_lib_bmlib_end:
    .byte 0

    .balign 4
    .globl bm_lib_bmnet, bm_lib_bmnet_end
bm_lib_bmnet:
    .incbin "core/src/script/bmnet.lua"
bm_lib_bmnet_end:
    .byte 0

    .balign 4
    .globl bm_lib_riff, bm_lib_riff_end
bm_lib_riff:
    .incbin "core/src/script/riff.lua"
bm_lib_riff_end:
    .byte 0

    .balign 4
    .globl bm_lib_bmui, bm_lib_bmui_end
bm_lib_bmui:
    .incbin "core/src/script/bmui.lua"
bm_lib_bmui_end:
    .byte 0

    .balign 4
    .globl bm_lib_snes_asm, bm_lib_snes_asm_end
bm_lib_snes_asm:
    .incbin "core/src/script/snes_asm.lua"
bm_lib_snes_asm_end:
    .byte 0

    .balign 4
    .globl bm_lib_words, bm_lib_words_end
bm_lib_words:
bm_lib_words_end:
    .byte 0

/* the public key that checks the releases (core/src/net/release.c), as on the
 * Pi: PEM text, NUL-terminated */
    .section .rodata
    .balign 4
    .globl bm_release_key
bm_release_key:
    .incbin "core/keys/release-pub.pem"
    .byte 0

/* the Market's key (core/src/net/catalog.c), as on the Pi: its own Market tab
 * shows the catalog's .b16 games */
    .section .rodata
    .balign 4
    .globl bm_market_key
bm_market_key:
    .incbin "core/keys/market-pub.pem"
    .byte 0
