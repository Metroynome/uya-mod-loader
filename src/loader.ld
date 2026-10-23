ENTRY(campaignEntry)
SECTIONS {
    . = 0x000a0000;
    /* Stable 12-word discovery block; the table itself follows the payload. */
    .discovery : {
        LONG(ABSOLUTE(moduleTable)) LONG(0x444c5955) LONG(1) LONG(48)
        LONG(0) LONG(0) LONG(0) LONG(0) LONG(0) LONG(0) LONG(0) LONG(0)
    }
    .text : { KEEP(*(.entry)) *(.text .text.*) }
    .rodata ALIGN(16) : { *(.rodata .rodata.*) }
    .data ALIGN(16) : { *(.data .data.*) *(.sdata .sdata.*) . = ALIGN(16); }
    _bss_start = .;
    .bss ALIGN(16) (NOLOAD) : { *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON) }
    _payload_end = .;
    /* NOLOAD keeps recurring PNACH writes away from live loader state. */
    .state ALIGN(64) (NOLOAD) : { KEEP(*(.loader.state)) }
    _loader_end = .;

    /DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.pdr) *(.comment) *(.gnu.attributes) }
}
