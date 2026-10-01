/*
 * bm for the RGB30 (RK3566) and for QEMU's virt machine (tests): AArch64,
 * identity mapped. KERNEL_BASE comes from the command line (rgb30.mk):
 * 0x10000000 on the RK3566 (start.S moves the image there from wherever
 * U-Boot put it), 0x40080000 in QEMU (RAM starts at 0x40000000).
 */
ENTRY(_start)

STACK_SIZE = 0x100000;      /* 1 MiB: the Lua parser and VM recurse on it */

SECTIONS
{
    . = KERNEL_BASE;

    .text : {
        KEEP(*(.text.boot))
        *(.text .text.*)
    }

    .rodata : ALIGN(16) {
        *(.rodata .rodata.*)
    }

    .eh_frame : ALIGN(8) {
        *(.eh_frame .eh_frame.*)
    }

    .init_array : ALIGN(8) {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN(__init_array_end = .);
    }

    .data : ALIGN(16) {
        *(.data .data.*)
    }

    /* picolibc keeps errno in thread-local storage: the one block is used
     * in place (start.S points TPIDR_EL0 at it) */
    .tdata : ALIGN(16) {
        *(.tdata .tdata.*)
        . = ALIGN(16);      /* the BSS starts here: cleared with 16-byte stores, MMU off */
    }
    __tls_base = ADDR(.tdata);
    __tdata_source = LOADADDR(.tdata);
    __tdata_size = SIZEOF(.tdata);
    __load_end = .;

    .tbss (NOLOAD) : ALIGN(16) {
        *(.tbss .tbss.*)
        *(.tcommon)
    }
    __tbss_size = SIZEOF(.tbss);
    __tbss_offset = ADDR(.tbss) - ADDR(.tdata);
    __tls_size = ADDR(.tbss) + SIZEOF(.tbss) - ADDR(.tdata);
    __tls_align = MAX(ALIGNOF(.tdata), ALIGNOF(.tbss));
    __arm64_tls_tcb_offset = MAX(16, __tls_align);

    /* ld.bfd does not advance the location counter for .tbss */
    .tbss_space (NOLOAD) : {
        __bss_start = .;
        . = ADDR(.tbss) + SIZEOF(.tbss);
    }

    .bss (NOLOAD) : ALIGN(16) {
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
    }

    /* page tables (src/rgb30/mmu.c) and the stack: cleared with the bss */
    .stack (NOLOAD) : ALIGN(4096) {
        *(.pagetables)
        . = ALIGN(16);
        . += STACK_SIZE;
        __stack_top = .;
        __bss_end = .;
    }

    /* after the bss: kept across a reset: never cleared */
    .noinit (NOLOAD) : ALIGN(64) {
        *(.noinit .noinit.*)
    }

    __kernel_end = .;
    __image_size = __kernel_end - KERNEL_BASE;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
    }
}
