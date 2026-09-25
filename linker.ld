/* BCM2835 / ARM1176JZF-S: the VideoCore firmware loads kernel.img at 0x8000. */
ENTRY(_start)

SVC_STACK_SIZE = 0x10000;   /* 64 KiB */
IRQ_STACK_SIZE = 0x1000;
FIQ_STACK_SIZE = 0x1000;
ABT_STACK_SIZE = 0x1000;
UND_STACK_SIZE = 0x1000;

SECTIONS
{
    . = 0x8000;

    .text : {
        KEEP(*(.text.boot))
        *(.text .text.*)
    }

    .rodata : ALIGN(8) {
        *(.rodata .rodata.*)
    }

    .data : ALIGN(8) {
        *(.data .data.*)
    }

    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }

    .stack (NOLOAD) : ALIGN(16) {
        . += UND_STACK_SIZE; __und_stack_top = .;
        . += ABT_STACK_SIZE; __abt_stack_top = .;
        . += FIQ_STACK_SIZE; __fiq_stack_top = .;
        . += IRQ_STACK_SIZE; __irq_stack_top = .;
        . += SVC_STACK_SIZE; __svc_stack_top = .;
    }

    __kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.ARM.attributes)
    }
}
