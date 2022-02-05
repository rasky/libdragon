#include "../src/rspq/rsp_queue.S"

    .section .data.overlay

    RSPQ_OverlayHeader TEST2_STATE_START, TEST2_STATE_END, 0xE0

    .align 1
COMMAND_TABLE:
    RSPQ_DefineCommand Test2Cmd_test, 8           # 0xE0

    .align 3
TEST2_STATE_START:
    .quad 0
TEST2_STATE_END:

    .text 1

Test2Cmd_test:
    sw a0, %lo(TEST2_STATE_START) + 0x0
    jr ra
    sw a1, %lo(TEST2_STATE_START) + 0x4
    
