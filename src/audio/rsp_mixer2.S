## Auto-generated file, transpiled with RSPL
#define SAMPLE_CACHE_SIZE 192
#define MIX_BUFFER 512
#define WAVEFORM_POS_FRAC_BITS 12
#define WAVEFORM_POS_FRAC_BITS_16 13
#define POS_FRAC_MASK 0xfff
#define RESAMPLE_PRINT 1
#include <rsp_queue.inc>

.set noreorder
.set noat
.set nomacro

#undef zero
#undef at
#undef v0
#undef v1
#undef a0
#undef a1
#undef a2
#undef a3
#undef t0
#undef t1
#undef t2
#undef t3
#undef t4
#undef t5
#undef t6
#undef t7
#undef s0
#undef s1
#undef s2
#undef s3
#undef s4
#undef s5
#undef s6
#undef s7
#undef t8
#undef t9
#undef k0
#undef k1
#undef gp
#undef sp
#undef fp
#undef ra
.equ hex.$zero, 0
.equ hex.$at, 1
.equ hex.$v0, 2
.equ hex.$v1, 3
.equ hex.$a0, 4
.equ hex.$a1, 5
.equ hex.$a2, 6
.equ hex.$a3, 7
.equ hex.$t0, 8
.equ hex.$t1, 9
.equ hex.$t2, 10
.equ hex.$t3, 11
.equ hex.$t4, 12
.equ hex.$t5, 13
.equ hex.$t6, 14
.equ hex.$t7, 15
.equ hex.$s0, 16
.equ hex.$s1, 17
.equ hex.$s2, 18
.equ hex.$s3, 19
.equ hex.$s4, 20
.equ hex.$s5, 21
.equ hex.$s6, 22
.equ hex.$s7, 23
.equ hex.$t8, 24
.equ hex.$t9, 25
.equ hex.$k0, 26
.equ hex.$k1, 27
.equ hex.$gp, 28
.equ hex.$sp, 29
.equ hex.$fp, 30
.equ hex.$ra, 31
#define vco 0
#define vcc 1
#define vce 2

.data
  RSPQ_BeginOverlayHeader
    RSPQ_DefineCommand Mixer_Begin, 8
    RSPQ_DefineCommand Mixer_End, 4
    RSPQ_DefineCommand Mixer_Resample, 4
    RSPQ_DefineCommand Mixer_Interleave, 16
  RSPQ_EndOverlayHeader

  RSPQ_BeginSavedState
    STATE_MEM_START:
    .align 2
    RDRAM_BUFFER: .ds.b 4
    .align 1
    DMEM_BUFFER: .ds.b 2
    .align 1
    NSAMPLES: .ds.b 2
    .align 1
    GVOLUME: .ds.b 2
    .align 2
    TICKS: .ds.b 4
    .align 4
    PREBUFFER: .ds.b 32
    .align 4
    BUFFER: .ds.b 2064
    .align 4
    POSTBUFFER: .ds.b 32
    STATE_MEM_END:
  RSPQ_EndSavedState

    .align 4
    STEP8: .half 1, 5, 2, 6, 3, 7, 4, 8
    .align 1
    VOLUME_FILTER_TAPS: .half 57462, 8074

.bss
  TEMP_STATE_MEM_START:
    .align 2
    SETTINGS: .ds.b 32
    .align 4
    SCRATCH: .ds.b 128
    .align 4
    CUR_STEP8: .ds.b 32
    .align 4
    CUR_TVOLUME: .ds.b 32
    .align 3
    SAMPLE_CACHE: .ds.b 192
  TEMP_STATE_MEM_END:

.text
OVERLAY_CODE_START:

Mixer_Begin:
  andi $s7, $a0, 3                                   ## L:55   |      ^ | u32 misalign = rdram_buffer & 3;
  bne $s7, $zero, assertion_failed                   ## L:56   |      2 | if (misalign) assert(0x7002);
  lui $at, 28674                                     ## L:56   |     *4 | if (misalign) assert(0x7002);
  LABEL_Mixer_Begin_0001:
  sw $a0, %lo(RDRAM_BUFFER)($zero)                   ## L:57   |      5 | store(rdram_buffer, RDRAM_BUFFER);
  andi $s7, $a0, 7                                   ## L:59   |      6 | misalign = rdram_buffer & 7;
  ori $s4, $zero, %lo(BUFFER)                        ## L:60   |      7 | u16<$s4> dmem_buffer = BUFFER;
  addu $s4, $s4, $s7                                 ## L:61   |      8 | dmem_buffer += misalign;
  sh $s4, %lo(DMEM_BUFFER)($zero)                    ## L:62   |      9 | store(dmem_buffer, DMEM_BUFFER);
  srl $s6, $a1, 16                                   ## L:65   |     10 | u16 gvolume = nsamples >> 16;
  sh $s6, %lo(GVOLUME)($zero)                        ## L:66   |     11 | store(gvolume, GVOLUME);
  andi $s5, $a1, 65535                               ## L:68   |     12 | u16 ns = nsamples & 0xffff;
  sltiu $at, $s5, 513                                ## L:69   |     13 | if (ns > 512) assert(0x7001);
  beq $at, $zero, assertion_failed                   ## L:69   |     14 | if (ns > 512) assert(0x7001);
  lui $at, 28673                                     ## L:69   |    *16 | if (ns > 512) assert(0x7001);
  LABEL_Mixer_Begin_0002:
  sh $s5, %lo(NSAMPLES)($zero)                       ## L:70   |     17 | store(ns, NSAMPLES);
  ori $s3, $zero, %lo(BUFFER)                        ## L:73   |     18 | u32 ptr = BUFFER;
  addiu $s2, $zero, 32                               ## L:74   |     19 | u32 size = (512*2*2)/64;
  LABEL_Mixer_Begin_0003:
  sqv $v00, 0, 0, $s3                                ## L:76   |     20 | store(VZERO, ptr, 0x00);
  sqv $v00, 0, 16, $s3                               ## L:77   |     21 | store(VZERO, ptr, 0x10);
  sqv $v00, 0, 32, $s3                               ## L:78   |     22 | store(VZERO, ptr, 0x20);
  sqv $v00, 0, 48, $s3                               ## L:79   |     23 | store(VZERO, ptr, 0x30);
  addiu $s2, $s2, 65535                              ## L:81   |     24 | size -= 1;
  bgtz $s2, LABEL_Mixer_Begin_0003                   ## L:81   |     25 | size -= 1;
  addiu $s3, $s3, 64                                 ## L:80   |    *27 | ptr += 0x40;
  LABEL_Mixer_Begin_0004:
  j RSPQ_Loop                                        ## L:84   |     28 | }
  sqv $v00, 0, 0, $s3                                ## L:83   |    *30 | store(VZERO, ptr, 0x00);
Mixer_End:
  lw $s0, %lo(RDRAM_BUFFER + 0)                      ## L:88   |      ^ | u32<$s0> rdram_buffer = load(RDRAM_BUFFER);
  andi $s7, $s0, 7                                   ## L:90   |    **4 | u32 misalign = rdram_buffer & 7;
  beq $s7, $zero, LABEL_Mixer_End_0005               ## L:91   |      5 | if (misalign != 0) {
  nop                                                ## L:91   |     *7 | if (misalign != 0) {
  ori $s6, $zero, %lo(BUFFER)                        ## L:93   |      8 | u16 dmem_buffer = BUFFER;
  ori $t0, $zero, 7                                  ## L:95   |      9 | dma_in(BUFFER, rdram_buffer, 8);
  ori $s4, $zero, %lo(BUFFER)                        ## L:95   |     10 | dma_in(BUFFER, rdram_buffer, 8);
  addiu $t2, $zero, 12                               ## L:95   |     11 | dma_in(BUFFER, rdram_buffer, 8);
  jal DMAExec                                        ## L:95   |     12 | dma_in(BUFFER, rdram_buffer, 8); ## Args: $t0, $t1, $s0, $s4, $t2
  lw $s5, 4($s6)                                     ## L:94   |    *14 | u32 data = load(dmem_buffer, 4);
  sw $s5, 4($s6)                                     ## L:96   |   **17 | store(data, dmem_buffer, 4);
  LABEL_Mixer_End_0005:
  lhu $t0, %lo(NSAMPLES + 0)                         ## L:99   |     18 | u16<$t0> nsamples = load(NSAMPLES);
  sll $t0, $t0, 2                                    ## L:100  |   **21 | nsamples *= 4;
  addu $t0, $t0, $s7                                 ## L:101  |     22 | nsamples += misalign;
  ori $s4, $zero, %lo(BUFFER)                        ## L:102  |     23 | dma_out_async(BUFFER, rdram_buffer, nsamples);
  addiu $t2, $zero, -32768                           ## L:102  |     24 | dma_out_async(BUFFER, rdram_buffer, nsamples);
  jal DMAExec                                        ## L:102  |     25 | dma_out_async(BUFFER, rdram_buffer, nsamples); ## Args: $t0, $t1, $s0, $s4, $t2
  addiu $t0, $t0, -1                                 ## L:102  |    *27 | dma_out_async(BUFFER, rdram_buffer, nsamples);
  j RSPQ_Loop                                        ## L:103  |     28 | }
  nop                                                ## L:103  |    *30 | }
.align 3
Resample_8_Mono:
  LABEL_Resample_8_Mono_0006:
  lhu $t0, 0($k0)                                    ## L:180  |      1 | @Barrier("vpos") a0:u16 = load(SCRATCH_ADDR, 0x00); ## Barrier: 0x1
  lhu $t1, 2($k0)                                    ## L:181  |      2 | @Barrier("vpos") a1:u16 = load(SCRATCH_ADDR, 0x02); ## Barrier: 0x1
  lhu $t2, 4($k0)                                    ## L:182  |      3 | @Barrier("vpos") a2:u16 = load(SCRATCH_ADDR, 0x04); ## Barrier: 0x1
  lhu $t3, 6($k0)                                    ## L:183  |      4 | @Barrier("vpos") a3:u16 = load(SCRATCH_ADDR, 0x06); ## Barrier: 0x1
  lhu $t4, 8($k0)                                    ## L:184  |      5 | @Barrier("vpos") a4:u16 = load(SCRATCH_ADDR, 0x08); ## Barrier: 0x1
  lhu $t5, 10($k0)                                   ## L:185  |      6 | @Barrier("vpos") a5:u16 = load(SCRATCH_ADDR, 0x0a); ## Barrier: 0x1
  lhu $t6, 12($k0)                                   ## L:186  |      7 | @Barrier("vpos") a6:u16 = load(SCRATCH_ADDR, 0x0c); ## Barrier: 0x1
  lhu $t9, 14($k0)                                   ## L:187  |      8 | @Barrier("vpos") a7:u16 = load(SCRATCH_ADDR, 0x0e); ## Barrier: 0x1
  lpv $v08, 0, 0, $t0                                ## L:203  |      9 | b0 = load_vec_s8(a0, 0);
  lpv $v09, 0, 0, $t1                                ## L:204  |     10 | b1 = load_vec_s8(a1, 0);
  lpv $v10, 0, 0, $t2                                ## L:205  |     11 | b2 = load_vec_s8(a2, 0);
  lpv $v11, 0, 0, $t3                                ## L:206  |     12 | b3 = load_vec_s8(a3, 0);
  lpv $v12, 0, 0, $t4                                ## L:207  |     13 | b4 = load_vec_s8(a4, 0);
  lpv $v13, 0, 0, $t5                                ## L:208  |     14 | b5 = load_vec_s8(a5, 0);
  lpv $v14, 0, 0, $t6                                ## L:209  |     15 | b6 = load_vec_s8(a6, 0);
  lpv $v15, 0, 0, $t9                                ## L:210  |     16 | b7 = load_vec_s8(a7, 0);
  stv $v08, 2, 16, $k0                               ## L:216  |  ***20 | b0 = transpose(b0, SCRATCH_ADDR, 4, 8); ## Barrier: 0x2
  stv $v08, 4, 32, $k0                               ## L:216  |     21 | b0 = transpose(b0, SCRATCH_ADDR, 4, 8); ## Barrier: 0x2
  stv $v08, 6, 48, $k0                               ## L:216  |     22 | b0 = transpose(b0, SCRATCH_ADDR, 4, 8); ## Barrier: 0x2
  stv $v08, 8, 64, $k0                               ## L:216  |     23 | b0 = transpose(b0, SCRATCH_ADDR, 4, 8); ## Barrier: 0x2
  stv $v08, 10, 80, $k0                              ## L:216  |     24 | b0 = transpose(b0, SCRATCH_ADDR, 4, 8); ## Barrier: 0x2
  stv $v08, 12, 96, $k0                              ## L:216  |     25 | b0 = transpose(b0, SCRATCH_ADDR, 4, 8); ## Barrier: 0x2
  stv $v08, 14, 112, $k0                             ## L:216  |     26 | b0 = transpose(b0, SCRATCH_ADDR, 4, 8); ## Barrier: 0x2
  ltv $v08, 14, 16, $k0                              ## L:216  |     27 | b0 = transpose(b0, SCRATCH_ADDR, 4, 8); ## Barrier: 0x2
  ltv $v08, 12, 32, $k0                              ## L:216  |     28 | b0 = transpose(b0, SCRATCH_ADDR, 4, 8); ## Barrier: 0x2
  ltv $v08, 10, 48, $k0                              ## L:216  |     29 | b0 = transpose(b0, SCRATCH_ADDR, 4, 8); ## Barrier: 0x2
  ltv $v08, 8, 64, $k0                               ## L:216  |     30 | b0 = transpose(b0, SCRATCH_ADDR, 4, 8); ## Barrier: 0x2
  ltv $v08, 6, 80, $k0                               ## L:216  |     31 | b0 = transpose(b0, SCRATCH_ADDR, 4, 8); ## Barrier: 0x2
  ltv $v08, 4, 96, $k0                               ## L:216  |     32 | b0 = transpose(b0, SCRATCH_ADDR, 4, 8); ## Barrier: 0x2
  ltv $v08, 2, 112, $k0                              ## L:216  |     33 | b0 = transpose(b0, SCRATCH_ADDR, 4, 8); ## Barrier: 0x2
  vmudl $v06, $v20, $v20                             ## L:125  |      ^ | asm_op("vmudl", x2, x:ufract, x:ufract);
  vmudl $v07, $v06, $v20                             ## L:126  |  ***37 | asm_op("vmudl", x3, x2:ufract, x:ufract);
  vmudm $v29, $v16, $v31.e0                          ## L:129  |     38 | VTEMP   = ym1 >> 1;
  vmudm $v26, $v18, $v31.e0                          ## L:130  |     39 | y1_05   = y1  >> 1;
  vmudm $v22, $v19, $v31.e0                          ## L:131  |     40 | y2_05   = y2  >> 1;
  vsubc $v25, $v00, $v29.v                           ## L:132  |    *42 | ym1_05n = VZERO - VTEMP;
  vsubc $v28, $v00, $v17.v                           ## L:133  |     43 | y0_n    = VZERO - y0;
  vsubc $v27, $v00, $v18.v                           ## L:134  |     44 | y1_n    = VZERO - y1;
  vsubc $v23, $v00, $v22.v                           ## L:135  |     45 | y2_05n  = VZERO - y2_05;
  vmudm $v21, $v17, $v31.e0                          ## L:136  |     46 | y0_05   = y0  >> 1;
  vmudh $v29, $v17, $v30.e7                          ## L:138  |     47 | VTEMP = y0:sint  * 1;
  vmadm $v29, $v26, $v20.v                           ## L:139  |     48 | VTEMP = y1_05   +* x:ufract;
  vmadm $v29, $v25, $v20.v                           ## L:140  |     49 | VTEMP = ym1_05n +* x:ufract;
  vmadm $v29, $v16, $v06.v                           ## L:141  |     50 | VTEMP = ym1     +* x2:ufract;
  vmadm $v29, $v28, $v06.v                           ## L:142  |     51 | VTEMP = y0_n    +* x2:ufract;
  vmadm $v29, $v28, $v06.v                           ## L:143  |     52 | VTEMP = y0_n    +* x2:ufract;
  vmadm $v29, $v28, $v06.v                           ## L:144  |     53 | VTEMP = y0_n    +* x2:ufract;
  vmadm $v29, $v21, $v06.v                           ## L:145  |     54 | VTEMP = y0_05   +* x2:ufract;
  vmadm $v29, $v18, $v06.v                           ## L:146  |     55 | VTEMP = y1      +* x2:ufract;
  vmadm $v29, $v18, $v06.v                           ## L:147  |     56 | VTEMP = y1      +* x2:ufract;
  vmadm $v29, $v23, $v06.v                           ## L:148  |     57 | VTEMP = y2_05n  +* x2:ufract;
  vmadm $v29, $v22, $v07.v                           ## L:149  |     58 | VTEMP = y2_05   +* x3:ufract;
  vmadm $v29, $v25, $v07.v                           ## L:150  |     59 | VTEMP = ym1_05n +* x3:ufract;
  vmadm $v29, $v17, $v07.v                           ## L:151  |     60 | VTEMP = y0      +* x3:ufract;
  vmadm $v29, $v21, $v07.v                           ## L:152  |     61 | VTEMP = y0_05   +* x3:ufract;
  vmadm $v29, $v27, $v07.v                           ## L:153  |     62 | VTEMP = y1_n    +* x3:ufract;
  vmadm $v29, $v27, $v07.v                           ## L:154  |     63 | VTEMP = y1_n    +* x3:ufract;
  vmadm $v29, $v26, $v07.v                           ## L:155  |     64 | VTEMP = y1_05   +* x3:ufract;
  lqv $v07, 0, 0, $a2                                ## L:224  |      ^ | tvolume = load(CUR_TVOLUME_ADDR, 0x00);
  lqv $v06, 0, 16, $a2                               ## L:225  |     65 | @Barrier("volume") volume = load(CUR_TVOLUME_ADDR, 0x10); ## Barrier: 0x4
  lqv $v21, 0, 0, $s2                                ## L:231  |     66 | final_lr_1   = load(outptr,  0);
  lqv $v22, 0, 16, $s2                               ## L:232  |     67 | final_lr_2   = load(outptr, 16);
  lqv $v23, 0, 0, $s2                                ## L:233  |     68 | final_auxa_1 = load(outptr,  0);
  lqv $v24, 0, 16, $s2                               ## L:234  |     69 | final_auxa_2 = load(outptr, 16);
  lqv $v25, 0, 0, $s2                                ## L:235  |     70 | final_auxb_1 = load(outptr,  0);
  lqv $v26, 0, 16, $s2                               ## L:236  |     71 | final_auxb_2 = load(outptr, 16);
  vmudn $v20, $v02, $v30.e4                          ## L:238  |      ^ | x = vec_pos:ufract << (16 - 12 - 1);
  vmudm $v07, $v07, $v04.e3                          ## L:244  |     72 | tvolume = tvolume:sfract  * vec_step8:ufract.3;
  vmadm $v07, $v06, $v04.e2                          ## L:245  |     73 | tvolume = volume:sfract  +* vec_step8:ufract.2;
  vand $v29, $v29, $v05.v                            ## L:249  |     74 | VTEMP &= finish_mask;
  or $t7, $zero, $t8                                 ## L:250  |      ^ | prev_bitmask = finish_bitmask;
  .set macro # print                                 ## L:252  |      0 | print(finish_bitmask);
  emux_dump_gpr $t8                                  ## L:252  |      0 | print(finish_bitmask);
  .set noat # print                                  ## L:252  |      0 | print(finish_bitmask);
  .set nomacro # print                               ## L:252  |      0 | print(finish_bitmask);
  .set macro # print                                 ## L:253  |      0 | print(finish_mask);
  emux_dump_vpr $v05                                 ## L:253  |      0 | print(finish_mask);
  .set noat # print                                  ## L:253  |      0 | print(finish_mask);
  .set nomacro # print                               ## L:253  |      0 | print(finish_mask);
  .set macro # print                                 ## L:254  |      0 | print(vec_pos:sint);
  emux_dump_vpr $v01                                 ## L:254  |      0 | print(vec_pos:sint);
  .set noat # print                                  ## L:254  |      0 | print(vec_pos:sint);
  .set nomacro # print                               ## L:254  |      0 | print(vec_pos:sint);
  .set macro # print                                 ## L:255  |      0 | print(vec_pos:ufract);
  emux_dump_vpr $v02                                 ## L:255  |      0 | print(vec_pos:ufract);
  .set noat # print                                  ## L:255  |      0 | print(vec_pos:ufract);
  .set nomacro # print                               ## L:255  |      0 | print(vec_pos:ufract);
  .set macro # print                                 ## L:256  |      0 | print(y0);
  emux_dump_vpr $v17                                 ## L:256  |      0 | print(y0);
  .set noat # print                                  ## L:256  |      0 | print(y0);
  .set nomacro # print                               ## L:256  |      0 | print(y0);
  .set macro # print                                 ## L:257  |      0 | print(VTEMP);
  emux_dump_vpr $v29                                 ## L:257  |      0 | print(VTEMP);
  .set noat # print                                  ## L:257  |      0 | print(VTEMP);
  .set nomacro # print                               ## L:257  |      0 | print(VTEMP);
  vsubc $v27, $v02, $v04.e0                          ## L:265  |     75 | asm_op("vsubc", scratch, vec_pos:ufract, vec_step8:ufract.0);
  vge $v27, $v01, $v03.e0                            ## L:266  |     76 | asm_op("vge", scratch, vec_pos:sint, vec_step8:sint.0);
  cfc2 $t8, $vcc                                     ## L:267  |     77 | finish_bitmask = get_vcc();
  vnor $v05, $v00, $v00.v                            ## L:268  |      ^ | finish_mask = ~VZERO;
  sqv $v08, 0, 96, $k0                               ## L:285  |     78 | @Barrier("tap0") store(b0, SCRATCH_ADDR, 0x60); ## Barrier: 0x8
  lqv $v16, 0, 96, $k0                               ## L:286  |     79 | @Barrier("tap0") ym1 = load(SCRATCH_ADDR, 0x60); ## Barrier: 0x8
  sqv $v09, 0, 80, $k0                               ## L:288  |     80 | @Barrier("tap1") store(b1, SCRATCH_ADDR, 0x50); ## Barrier: 0x10
  lqv $v17, 0, 80, $k0                               ## L:289  |     81 | @Barrier("tap1") y0 = load(SCRATCH_ADDR, 0x50); ## Barrier: 0x10
  sqv $v10, 0, 64, $k0                               ## L:291  |     82 | @Barrier("tap2") store(b2, SCRATCH_ADDR, 0x40); ## Barrier: 0x20
  lqv $v18, 0, 64, $k0                               ## L:292  |     83 | @Barrier("tap2") y1 = load(SCRATCH_ADDR, 0x40); ## Barrier: 0x20
  sqv $v11, 0, 48, $k0                               ## L:294  |     84 | @Barrier("tap3") store(b3, SCRATCH_ADDR, 0x30); ## Barrier: 0x40
  lqv $v19, 0, 48, $k0                               ## L:295  |     85 | @Barrier("tap3") y2 = load(SCRATCH_ADDR, 0x30); ## Barrier: 0x40
  vaddc $v02, $v02, $v04.e7                          ## L:303  |      ^ | vec_pos += vec_step8.7;
  vadd $v01, $v01, $v03.e7                           ## L:303  |     86 | vec_pos += vec_step8.7;
  vmudl $v08, $v02, $v30.e3                          ## L:306  |   **89 | vec32 vec_pos_int = vec_pos << 4;
  vmadn $v08, $v01, $v30.e3                          ## L:306  |     90 | vec32 vec_pos_int = vec_pos << 4;
  vmudn $v09, $v02, $v30.e3                          ## L:306  |     91 | vec32 vec_pos_int = vec_pos << 4;
  vmudh $v21, $v21, $v30.e7                          ## L:309  |     92 | final_lr_1 = final_lr_1:sint    * 1;
  vmacf $v21, $v06, $v29.q0                          ## L:310  |     93 | final_lr_1 = volume:sfract     +* VTEMP:sint.00224466;
  vmudh $v22, $v22, $v30.e7                          ## L:311  |     94 | final_lr_2 = final_lr_2:sint    * 1;
  vmacf $v22, $v06, $v29.q1                          ## L:312  |     95 | final_lr_2 = volume:sfract     +* VTEMP:sint.11335577;
  vmudh $v23, $v23, $v30.e7                          ## L:314  |     96 | final_auxa_1 = final_auxa_1:sint    * 1;
  vmacf $v23, $v06, $v29.q0                          ## L:315  |     97 | final_auxa_1 = volume:sfract     +* VTEMP:sint.00224466;
  vmudh $v24, $v24, $v30.e7                          ## L:316  |     98 | final_auxa_2 = final_auxa_2:sint    * 1;
  vmacf $v24, $v06, $v29.q1                          ## L:317  |     99 | final_auxa_2 = volume:sfract     +* VTEMP:sint.11335577;
  vmudh $v25, $v25, $v30.e7                          ## L:319  |    100 | final_auxb_1 = final_auxb_1:sint    * 1;
  vmacf $v25, $v06, $v29.q0                          ## L:320  |    101 | final_auxb_1 = volume:sfract     +* VTEMP:sint.00224466;
  vmudh $v26, $v26, $v30.e7                          ## L:321  |    102 | final_auxb_2 = final_auxb_2:sint    * 1;
  vmacf $v26, $v06, $v29.q1                          ## L:322  |    103 | final_auxb_2 = volume:sfract     +* VTEMP:sint.11335577;
  sqv $v08, 0, 0, $k0                                ## L:327  |      ^ | @Barrier("vpos") store(vec_pos_int:sint, SCRATCH_ADDR, 0x00); ## Barrier: 0x1
  sqv $v07, 0, 16, $a2                               ## L:330  |    104 | @Barrier("volume") store(tvolume, CUR_TVOLUME_ADDR, 0x10); ## Barrier: 0x4
  sqv $v21, 0, 0, $s2                                ## L:333  |    105 | store(final_lr_1,   outptr,  0);
  sqv $v22, 0, 16, $s2                               ## L:334  |    106 | store(final_lr_2,   outptr, 16);
  sqv $v23, 0, 0, $s2                                ## L:335  |    107 | store(final_auxa_1, outptr,  0);
  sqv $v24, 0, 16, $s2                               ## L:336  |    108 | store(final_auxa_2, outptr, 16);
  sqv $v25, 0, 0, $s2                                ## L:337  |    109 | store(final_auxb_1, outptr,  0);
  sqv $v26, 0, 16, $s2                               ## L:338  |    110 | store(final_auxb_2, outptr, 16);
  .set macro # print                                 ## L:340  |      0 | print(final_lr_1);
  emux_dump_vpr $v21                                 ## L:340  |      0 | print(final_lr_1);
  .set noat # print                                  ## L:340  |      0 | print(final_lr_1);
  .set nomacro # print                               ## L:340  |      0 | print(final_lr_1);
  .set macro # print                                 ## L:341  |      0 | print(final_lr_2);
  emux_dump_vpr $v22                                 ## L:341  |      0 | print(final_lr_2);
  .set noat # print                                  ## L:341  |      0 | print(final_lr_2);
  .set nomacro # print                               ## L:341  |      0 | print(final_lr_2);
  addiu $s2, $s2, 32                                 ## L:345  |    111 | outptr += 32;
  bne $t7, $zero, LABEL_Resample_8_Mono_0007         ## L:347  |    112 | if (prev_bitmask != 0)
  addiu $a1, $a1, 65528                              ## L:344  |   *114 | nsamples -= 8;
  bgtz $a1, LABEL_Resample_8_Mono_0006               ## L:348  |    115 | break;
  nop                                                ## L:348  |   *117 | break;
  LABEL_Resample_8_Mono_0007:
  jr $ra                                             ## L:349  |    118 | 
  nop                                                ## L:349  |   *120 | 
.align 3
Resample_16_Mono:
  LABEL_Resample_16_Mono_000A:
  lhu $t0, 0($k0)                                    ## L:379  |      1 | @Barrier("vpos") a0:u16 = load(SCRATCH_ADDR, 0x00); ## Barrier: 0x1
  lhu $t1, 2($k0)                                    ## L:380  |      2 | @Barrier("vpos") a1:u16 = load(SCRATCH_ADDR, 0x02); ## Barrier: 0x1
  lhu $t2, 4($k0)                                    ## L:381  |      3 | @Barrier("vpos") a2:u16 = load(SCRATCH_ADDR, 0x04); ## Barrier: 0x1
  lhu $t3, 6($k0)                                    ## L:382  |      4 | @Barrier("vpos") a3:u16 = load(SCRATCH_ADDR, 0x06); ## Barrier: 0x1
  lhu $t4, 8($k0)                                    ## L:383  |      5 | @Barrier("vpos") a4:u16 = load(SCRATCH_ADDR, 0x08); ## Barrier: 0x1
  lhu $t5, 10($k0)                                   ## L:384  |      6 | @Barrier("vpos") a5:u16 = load(SCRATCH_ADDR, 0x0a); ## Barrier: 0x1
  lhu $t6, 12($k0)                                   ## L:385  |      7 | @Barrier("vpos") a6:u16 = load(SCRATCH_ADDR, 0x0c); ## Barrier: 0x1
  lhu $t9, 14($k0)                                   ## L:386  |      8 | @Barrier("vpos") a7:u16 = load(SCRATCH_ADDR, 0x0e); ## Barrier: 0x1
  ldv $v08, 0, 0, $t0                                ## L:392  |      9 | b0 = load(a0, 0).0123;
  ldv $v09, 0, 0, $t1                                ## L:393  |     10 | b1 = load(a1, 0).0123;
  ldv $v10, 0, 0, $t2                                ## L:394  |     11 | b2 = load(a2, 0).0123;
  ldv $v11, 0, 0, $t3                                ## L:395  |     12 | b3 = load(a3, 0).0123;
  ldv $v12, 0, 0, $t4                                ## L:396  |     13 | b4 = load(a4, 0).0123;
  ldv $v13, 0, 0, $t5                                ## L:397  |     14 | b5 = load(a5, 0).0123;
  ldv $v14, 0, 0, $t6                                ## L:398  |     15 | b6 = load(a6, 0).0123;
  ldv $v15, 0, 0, $t9                                ## L:399  |     16 | b7 = load(a7, 0).0123;
  stv $v08, 2, 16, $k0                               ## L:415  |  ***20 | b0 = transpose(b0, SCRATCH_ADDR, 4, 8); ## Barrier: 0x80
  stv $v08, 4, 32, $k0                               ## L:415  |     21 | b0 = transpose(b0, SCRATCH_ADDR, 4, 8); ## Barrier: 0x80
  stv $v08, 6, 48, $k0                               ## L:415  |     22 | b0 = transpose(b0, SCRATCH_ADDR, 4, 8); ## Barrier: 0x80
  stv $v08, 8, 64, $k0                               ## L:415  |     23 | b0 = transpose(b0, SCRATCH_ADDR, 4, 8); ## Barrier: 0x80
  stv $v08, 10, 80, $k0                              ## L:415  |     24 | b0 = transpose(b0, SCRATCH_ADDR, 4, 8); ## Barrier: 0x80
  stv $v08, 12, 96, $k0                              ## L:415  |     25 | b0 = transpose(b0, SCRATCH_ADDR, 4, 8); ## Barrier: 0x80
  stv $v08, 14, 112, $k0                             ## L:415  |     26 | b0 = transpose(b0, SCRATCH_ADDR, 4, 8); ## Barrier: 0x80
  ltv $v08, 14, 16, $k0                              ## L:415  |     27 | b0 = transpose(b0, SCRATCH_ADDR, 4, 8); ## Barrier: 0x80
  ltv $v08, 12, 32, $k0                              ## L:415  |     28 | b0 = transpose(b0, SCRATCH_ADDR, 4, 8); ## Barrier: 0x80
  ltv $v08, 10, 48, $k0                              ## L:415  |     29 | b0 = transpose(b0, SCRATCH_ADDR, 4, 8); ## Barrier: 0x80
  ltv $v08, 8, 64, $k0                               ## L:415  |     30 | b0 = transpose(b0, SCRATCH_ADDR, 4, 8); ## Barrier: 0x80
  ltv $v08, 6, 80, $k0                               ## L:415  |     31 | b0 = transpose(b0, SCRATCH_ADDR, 4, 8); ## Barrier: 0x80
  ltv $v08, 4, 96, $k0                               ## L:415  |     32 | b0 = transpose(b0, SCRATCH_ADDR, 4, 8); ## Barrier: 0x80
  ltv $v08, 2, 112, $k0                              ## L:415  |     33 | b0 = transpose(b0, SCRATCH_ADDR, 4, 8); ## Barrier: 0x80
  vmudl $v06, $v20, $v20                             ## L:125  |      ^ | asm_op("vmudl", x2, x:ufract, x:ufract);
  vmudl $v07, $v06, $v20                             ## L:126  |  ***37 | asm_op("vmudl", x3, x2:ufract, x:ufract);
  vmudm $v29, $v16, $v31.e0                          ## L:129  |     38 | VTEMP   = ym1 >> 1;
  vmudm $v26, $v18, $v31.e0                          ## L:130  |     39 | y1_05   = y1  >> 1;
  vmudm $v22, $v19, $v31.e0                          ## L:131  |     40 | y2_05   = y2  >> 1;
  vsubc $v25, $v00, $v29.v                           ## L:132  |    *42 | ym1_05n = VZERO - VTEMP;
  vsubc $v28, $v00, $v17.v                           ## L:133  |     43 | y0_n    = VZERO - y0;
  vsubc $v27, $v00, $v18.v                           ## L:134  |     44 | y1_n    = VZERO - y1;
  vsubc $v23, $v00, $v22.v                           ## L:135  |     45 | y2_05n  = VZERO - y2_05;
  vmudm $v21, $v17, $v31.e0                          ## L:136  |     46 | y0_05   = y0  >> 1;
  vmudh $v29, $v17, $v30.e7                          ## L:138  |     47 | VTEMP = y0:sint  * 1;
  vmadm $v29, $v26, $v20.v                           ## L:139  |     48 | VTEMP = y1_05   +* x:ufract;
  vmadm $v29, $v25, $v20.v                           ## L:140  |     49 | VTEMP = ym1_05n +* x:ufract;
  vmadm $v29, $v16, $v06.v                           ## L:141  |     50 | VTEMP = ym1     +* x2:ufract;
  vmadm $v29, $v28, $v06.v                           ## L:142  |     51 | VTEMP = y0_n    +* x2:ufract;
  vmadm $v29, $v28, $v06.v                           ## L:143  |     52 | VTEMP = y0_n    +* x2:ufract;
  vmadm $v29, $v28, $v06.v                           ## L:144  |     53 | VTEMP = y0_n    +* x2:ufract;
  vmadm $v29, $v21, $v06.v                           ## L:145  |     54 | VTEMP = y0_05   +* x2:ufract;
  vmadm $v29, $v18, $v06.v                           ## L:146  |     55 | VTEMP = y1      +* x2:ufract;
  vmadm $v29, $v18, $v06.v                           ## L:147  |     56 | VTEMP = y1      +* x2:ufract;
  vmadm $v29, $v23, $v06.v                           ## L:148  |     57 | VTEMP = y2_05n  +* x2:ufract;
  vmadm $v29, $v22, $v07.v                           ## L:149  |     58 | VTEMP = y2_05   +* x3:ufract;
  vmadm $v29, $v25, $v07.v                           ## L:150  |     59 | VTEMP = ym1_05n +* x3:ufract;
  vmadm $v29, $v17, $v07.v                           ## L:151  |     60 | VTEMP = y0      +* x3:ufract;
  vmadm $v29, $v21, $v07.v                           ## L:152  |     61 | VTEMP = y0_05   +* x3:ufract;
  vmadm $v29, $v27, $v07.v                           ## L:153  |     62 | VTEMP = y1_n    +* x3:ufract;
  vmadm $v29, $v27, $v07.v                           ## L:154  |     63 | VTEMP = y1_n    +* x3:ufract;
  vmadm $v29, $v26, $v07.v                           ## L:155  |     64 | VTEMP = y1_05   +* x3:ufract;
  lqv $v07, 0, 0, $a2                                ## L:423  |      ^ | tvolume = load(CUR_TVOLUME_ADDR, 0x00);
  lqv $v06, 0, 16, $a2                               ## L:424  |     65 | @Barrier("volume") volume = load(CUR_TVOLUME_ADDR, 0x10); ## Barrier: 0x4
  lqv $v21, 0, 0, $s2                                ## L:430  |     66 | final_lr_1   = load(outptr,  0);
  lqv $v22, 0, 16, $s2                               ## L:431  |     67 | final_lr_2   = load(outptr, 16);
  lqv $v23, 0, 0, $s2                                ## L:432  |     68 | final_auxa_1 = load(outptr,  0);
  lqv $v24, 0, 16, $s2                               ## L:433  |     69 | final_auxa_2 = load(outptr, 16);
  lqv $v25, 0, 0, $s2                                ## L:434  |     70 | final_auxb_1 = load(outptr,  0);
  lqv $v26, 0, 16, $s2                               ## L:435  |     71 | final_auxb_2 = load(outptr, 16);
  vmudn $v20, $v02, $v30.e4                          ## L:437  |      ^ | x = vec_pos:ufract << (16 - 12 - 1);
  vmudm $v07, $v07, $v04.e3                          ## L:443  |     72 | tvolume = tvolume:sfract  * vec_step8:ufract.3;
  vmadm $v07, $v06, $v04.e2                          ## L:444  |     73 | tvolume = volume:sfract  +* vec_step8:ufract.2;
  vand $v29, $v29, $v05.v                            ## L:448  |     74 | VTEMP &= finish_mask;
  or $t7, $zero, $t8                                 ## L:449  |      ^ | prev_bitmask = finish_bitmask;
  .set macro # print                                 ## L:451  |      0 | print(finish_bitmask);
  emux_dump_gpr $t8                                  ## L:451  |      0 | print(finish_bitmask);
  .set noat # print                                  ## L:451  |      0 | print(finish_bitmask);
  .set nomacro # print                               ## L:451  |      0 | print(finish_bitmask);
  .set macro # print                                 ## L:452  |      0 | print(finish_mask);
  emux_dump_vpr $v05                                 ## L:452  |      0 | print(finish_mask);
  .set noat # print                                  ## L:452  |      0 | print(finish_mask);
  .set nomacro # print                               ## L:452  |      0 | print(finish_mask);
  .set macro # print                                 ## L:453  |      0 | print(vec_pos:sint);
  emux_dump_vpr $v01                                 ## L:453  |      0 | print(vec_pos:sint);
  .set noat # print                                  ## L:453  |      0 | print(vec_pos:sint);
  .set nomacro # print                               ## L:453  |      0 | print(vec_pos:sint);
  .set macro # print                                 ## L:454  |      0 | print(vec_pos:ufract);
  emux_dump_vpr $v02                                 ## L:454  |      0 | print(vec_pos:ufract);
  .set noat # print                                  ## L:454  |      0 | print(vec_pos:ufract);
  .set nomacro # print                               ## L:454  |      0 | print(vec_pos:ufract);
  .set macro # print                                 ## L:455  |      0 | print(y0);
  emux_dump_vpr $v17                                 ## L:455  |      0 | print(y0);
  .set noat # print                                  ## L:455  |      0 | print(y0);
  .set nomacro # print                               ## L:455  |      0 | print(y0);
  .set macro # print                                 ## L:456  |      0 | print(VTEMP);
  emux_dump_vpr $v29                                 ## L:456  |      0 | print(VTEMP);
  .set noat # print                                  ## L:456  |      0 | print(VTEMP);
  .set nomacro # print                               ## L:456  |      0 | print(VTEMP);
  vsubc $v27, $v02, $v04.e0                          ## L:464  |     75 | asm_op("vsubc", scratch, vec_pos:ufract, vec_step8:ufract.0);
  vge $v27, $v01, $v03.e0                            ## L:465  |     76 | asm_op("vge", scratch, vec_pos:sint, vec_step8:sint.0);
  cfc2 $t8, $vcc                                     ## L:466  |     77 | finish_bitmask = get_vcc();
  vnor $v05, $v00, $v00.v                            ## L:467  |      ^ | finish_mask = ~VZERO;
  vnor $v27, $v00, $v00.v                            ## L:476  |     78 | scratch = ~VZERO;
  vmudn $v28, $v27, $v30.e6                          ## L:477  |  ***82 | posmask = scratch << 1;
  sqv $v08, 0, 96, $k0                               ## L:484  |      ^ | @Barrier("tap0") store(b0, SCRATCH_ADDR, 0x60); ## Barrier: 0x8
  lqv $v16, 0, 96, $k0                               ## L:485  |     83 | @Barrier("tap0") ym1 = load(SCRATCH_ADDR, 0x60); ## Barrier: 0x8
  sqv $v09, 0, 80, $k0                               ## L:487  |     84 | @Barrier("tap1") store(b1, SCRATCH_ADDR, 0x50); ## Barrier: 0x10
  lqv $v17, 0, 80, $k0                               ## L:488  |     85 | @Barrier("tap1") y0 = load(SCRATCH_ADDR, 0x50); ## Barrier: 0x10
  sqv $v10, 0, 64, $k0                               ## L:490  |     86 | @Barrier("tap2") store(b2, SCRATCH_ADDR, 0x40); ## Barrier: 0x20
  lqv $v18, 0, 64, $k0                               ## L:491  |     87 | @Barrier("tap2") y1 = load(SCRATCH_ADDR, 0x40); ## Barrier: 0x20
  sqv $v11, 0, 48, $k0                               ## L:493  |     88 | @Barrier("tap3") store(b3, SCRATCH_ADDR, 0x30); ## Barrier: 0x40
  lqv $v19, 0, 48, $k0                               ## L:494  |     89 | @Barrier("tap3") y2 = load(SCRATCH_ADDR, 0x30); ## Barrier: 0x40
  vaddc $v02, $v02, $v04.e7                          ## L:502  |      ^ | vec_pos += vec_step8.7;
  vadd $v01, $v01, $v03.e7                           ## L:502  |     90 | vec_pos += vec_step8.7;
  vmudl $v08, $v02, $v30.e3                          ## L:505  |   **93 | vec32 vec_pos_int = vec_pos << 4;
  vmadn $v08, $v01, $v30.e3                          ## L:505  |     94 | vec32 vec_pos_int = vec_pos << 4;
  vmudn $v09, $v02, $v30.e3                          ## L:505  |     95 | vec32 vec_pos_int = vec_pos << 4;
  vmudh $v21, $v21, $v30.e7                          ## L:508  |     96 | final_lr_1 = final_lr_1:sint    * 1;
  vmacf $v21, $v06, $v29.q0                          ## L:509  |     97 | final_lr_1 = volume:sfract     +* VTEMP:sint.00224466;
  vmudh $v22, $v22, $v30.e7                          ## L:510  |     98 | final_lr_2 = final_lr_2:sint    * 1;
  vmacf $v22, $v06, $v29.q1                          ## L:511  |     99 | final_lr_2 = volume:sfract     +* VTEMP:sint.11335577;
  vmudh $v23, $v23, $v30.e7                          ## L:513  |    100 | final_auxa_1 = final_auxa_1:sint    * 1;
  vmacf $v23, $v06, $v29.q0                          ## L:514  |    101 | final_auxa_1 = volume:sfract     +* VTEMP:sint.00224466;
  vmudh $v24, $v24, $v30.e7                          ## L:515  |    102 | final_auxa_2 = final_auxa_2:sint    * 1;
  vmacf $v24, $v06, $v29.q1                          ## L:516  |    103 | final_auxa_2 = volume:sfract     +* VTEMP:sint.11335577;
  vmudh $v25, $v25, $v30.e7                          ## L:518  |    104 | final_auxb_1 = final_auxb_1:sint    * 1;
  vmacf $v25, $v06, $v29.q0                          ## L:519  |    105 | final_auxb_1 = volume:sfract     +* VTEMP:sint.00224466;
  vmudh $v26, $v26, $v30.e7                          ## L:520  |    106 | final_auxb_2 = final_auxb_2:sint    * 1;
  vmacf $v26, $v06, $v29.q1                          ## L:521  |    107 | final_auxb_2 = volume:sfract     +* VTEMP:sint.11335577;
  vand $v08, $v08, $v28.v                            ## L:524  |    108 | vec_pos_int:sint &= posmask;
  sqv $v08, 0, 0, $k0                                ## L:526  | ***112 | @Barrier("vpos") store(vec_pos_int:sint, SCRATCH_ADDR, 0x00); ## Barrier: 0x1
  sqv $v07, 0, 16, $a2                               ## L:529  |    113 | @Barrier("volume") store(tvolume, CUR_TVOLUME_ADDR, 0x10); ## Barrier: 0x4
  sqv $v21, 0, 0, $s2                                ## L:532  |    114 | store(final_lr_1,   outptr,  0);
  sqv $v22, 0, 16, $s2                               ## L:533  |    115 | store(final_lr_2,   outptr, 16);
  sqv $v23, 0, 0, $s2                                ## L:534  |    116 | store(final_auxa_1, outptr,  0);
  sqv $v24, 0, 16, $s2                               ## L:535  |    117 | store(final_auxa_2, outptr, 16);
  sqv $v25, 0, 0, $s2                                ## L:536  |    118 | store(final_auxb_1, outptr,  0);
  sqv $v26, 0, 16, $s2                               ## L:537  |    119 | store(final_auxb_2, outptr, 16);
  .set macro # print                                 ## L:539  |      0 | print(final_lr_1);
  emux_dump_vpr $v21                                 ## L:539  |      0 | print(final_lr_1);
  .set noat # print                                  ## L:539  |      0 | print(final_lr_1);
  .set nomacro # print                               ## L:539  |      0 | print(final_lr_1);
  .set macro # print                                 ## L:540  |      0 | print(final_lr_2);
  emux_dump_vpr $v22                                 ## L:540  |      0 | print(final_lr_2);
  .set noat # print                                  ## L:540  |      0 | print(final_lr_2);
  .set nomacro # print                               ## L:540  |      0 | print(final_lr_2);
  addiu $s2, $s2, 32                                 ## L:544  |    120 | outptr += 32;
  bne $t7, $zero, LABEL_Resample_16_Mono_000B        ## L:546  |    121 | if (prev_bitmask != 0)
  addiu $a1, $a1, 65528                              ## L:543  |   *123 | nsamples -= 8;
  bgtz $a1, LABEL_Resample_16_Mono_000A              ## L:547  |    124 | break;
  nop                                                ## L:547  |   *126 | break;
  LABEL_Resample_16_Mono_000B:
  jr $ra                                             ## L:548  |    127 | 
  nop                                                ## L:548  |   *129 | 
Mixer_Resample:
  mfc0 $s7, COP0_DP_CLOCK                            ## L:578  |      ^ | u32 ticks = get_ticks();
  lhu $a1, %lo(NSAMPLES + 0)                         ## L:582  |      2 | u16<$a1> nsamples = load(NSAMPLES);
  or $s0, $zero, $a0                                 ## L:584  |      3 | dma_in(SETTINGS, waveform_ptr);
  ori $t0, $zero, 31                                 ## L:584  |      4 | dma_in(SETTINGS, waveform_ptr);
  ori $s4, $zero, %lo(SETTINGS)                      ## L:584  |      5 | dma_in(SETTINGS, waveform_ptr);
  addiu $t2, $zero, 12                               ## L:584  |      6 | dma_in(SETTINGS, waveform_ptr);
  jal DMAExec                                        ## L:584  |      7 | dma_in(SETTINGS, waveform_ptr); ## Args: $t0, $t1, $s0, $s4, $t2
  sw $s7, %lo(TICKS)($zero)                          ## L:579  |     *9 | store(ticks, TICKS);
  blez $a1, assertion_failed                         ## L:585  |     10 | if (nsamples <= 0) assert(0x7003);
  lui $at, 28675                                     ## L:585  |    *12 | if (nsamples <= 0) assert(0x7003);
  LABEL_Mixer_Resample_000E:
  sltiu $at, $a1, 513                                ## L:586  |     13 | if (nsamples > 512) assert(0x7004);
  beq $at, $zero, assertion_failed                   ## L:586  |     14 | if (nsamples > 512) assert(0x7004);
  lui $at, 28676                                     ## L:586  |    *16 | if (nsamples > 512) assert(0x7004);
  LABEL_Mixer_Resample_000F:
  ori $k0, $zero, %lo(SCRATCH)                       ## L:590  |     17 | u32<$k0> SCRATCH_ADDR = SCRATCH;
  ori $a2, $zero, %lo(CUR_TVOLUME)                   ## L:591  |     18 | u32<$a2> CUR_TVOLUME_ADDR = CUR_TVOLUME;
  ori $s7, $zero, %lo(SETTINGS)                      ## L:592  |     19 | u32 SETTINGS_ADDR = SETTINGS;
  lhu $s2, %lo(DMEM_BUFFER + 0)                      ## L:594  |     20 | u16<$s2> outptr = load(DMEM_BUFFER);
  lw $s6, 16($s7)                                    ## L:598  |     21 | wv_addr      = load(SETTINGS_ADDR, 0x10);
  lw $s5, 0($s7)                                     ## L:599  |     22 | wv_pos       = load(SETTINGS_ADDR, 0x00);
  lw $s3, 4($s7)                                     ## L:600  |     23 | wv_step      = load(SETTINGS_ADDR, 0x04);
  lw $s1, 8($s7)                                     ## L:601  |     24 | wv_len       = load(SETTINGS_ADDR, 0x08);
  lw $fp, 12($s7)                                    ## L:602  |     25 | wv_loop_len  = load(SETTINGS_ADDR, 0x0c);
  andi $k1, $s6, 3                                   ## L:606  |     26 | u32 fmt = wv_addr & 3;
  bne $k1, $zero, LABEL_Mixer_Resample_0010          ## L:607  |     27 | if (fmt == 0) {
  ori $s4, $zero, %lo(SAMPLE_CACHE)                  ## L:589  |    *29 | u32<$s4> sample_cache_addr = SAMPLE_CACHE;
  beq $zero, $zero, LABEL_Mixer_Resample_0011        ## L:608  |     30 | ResampleFunc = Resample_16_Mono;
  ori $sp, $zero, %lo(Resample_16_Mono)              ## L:608  |    *32 | ResampleFunc = Resample_16_Mono;
  LABEL_Mixer_Resample_0010:
  addiu $at, $zero, 1                                ## L:609  |     33 | } else if (fmt == 1) {
  bne $k1, $at, LABEL_Mixer_Resample_0012            ## L:609  |     34 | } else if (fmt == 1) {
  nop                                                ## L:609  |    *36 | } else if (fmt == 1) {
  beq $zero, $zero, LABEL_Mixer_Resample_0011        ## L:610  |     37 | ResampleFunc = Resample_8_Mono;
  ori $sp, $zero, %lo(Resample_8_Mono)               ## L:610  |    *39 | ResampleFunc = Resample_8_Mono;
  LABEL_Mixer_Resample_0012:
  j assertion_failed                                 ## L:612  |     40 | assert(0x7005);
  lui $at, 28677                                     ## L:612  |    *42 | assert(0x7005);
  LABEL_Mixer_Resample_0011:
  ori $at, $zero, %lo(STEP8)                         ## L:619  |     43 | step8 = load(STEP8);
  lqv $v26, 0, 0, $at                                ## L:619  |     44 | step8 = load(STEP8);
  mtc2 $s3, $v28.e0                                  ## L:620  |     45 | vec_step8.0 = wv_step;
  srl $at, $s3, 16                                   ## L:620  |     46 | vec_step8.0 = wv_step;
  mtc2 $at, $v27.e0                                  ## L:620  |   **49 | vec_step8.0 = wv_step;
  vmudm $v28, $v26, $v28.e0                          ## L:621  |      ^ | vec_step8 = step8:sint * vec_step8.0;
  vmadh $v27, $v26, $v27.e0                          ## L:621  |  ***53 | vec_step8 = step8:sint * vec_step8.0;
  vmadn $v28, $v00, $v00                             ## L:621  |     54 | vec_step8 = step8:sint * vec_step8.0;
  ori $at, $zero, %lo(CUR_STEP8)                     ## L:622  |      ^ | store(vec_step8, CUR_STEP8);
  sqv $v27, 0, 0, $at                                ## L:622  |   **57 | store(vec_step8, CUR_STEP8);
  sqv $v28, 0, 16, $at                               ## L:622  |     58 | store(vec_step8, CUR_STEP8);
  addiu $at, $zero, 21845                            ## L:629  |     59 | set_vcc(0x5555);
  ctc2 $at, $vcc                                     ## L:629  |     60 | set_vcc(0x5555);
  llv $v25, 0, 20, $s7                               ## L:630  |     61 | tvolume.01 = load(SETTINGS_ADDR, 0x14).01;
  llv $v24, 0, 24, $s7                               ## L:631  |  ***65 | volume.01 = load(SETTINGS_ADDR, 0x18).01;
  vor $v23, $v00, $v25.e0                            ## L:632  |      ^ | tmp1 = tvolume.0;
  vor $v22, $v00, $v24.e0                            ## L:633  |  ***69 | tmp2 = volume.0;
  vmrg $v25, $v23, $v25.e1                           ## L:634  |     70 | tvolume = select(tmp1, tvolume.1);
  vmrg $v24, $v22, $v24.e1                           ## L:635  |  ***74 | volume = select(tmp2, volume.1);
  sqv $v25, 0, 0, $a2                                ## L:636  |      ^ | store(tvolume, CUR_TVOLUME_ADDR, 0x00);
  sqv $v24, 0, 16, $a2                               ## L:637  |  ***78 | store(volume,  CUR_TVOLUME_ADDR, 0x10);
  LABEL_Mixer_Resample_0014:
  .set macro # print                                 ## L:647  |      0 | print("begin loop\n");
  emux_log_string "begin loop\n"                     ## L:647  |      0 | print("begin loop\n");
  .set noat # print                                  ## L:647  |      0 | print("begin loop\n");
  .set nomacro # print                               ## L:647  |      0 | print("begin loop\n");
  .set macro # print                                 ## L:648  |      0 | print(wv_pos, wv_len, nsamples);
  emux_dump_gpr $s5, $s1, $a1                        ## L:648  |      0 | print(wv_pos, wv_len, nsamples);
  .set noat # print                                  ## L:648  |      0 | print(wv_pos, wv_len, nsamples);
  .set nomacro # print                               ## L:648  |      0 | print(wv_pos, wv_len, nsamples);
  LABEL_Mixer_Resample_0016:
  sltu $at, $s5, $s1                                 ## L:651  |     79 | while (wv_pos >= wv_len) {
  bne $at, $zero, LABEL_Mixer_Resample_0017          ## L:651  |     80 | while (wv_pos >= wv_len) {
  nop                                                ## L:651  |    *82 | while (wv_pos >= wv_len) {
  bne $fp, $zero, LABEL_Mixer_Resample_0018          ## L:654  |     83 | if (wv_loop_len == 0) {
  nop                                                ## L:654  |    *85 | if (wv_loop_len == 0) {
  j Mixer_Resample_Epilog                            ## L:656  |     86 | goto Mixer_Resample_Epilog;
  or $s5, $zero, $s1                                 ## L:655  |    *88 | wv_pos = wv_len;
  LABEL_Mixer_Resample_0018:
  j LABEL_Mixer_Resample_0016                        ## L:658  |     89 | wv_pos -= wv_loop_len;
  subu $s5, $s5, $fp                                 ## L:658  |    *91 | wv_pos -= wv_loop_len;
  LABEL_Mixer_Resample_0017:
  srl $k1, $s5, 12                                   ## L:662  |     92 | wv_pos_to_dmem = wv_pos >> 12;
  addu $s0, $k1, $s6                                 ## L:663  |     93 | rdram_addr = wv_pos_to_dmem + wv_addr;
  addiu $s0, $s0, 65534                              ## L:664  |     94 | rdram_addr -= 2;
  .set macro # print                                 ## L:666  |      0 | print("toread: ");
  emux_log_string "toread: "                         ## L:666  |      0 | print("toread: ");
  .set noat # print                                  ## L:666  |      0 | print("toread: ");
  .set nomacro # print                               ## L:666  |      0 | print("toread: ");
  subu $t9, $s1, $s5                                 ## L:667  |     95 | u32 toread = wv_len - wv_pos;
  srl $t9, $t9, 12                                   ## L:668  |     96 | toread >>= 12;
  sltiu $at, $t9, 193                                ## L:670  |     97 | if (toread > 192) {
  bne $at, $zero, LABEL_Mixer_Resample_0019          ## L:670  |     98 | if (toread > 192) {
  nop                                                ## L:670  |   *100 | if (toread > 192) {
  addiu $t9, $zero, 192                              ## L:671  |    101 | toread = 192;
  LABEL_Mixer_Resample_0019:
  .set macro # print                                 ## L:673  |      0 | print(toread);
  emux_dump_gpr $t9                                  ## L:673  |      0 | print(toread);
  .set noat # print                                  ## L:673  |      0 | print(toread);
  .set nomacro # print                               ## L:673  |      0 | print(toread);
  addiu $t0, $t9, 24                                 ## L:681  |    102 | u32<$t0> dmaread = toread + 16 + 8;
  .set macro # print                                 ## L:682  |      0 | print(dmaread);
  emux_dump_gpr $t0                                  ## L:682  |      0 | print(dmaread);
  .set noat # print                                  ## L:682  |      0 | print(dmaread);
  .set nomacro # print                               ## L:682  |      0 | print(dmaread);
  addiu $t0, $t0, -1                                 ## L:684  |    103 | dma_in_async(sample_cache_addr, rdram_addr, dmaread);
  or $t2, $zero, $zero                               ## L:684  |    104 | dma_in_async(sample_cache_addr, rdram_addr, dmaread);
  jal DMAExec                                        ## L:684  |    105 | dma_in_async(sample_cache_addr, rdram_addr, dmaread); ## Args: $t0, $t1, $s0, $s4, $t2
  ori $s4, $zero, %lo(SAMPLE_CACHE)                  ## L:683  |   *107 | sample_cache_addr = SAMPLE_CACHE;
  subu $k1, $k1, $s4                                 ## L:685  |    108 | wv_pos_to_dmem -= sample_cache_addr;
  sll $k1, $k1, 12                                   ## L:686  |    109 | wv_pos_to_dmem <<= 12;
  subu $t8, $s5, $k1                                 ## L:693  |    110 | u32 begin_pos = wv_pos - wv_pos_to_dmem;
  srl $t7, $s5, 12                                   ## L:694  |    111 | u32 end_pos = wv_pos >> 12;
  addu $t7, $t7, $t9                                 ## L:695  |    112 | end_pos += toread;
  sll $t7, $t7, 12                                   ## L:696  |    113 | end_pos <<= 12;
  subu $t7, $t7, $k1                                 ## L:697  |    114 | end_pos -= wv_pos_to_dmem;
  .set macro # print                                 ## L:698  |      0 | print(end_pos);
  emux_dump_gpr $t7                                  ## L:698  |      0 | print(end_pos);
  .set noat # print                                  ## L:698  |      0 | print(end_pos);
  .set nomacro # print                               ## L:698  |      0 | print(end_pos);
  ori $at, $zero, %lo(CUR_STEP8)                     ## L:701  |    115 | vec_step8 = load(CUR_STEP8);
  lqv $v03, 0, 0, $at                                ## L:701  |    116 | vec_step8 = load(CUR_STEP8);
  lqv $v04, 0, 16, $at                               ## L:701  |    117 | vec_step8 = load(CUR_STEP8);
  mtc2 $t8, $v02.e0                                  ## L:702  |  **120 | vec_pos.0 = begin_pos;
  srl $at, $t8, 16                                   ## L:702  |    121 | vec_pos.0 = begin_pos;
  mtc2 $at, $v01.e0                                  ## L:702  |  **124 | vec_pos.0 = begin_pos;
  vaddc $v02, $v04, $v02.e0                          ## L:703  |      ^ | vec_pos = vec_step8 + vec_pos.0;
  vadd $v01, $v03, $v01.e0                           ## L:703  | ***128 | vec_pos = vec_step8 + vec_pos.0;
  vsubc $v02, $v02, $v04.e0                          ## L:704  |    129 | vec_pos -= vec_step8.0;
  vsub $v01, $v01, $v03.e0                           ## L:704  |  **132 | vec_pos -= vec_step8.0;
  mtc2 $t7, $v04.e0                                  ## L:709  |      ^ | vec_step8.0 = end_pos;
  srl $at, $t7, 16                                   ## L:709  |    133 | vec_step8.0 = end_pos;
  mtc2 $at, $v03.e0                                  ## L:709  |   *135 | vec_step8.0 = end_pos;
  ori $at, $zero, %lo(VOLUME_FILTER_TAPS)            ## L:716  |    136 | vec_step8:ufract.23 = load(VOLUME_FILTER_TAPS, 0x00).01;
  llv $v04, 4, 0, $at                                ## L:716  |    137 | vec_step8:ufract.23 = load(VOLUME_FILTER_TAPS, 0x00).01;
  ori $at, $zero, %lo(CUR_TVOLUME)                   ## L:717  |    138 | vec_step8:ufract.45 = load(CUR_TVOLUME).01;
  llv $v04, 8, 0, $at                                ## L:717  |    139 | vec_step8:ufract.45 = load(CUR_TVOLUME).01;
  vxor $v05, $v00, $v00.e0                           ## L:722  |      ^ | vec16<$v05> finish_mask; finish_mask = 0;
  or $t8, $zero, $zero                               ## L:723  |    140 | u32<$t8> finish_bitmask = 0; u32<$t7> prev_bitmask = 0;
  or $t7, $zero, $zero                               ## L:723  |    141 | u32<$t8> finish_bitmask = 0; u32<$t7> prev_bitmask = 0;
  vmudl $v28, $v02, $v30.e4                          ## L:726  |      ^ | vec16 vec_pos_int = vec_pos << 3;
  vmadn $v28, $v01, $v30.e4                          ## L:726  |    142 | vec16 vec_pos_int = vec_pos << 3;
  vmudn $v28, $v28, $v30.e6                          ## L:727  | ***146 | vec_pos_int <<= 1;
  sqv $v28, 0, 0, $k0                                ## L:728  | ***150 | @Barrier("vpos") store(vec_pos_int, SCRATCH_ADDR, 0x00); ## Barrier: 0x1
  addiu $s2, $s2, 65504                              ## L:731  |    151 | outptr -= 32;
  addiu $a1, $a1, 8                                  ## L:732  |    152 | nsamples += 8;
  andi $t9, $s2, 15                                  ## L:735  |    153 | u32 align = outptr & 15;
  bne $t9, $zero, assertion_failed                   ## L:736  |    154 | if (align != 0) assert(0x7006);
  lui $at, 28678                                     ## L:736  |   *156 | if (align != 0) assert(0x7006);
  LABEL_Mixer_Resample_001A:
  jal $sp                                            ## L:740  |    157 | ResampleFunc();
  nop                                                ## L:740  |   *159 | ResampleFunc();
  ssv $v01, 0, 0, $k0                                ## L:746  |    160 | @Barrier("vpos") store(vec_pos:sint.0, SCRATCH_ADDR, 0x00); ## Barrier: 0x1
  ssv $v02, 0, 2, $k0                                ## L:747  |    161 | @Barrier("vpos") store(vec_pos:ufract.0, SCRATCH_ADDR, 0x02); ## Barrier: 0x1
  lw $s5, 0($k0)                                     ## L:748  |    162 | @Barrier("vpos") wv_pos = load(SCRATCH_ADDR, 0x00); ## Barrier: 0x1
  sll $t9, $s3, 3                                    ## L:749  |    163 | wv_pos -= wv_step * 8;
  subu $s5, $s5, $t9                                 ## L:749  |   *165 | wv_pos -= wv_step * 8;
  bgtz $a1, LABEL_Mixer_Resample_0014                ## L:752  |    166 | wv_pos += wv_pos_to_dmem;
  addu $s5, $s5, $k1                                 ## L:752  |   *168 | wv_pos += wv_pos_to_dmem;
  Mixer_Resample_Epilog:
  lw $k1, %lo(CUR_TVOLUME + 16)                      ## L:794  |    169 | u32 volume = load(CUR_TVOLUME, 0x10).01;
  sw $k1, %lo(24 + SETTINGS)($zero)                  ## L:797  |  **172 | store(volume, SETTINGS, 0x18);
  or $s0, $zero, $a0                                 ## L:798  |    173 | dma_out_async(SETTINGS, waveform_ptr);
  ori $t0, $zero, 31                                 ## L:798  |    174 | dma_out_async(SETTINGS, waveform_ptr);
  ori $s4, $zero, %lo(SETTINGS)                      ## L:798  |    175 | dma_out_async(SETTINGS, waveform_ptr);
  addiu $t2, $zero, -32768                           ## L:798  |    176 | dma_out_async(SETTINGS, waveform_ptr);
  jal DMAExec                                        ## L:798  |    177 | dma_out_async(SETTINGS, waveform_ptr); ## Args: $t0, $t1, $s0, $s4, $t2
  sw $s5, %lo(0 + SETTINGS)($zero)                   ## L:796  |   *179 | store(wv_pos, SETTINGS, 0x00);
  mfc0 $t9, COP0_DP_CLOCK                            ## L:801  |    180 | u32 ticks2 = get_ticks();
  lw $t8, %lo(TICKS + 0)                             ## L:802  |    181 | u32 ticks1 = load(TICKS);
  subu $t9, $t9, $t8                                 ## L:803  |  **184 | ticks2 -= ticks1;
  .set macro # print                                 ## L:804  |      0 | print("Mixer_Resample ticks: ");
  emux_log_string "Mixer_Resample ticks: "           ## L:804  |      0 | print("Mixer_Resample ticks: ");
  .set noat # print                                  ## L:804  |      0 | print("Mixer_Resample ticks: ");
  .set nomacro # print                               ## L:804  |      0 | print("Mixer_Resample ticks: ");
  .set macro # print                                 ## L:805  |      0 | print(ticks2);
  emux_dump_gpr $t9                                  ## L:805  |      0 | print(ticks2);
  .set noat # print                                  ## L:805  |      0 | print(ticks2);
  .set nomacro # print                               ## L:805  |      0 | print(ticks2);
  j RSPQ_Loop                                        ## L:806  |    185 | }
  nop                                                ## L:806  |   *187 | }
Mixer_Interleave:
  addiu $a0, $a0, 8                                  ## L:815  |      ^ | nsamples += 8;
  addiu $a3, $a3, 65504                              ## L:816  |      2 | out -= 32;
  or $s7, $zero, $a3                                 ## L:817  |      3 | u32 old_out = out;
  lqv $v24, 0, 0, $a3                                ## L:818  |      4 | vec16 old1 = load(out, 0x00);
  lqv $v23, 0, 16, $a3                               ## L:819  |      5 | vec16 old2 = load(out, 0x10);
  addiu $at, $zero, 21845                            ## L:821  |      6 | set_vcc(0x5555);
  ctc2 $at, $vcc                                     ## L:821  |     *8 | set_vcc(0x5555);
  LABEL_Mixer_Interleave_001B:
  vmov $v22.e0, $v28.e4                              ## L:824  |      ^ | merged.0 = s1.4;
  vmov $v22.e1, $v27.e4                              ## L:825  |      9 | merged.1 = s2.4;
  vmov $v22.e2, $v28.e5                              ## L:826  |     10 | merged.2 = s1.5;
  vmov $v22.e3, $v27.e5                              ## L:827  |     11 | merged.3 = s2.5;
  vmov $v22.e4, $v28.e6                              ## L:828  |     12 | merged.4 = s1.6;
  vmov $v22.e5, $v27.e6                              ## L:829  |     13 | merged.5 = s2.6;
  vmov $v22.e6, $v28.e7                              ## L:830  |     14 | merged.6 = s1.7;
  vmov $v22.e7, $v27.e7                              ## L:831  |     15 | merged.7 = s2.7;
  slv $v28, 0, 0, $a3                                ## L:833  |      ^ | store(s1.01, out, 0x00);
  slv $v27, 0, 4, $a3                                ## L:834  |     16 | store(s2.01, out, 0x04);
  slv $v28, 4, 8, $a3                                ## L:835  |     17 | store(s1.23, out, 0x08);
  slv $v27, 4, 12, $a3                               ## L:836  |     18 | store(s2.23, out, 0x0C);
  sqv $v22, 0, 16, $a3                               ## L:837  |     19 | store(merged, out, 0x10);
  lqv $v26, 0, 0, $a1                                ## L:839  |     20 | n1 = load(in1);
  lqv $v25, 0, 0, $a2                                ## L:840  |  ***24 | n2 = load(in2);
  vor $v21, $v00, $v26.q1                            ## L:842  |      ^ | vec16 tmp1 = n1.11335577;
  vmrg $v26, $v26, $v25.q0                           ## L:843  |  ***28 | n1 = select(n1, n2.00224466);
  vmrg $v25, $v21, $v25.q1                           ## L:844  |     29 | n2 = select(tmp1, n2.11335577);
  addiu $a0, $a0, 65528                              ## L:846  |      ^ | nsamples -= 8;
  vor $v27, $v00, $v25                               ## L:849  |  ***33 | s2 = n2;
  addiu $a3, $a3, 32                                 ## L:851  |      ^ | out += 32;
  addiu $a1, $a1, 16                                 ## L:852  |     34 | in1 += 16;
  addiu $a2, $a2, 16                                 ## L:853  |     35 | in2 += 16;
  bgtz $a0, LABEL_Mixer_Interleave_001B              ## L:853  |     36 | in2 += 16;
  vor $v28, $v00, $v26                               ## L:848  |    *38 | s1 = n1;
  LABEL_Mixer_Interleave_001C:
  sqv $v23, 0, 16, $s7                               ## L:858  |     39 | store(old2, old_out, 0x10);
  j RSPQ_Loop                                        ## L:859  |     40 | }
  sqv $v24, 0, 0, $s7                                ## L:857  |    *42 | store(old1, old_out, 0x00);

OVERLAY_CODE_END:

#define zero $0
#define v0 $2
#define v1 $3
#define a0 $4
#define a1 $5
#define a2 $6
#define a3 $7
#define t0 $8
#define t1 $9
#define t2 $10
#define t3 $11
#define t4 $12
#define t5 $13
#define t6 $14
#define t7 $15
#define s0 $16
#define s1 $17
#define s2 $18
#define s3 $19
#define s4 $20
#define s5 $21
#define s6 $22
#define s7 $23
#define t8 $24
#define t9 $25
#define k0 $26
#define k1 $27
#define gp $28
#define sp $29
#define fp $30
#define ra $31

.set at
.set macro