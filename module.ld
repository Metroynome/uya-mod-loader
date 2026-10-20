ENTRY(moduleInit)
SECTIONS {
    . = MODULE_ADDRESS - 40;
    .module : {
        LONG(0x4d415955) LONG(1) LONG(MODULE_REGION) LONG(MODULE_ADDRESS)
        LONG(_image_end - MODULE_ADDRESS) LONG(_memory_end - MODULE_ADDRESS)
        LONG(moduleInit - MODULE_ADDRESS) LONG(moduleUpdate - MODULE_ADDRESS)
        LONG(MODULE_MODES) LONG(0)
    }
    .text : { KEEP(*(.text.moduleInit .text.moduleUpdate)) *(.text .text.*) }
    .rodata ALIGN(16) : { *(.rodata .rodata.*) }
    .data ALIGN(16) : { *(.data .data.*) *(.sdata .sdata.*) . = ALIGN(16); }
    _image_end = .;
    .bss ALIGN(16) (NOLOAD) : { *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON) }
    _memory_end = ALIGN(64);

    /DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.pdr) *(.comment) *(.gnu.attributes) }
}
