/* bmhost: the Lua libraries of require() (core/src/bm/require.c), as in the
 * kernel's core/src/script/embed.S. Assembled from the top of the repository,
 * with -I$(BUILD) for the dictionaries of the word completion. */
    .section .rodata
    .global bm_lib_assist, bm_lib_assist_end, bm_lib_bm3d, bm_lib_bm3d_end, bm_lib_bmlib, bm_lib_bmlib_end
    .global bm_lib_predict, bm_lib_predict_end, bm_lib_words, bm_lib_words_end, bm_lib_bmnet, bm_lib_bmnet_end
    .global bm_lib_padtype, bm_lib_padtype_end, bm_lib_riff, bm_lib_riff_end, bm_lib_bmui, bm_lib_bmui_end, bm_lib_snes_asm, bm_lib_snes_asm_end
bm_lib_assist:
    .incbin "core/src/ai/assist.lua"
bm_lib_assist_end:
    .byte 0
bm_lib_bm3d:
    .incbin "core/src/script/bm3d.lua"
bm_lib_bm3d_end:
    .byte 0
bm_lib_bmlib:
    .incbin "core/src/script/bmlib.lua"
bm_lib_bmlib_end:
    .byte 0
bm_lib_bmnet:
    .incbin "core/src/script/bmnet.lua"
bm_lib_bmnet_end:
    .byte 0
bm_lib_predict:
    .incbin "core/src/ai/predict.lua"
bm_lib_predict_end:
    .byte 0
bm_lib_words:
    .incbin "words.lua"
bm_lib_words_end:
    .byte 0
bm_lib_padtype:
    .incbin "core/src/ai/padtype.lua"
bm_lib_padtype_end:
    .byte 0
bm_lib_riff:
    .incbin "core/src/script/riff.lua"
bm_lib_riff_end:
bm_lib_bmui:
    .incbin "core/src/script/bmui.lua"
bm_lib_bmui_end:
    .byte 0
bm_lib_snes_asm:
    .incbin "core/src/script/snes_asm.lua"
bm_lib_snes_asm_end:
    .byte 0
    .section .note.GNU-stack,"",@progbits
