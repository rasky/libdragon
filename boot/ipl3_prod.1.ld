OUTPUT_FORMAT ("elf32-bigmips", "elf32-bigmips", "elf32-littlemips")
OUTPUT_ARCH (mips)
EXTERN (_start)
ENTRY (_start)

MEMORY
{
    dmem : ORIGIN = 0xA4000000, LENGTH = 4096*4
    rom  : ORIGIN = 0xB0001000, LENGTH = 16384
    tlb  : ORIGIN = 0xE0000000, LENGTH = 16*1024
}

SECTIONS {
    /DISCARD/ : { *(.MIPS.abiflags) }
    /DISCARD/ : { *(.comment) }
    /DISCARD/ : { *(.reginfo) }
    /DISCARD/ : { *(.mdebug.*) }
    /DISCARD/ : { *(.gnu.*) }

    .text.prologue 0xA4000000 : {
        KEEP(*(.header))
    } > dmem

    .text.stage0 : {
        *(.stage0)
    } > dmem

    .text.stage1 : {
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
        KEEP(build/loader.o(.text .text.*))
        KEEP(build/loader.o(.data .data.*))
        KEEP(build/loader.o(.rodata .rodata.*))
        . = ALIGN(8);
        __stage2_end = .;
    } > tlb
}
