! kern.S -- timing kernels and P2 helpers for cachetest.c (caches / store queues of the SH7091).
!
! Everything that is timed lives in one 8 KB-aligned section, so its instruction-cache indexes are known:
!   offset 0x000..0x7FF  run_kernel and the body functions (IC entries 0..63)
!   offset 0x800..0xBFF  blkA: 32 lines of straight-line NOPs               (IC entries 64..95)
!   offset 0xC00..0xFFF  blkB: 32 lines, each "bra next line ; nop" + pad    (IC entries 96..127)
!   offset 0x1000..0x13FF blkC: like blkB but entered at byte 24 of each line (IC entries 128..159)
!   offset 0x1400        icprobe: "rts ; nop" (address bit 12 = 1, bits 11:5 = 32)
!
! struct kdesc (cachetest.c): 0 prep mode, 4 prep base, 8 prep n, 12 prep stride, 16 body fn, 20..32 body r4..r7,
!                             36 out cycles (PMCTR1 delta), 40 out event (PMCTR0 delta)
! The body is a leaf that uses r0..r7 only.  Between the end of the prep and the first counter read no data access
! is made (all body arguments are in registers already), so the operand-cache state the prep made is what the body
! meets.

    .section .text.cachetest,"ax"
    .balign 8192
    .globl _run_kernel
_run_kernel:
    mov.l   r8,@-r15
    mov.l   r9,@-r15
    mov.l   r10,@-r15
    mov.l   r11,@-r15
    mov.l   r12,@-r15
    mov.l   r13,@-r15
    mov.l   r14,@-r15
    sts.l   pr,@-r15
    mov     r4,r14
    mov.l   .Lpm1,r10
    mov.l   .Lpm0,r11
    mov.l   @(0,r14),r12
    mov.l   @(4,r14),r0
    mov.l   @(8,r14),r1
    mov.l   @(12,r14),r2
    mov.l   @(16,r14),r13
    mov.l   @(20,r14),r4
    mov.l   @(24,r14),r5
    mov.l   @(28,r14),r6
    mov.l   @(32,r14),r7
    mov     #1,r3
    cmp/eq  r3,r12
    bt      .Lp_ocbp
    mov     #2,r3
    cmp/eq  r3,r12
    bt      .Lp_touch
    mov     #3,r3
    cmp/eq  r3,r12
    bt      .Lp_dirtyv
    mov     #4,r3
    cmp/eq  r3,r12
    bt      .Lp_dirty
    mov     #5,r3
    cmp/eq  r3,r12
    bt      .Lp_cleanv
    mov     #6,r3
    cmp/eq  r3,r12
    bt      .Lp_warm
    mov     #7,r3
    cmp/eq  r3,r12
    bt      .Lp_ocbi
    bra     .Lsettle
    nop
.Lp_ocbp:                       ! 1: purge the lines (write back if dirty, invalidate)
    ocbp    @r0
    dt      r1
    bf/s    .Lp_ocbp
    add     r2,r0
    bra     .Lsettle
    nop
.Lp_touch:                      ! 2: load one word of every line (lines valid)
    mov.l   @r0,r3
    dt      r1
    bf/s    .Lp_touch
    add     r2,r0
    bra     .Lsettle
    nop
.Lp_dirtyv:                     ! 3: purge the lines, then dirty the line 16 KB above each (same OC entry)
    mov     r0,r8
    mov     r1,r9
.Lp_dv1:
    ocbp    @r0
    dt      r1
    bf/s    .Lp_dv1
    add     r2,r0
    mov     #64,r3
    shll8   r3
    add     r3,r8
.Lp_dv2:
    mov.l   r3,@r8
    dt      r9
    bf/s    .Lp_dv2
    add     r2,r8
    bra     .Lsettle
    nop
.Lp_dirty:                      ! 4: store to word 0 of every line (lines valid and dirty)
    mov.l   r3,@r0
    dt      r1
    bf/s    .Lp_dirty
    add     r2,r0
    bra     .Lsettle
    nop
.Lp_cleanv:                     ! 5: purge the lines, then load the line 16 KB above each (clean valid victims)
    mov     r0,r8
    mov     r1,r9
.Lp_cv1:
    ocbp    @r0
    dt      r1
    bf/s    .Lp_cv1
    add     r2,r0
    mov     #64,r3
    shll8   r3
    add     r3,r8
.Lp_cv2:
    mov.l   @r8,r3
    dt      r9
    bf/s    .Lp_cv2
    add     r2,r8
    bra     .Lsettle
    nop
.Lp_warm:                       ! 6: run the body once (instruction-cache warm-up), then reload its arguments
    jsr     @r13
    nop
    mov.l   @(20,r14),r4
    mov.l   @(24,r14),r5
    mov.l   @(28,r14),r6
    mov.l   @(32,r14),r7
    bra     .Lsettle
    nop
.Lp_ocbi:                       ! 7: invalidate the lines (no write-back)
    ocbi    @r0
    dt      r1
    bf/s    .Lp_ocbi
    add     r2,r0
.Lsettle:                       ! let write-backs / fills started by the prep drain (~1000 cycles)
    mov     #-1,r3
    extu.b  r3,r3
    shll2   r3
.Lset1:
    dt      r3
    bf      .Lset1
    mov.l   @r10,r8
    mov.l   @r11,r9
    jsr     @r13
    nop
    mov.l   @r10,r0
    mov.l   @r11,r1
    sub     r8,r0
    sub     r9,r1
    mov.l   r0,@(36,r14)
    mov.l   r1,@(40,r14)
    lds.l   @r15+,pr
    mov.l   @r15+,r14
    mov.l   @r15+,r13
    mov.l   @r15+,r12
    mov.l   @r15+,r11
    mov.l   @r15+,r10
    mov.l   @r15+,r9
    rts
    mov.l   @r15+,r8
    .align 2
.Lpm1:  .long 0xFF100010        ! PMCTR1 low: elapsed CPU cycles
.Lpm0:  .long 0xFF100008        ! PMCTR0 low: the selected event

! ---------------------------------------------------------------------------------------------------------------
! bodies: r4 = base (or first node), r5 = count, r6 = stride, r7 = extra
    .globl _b_empty, _b_chain, _b_ldstream, _b_ld2, _b_ldhot, _b_ststream, _b_stgap, _b_prefld, _b_prefstream
    .globl _b_ocbwb, _b_ocbp, _b_ocbi, _b_movca, _b_sqnp, _b_sq1, _b_sq2, _b_sqpp, _b_sqw, _b_sq1gap, _b_ldgap
    .align 5
_b_empty:
    rts
    nop
    .align 5
_b_chain:                       ! pointer chase: r4 = first node, r5 = n
    mov.l   @r4,r4
    dt      r5
    bf      _b_chain
    rts
    nop
    .align 5
_b_ldstream:                    ! independent loads, one per line
    mov.l   @r4,r0
    dt      r5
    bf/s    _b_ldstream
    add     r6,r4
    rts
    nop
    .align 5
_b_ld2:                         ! word 0 then word 7 of each line
    mov.l   @r4,r0
    mov.l   @(28,r4),r1
    dt      r5
    bf/s    _b_ld2
    add     r6,r4
    rts
    nop
    .align 5
_b_ldhot:                       ! a load per line, then a load of the hot line r7
    mov.l   @r4,r0
    mov.l   @r7,r1
    dt      r5
    bf/s    _b_ldhot
    add     r6,r4
    rts
    nop
    .align 5
_b_ldgap:                       ! a load per line, then r7 turns of a delay loop
    mov.l   @r4,r0
    mov     r7,r1
1:  dt      r1
    bf      1b
    dt      r5
    bf/s    _b_ldgap
    add     r6,r4
    rts
    nop
    .align 5
_b_ststream:                    ! a store per r6 bytes
    mov.l   r0,@r4
    dt      r5
    bf/s    _b_ststream
    add     r6,r4
    rts
    nop
    .align 5
_b_stgap:                       ! a store, then r7 turns of a delay loop
    mov.l   r0,@r4
    mov     r7,r1
1:  dt      r1
    bf      1b
    dt      r5
    bf/s    _b_stgap
    add     r6,r4
    rts
    nop
    .align 5
_b_prefld:                      ! PREF a line, r7 turns of a delay loop, then load it
    pref    @r4
    mov     r7,r1
1:  dt      r1
    bf      1b
    mov.l   @r4,r0
    dt      r5
    bf/s    _b_prefld
    add     r6,r4
    rts
    nop
    .align 5
_b_prefstream:
    pref    @r4
    dt      r5
    bf/s    _b_prefstream
    add     r6,r4
    rts
    nop
    .align 5
_b_ocbwb:
    ocbwb   @r4
    dt      r5
    bf/s    _b_ocbwb
    add     r6,r4
    rts
    nop
    .align 5
_b_ocbp:
    ocbp    @r4
    dt      r5
    bf/s    _b_ocbp
    add     r6,r4
    rts
    nop
    .align 5
_b_ocbi:
    ocbi    @r4
    dt      r5
    bf/s    _b_ocbi
    add     r6,r4
    rts
    nop
    .align 5
_b_movca:
    movca.l r0,@r4
    dt      r5
    bf/s    _b_movca
    add     r6,r4
    rts
    nop
    .align 5
_b_sqnp:                        ! 8 longword stores to SQ0, no PREF
    mov.l   r0,@(0,r4)
    mov.l   r0,@(4,r4)
    mov.l   r0,@(8,r4)
    mov.l   r0,@(12,r4)
    mov.l   r0,@(16,r4)
    mov.l   r0,@(20,r4)
    mov.l   r0,@(24,r4)
    mov.l   r0,@(28,r4)
    dt      r5
    bf/s    _b_sqnp
    add     r6,r4
    rts
    nop
    .align 5
_b_sq1:                         ! fill SQ0, PREF it; the next fill waits for the transfer
    mov.l   r0,@(0,r4)
    mov.l   r0,@(4,r4)
    mov.l   r0,@(8,r4)
    mov.l   r0,@(12,r4)
    mov.l   r0,@(16,r4)
    mov.l   r0,@(20,r4)
    mov.l   r0,@(24,r4)
    mov.l   r0,@(28,r4)
    pref    @r4
    dt      r5
    bf/s    _b_sq1
    add     r6,r4
    rts
    nop
    .align 5
_b_sq2:                         ! fill SQ0, PREF, fill SQ1, PREF
    mov.l   r0,@(0,r4)
    mov.l   r0,@(4,r4)
    mov.l   r0,@(8,r4)
    mov.l   r0,@(12,r4)
    mov.l   r0,@(16,r4)
    mov.l   r0,@(20,r4)
    mov.l   r0,@(24,r4)
    mov.l   r0,@(28,r4)
    pref    @r4
    mov     r4,r1
    add     #32,r1
    mov.l   r0,@(32,r4)
    mov.l   r0,@(36,r4)
    mov.l   r0,@(40,r4)
    mov.l   r0,@(44,r4)
    mov.l   r0,@(48,r4)
    mov.l   r0,@(52,r4)
    mov.l   r0,@(56,r4)
    mov.l   r0,@(60,r4)
    pref    @r1
    dt      r5
    bf/s    _b_sq2
    add     r6,r4
    rts
    nop
    .align 5
_b_sqpp:                        ! PREF SQ0 ; PREF SQ1 back to back (same destination block each time)
    mov     r4,r1
    add     #32,r1
1:  pref    @r4
    pref    @r1
    dt      r5
    bf      1b
    rts
    nop
    .align 5
_b_sqw:                         ! PREF SQ0 then store to SQ0 at once (the store waits for the transfer)
    pref    @r4
    mov.l   r0,@r4
    dt      r5
    bf      _b_sqw
    rts
    nop
    .align 5
_b_sq1gap:                      ! fill SQ0, PREF, r7 turns of a delay loop
    mov.l   r0,@(0,r4)
    mov.l   r0,@(4,r4)
    mov.l   r0,@(8,r4)
    mov.l   r0,@(12,r4)
    mov.l   r0,@(16,r4)
    mov.l   r0,@(20,r4)
    mov.l   r0,@(24,r4)
    mov.l   r0,@(28,r4)
    pref    @r4
    mov     r7,r1
1:  dt      r1
    bf      1b
    dt      r5
    bf/s    _b_sq1gap
    add     r6,r4
    rts
    nop

    .globl _b_stld, _b_stst, _b_ldst, _b_stldo, _b_ldldo, _b_ldsto
    .align 5
_b_stld:                        ! store word 0, load word 1 of the same line
    mov.l   r0,@r4
    mov.l   @(4,r4),r1
    dt      r5
    bf/s    _b_stld
    add     r6,r4
    rts
    nop
    .align 5
_b_stst:                        ! store word 0, store word 1 of the same line
    mov.l   r0,@r4
    mov.l   r0,@(4,r4)
    dt      r5
    bf/s    _b_stst
    add     r6,r4
    rts
    nop
    .align 5
_b_ldst:                        ! load word 0, store word 1 of the same line
    mov.l   @r4,r1
    mov.l   r0,@(4,r4)
    dt      r5
    bf/s    _b_ldst
    add     r6,r4
    rts
    nop
    .align 5
_b_stldo:                       ! store to a line of A, load a line of B = A + r7
    mov.l   r0,@r4
    mov     r4,r1
    add     r7,r1
    mov.l   @r1,r2
    dt      r5
    bf/s    _b_stldo
    add     r6,r4
    rts
    nop
    .align 5
_b_ldldo:                       ! load a line of A, load a line of B = A + r7 (independent)
    mov.l   @r4,r0
    mov     r4,r1
    add     r7,r1
    mov.l   @r1,r2
    dt      r5
    bf/s    _b_ldldo
    add     r6,r4
    rts
    nop
    .align 5
_b_ldsto:                       ! load a line of A, store to a line of B = A + r7
    mov.l   @r4,r2
    mov     r4,r1
    add     r7,r1
    mov.l   r0,@r1
    dt      r5
    bf/s    _b_ldsto
    add     r6,r4
    rts
    nop

    .globl _b_ld2gap, _b_stldgap
    .align 5
_b_ld2gap:                      ! load word 0, r7 turns of a delay loop, load word 7 of the same line
    mov.l   @r4,r0
    mov     r7,r1
1:  dt      r1
    bf      1b
    mov.l   @(28,r4),r2
    dt      r5
    bf/s    _b_ld2gap
    add     r6,r4
    rts
    nop
    .align 5
_b_stldgap:                     ! store word 0, r7 turns of a delay loop, load word 7 of the same line
    mov.l   r0,@r4
    mov     r7,r1
1:  dt      r1
    bf      1b
    mov.l   @(28,r4),r2
    dt      r5
    bf/s    _b_stldgap
    add     r6,r4
    rts
    nop

    .globl _b_ocbpgap, _b_ocbwbgap, _b_sqld, _b_ldw
    .align 5
_b_ocbpgap:                     ! OCBP, then r7 turns of a delay loop
    ocbp    @r4
    mov     r7,r1
1:  dt      r1
    bf      1b
    dt      r5
    bf/s    _b_ocbpgap
    add     r6,r4
    rts
    nop
    .align 5
_b_ocbwbgap:                    ! OCBWB, then r7 turns of a delay loop
    ocbwb   @r4
    mov     r7,r1
1:  dt      r1
    bf      1b
    dt      r5
    bf/s    _b_ocbwbgap
    add     r6,r4
    rts
    nop
    .align 5
_b_sqld:                        ! fill SQ0 at r7, PREF it, then a load of line r4
    mov.l   r0,@(0,r7)
    mov.l   r0,@(4,r7)
    mov.l   r0,@(8,r7)
    mov.l   r0,@(12,r7)
    mov.l   r0,@(16,r7)
    mov.l   r0,@(20,r7)
    mov.l   r0,@(24,r7)
    mov.l   r0,@(28,r7)
    pref    @r7
    mov.l   @r4,r2
    dt      r5
    bf/s    _b_sqld
    add     r6,r4
    rts
    nop
    .align 5
_b_ldw:                         ! load word 0 of a line, then at once the word at offset r7 of the same line
    mov     r7,r0
1:  mov.l   @r4,r2
    mov.l   @(r0,r4),r1
    dt      r5
    bf/s    1b
    add     r6,r4
    rts
    nop

! ---------------------------------------------------------------------------------------------------------------
! instruction-cache blocks (32 lines each)
    .globl _blkA, _blkB, _blkC, _blkC_entry, _icprobe
    .org 0x800
_blkA:                          ! 32 lines x 16 NOPs; the last line ends rts ; nop      (IC entries 64..95)
    .rept 510
    nop
    .endr
    rts
    nop
    .org 0xC00
_blkB:                          ! each line: bra to the next line's start ; nop ; 14 pad (IC entries 96..127)
    .rept 31
    bra     .+32
    nop
    .rept 14
    nop
    .endr
    .endr
    rts
    nop
    .rept 14
    nop
    .endr
    .org 0x1000
_blkC:                          ! each line: 12 pad ; bra to the next line's byte 24 ; nop ; 2 pad (IC 128..159)
    .rept 12
    nop
    .endr
_blkC_entry:
    bra     .+32
    nop
    .rept 2
    nop
    .endr
    .rept 30
    .rept 12
    nop
    .endr
    bra     .+32
    nop
    .rept 2
    nop
    .endr
    .endr
    .rept 12
    nop
    .endr
    rts
    nop
    .rept 2
    nop
    .endr
    .org 0x1400
_icprobe:                       ! address bit 12 = 1, bits 11:5 = 32 (IC entry 160 with IIX = 0)
    rts
    nop

! ---------------------------------------------------------------------------------------------------------------
! P2 helpers (the manual: CCR and the memory-mapped cache arrays are accessed by a P2 program; a branch back to a
! cached area comes 8 instructions or more after the access)
    .text
    .globl _p2_rd, _p2_wr, _set_ccr_p2, _ccr_time_p2, _try_rd, _ic_probe_p2
    .align 5
_p2_rd:                         ! r0 = *(r4)
    mova    1f,r0
    mov.l   .Lp2m,r1
    or      r1,r0
    jmp     @r0
    nop
    .align 2
1:  mov.l   @r4,r0
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    rts
    nop
    .align 5
_p2_wr:                         ! *(r4) = r5
    mova    1f,r0
    mov.l   .Lp2m,r1
    or      r1,r0
    jmp     @r0
    nop
    .align 2
1:  mov.l   r5,@r4
    mov.l   @r4,r0              ! read back: the write has completed
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    rts
    nop
    .align 5
! set_ccr_p2(r4 = CCR value, r5 = purge: 0 none, 1 every OC entry, 2 only entries 0-127 / 256-383)
! The purge writes 0 to the OC address array entry (non-associative): a dirty valid line is written back first.
_set_ccr_p2:
    mova    1f,r0
    mov.l   .Lp2m,r1
    or      r1,r0
    jmp     @r0
    nop
    .align 2
1:  tst     r5,r5
    bt      3f
    mov.l   .Loca,r1
    mov     #0,r3
    mov     #0,r6               ! entry number
    mov.w   .L512,r2
2:  mov     r5,r0
    cmp/eq  #2,r0
    bf      4f
    mov     r6,r0
    tst     #128,r0
    bf      5f                  ! RAM-mode entry: skip
4:  mov.l   r3,@r1
5:  add     #1,r6
    dt      r2
    bf/s    2b
    add     #32,r1
3:  mov.l   .Lccr,r1
    mov.l   r4,@r1
    mov.l   @r1,r0
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    rts
    nop
    .align 5
! ccr_time_p2(r4 = CCR value, r5 = purge first) -> r0 = PMCTR1 cycles across the CCR write
_ccr_time_p2:
    mova    1f,r0
    mov.l   .Lp2m,r1
    or      r1,r0
    jmp     @r0
    nop
    .align 2
1:  tst     r5,r5
    bt      3f
    mov.l   .Loca,r1
    mov     #0,r3
    mov.w   .L512,r2
2:  mov.l   r3,@r1
    dt      r2
    bf/s    2b
    add     #32,r1
3:  mov.l   .Lccr,r1
    mov.l   .Lpm1b,r2
    mov.l   @r2,r6
    mov.l   r4,@r1
    mov.l   @r2,r7
    mov     r7,r0
    sub     r6,r0
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    rts
    nop
    .align 5
! ic_probe_p2(r4 = function to call, r5 = array address 1, r6 = array address 2, r7 = out[2]): from P2, call the
! function (its fetch fills the instruction cache), then read the two array words
_ic_probe_p2:
    sts.l   pr,@-r15
    mova    1f,r0
    mov.l   .Lp2m,r1
    or      r1,r0
    jmp     @r0
    nop
    .align 2
1:  jsr     @r4
    nop
    mov.l   @r5,r0
    mov.l   r0,@(0,r7)
    mov.l   @r6,r0
    mov.l   r0,@(4,r7)
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    lds.l   @r15+,pr
    rts
    nop
    .align 5
! try_rd(r4 = address, r5 = marker) -> r0 = *(r4), or the marker if the load faulted (the handler skips it)
_try_rd:
    mov     r5,r0
    .globl _try_rd_insn
_try_rd_insn:
    mov.l   @r4,r0
    rts
    nop
    .align 2
.Lp2m:  .long 0xA0000000
.Loca:  .long 0xF4000000
.Lccr:  .long 0xFF00001C
.Lpm1b: .long 0xFF100010
.L512:  .word 512
