ENTRY(campaignEntry)
SECTIONS {
    . = 0x000f0000;
    .text : { KEEP(*(.entry)) *(.text .text.*) }
    .rodata ALIGN(16) : { *(.rodata .rodata.*) }
    .data ALIGN(16) : { *(.data .data.*) *(.sdata .sdata.*) . = ALIGN(16); }
    _bss_start = .;
    .bss ALIGN(16) (NOLOAD) : { *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON) }
    _payload_end = .;

    /DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.pdr) *(.comment) *(.gnu.attributes) }
}
