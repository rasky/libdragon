OUTPUT_FORMAT ("elf32-bigmips", "elf32-bigmips", "elf32-littlemips")
OUTPUT_ARCH (mips)

MEMORY
{
    rom  : ORIGIN = 0xB0000000, LENGTH = 65536
    dmem : ORIGIN = 0xA4000000, LENGTH = 4096
}

SECTIONS {
    .text.prologue 0xB0000000 : {
        KEEP(*(.header))
        KEEP(*(.text.boot_trampoline))
        KEEP(*(.text.ique_trampoline))
    } > rom

    .text.stage0 0xA4000040 : AT ( 0xB0001040 ) {
        *(.stage0)
    } > dmem

    .text.banner : {
        *(.banner)
    } > dmem

    .text.stage1 : {
        *(.stage1bin)
    } > dmem

    .text.stage2 : {
        *(.stage2bin)
    } > dmem

    .text.debug 0xB0003000 : {
        *(.debugbin)
    } > rom

    /DISCARD/ : { *(.MIPS.abiflags) }
}
