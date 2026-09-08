OUTPUT_FORMAT(binary)
ENTRY(_start)

SECTIONS
{
        /* Design D1: the image is loaded at GPA 0x8000 and [0, mem_size) is
           identity mapped, so the guest is linked at its run address. */
        . = 0x8000;

        .start   : { *(.start) }
        .text    : { *(.text*) }
        .rodata  : { *(.rodata*) }      /* -O2 emits .rodata.str1.1, not .rodata */
        .data    : { *(.data*) }

        /* Symbols live inside the output section so they track its real start,
           not the location counter before ld applies the section alignment. */
        .bss     : {
                __bss_start = .;
                *(.bss*)
                *(COMMON)
                . = ALIGN(8);
                __bss_end = .;
        }

        /DISCARD/ : { *(.eh_frame) *(.comment) *(.note*) }
}
