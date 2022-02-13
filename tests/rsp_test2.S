#include "../src/rspq/rsp_queue.S"

    .data

    RSPQ_BeginOverlayHeader TEST2_STATE_START, TEST2_STATE_END
    RSPQ_DefineCommand Test2Cmd_test, 8           # 0x00
	RSPQ_EndOverlayHeader

    .align 3
TEST2_STATE_START:
    .quad 0
TEST2_STATE_END:

    .text

Test2Cmd_test:
    sw a0, %lo(TEST2_STATE_START) + 0x0
    jr ra
    sw a1, %lo(TEST2_STATE_START) + 0x4
    
