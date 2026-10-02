/* bmhost: the Lua libraries of require() (src/bm/require.c), as in the
 * kernel's src/script/embed.S. Assembled from the top of the repository. */
    .section .rodata
    .global bm_lib_assist, bm_lib_assist_end, bm_lib_bm3d, bm_lib_bm3d_end
bm_lib_assist:
    .incbin "src/ai/assist.lua"
bm_lib_assist_end:
    .byte 0
bm_lib_bm3d:
    .incbin "src/script/bm3d.lua"
bm_lib_bm3d_end:
    .byte 0
    .section .note.GNU-stack,"",@progbits
