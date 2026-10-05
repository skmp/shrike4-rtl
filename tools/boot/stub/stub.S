/* stub.S -- the direct-boot stub of the v1 SoC testbench (rtl/v1/SOC.md 7.3; rtl/v1/spec/board.md 5.2 / 5.3).
 *
 * The board maps this image at physical 0 (the boot-ROM area).  The chip leaves its power-on reset at 0xA0000000
 * (SR 0x700000F0: MD, RB, BL, IMASK 15; caches and MMU off) and fetches the stub uncached.  The stub
 *   1. writes the post-BIOS chip registers of board.md 5.3 over the real P4 bus, in SOC.md 7.3's order, from the
 *      table below (address, value, size): the BSC first (the chip does not touch SDRAM before these; the SDRAM
 *      power-on sequence of hm 13.3.5 without the dummy refreshes: MCR with MRSET = 0, an SDMR3 write = PALL, MCR with
 *      MRSET = 1, an SDMR3 write = MRS; M3-fix, M3-verify F6: the PALL, and both SDMR3 writes only after a power-on
 *      reset -- EXPEVT = 0; an entry whose size carries 0x100 is skipped otherwise -- since a manual or CPU-only reset
 *      keeps the BSC and the SDRAM's mode (SOC.md 2.3) and a DMAC transfer running across it may hold a row open), then the CPG,
 *      the DMAC (channels before DMAOR), the SCIF, the UBC, the PMC and the CCN;
 *   2. zeroes FR0-15 of both banks, MACH, MACL, FPUL;
 *   3. loads SSR, SPC, DBR, GBR, VBR, PR, R15, FPSCR from its parameter block, optionally PMCR2 and CCR (the sweep
 *      board: CCR = 0x105 must be written from P2, which this is, and 8 instructions must follow);
 *   4. writes SR (LDC: MD, RB 0, BL 0, IMASK 15 by default), clears R8-R13 and jumps to the entry, clearing R14 in
 *      the delay slot after JMP has read it.
 * It uses only R8-R15 (unbanked): R0-R7 of both banks stay 0 from reset (SOC.md 2.3).  SGR is not writable.
 *
 * Parameter block: found by the board's loader through its magic ('BSTB', version 1) and patched per start-state
 * preset (tb/dc_board/dc_loader.h StartState).  The defaults assembled here are the "testbench" column of
 * board.md 5.2.  Build: make (sh-elf binutils); the image is stub.bin.  Owner WI-BOARD.
 */
        .little
        .text
        .global _start
_start:
        mov.l   tab_p, r8               /* r8 = register table */
1:      mov.l   @r8+, r9                /* address; 0 ends the table */
        tst     r9, r9
        bt      2f
        mov.l   @r8+, r10               /* value */
        mov.l   @r8+, r11               /* size in bytes: 1, 2 or 4; + 0x100: power-on resets only */
        mov     #1, r12
        shll8   r12
        tst     r12, r11
        bt      5f
        mov.l   expevt_p, r13           /* EXPEVT: 0x000 after a power-on reset (pin, WDT, H-UDI), 0x020 / 0x140 */
        mov.l   @r13, r13               /* after a manual or CPU-only reset, which keep the BSC and the SDRAM's mode */
        tst     r13, r13                /* (SOC.md 2.3: the BSC registers held, refresh continues) */
        bf      1b                      /* not a power-on reset: the entry is skipped */
        extu.b  r11, r11
5:      mov     #1, r12
        cmp/eq  r12, r11
        bt      .Lb
        mov     #2, r12
        cmp/eq  r12, r11
        bt      .Lw
        bra     1b
        mov.l   r10, @r9
.Lb:    bra     1b
        mov.b   r10, @r9
.Lw:    bra     1b
        mov.w   r10, @r9

2:      /* FPSCR is 0x00040001 from reset: PR = 0, SZ = 0, FR = 0 */
        fldi0   fr0
        fldi0   fr1
        fldi0   fr2
        fldi0   fr3
        fldi0   fr4
        fldi0   fr5
        fldi0   fr6
        fldi0   fr7
        fldi0   fr8
        fldi0   fr9
        fldi0   fr10
        fldi0   fr11
        fldi0   fr12
        fldi0   fr13
        fldi0   fr14
        fldi0   fr15
        frchg
        fldi0   fr0
        fldi0   fr1
        fldi0   fr2
        fldi0   fr3
        fldi0   fr4
        fldi0   fr5
        fldi0   fr6
        fldi0   fr7
        fldi0   fr8
        fldi0   fr9
        fldi0   fr10
        fldi0   fr11
        fldi0   fr12
        fldi0   fr13
        fldi0   fr14
        fldi0   fr15
        frchg
        mov     #0, r9
        lds     r9, mach
        lds     r9, macl
        lds     r9, fpul

        mov.l   par_p, r8               /* r8 = parameter block */
        mov.l   @(0x24,r8), r9
        ldc     r9, ssr
        mov.l   @(0x28,r8), r9
        ldc     r9, spc
        mov.l   @(0x1C,r8), r9
        ldc     r9, dbr
        mov.l   @(0x20,r8), r9
        ldc     r9, gbr
        mov.l   @(0x18,r8), r9
        ldc     r9, vbr
        mov.l   @(0x14,r8), r9
        lds     r9, pr
        mov.l   @(0x10,r8), r15
        mov.l   @(0x2C,r8), r9
        lds     r9, fpscr
        mov.l   @(0x30,r8), r10         /* flags */
        mov     #2, r9
        tst     r9, r10                 /* bit 1: PMCR2 */
        bt      3f
        mov.l   pmcr2_p, r9
        mov.l   @(0x38,r8), r11
        mov.w   r11, @r9
3:      mov     #1, r9
        tst     r9, r10                 /* bit 0: CCR, from P2, then 8 instructions */
        bt      4f
        mov.l   ccr_p, r9
        mov.l   @(0x34,r8), r11
        mov.l   r11, @r9
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
4:      mov.l   @(0x0C,r8), r9          /* SR */
        mov.l   @(0x08,r8), r14         /* entry */
        ldc     r9, sr
        mov     #0, r8
        mov     #0, r9
        mov     #0, r10
        mov     #0, r11
        mov     #0, r12
        mov     #0, r13
        jmp     @r14
        mov     #0, r14

        .align  2
tab_p:  .long   table
par_p:  .long   params
pmcr2_p: .long  0xFF000088
ccr_p:  .long   0xFF00001C
expevt_p: .long 0xFF000024

        /* the chip registers, SOC.md 7.3 verbatim: address, value, size */
        .align  2
table:
        .long   0xFF800000, 0xA3020008, 4       /* BCR1 */
        .long   0xFF800004, 0x00000001, 2       /* BCR2 */
        .long   0xFF800008, 0x01110111, 4       /* WCR1 */
        .long   0xFF80000C, 0x618066D8, 4       /* WCR2 */
        .long   0xFF800010, 0x07777777, 4       /* WCR3 */
        .long   0xFF800014, 0x800A0E24, 4       /* MCR, MRSET = 0 (the power-on sequence's PALL first) */
        .long   0xFF800018, 0x00000000, 2       /* PCR */
        .long   0xFF800024, 0x0000A55E, 2       /* RTCOR (key A5) */
        .long   0xFF80001C, 0x0000A510, 2       /* RTCSR (key A5; CKS = CKIO/16) */
        .long   0xFF940190, 0x00000000, 0x101   /* SDMR3 with MRSET = 0: PALL (hm 13.3.5 Power-On Sequence), power-on
                                                   resets only */
        .long   0xFF800014, 0xC00A0E24, 4       /* MCR, MRSET = 1 */
        .long   0xFF940190, 0x00000000, 0x101   /* SDMR3: MRS, CL3 / BL4 (address-encoded; data ignored), power-on
                                                   resets only: after a manual or CPU-only reset the SDRAM keeps its
                                                   mode and refresh, and a DMAC transfer running across the reset may
                                                   hold a row open, which an MRS must not meet (M3-verify F6) */
        .long   0xFF80002C, 0x000A03F0, 4       /* PCTRA */
        .long   0xFF800030, 0x00000004, 2       /* PDTRA */
        .long   0xFF800040, 0x00000000, 4       /* PCTRB */
        .long   0xFF800048, 0x00000000, 2       /* GPIOIC */
        .long   0xFFC00004, 0x00000003, 1       /* STBCR: SCI, RTC module-stopped */
        .long   0xFFA0000C, 0x000853C3, 4       /* CHCR0 (TE written as 1 is ignored) */
        .long   0xFFA0002C, 0x000012C0, 4       /* CHCR2 */
        .long   0xFFA00040, 0x00008201, 4       /* DMAOR: DDT, DME */
        .long   0xFFE80008, 0x00000000, 2       /* SCSCR2 = 0 */
        .long   0xFFE80018, 0x00000006, 2       /* SCFCR2: FIFO reset */
        .long   0xFFE80000, 0x00000000, 2       /* SCSMR2 */
        .long   0xFFE80004, 0x0000000C, 1       /* SCBRR2 */
        .long   0xFFE80018, 0x00000040, 2       /* SCFCR2 */
        .long   0xFFE80020, 0x00000011, 2       /* SCSPTR2 */
        .long   0xFFE80008, 0x00000030, 2       /* SCSCR2: TE, RE */
        .long   0xFF200020, 0x00000000, 2       /* BRCR */
        .long   0xFF000084, 0x0000C023, 2       /* PMCR1 */
        .long   0xFF000038, 0x00000004, 4       /* QACR0 */
        .long   0xFF00003C, 0x00000004, 4       /* QACR1 */
        .long   0

        /* parameter block (tb/dc_board/dc_loader.h): magic, version, PC, SR, R15, PR, VBR, DBR, GBR, SSR, SPC,
           FPSCR, flags, CCR, PMCR2, reserved */
        .align  2
params:
        .long   0x42535442, 1
        .long   0x8C010000, 0x400000F0, 0x8C00F400, 0x8C00F800, 0x8C000000, 0x8C000010, 0x8C000000
        .long   0x40000001, 0x8C000776, 0x00040001
        .long   0, 0, 0, 0
