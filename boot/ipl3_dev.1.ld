OUTPUT_FORMAT ("elf32-bigmips", "elf32-bigmips", "elf32-littlemips")
OUTPUT_ARCH (mips)
EXTERN (_start)
ENTRY (_start)

MEMORY
{
    rom  : ORIGIN = 0xB0000000, LENGTH = 65536
    dmem : ORIGIN = 0xA4000000, LENGTH = 4096
    tlb  : ORIGIN = 0xE0000000, LENGTH = 16*1024
}

/* Start of IPL3 in ROM (after trampoline) */
__ipl3rombase = 0xB0001040;

SECTIONS {
    .text.prologue 0xB0000000 : {
        KEEP(*(.header))
        KEEP(*(.text.boot_trampoline))
        KEEP(*(.text.ique_trampoline))
    } > rom

    .text.stage0 0xA4000040 : AT ( __ipl3rombase ) {
        *(.stage0)
    } > dmem

    .text.stage1 0xA4000040 + SIZEOF(.text.stage0) : {
        __stage1 = .;
        LONG(0x3044d236);  /* special dummy opcode to simplify hash cracking; this is not needed in dev mode, but we keep it in case it matters */
        KEEP(*(.stage1))
        build/ipl3.o       
        build/rdram.o      
        build/minidragon.o 
        build/entropy.o    
        *(.bss)
        . = ALIGN(8);
    } > dmem

    .text.stage2 0xE0000000 : AT( 0xB0002000 ) {
        __stage2 = .;
        __stage2_start = .;
        KEEP(build/loader.o(.text.loader))
        KEEP(build/loader.o)
        . = ALIGN(8);
        __stage2_end = .;
    } > tlb

    __stage2_size = __stage2_end - __stage2_start;

    .text.debug 0xB0003000 : {
        build/debug.o      (.text .text.* .data)
        *(.rodata.debug)
    } > rom

    /DISCARD/ : { *(.MIPS.abiflags) }
}
