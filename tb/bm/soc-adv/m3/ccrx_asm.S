/* ccrx_asm.S -- ccrx.c's sequences (tb/bm/soc-adv/m3, from agents/verify/M3/adv/src).  Every routine enters through its P1 name and runs its
 * body from the P2 alias (P2ENTRY): CCR and the instruction-cache arrays are written by a program in P2 (hm 4.2.1,
 * 4.5), and the body's timing does not depend on the I-cache.  k-cycle gaps are DT loops (r6 = k; 0: none).
 * Every routine that ends in an OCI first writes every dirty line back (FLUSH: 16 KB of dummy reads at 0x8C500000,
 * every OC index), so the OCI loses nothing of the C program (its stack, its data): only the sequence's own lines.
 *   x_pref_oci(line, ccr, k, aa)   PREF @line ; k ; CCR <- ccr ; *aa <- OCAA(line's entry)  (a PREF fill in flight
 *                                  at the OCI)
 *   x_ld_oci(line, ccr, k, aa)     MOV.L @line ; k ; CCR <- ccr ; *aa <- OCAA  (a load's fill: its tail in flight)
 *   x_st_oci(line, ccr, k, v)      MOV.L v,@line ; k ; CCR <- ccr    (a store miss's allocation in flight)
 *   x_evict_oci(x, ccr, y, v)      MOV.L v,@x ; MOV.L @y ; CCR <- ccr (x's write-back in the WBB at the OCI)
 *   x_ocbp_oci(line, ccr)          OCBP @line ; CCR <- ccr
 *   x_pref_sq_oci(sqa, ccr)        PREF @sqa (a store-queue burst) ; CCR <- ccr
 *   x_icseq / x_icaa_seq / x_ice_seq   the IC sequences (below)
 *   x_st32(a, v) / x_ld32(a)       one longword store / load from P2 (the arrays)
 *   x_flush_ccr(dummy_p1, ccr)     read one longword per line of 16 KB at dummy_p1, then CCR <- ccr
 *   r_one / r_two                  P1 routines, each alone in its own 32-byte line: MOV #1,R0 ; RTS ; NOP (patched) */
        .macro P2ENTRY name
        .text
        .balign 32
        .global _\name
_\name:
        mov.l   1f,r0
        jmp     @r0
        nop
        .balign 4
1:      .long   2f + 0x20000000
2:
        .endm
        .macro KDELAY
        tst     r6,r6
        bt      3f
4:      dt      r6
        bf      4b
3:
        .endm
        .macro CCRW reg
        mov.l   5f,r1
        mov.l   \reg,@r1
        mov.l   @r1,r0
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        .endm
        .macro FLUSH
        mov.l   8f,r3               /* 16 KB of dummy lines: every OC index (OIX = 0) -- every dirty line written back */
        mov.w   9f,r1
10:     mov.l   @r3,r0
        dt      r1
        bf/s    10b
        add     #32,r3
        .endm
        .macro FLUSHLIT
        .balign 4
8:      .long   0x8C500000
9:      .word   512
        .endm
        .macro CCRLIT
        .balign 4
5:      .long   0xFF00001C
        .endm

        P2ENTRY x_pref_oci
        FLUSH
        pref    @r4
        KDELAY
        CCRW    r5
        mov.l   11f,r0              /* OCAA(entry of r4) -> @r7 */
        and     r4,r0
        mov.l   12f,r1
        or      r1,r0
        mov.l   @r0,r0
        rts
        mov.l   r0,@r7
        .balign 4
11:     .long   0x00003FE0
12:     .long   0xF4000000
        CCRLIT
        FLUSHLIT

        P2ENTRY x_ld_oci
        FLUSH
        mov.l   @r4,r2
        KDELAY
        CCRW    r5
        mov.l   11f,r0              /* OCAA(entry of r4) -> @r7 */
        and     r4,r0
        mov.l   12f,r1
        or      r1,r0
        mov.l   @r0,r0
        mov.l   r0,@r7
        rts
        mov     r2,r0
        .balign 4
11:     .long   0x00003FE0
12:     .long   0xF4000000
        CCRLIT
        FLUSHLIT

        P2ENTRY x_st_oci
        FLUSH
        mov.l   r7,@r4
        KDELAY
        CCRW    r5
        rts
        nop
        CCRLIT
        FLUSHLIT

        P2ENTRY x_evict_oci
        FLUSH
        mov.l   r7,@r4
        mov.l   @r6,r2
        CCRW    r5
        rts
        mov     r2,r0
        CCRLIT
        FLUSHLIT

        P2ENTRY x_ocbp_oci
        FLUSH
        ocbp    @r4
        CCRW    r5
        rts
        nop
        CCRLIT
        FLUSHLIT

        P2ENTRY x_pref_sq_oci
        FLUSH
        pref    @r4
        CCRW    r5
        rts
        nop
        CCRLIT
        FLUSHLIT

/* the IC sequences, all in P2 so that the only cacheable fetches are the calls of fn (no other code can take fn's IC
 * entry between the steps).  res (a P2 pointer) receives the words listed; PATCHn writes MOV #n,R0 ; RTS over fn's
 * first longword through P2; ICI writes CCR <- KOS | ICI.
 *   x_icseq(fn, res, k):  PATCH1 ICI res0 = fn() (1) ; PATCH2 res1 = fn() (1: stale) ; ICI res2 = fn() (2) ;
 *                         PATCH1 ICI ; res3 = fn() ; k ; ICI (fn's fill in flight) ; res4 = ICAA(e) ; PATCH2 ;
 *                         res5 = fn() (2)
 *   x_icaa_seq(fn, res, k): PATCH1 ICI ; res0 = fn() ; k ; ICAA(e) | A <- fn & ~0x3FF (V = 0: an associative purge
 *                         while the fill runs) ; res1 = ICAA(e) ; PATCH2 ; res2 = fn() (2)
 *   x_ice_seq(fn, res):   PATCH1 ICI res0 = fn() (1) ; CCR <- KOS & ~ICE ; PATCH3 ; res1 = fn() (3: uncached) ;
 *                         CCR <- KOS ; res2 = fn() (1: the line is still valid) ; ICI ; res3 = fn() (3) */
        .macro PATCH n
        mov.l   20f,r1
        or      r8,r1               /* 0x000BE000 | n */
        mov     #\n,r0
        or      r0,r1
        mov.l   r1,@r9
        .endm
        .macro CALLFN
        jsr     @r10
        nop
        .endm
        .macro CCRV lit
        mov.l   \lit,r0
        mov.l   5f,r1
        mov.l   r0,@r1
        mov.l   @r1,r0
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        .endm
        .macro ICSEQLIT
        .balign 4
5:      .long   0xFF00001C
20:     .long   0x000BE000
21:     .long   0x00000905          /* KOS | ICI */
22:     .long   0x00000005          /* KOS & ~ICE */
23:     .long   0x00000105          /* KOS */
24:     .long   0x1FFFFFFF
25:     .long   0xA0000000
26:     .long   0xF0000000
27:     .long   0x00001FE0
        .endm
        .macro ICSEQPRO
        mov.l   r8,@-r15
        mov.l   r9,@-r15
        mov.l   r10,@-r15
        mov.l   r11,@-r15
        sts.l   pr,@-r15
        mov     #0,r8
        mov     r4,r10              /* fn (P1) */
        mov.l   24f,r0
        and     r4,r0
        mov.l   25f,r1
        or      r0,r1
        mov     r1,r9               /* fn's P2 alias: the patch address */
        mov     r5,r11              /* res (P2) */
        .endm
        .macro ICSEQEPI
        lds.l   @r15+,pr
        mov.l   @r15+,r11
        mov.l   @r15+,r10
        mov.l   @r15+,r9
        rts
        mov.l   @r15+,r8
        .endm

        P2ENTRY x_icseq
        ICSEQPRO
        PATCH   1
        CCRV    21f
        CALLFN
        mov.l   r0,@(0,r11)
        PATCH   2
        CALLFN
        mov.l   r0,@(4,r11)
        CCRV    21f
        CALLFN
        mov.l   r0,@(8,r11)
        PATCH   1
        CCRV    21f
        CALLFN
        mov     r0,r2
        KDELAY
        CCRV    21f
        mov.l   r2,@(12,r11)
        mov.l   27f,r0
        and     r10,r0
        mov.l   26f,r1
        or      r1,r0
        mov.l   @r0,r0              /* ICAA(e) */
        mov.l   r0,@(16,r11)
        PATCH   2
        CALLFN
        mov.l   r0,@(20,r11)
        ICSEQEPI
        ICSEQLIT

        P2ENTRY x_icaa_seq
        ICSEQPRO
        PATCH   1
        CCRV    21f
        mov.l   27f,r2
        and     r10,r2
        mov.l   26f,r1
        or      r1,r2               /* ICAA(e) */
        mov     r10,r3
        mov     #-4,r0
        shll8   r0                  /* 0xFFFFFC00 */
        and     r0,r3               /* the tag, V = 0 */
        CALLFN
        mov     r0,r7
        KDELAY
        mov     r2,r0
        or      #8,r0               /* A = 1 */
        mov.l   r3,@r0
        mov.l   r7,@(0,r11)
        mov.l   @r2,r0
        mov.l   r0,@(4,r11)
        PATCH   2
        CALLFN
        mov.l   r0,@(8,r11)
        ICSEQEPI
        ICSEQLIT

        P2ENTRY x_ice_seq
        ICSEQPRO
        PATCH   1
        CCRV    21f
        CALLFN
        mov.l   r0,@(0,r11)
        CCRV    22f
        PATCH   3
        CALLFN
        mov.l   r0,@(4,r11)
        CCRV    23f
        CALLFN
        mov.l   r0,@(8,r11)
        CCRV    21f
        CALLFN
        mov.l   r0,@(12,r11)
        ICSEQEPI
        ICSEQLIT

/* x_oce_seq(x, res, v1, v2): all in P2, no other data access: MOV.L v1,@x (dirty) ; CCR <- KOS & ~OCE ;
 * res0 = @x (uncached: memory) ; MOV.L v2,@x (uncached: memory) ; res1 = @x ; CCR <- KOS ; res2 = @x (the dirty
 * line again) ; OCBP @x ; res3 = @(x | P2) */
        P2ENTRY x_oce_seq
        mov.l   r6,@r4
        mov.l   30f,r0
        mov.l   5f,r1
        mov.l   r0,@r1
        mov.l   @r1,r0
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        mov.l   @r4,r0
        mov.l   r0,@(0,r5)
        mov.l   r7,@r4
        mov.l   @r4,r0
        mov.l   r0,@(4,r5)
        mov.l   31f,r0
        mov.l   5f,r1
        mov.l   r0,@r1
        mov.l   @r1,r0
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        mov.l   @r4,r0
        mov.l   r0,@(8,r5)
        ocbp    @r4
        mov.l   32f,r0
        and     r4,r0
        mov.l   33f,r1
        or      r1,r0
        mov.l   @r0,r0
        rts
        mov.l   r0,@(12,r5)
        .balign 4
5:      .long   0xFF00001C
30:     .long   0x00000104          /* KOS & ~OCE */
31:     .long   0x00000105          /* KOS */
32:     .long   0x1FFFFFFF
33:     .long   0xA0000000

        P2ENTRY x_st32
        rts
        mov.l   r5,@r4

        P2ENTRY x_ld32
        rts
        mov.l   @r4,r0

        P2ENTRY x_flush_ccr
        mov.w   6f,r1
7:      mov.l   @r4,r0
        dt      r1
        bf/s    7b
        add     #32,r4
        CCRW    r5
        rts
        nop
        .balign 4
6:      .word   512
        CCRLIT

/* the patchable P1 routines: each alone in a 32-byte line, 1 KB apart (distinct IC entries) */
        .section .text.rtn,"ax"
        .balign 1024
        .global _r_one, _r_two
_r_one:
        mov     #1,r0
        rts
        nop
        .balign 1024
_r_two:
        mov     #1,r0
        rts
        nop
        .balign 32

/* ---- the same sequences from P1 (cached; each called once dry to warm its lines): from P2 every instruction fetch is
 * an uncached bus read that queues behind the fill, so the CCR write of a P2 routine lands only after the fill has
 * completed.  From P1 the CCR store follows the access by 2 + k cycles (a NOP sled entered k from its end), with the fill
 * still in flight.  (hm 4.2.1 asks for CCR writes from P2: these violate it on purpose, to reach v1_ocache / v1_icache's
 * kill of a fill in flight; Sh4Sys applies the flash clear in program order either way.)
 *   x1_pref_oci(line, ccr, k, aa)   FLUSH ; PREF @line ; sled ; CCR <- ccr ; *aa <- OCAA(line's entry)
 *   x1_ld_oci(line, ccr, k, aa)     FLUSH ; MOV.L @line ; sled ; CCR <- ccr ; *aa <- OCAA ; returns the load
 *   x1_st_oci(line, ccr, k, v)      FLUSH ; MOV.L v,@line ; sled ; CCR <- ccr
 *   x1_call_ici(fn, ccr, k)         JSR @fn ; sled ; CCR <- ccr ; returns fn's r0 */
        .macro SLED
        mova    30f,r0
        shll    r6
        sub     r6,r0
        jmp     @r0
        nop
        .rept   24
        nop
        .endr
        .balign 4
30:
        .endm
        .section .text.p1seq,"ax"
        .balign 32
        .global _x1_pref_oci, _x1_ld_oci, _x1_st_oci, _x1_call_ici
_x1_pref_oci:
        FLUSH
        mov.l   31f,r1
        mov.l   32f,r2
        pref    @r4
        SLED
        mov.l   r5,@r1
        mov.l   @r1,r0
        nop
        nop
        nop
        nop
        mov.l   33f,r0              /* OCAA(entry of r4) -> @r7 */
        and     r4,r0
        or      r2,r0
        mov.l   @r0,r0
        rts
        mov.l   r0,@r7
        .balign 4
31:     .long   0xFF00001C
32:     .long   0xF4000000
33:     .long   0x00003FE0
        FLUSHLIT
        .balign 32
_x1_ld_oci:
        FLUSH
        mov.l   31f,r1
        mov.l   @r4,r3
        SLED
        mov.l   r5,@r1
        mov.l   @r1,r0
        nop
        nop
        nop
        nop
        mov.l   33f,r0
        and     r4,r0
        mov.l   32f,r2
        or      r2,r0
        mov.l   @r0,r0
        mov.l   r0,@r7
        rts
        mov     r3,r0
        .balign 4
31:     .long   0xFF00001C
32:     .long   0xF4000000
33:     .long   0x00003FE0
        FLUSHLIT
        .balign 32
_x1_st_oci:
        FLUSH
        mov.l   31f,r1
        mov.l   r7,@r4
        SLED
        mov.l   r5,@r1
        mov.l   @r1,r0
        nop
        nop
        nop
        nop
        rts
        nop
        .balign 4
31:     .long   0xFF00001C
        FLUSHLIT
        .balign 32
_x1_call_ici:
        sts.l   pr,@-r15
        mov.l   31f,r1
        mov.l   r1,@-r15
        jsr     @r4
        nop
        mov     r0,r3
        mov.l   @r15+,r1
        SLED
        mov.l   r5,@r1
        mov.l   @r1,r0
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
        mov     r3,r0
        .balign 4
31:     .long   0xFF00001C
