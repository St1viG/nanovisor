OUTPUT_FORMAT(binary)
ENTRY(_start)

SECTIONS
{
        /* Origin stays implicit-0 while the host maps GVA 0..0xFFFF -> GPA 0x8000.
           Task A.4 flips it to 0x8000 together with the identity map (design D1). */

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
