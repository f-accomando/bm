/*
 * The firmware loads chainloader.img at 0x8000; start.S copies it to
 * LOAD_ADDR and continues there, freeing 0x8000 for the received kernel.
 */
ENTRY(_start)

LOAD_ADDR = 0x02000000;     /* 32 MiB */

SECTIONS
{
    . = LOAD_ADDR;

    .text : {
        KEEP(*(.text.boot))
        *(.text .text.*)
    }
    .rodata : ALIGN(8) { *(.rodata .rodata.*) }
    .data : ALIGN(8) { *(.data .data.*) }
    __image_end = .;

    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.ARM.attributes)
    }
}
