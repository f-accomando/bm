/* The Lua libraries the cartridges can require (src/bm/require.c), as the
 * Pi's src/script/embed.S has them; the word dictionaries and the Texture
 * Room benchmark are built files of the Pi's build: empty here. */
    .section .rodata
    .balign 4
    .globl bm_lib_assist, bm_lib_assist_end
bm_lib_assist:
    .incbin "src/ai/assist.lua"
bm_lib_assist_end:
    .byte 0

    .balign 4
    .globl bm_lib_predict, bm_lib_predict_end
bm_lib_predict:
    .incbin "src/ai/predict.lua"
bm_lib_predict_end:
    .byte 0

    .balign 4
    .globl bm_lib_bm3d, bm_lib_bm3d_end
bm_lib_bm3d:
    .incbin "src/script/bm3d.lua"
bm_lib_bm3d_end:
    .byte 0

    .balign 4
    .globl bm_lib_bmlib, bm_lib_bmlib_end
bm_lib_bmlib:
    .incbin "src/script/bmlib.lua"
bm_lib_bmlib_end:
    .byte 0

    .balign 4
    .globl bm_lib_bmnet, bm_lib_bmnet_end
bm_lib_bmnet:
    .incbin "src/script/bmnet.lua"
bm_lib_bmnet_end:
    .byte 0

    .balign 4
    .globl bm_lib_words, bm_lib_words_end, bm_texroom_cart, bm_texroom_cart_end
bm_lib_words:
bm_lib_words_end:
bm_texroom_cart:
bm_texroom_cart_end:
    .byte 0

/* the public key that checks the releases (src/net/release.c), as on the
 * Pi: PEM text, NUL-terminated */
    .section .rodata
    .balign 4
    .globl bm_release_key
bm_release_key:
    .incbin "keys/release-pub.pem"
    .byte 0
