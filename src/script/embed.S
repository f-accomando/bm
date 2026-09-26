/* Lua scripts built into the kernel image. */
    .section .rodata
    .balign 4
    .globl boot_lua, boot_lua_end
boot_lua:
    .incbin "src/script/boot.lua"
boot_lua_end:
    .byte 0
