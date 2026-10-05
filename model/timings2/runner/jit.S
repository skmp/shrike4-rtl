! jit.S -- enter / leave a JIT-generated timing loop with a complete register image.
!
! void jit_enter(uint32_t *state)       state layout (bytes):
!     0..48 r0..r12   52 r13 (iterations)   56 entry address   60 SR
!     64 GBR  68 SSR  72 SPC  76 DBR  80 MACH  84 MACL  88 PR  92 FPUL  96 FPSCR
!     100..128 R0_BANK..R7_BANK   132..192 bank-0 FR0..FR15   196..256 bank-1 FR0..FR15
! The JIT code ends with  mov.l @(k,PC),r13 ; jmp @r13 ; nop  into jit_exit, which restores the C context saved here
! (everything the tested instructions can touch: r8-r15, PR, MACH, MACL, GBR, SSR, SPC, DBR, VBR, SR, FPSCR, FPUL,
! R0_BANK..R7_BANK, FR12-FR15) and returns to the caller of jit_enter.  The exception safety net in t2run.c sends
! a faulting case to jit_exit as well.

    .text
    .align 2
    .globl _jit_enter
    .globl _jit_exit
    .globl _jit_save

_jit_enter:
    mov.l   .Lsave1, r0
    mov.l   r8, @(0,r0)
    mov.l   r9, @(4,r0)
    mov.l   r10, @(8,r0)
    mov.l   r11, @(12,r0)
    mov.l   r12, @(16,r0)
    mov.l   r13, @(20,r0)
    mov.l   r14, @(24,r0)
    mov.l   r15, @(28,r0)
    sts     pr, r1
    mov.l   r1, @(32,r0)
    sts     mach, r1
    mov.l   r1, @(36,r0)
    sts     macl, r1
    mov.l   r1, @(40,r0)
    stc     gbr, r1
    mov.l   r1, @(44,r0)
    stc     ssr, r1
    mov.l   r1, @(48,r0)
    stc     spc, r1
    mov.l   r1, @(52,r0)
    stc     dbr, r1
    mov.l   r1, @(56,r0)
    stc     vbr, r1
    mov.l   r1, @(60,r0)
    add     #64, r0
    stc     sr, r1
    mov.l   r1, @(0,r0)         ! save+64  SR
    sts     fpscr, r1
    mov.l   r1, @(4,r0)         ! save+68  FPSCR
    sts     fpul, r1
    mov.l   r1, @(8,r0)         ! save+72  FPUL
    stc     r0_bank, r1
    mov.l   r1, @(12,r0)        ! save+76..104  R0_BANK..R7_BANK
    stc     r1_bank, r1
    mov.l   r1, @(16,r0)
    stc     r2_bank, r1
    mov.l   r1, @(20,r0)
    stc     r3_bank, r1
    mov.l   r1, @(24,r0)
    stc     r4_bank, r1
    mov.l   r1, @(28,r0)
    stc     r5_bank, r1
    mov.l   r1, @(32,r0)
    stc     r6_bank, r1
    mov.l   r1, @(36,r0)
    stc     r7_bank, r1
    mov.l   r1, @(40,r0)
    add     #44, r0             ! save+108..120  FR12..FR15 (C runs with SZ = 0, FR = 0)
    fmov.s  fr12, @r0
    add     #4, r0
    fmov.s  fr13, @r0
    add     #4, r0
    fmov.s  fr14, @r0
    add     #4, r0
    fmov.s  fr15, @r0

    ! ---- load the image
    mov.l   .Lfpscr_load, r1
    lds     r1, fpscr           ! SZ = 0, PR = 0, FR = 0 while the FP registers are loaded
    mov     r4, r1
    add     #127, r1
    add     #5, r1              ! r1 = state + 132
    fmov.s  @r1+, fr0
    fmov.s  @r1+, fr1
    fmov.s  @r1+, fr2
    fmov.s  @r1+, fr3
    fmov.s  @r1+, fr4
    fmov.s  @r1+, fr5
    fmov.s  @r1+, fr6
    fmov.s  @r1+, fr7
    fmov.s  @r1+, fr8
    fmov.s  @r1+, fr9
    fmov.s  @r1+, fr10
    fmov.s  @r1+, fr11
    fmov.s  @r1+, fr12
    fmov.s  @r1+, fr13
    fmov.s  @r1+, fr14
    fmov.s  @r1+, fr15
    frchg
    fmov.s  @r1+, fr0
    fmov.s  @r1+, fr1
    fmov.s  @r1+, fr2
    fmov.s  @r1+, fr3
    fmov.s  @r1+, fr4
    fmov.s  @r1+, fr5
    fmov.s  @r1+, fr6
    fmov.s  @r1+, fr7
    fmov.s  @r1+, fr8
    fmov.s  @r1+, fr9
    fmov.s  @r1+, fr10
    fmov.s  @r1+, fr11
    fmov.s  @r1+, fr12
    fmov.s  @r1+, fr13
    fmov.s  @r1+, fr14
    fmov.s  @r1+, fr15
    frchg
    mov     r4, r2
    add     #64, r2             ! r2 = state + 64
    mov.l   @(0,r2), r1
    ldc     r1, gbr
    mov.l   @(4,r2), r1
    ldc     r1, ssr
    mov.l   @(8,r2), r1
    ldc     r1, spc
    mov.l   @(12,r2), r1
    ldc     r1, dbr
    mov.l   @(16,r2), r1
    lds     r1, mach
    mov.l   @(20,r2), r1
    lds     r1, macl
    mov.l   @(24,r2), r1
    lds     r1, pr
    mov.l   @(28,r2), r1
    lds     r1, fpul
    mov.l   @(36,r2), r1        ! +100
    ldc     r1, r0_bank
    mov.l   @(40,r2), r1
    ldc     r1, r1_bank
    mov.l   @(44,r2), r1
    ldc     r1, r2_bank
    mov.l   @(48,r2), r1
    ldc     r1, r3_bank
    mov.l   @(52,r2), r1
    ldc     r1, r4_bank
    mov.l   @(56,r2), r1
    ldc     r1, r5_bank
    mov.l   @(60,r2), r1
    ldc     r1, r6_bank
    add     #64, r2             ! r2 = state + 128
    mov.l   @(0,r2), r1
    ldc     r1, r7_bank
    mov     r4, r2
    add     #96, r2
    mov.l   @(0,r2), r1         ! +96 FPSCR (the case's mode; no FP loads after this)
    lds     r1, fpscr
    mov.l   @(60,r4), r1        ! SR: IMASK 15, the case's T / S / Q / M
    ldc     r1, sr
    mov     r4, r14
    mov.l   @(0,r14), r0
    mov.l   @(4,r14), r1
    mov.l   @(8,r14), r2
    mov.l   @(12,r14), r3
    mov.l   @(16,r14), r4
    mov.l   @(20,r14), r5
    mov.l   @(24,r14), r6
    mov.l   @(28,r14), r7
    mov.l   @(32,r14), r8
    mov.l   @(36,r14), r9
    mov.l   @(40,r14), r10
    mov.l   @(44,r14), r11
    mov.l   @(48,r14), r12
    mov.l   @(52,r14), r13
    mov.l   @(56,r14), r14
    jmp     @r14
    mov     #0, r14

    .align 2
_jit_exit:
    mov.l   .Lsave2, r0
    mov     r0, r2
    add     #64, r2             ! r2 = save + 64
    mov.l   @(4,r2), r1
    lds     r1, fpscr
    mov.l   @(0,r2), r1
    ldc     r1, sr
    mov.l   @(8,r2), r1
    lds     r1, fpul
    mov.l   @(12,r2), r1
    ldc     r1, r0_bank
    mov.l   @(16,r2), r1
    ldc     r1, r1_bank
    mov.l   @(20,r2), r1
    ldc     r1, r2_bank
    mov.l   @(24,r2), r1
    ldc     r1, r3_bank
    mov.l   @(28,r2), r1
    ldc     r1, r4_bank
    mov.l   @(32,r2), r1
    ldc     r1, r5_bank
    mov.l   @(36,r2), r1
    ldc     r1, r6_bank
    mov.l   @(40,r2), r1
    ldc     r1, r7_bank
    mov     r2, r3
    add     #44, r3
    fmov.s  @r3+, fr12
    fmov.s  @r3+, fr13
    fmov.s  @r3+, fr14
    fmov.s  @r3+, fr15
    mov.l   @(32,r0), r1
    lds     r1, pr
    mov.l   @(36,r0), r1
    lds     r1, mach
    mov.l   @(40,r0), r1
    lds     r1, macl
    mov.l   @(44,r0), r1
    ldc     r1, gbr
    mov.l   @(48,r0), r1
    ldc     r1, ssr
    mov.l   @(52,r0), r1
    ldc     r1, spc
    mov.l   @(56,r0), r1
    ldc     r1, dbr
    mov.l   @(60,r0), r1
    ldc     r1, vbr
    mov.l   @(0,r0), r8
    mov.l   @(4,r0), r9
    mov.l   @(8,r0), r10
    mov.l   @(12,r0), r11
    mov.l   @(16,r0), r12
    mov.l   @(20,r0), r13
    mov.l   @(24,r0), r14
    mov.l   @(28,r0), r15
    rts
    mov     #0, r0

    .align 2
.Lsave1:
    .long   _jit_save
.Lsave2:
    .long   _jit_save
.Lfpscr_load:
    .long   0x00040000

    .data
    .align 5
_jit_save:
    .space  128
