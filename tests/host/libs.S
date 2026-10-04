/* bmhost: the Lua libraries of require() (src/bm/require.c), as in the
 * kernel's src/script/embed.S. Assembled from the top of the repository,
 * with -I$(BUILD) for the dictionaries of the word completion. */
    .section .rodata
    .global bm_lib_assist, bm_lib_assist_end, bm_lib_bm3d, bm_lib_bm3d_end, bm_lib_bmlib, bm_lib_bmlib_end
    .global bm_lib_predict, bm_lib_predict_end, bm_lib_words, bm_lib_words_end, bm_lib_bmnet, bm_lib_bmnet_end
bm_lib_assist:
    .incbin "src/ai/assist.lua"
bm_lib_assist_end:
    .byte 0
bm_lib_bm3d:
    .incbin "src/script/bm3d.lua"
bm_lib_bm3d_end:
    .byte 0
bm_lib_bmlib:
    .incbin "src/script/bmlib.lua"
bm_lib_bmlib_end:
    .byte 0
bm_lib_bmnet:
    .incbin "src/script/bmnet.lua"
bm_lib_bmnet_end:
    .byte 0
bm_lib_predict:
    .incbin "src/ai/predict.lua"
bm_lib_predict_end:
    .byte 0
bm_lib_words:
    .incbin "words.lua"
bm_lib_words_end:
    .byte 0
    .section .note.GNU-stack,"",@progbits
