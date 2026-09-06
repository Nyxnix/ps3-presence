OUTPUT_FORMAT("elf64-powerpc")
OUTPUT_ARCH(powerpc:common64)
ENTRY(__presence_start_code)
/* Intermediate image: the packer splits RX/RW at __presence_data_start and
   translates absolute references into SCE_PPURELA records for both segments. */
PHDRS { image PT_LOAD FLAGS(7); }
SECTIONS {
    . = 0x10000;
    .module_info : { KEEP(*(.module_info)) } :image
    .text : {
        __presence_start_code = .; KEEP(*(.text.module_start))
        __presence_stop_code = .; KEEP(*(.text.module_stop))
        *(.text .text.*)
    } :image
    .module_exports : { KEEP(*(.module_exports)) } :image
    .module_imports : { KEEP(*(.module_imports)) } :image
    . = ALIGN(16);
    __presence_data_start = .;
    .opd : { *(.opd) } :image
    . = ALIGN(8);
    .toc : { PROVIDE(.TOC. = . + 0x8000); *(.toc .toc.* .toc1) *(.got .got.*) } :image
    __presence_toc = .TOC.;
    .data : { *(.data .data.*) *(.sdata .sdata.*) } :image
    /* Pack constants into the spare writable-segment capacity. Code remains
       in RX; this avoids another 64 KiB RX allocation page in the small build.
       C const ownership is unchanged, but these constants share RW protection. */
    .rodata : { *(.rodata .rodata.*) } :image
    . = ALIGN(8);
    .bss : { *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON) } :image
    /DISCARD/ : { *(.eh_frame*) *(.comment) *(.note*) }
}
