"""SH-4 (SH7091) instruction database + functional model for the pairwise pipeline-timing sweep.

Every instruction form of the programming manual's instruction table (section 9) is here, plus FSCA / FSRRA, with
  - its encoding pattern (letters n m i d are fields),
  - its operands and their roles (read / write / read-write), incl. implicit ones (R0, FR0, T, FPUL, MAC, PR, ...),
  - its memory access (addressing mode, size, kind),
  - the FPU modes it exists in (PR, SZ),
  - branch behaviour,
  - value semantics (enough to know every value that can reach an address, a branch target, SR / FPSCR / VBR,
    or the T bit a conditional branch reads; FP arithmetic results are 'unknown', which is fine because no FP
    result can reach such a slot inside one pair).
Nothing here says anything about timing: issue groups, latencies and resource locks are what the sweep measures.

Excluded forms (not measurable without exceptions / halting): TRAPA (always traps), SLEEP (halts until an
interrupt, and interrupts are masked while timing).
"""
import re

M32 = 0xFFFFFFFF


def sx8(v):
    v &= 0xFF
    return v - 0x100 if v & 0x80 else v


def sx16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def s32(v):
    v &= M32
    return v - (1 << 32) if v & 0x80000000 else v


# ---------------------------------------------------------------------------------------------------------------
# FPU modes.  M0 = PR 0 SZ 0, M1 = PR 0 SZ 1, M2 = PR 1 SZ 0.  (PR 1 SZ 1 is reserved.)
MODES = {'M0': (0, 0), 'M1': (0, 1), 'M2': (1, 0)}
MODE_OF = {v: k for k, v in MODES.items()}

# ---------------------------------------------------------------------------------------------------------------
# Operand tokens (space separated in the table):
#   prefix ''  read, '=' write, '+' read and write
#   GPR:   Rn Rm R0                    FP: FRn FRm FR0 DRn DRm XDn XDm FVn FVm XMTRX FPUL
#   system: T S QM SR GBR VBR SSR SPC SGR DBR BANKn BANKm MACH MACL PR TLB PTE FPSCR FRB (FPSCR.FR) SZB (FPSCR.SZ)
#   memory: M:<mode>:<base>:<size>:<kind>
#           mode  ind @Rx | post @Rx+ | pre @-Rx | disp @(disp,Rx) | r0 @(R0,Rx) | gbr @(disp,GBR) | gbr0 @(R0,GBR)
#                 | pc @(disp,PC)
#           kind  L load | S store | M read-modify-write | T TAS.B | P PREF | I OCBI | B OCBP | W OCBWB | A MOVCA.L
# Flags: 'hard:<slot>=<what>'  the value in that slot must be <what> (sr, vbr, fpscr, zero, target)
#        'br:<kind>'  cond_t / cond_f (taken when T=1 / T=0), uncond, braf, bsrf, jmp, jsr, rts, rte  (+ delayed flag)
FORMS_SRC = r"""
# name                     | enc               | modes | ops                                  | flags
MOV #imm,Rn                | 1110nnnniiiiiiii  | *   | =Rn                                    |
MOV.W @(disp,PC),Rn        | 1001nnnndddddddd  | *   | =Rn M:pc:PC:2:L                        | pcrel
MOV.L @(disp,PC),Rn        | 1101nnnndddddddd  | *   | =Rn M:pc:PC:4:L                        | pcrel
MOV Rm,Rn                  | 0110nnnnmmmm0011  | *   | Rm =Rn                                 |
MOV.B Rm,@Rn               | 0010nnnnmmmm0000  | *   | Rm M:ind:Rn:1:S                        |
MOV.W Rm,@Rn               | 0010nnnnmmmm0001  | *   | Rm M:ind:Rn:2:S                        |
MOV.L Rm,@Rn               | 0010nnnnmmmm0010  | *   | Rm M:ind:Rn:4:S                        |
MOV.B @Rm,Rn               | 0110nnnnmmmm0000  | *   | =Rn M:ind:Rm:1:L                       |
MOV.W @Rm,Rn               | 0110nnnnmmmm0001  | *   | =Rn M:ind:Rm:2:L                       |
MOV.L @Rm,Rn               | 0110nnnnmmmm0010  | *   | =Rn M:ind:Rm:4:L                       |
MOV.B Rm,@-Rn              | 0010nnnnmmmm0100  | *   | Rm M:pre:Rn:1:S                        |
MOV.W Rm,@-Rn              | 0010nnnnmmmm0101  | *   | Rm M:pre:Rn:2:S                        |
MOV.L Rm,@-Rn              | 0010nnnnmmmm0110  | *   | Rm M:pre:Rn:4:S                        |
MOV.B @Rm+,Rn              | 0110nnnnmmmm0100  | *   | =Rn M:post:Rm:1:L                      |
MOV.W @Rm+,Rn              | 0110nnnnmmmm0101  | *   | =Rn M:post:Rm:2:L                      |
MOV.L @Rm+,Rn              | 0110nnnnmmmm0110  | *   | =Rn M:post:Rm:4:L                      |
MOV.B R0,@(disp,Rn)        | 10000000nnnndddd  | *   | R0 M:disp:Rn:1:S                       |
MOV.W R0,@(disp,Rn)        | 10000001nnnndddd  | *   | R0 M:disp:Rn:2:S                       |
MOV.L Rm,@(disp,Rn)        | 0001nnnnmmmmdddd  | *   | Rm M:disp:Rn:4:S                       |
MOV.B @(disp,Rm),R0        | 10000100mmmmdddd  | *   | =R0 M:disp:Rm:1:L                      |
MOV.W @(disp,Rm),R0        | 10000101mmmmdddd  | *   | =R0 M:disp:Rm:2:L                      |
MOV.L @(disp,Rm),Rn        | 0101nnnnmmmmdddd  | *   | =Rn M:disp:Rm:4:L                      |
MOV.B Rm,@(R0,Rn)          | 0000nnnnmmmm0100  | *   | Rm M:r0:Rn:1:S                         |
MOV.W Rm,@(R0,Rn)          | 0000nnnnmmmm0101  | *   | Rm M:r0:Rn:2:S                         |
MOV.L Rm,@(R0,Rn)          | 0000nnnnmmmm0110  | *   | Rm M:r0:Rn:4:S                         |
MOV.B @(R0,Rm),Rn          | 0000nnnnmmmm1100  | *   | =Rn M:r0:Rm:1:L                        |
MOV.W @(R0,Rm),Rn          | 0000nnnnmmmm1101  | *   | =Rn M:r0:Rm:2:L                        |
MOV.L @(R0,Rm),Rn          | 0000nnnnmmmm1110  | *   | =Rn M:r0:Rm:4:L                        |
MOV.B R0,@(disp,GBR)       | 11000000dddddddd  | *   | R0 M:gbr:GBR:1:S                       |
MOV.W R0,@(disp,GBR)       | 11000001dddddddd  | *   | R0 M:gbr:GBR:2:S                       |
MOV.L R0,@(disp,GBR)       | 11000010dddddddd  | *   | R0 M:gbr:GBR:4:S                       |
MOV.B @(disp,GBR),R0       | 11000100dddddddd  | *   | =R0 M:gbr:GBR:1:L                      |
MOV.W @(disp,GBR),R0       | 11000101dddddddd  | *   | =R0 M:gbr:GBR:2:L                      |
MOV.L @(disp,GBR),R0       | 11000110dddddddd  | *   | =R0 M:gbr:GBR:4:L                      |
MOVA @(disp,PC),R0         | 11000111dddddddd  | *   | =R0                                    | pcrel
MOVT Rn                    | 0000nnnn00101001  | *   | T =Rn                                  |
SWAP.B Rm,Rn               | 0110nnnnmmmm1000  | *   | Rm =Rn                                 |
SWAP.W Rm,Rn               | 0110nnnnmmmm1001  | *   | Rm =Rn                                 |
XTRCT Rm,Rn                | 0010nnnnmmmm1101  | *   | Rm +Rn                                 |
ADD Rm,Rn                  | 0011nnnnmmmm1100  | *   | Rm +Rn                                 |
ADD #imm,Rn                | 0111nnnniiiiiiii  | *   | +Rn                                    |
ADDC Rm,Rn                 | 0011nnnnmmmm1110  | *   | Rm +Rn +T                              |
ADDV Rm,Rn                 | 0011nnnnmmmm1111  | *   | Rm +Rn =T                              |
CMP/EQ #imm,R0             | 10001000iiiiiiii  | *   | R0 =T                                  |
CMP/EQ Rm,Rn               | 0011nnnnmmmm0000  | *   | Rm Rn =T                               |
CMP/HS Rm,Rn               | 0011nnnnmmmm0010  | *   | Rm Rn =T                               |
CMP/GE Rm,Rn               | 0011nnnnmmmm0011  | *   | Rm Rn =T                               |
CMP/HI Rm,Rn               | 0011nnnnmmmm0110  | *   | Rm Rn =T                               |
CMP/GT Rm,Rn               | 0011nnnnmmmm0111  | *   | Rm Rn =T                               |
CMP/PZ Rn                  | 0100nnnn00010001  | *   | Rn =T                                  |
CMP/PL Rn                  | 0100nnnn00010101  | *   | Rn =T                                  |
CMP/STR Rm,Rn              | 0010nnnnmmmm1100  | *   | Rm Rn =T                               |
DIV1 Rm,Rn                 | 0011nnnnmmmm0100  | *   | Rm +Rn +T +QM                          |
DIV0S Rm,Rn                | 0010nnnnmmmm0111  | *   | Rm Rn =T =QM                           |
DIV0U                      | 0000000000011001  | *   | =T =QM                                 |
DMULS.L Rm,Rn              | 0011nnnnmmmm1101  | *   | Rm Rn =MACH =MACL                      |
DMULU.L Rm,Rn              | 0011nnnnmmmm0101  | *   | Rm Rn =MACH =MACL                      |
DT Rn                      | 0100nnnn00010000  | *   | +Rn =T                                 |
EXTS.B Rm,Rn               | 0110nnnnmmmm1110  | *   | Rm =Rn                                 |
EXTS.W Rm,Rn               | 0110nnnnmmmm1111  | *   | Rm =Rn                                 |
EXTU.B Rm,Rn               | 0110nnnnmmmm1100  | *   | Rm =Rn                                 |
EXTU.W Rm,Rn               | 0110nnnnmmmm1101  | *   | Rm =Rn                                 |
MAC.L @Rm+,@Rn+            | 0000nnnnmmmm1111  | *   | M:post:Rm:4:L M:post:Rn:4:L +MACH +MACL S |
MAC.W @Rm+,@Rn+            | 0100nnnnmmmm1111  | *   | M:post:Rm:2:L M:post:Rn:2:L +MACH +MACL S |
MUL.L Rm,Rn                | 0000nnnnmmmm0111  | *   | Rm Rn =MACL                            |
MULS.W Rm,Rn               | 0010nnnnmmmm1111  | *   | Rm Rn =MACL                            |
MULU.W Rm,Rn               | 0010nnnnmmmm1110  | *   | Rm Rn =MACL                            |
NEG Rm,Rn                  | 0110nnnnmmmm1011  | *   | Rm =Rn                                 |
NEGC Rm,Rn                 | 0110nnnnmmmm1010  | *   | Rm =Rn +T                              |
SUB Rm,Rn                  | 0011nnnnmmmm1000  | *   | Rm +Rn                                 |
SUBC Rm,Rn                 | 0011nnnnmmmm1010  | *   | Rm +Rn +T                              |
SUBV Rm,Rn                 | 0011nnnnmmmm1011  | *   | Rm +Rn =T                              |
AND Rm,Rn                  | 0010nnnnmmmm1001  | *   | Rm +Rn                                 |
AND #imm,R0                | 11001001iiiiiiii  | *   | +R0                                    |
AND.B #imm,@(R0,GBR)       | 11001101iiiiiiii  | *   | M:gbr0:GBR:1:M                         |
NOT Rm,Rn                  | 0110nnnnmmmm0111  | *   | Rm =Rn                                 |
OR Rm,Rn                   | 0010nnnnmmmm1011  | *   | Rm +Rn                                 |
OR #imm,R0                 | 11001011iiiiiiii  | *   | +R0                                    |
OR.B #imm,@(R0,GBR)        | 11001111iiiiiiii  | *   | M:gbr0:GBR:1:M                         |
TAS.B @Rn                  | 0100nnnn00011011  | *   | M:ind:Rn:1:T =T                        |
TST Rm,Rn                  | 0010nnnnmmmm1000  | *   | Rm Rn =T                               |
TST #imm,R0                | 11001000iiiiiiii  | *   | R0 =T                                  |
TST.B #imm,@(R0,GBR)       | 11001100iiiiiiii  | *   | M:gbr0:GBR:1:L =T                      |
XOR Rm,Rn                  | 0010nnnnmmmm1010  | *   | Rm +Rn                                 |
XOR #imm,R0                | 11001010iiiiiiii  | *   | +R0                                    |
XOR.B #imm,@(R0,GBR)       | 11001110iiiiiiii  | *   | M:gbr0:GBR:1:M                         |
ROTL Rn                    | 0100nnnn00000100  | *   | +Rn =T                                 |
ROTR Rn                    | 0100nnnn00000101  | *   | +Rn =T                                 |
ROTCL Rn                   | 0100nnnn00100100  | *   | +Rn +T                                 |
ROTCR Rn                   | 0100nnnn00100101  | *   | +Rn +T                                 |
SHAD Rm,Rn                 | 0100nnnnmmmm1100  | *   | Rm +Rn                                 |
SHAL Rn                    | 0100nnnn00100000  | *   | +Rn =T                                 |
SHAR Rn                    | 0100nnnn00100001  | *   | +Rn =T                                 |
SHLD Rm,Rn                 | 0100nnnnmmmm1101  | *   | Rm +Rn                                 |
SHLL Rn                    | 0100nnnn00000000  | *   | +Rn =T                                 |
SHLR Rn                    | 0100nnnn00000001  | *   | +Rn =T                                 |
SHLL2 Rn                   | 0100nnnn00001000  | *   | +Rn                                    |
SHLR2 Rn                   | 0100nnnn00001001  | *   | +Rn                                    |
SHLL8 Rn                   | 0100nnnn00011000  | *   | +Rn                                    |
SHLR8 Rn                   | 0100nnnn00011001  | *   | +Rn                                    |
SHLL16 Rn                  | 0100nnnn00101000  | *   | +Rn                                    |
SHLR16 Rn                  | 0100nnnn00101001  | *   | +Rn                                    |
BF (taken)                 | 10001011dddddddd  | *   | T                                      | br:cond_f want_t=0
BF (not taken)             | 10001011dddddddd  | *   | T                                      | br:cond_f want_t=1
BF/S (taken)               | 10001111dddddddd  | *   | T                                      | br:cond_f delayed want_t=0
BF/S (not taken)           | 10001111dddddddd  | *   | T                                      | br:cond_f delayed want_t=1
BT (taken)                 | 10001001dddddddd  | *   | T                                      | br:cond_t want_t=1
BT (not taken)             | 10001001dddddddd  | *   | T                                      | br:cond_t want_t=0
BT/S (taken)               | 10001101dddddddd  | *   | T                                      | br:cond_t delayed want_t=1
BT/S (not taken)           | 10001101dddddddd  | *   | T                                      | br:cond_t delayed want_t=0
BRA label                  | 1010dddddddddddd  | *   |                                        | br:uncond delayed
BRAF Rn                    | 0000nnnn00100011  | *   | Rn                                     | br:braf delayed hard:Rn=zero
BSR label                  | 1011dddddddddddd  | *   | =PR                                    | br:bsr delayed
BSRF Rn                    | 0000nnnn00000011  | *   | Rn =PR                                 | br:bsrf delayed hard:Rn=zero
JMP @Rn                    | 0100nnnn00101011  | *   | Rn                                     | br:jmp delayed hard:Rn=target
JSR @Rn                    | 0100nnnn00001011  | *   | Rn =PR                                 | br:jsr delayed hard:Rn=target
RTS                        | 0000000000001011  | *   | PR                                     | br:rts delayed hard:PR=target
CLRMAC                     | 0000000000101000  | *   | =MACH =MACL                            |
CLRS                       | 0000000001001000  | *   | =S                                     |
CLRT                       | 0000000000001000  | *   | =T                                     |
LDC Rm,SR                  | 0100mmmm00001110  | *   | Rm =SR =T =S =QM                       | hard:Rm=sr
LDC Rm,GBR                 | 0100mmmm00011110  | *   | Rm =GBR                                | gbrval:Rm
LDC Rm,VBR                 | 0100mmmm00101110  | *   | Rm =VBR                                | hard:Rm=vbr
LDC Rm,SSR                 | 0100mmmm00111110  | *   | Rm =SSR                                |
LDC Rm,SPC                 | 0100mmmm01001110  | *   | Rm =SPC                                |
LDC Rm,DBR                 | 0100mmmm11111010  | *   | Rm =DBR                                |
LDC Rm,Rn_BANK             | 0100mmmm1nnn1110  | *   | Rm =BANKn                              |
LDC.L @Rm+,SR              | 0100mmmm00000111  | *   | M:post:Rm:4:L =SR =T =S =QM            | hardmem=sr
LDC.L @Rm+,GBR             | 0100mmmm00010111  | *   | M:post:Rm:4:L =GBR                     | gbrmem
LDC.L @Rm+,VBR             | 0100mmmm00100111  | *   | M:post:Rm:4:L =VBR                     | hardmem=vbr
LDC.L @Rm+,SSR             | 0100mmmm00110111  | *   | M:post:Rm:4:L =SSR                     |
LDC.L @Rm+,SPC             | 0100mmmm01000111  | *   | M:post:Rm:4:L =SPC                     |
LDC.L @Rm+,DBR             | 0100mmmm11110110  | *   | M:post:Rm:4:L =DBR                     |
LDC.L @Rm+,Rn_BANK         | 0100mmmm1nnn0111  | *   | M:post:Rm:4:L =BANKn                   |
LDS Rm,MACH                | 0100mmmm00001010  | *   | Rm =MACH                               |
LDS Rm,MACL                | 0100mmmm00011010  | *   | Rm =MACL                               |
LDS Rm,PR                  | 0100mmmm00101010  | *   | Rm =PR                                 |
LDS.L @Rm+,MACH            | 0100mmmm00000110  | *   | M:post:Rm:4:L =MACH                    |
LDS.L @Rm+,MACL            | 0100mmmm00010110  | *   | M:post:Rm:4:L =MACL                    |
LDS.L @Rm+,PR              | 0100mmmm00100110  | *   | M:post:Rm:4:L =PR                      |
LDTLB                      | 0000000000111000  | *   | PTE =TLB                               |
MOVCA.L R0,@Rn             | 0000nnnn11000011  | *   | R0 M:ind:Rn:4:A                        |
NOP                        | 0000000000001001  | *   |                                        |
OCBI @Rn                   | 0000nnnn10010011  | *   | M:ind:Rn:4:I                           |
OCBP @Rn                   | 0000nnnn10100011  | *   | M:ind:Rn:4:B                           |
OCBWB @Rn                  | 0000nnnn10110011  | *   | M:ind:Rn:4:W                           |
PREF @Rn                   | 0000nnnn10000011  | *   | M:ind:Rn:4:P                           |
RTE                        | 0000000000101011  | *   | SSR SPC =SR =T =S =QM                  | br:rte delayed hard:SPC=target hard:SSR=sr
SETS                       | 0000000001011000  | *   | =S                                     |
SETT                       | 0000000000011000  | *   | =T                                     |
STC SR,Rn                  | 0000nnnn00000010  | *   | SR T S QM =Rn                          |
STC GBR,Rn                 | 0000nnnn00010010  | *   | GBR =Rn                                |
STC VBR,Rn                 | 0000nnnn00100010  | *   | VBR =Rn                                |
STC SSR,Rn                 | 0000nnnn00110010  | *   | SSR =Rn                                |
STC SPC,Rn                 | 0000nnnn01000010  | *   | SPC =Rn                                |
STC SGR,Rn                 | 0000nnnn00111010  | *   | SGR =Rn                                |
STC DBR,Rn                 | 0000nnnn11111010  | *   | DBR =Rn                                |
STC Rm_BANK,Rn             | 0000nnnn1mmm0010  | *   | BANKm =Rn                              |
STC.L SR,@-Rn              | 0100nnnn00000011  | *   | SR T S QM M:pre:Rn:4:S                 |
STC.L GBR,@-Rn             | 0100nnnn00010011  | *   | GBR M:pre:Rn:4:S                       |
STC.L VBR,@-Rn             | 0100nnnn00100011  | *   | VBR M:pre:Rn:4:S                       |
STC.L SSR,@-Rn             | 0100nnnn00110011  | *   | SSR M:pre:Rn:4:S                       |
STC.L SPC,@-Rn             | 0100nnnn01000011  | *   | SPC M:pre:Rn:4:S                       |
STC.L SGR,@-Rn             | 0100nnnn00110010  | *   | SGR M:pre:Rn:4:S                       |
STC.L DBR,@-Rn             | 0100nnnn11110010  | *   | DBR M:pre:Rn:4:S                       |
STC.L Rm_BANK,@-Rn         | 0100nnnn1mmm0011  | *   | BANKm M:pre:Rn:4:S                     |
STS MACH,Rn                | 0000nnnn00001010  | *   | MACH =Rn                               |
STS MACL,Rn                | 0000nnnn00011010  | *   | MACL =Rn                               |
STS PR,Rn                  | 0000nnnn00101010  | *   | PR =Rn                                 |
STS.L MACH,@-Rn            | 0100nnnn00000010  | *   | MACH M:pre:Rn:4:S                      |
STS.L MACL,@-Rn            | 0100nnnn00010010  | *   | MACL M:pre:Rn:4:S                      |
STS.L PR,@-Rn              | 0100nnnn00100010  | *   | PR M:pre:Rn:4:S                        |
FLDI0 FRn                  | 1111nnnn10001101  | M0 M1 M2 | =FRn FPSCR                        |
FLDI1 FRn                  | 1111nnnn10011101  | M0 M1 M2 | =FRn FPSCR                        |
FMOV FRm,FRn               | 1111nnnnmmmm1100  | M0 M2 | FRm =FRn FPSCR                       |
FMOV.S @Rm,FRn             | 1111nnnnmmmm1000  | M0 M2 | =FRn M:ind:Rm:4:L FPSCR              |
FMOV.S @(R0,Rm),FRn        | 1111nnnnmmmm0110  | M0 M2 | =FRn M:r0:Rm:4:L FPSCR               |
FMOV.S @Rm+,FRn            | 1111nnnnmmmm1001  | M0 M2 | =FRn M:post:Rm:4:L FPSCR             |
FMOV.S FRm,@Rn             | 1111nnnnmmmm1010  | M0 M2 | FRm M:ind:Rn:4:S FPSCR               |
FMOV.S FRm,@-Rn            | 1111nnnnmmmm1011  | M0 M2 | FRm M:pre:Rn:4:S FPSCR               |
FMOV.S FRm,@(R0,Rn)        | 1111nnnnmmmm0111  | M0 M2 | FRm M:r0:Rn:4:S FPSCR                |
FMOV DRm,DRn               | 1111nnn0mmm01100  | M1    | DRm =DRn FPSCR                       |
FMOV @Rm,DRn               | 1111nnn0mmmm1000  | M1    | =DRn M:ind:Rm:8:L FPSCR              |
FMOV @(R0,Rm),DRn          | 1111nnn0mmmm0110  | M1    | =DRn M:r0:Rm:8:L FPSCR               |
FMOV @Rm+,DRn              | 1111nnn0mmmm1001  | M1    | =DRn M:post:Rm:8:L FPSCR             |
FMOV DRm,@Rn               | 1111nnnnmmm01010  | M1    | DRm M:ind:Rn:8:S FPSCR               |
FMOV DRm,@-Rn              | 1111nnnnmmm01011  | M1    | DRm M:pre:Rn:8:S FPSCR               |
FMOV DRm,@(R0,Rn)          | 1111nnnnmmm00111  | M1    | DRm M:r0:Rn:8:S FPSCR                |
FMOV DRm,XDn               | 1111nnn1mmm01100  | M1    | DRm =XDn FPSCR                       |
FMOV XDm,DRn               | 1111nnn0mmm11100  | M1    | XDm =DRn FPSCR                       |
FMOV XDm,XDn               | 1111nnn1mmm11100  | M1    | XDm =XDn FPSCR                       |
FMOV @Rm,XDn               | 1111nnn1mmmm1000  | M1    | =XDn M:ind:Rm:8:L FPSCR              |
FMOV @Rm+,XDn              | 1111nnn1mmmm1001  | M1    | =XDn M:post:Rm:8:L FPSCR             |
FMOV @(R0,Rm),XDn          | 1111nnn1mmmm0110  | M1    | =XDn M:r0:Rm:8:L FPSCR               |
FMOV XDm,@Rn               | 1111nnnnmmm11010  | M1    | XDm M:ind:Rn:8:S FPSCR               |
FMOV XDm,@-Rn              | 1111nnnnmmm11011  | M1    | XDm M:pre:Rn:8:S FPSCR               |
FMOV XDm,@(R0,Rn)          | 1111nnnnmmm10111  | M1    | XDm M:r0:Rn:8:S FPSCR                |
FLDS FRm,FPUL              | 1111mmmm00011101  | M0 M1 M2 | FRm =FPUL FPSCR                   |
FSTS FPUL,FRn              | 1111nnnn00001101  | M0 M1 M2 | FPUL =FRn FPSCR                   |
FABS FRn                   | 1111nnnn01011101  | M0 M1 | +FRn FPSCR                           |
FADD FRm,FRn               | 1111nnnnmmmm0000  | M0 M1 | FRm +FRn +FPSCR                      |
FCMP/EQ FRm,FRn            | 1111nnnnmmmm0100  | M0 M1 | FRm FRn =T +FPSCR                    |
FCMP/GT FRm,FRn            | 1111nnnnmmmm0101  | M0 M1 | FRm FRn =T +FPSCR                    |
FDIV FRm,FRn               | 1111nnnnmmmm0011  | M0 M1 | FRm +FRn +FPSCR                      |
FLOAT FPUL,FRn             | 1111nnnn00101101  | M0 M1 | FPUL =FRn +FPSCR                     |
FMAC FR0,FRm,FRn           | 1111nnnnmmmm1110  | M0 M1 | FR0 FRm +FRn +FPSCR                  |
FMUL FRm,FRn               | 1111nnnnmmmm0010  | M0 M1 | FRm +FRn +FPSCR                      |
FNEG FRn                   | 1111nnnn01001101  | M0 M1 | +FRn FPSCR                           |
FSQRT FRn                  | 1111nnnn01101101  | M0 M1 | +FRn +FPSCR                          |
FSUB FRm,FRn               | 1111nnnnmmmm0001  | M0 M1 | FRm +FRn +FPSCR                      |
FTRC FRm,FPUL              | 1111mmmm00111101  | M0 M1 | FRm =FPUL +FPSCR                     |
FABS DRn                   | 1111nnn001011101  | M2    | +DRn FPSCR                           |
FADD DRm,DRn               | 1111nnn0mmm00000  | M2    | DRm +DRn +FPSCR                      |
FCMP/EQ DRm,DRn            | 1111nnn0mmm00100  | M2    | DRm DRn =T +FPSCR                    |
FCMP/GT DRm,DRn            | 1111nnn0mmm00101  | M2    | DRm DRn =T +FPSCR                    |
FDIV DRm,DRn               | 1111nnn0mmm00011  | M2    | DRm +DRn +FPSCR                      |
FCNVDS DRm,FPUL            | 1111mmm010111101  | M2    | DRm =FPUL +FPSCR                     |
FCNVSD FPUL,DRn            | 1111nnn010101101  | M2    | FPUL =DRn +FPSCR                     |
FLOAT FPUL,DRn             | 1111nnn000101101  | M2    | FPUL =DRn +FPSCR                     |
FMUL DRm,DRn               | 1111nnn0mmm00010  | M2    | DRm +DRn +FPSCR                      |
FNEG DRn                   | 1111nnn001001101  | M2    | +DRn FPSCR                           |
FSQRT DRn                  | 1111nnn001101101  | M2    | +DRn +FPSCR                          |
FSUB DRm,DRn               | 1111nnn0mmm00001  | M2    | DRm +DRn +FPSCR                      |
FTRC DRm,FPUL              | 1111mmm000111101  | M2    | DRm =FPUL +FPSCR                     |
LDS Rm,FPSCR               | 0100mmmm01101010  | M0 M1 M2 | Rm =FPSCR                         | hard:Rm=fpscr
LDS Rm,FPUL                | 0100mmmm01011010  | M0 M1 M2 | Rm =FPUL                          |
LDS.L @Rm+,FPSCR           | 0100mmmm01100110  | M0 M1 M2 | M:post:Rm:4:L =FPSCR              | hardmem=fpscr
LDS.L @Rm+,FPUL            | 0100mmmm01010110  | M0 M1 M2 | M:post:Rm:4:L =FPUL               |
STS FPSCR,Rn               | 0000nnnn01101010  | M0 M1 M2 | FPSCR =Rn                         |
STS FPUL,Rn                | 0000nnnn01011010  | M0 M1 M2 | FPUL =Rn                          |
STS.L FPSCR,@-Rn           | 0100nnnn01100010  | M0 M1 M2 | FPSCR M:pre:Rn:4:S                |
STS.L FPUL,@-Rn            | 0100nnnn01010010  | M0 M1 M2 | FPUL M:pre:Rn:4:S                 |
FIPR FVm,FVn               | 1111nnmm11101101  | M0 M1 | FVm FVn =FIPRn +FPSCR                |
FTRV XMTRX,FVn             | 1111nn0111111101  | M0 M1 | XMTRX +FVn +FPSCR                    |
FRCHG                      | 1111101111111101  | M0 M1 | +FRB                                 |
FSCHG                      | 1111001111111101  | M0 M1 | +SZB                                 |
FSCA FPUL,DRn              | 1111nnn011111101  | M0 M1 | FPUL =DRn +FPSCR                     |
FSRRA FRn                  | 1111nnnn01111101  | M0 M1 | +FRn +FPSCR                          |
"""

EXCLUDED = {
    'TRAPA #imm': 'always raises an unconditional-trap exception',
    'SLEEP': 'halts the CPU until an interrupt; interrupts are masked while timing, so it would never return',
}


class Mem:
    __slots__ = ('mode', 'base', 'size', 'kind')

    def __init__(self, mode, base, size, kind):
        self.mode, self.base, self.size, self.kind = mode, base, size, kind

    def __repr__(self):
        return f'M:{self.mode}:{self.base}:{self.size}:{self.kind}'


class Operand:
    __slots__ = ('name', 'acc')   # acc: 'r' | 'w' | 'rw'

    def __init__(self, name, acc):
        self.name, self.acc = name, acc

    def __repr__(self):
        return {'r': '', 'w': '=', 'rw': '+'}[self.acc] + self.name


class Form:
    def __init__(self, name, enc, modes, ops, flags):
        self.name, self.enc = name, enc
        self.modes = modes
        self.ops, self.mems, self.flags = [], [], {}
        for tok in ops.split():
            if tok.startswith('M:'):
                _, mode, base, size, kind = tok.split(':')
                self.mems.append(Mem(mode, base, int(size), kind))
                continue
            acc = 'r'
            if tok[0] == '=':
                acc, tok = 'w', tok[1:]
            elif tok[0] == '+':
                acc, tok = 'rw', tok[1:]
            self.ops.append(Operand(tok, acc))
        for m in self.mems:   # address registers are operands too
            if m.base in ('Rn', 'Rm'):
                self.ops.append(Operand(m.base, 'rw' if m.mode in ('post', 'pre') else 'r'))
            if m.mode in ('r0', 'gbr0'):
                self.ops.append(Operand('R0', 'r'))
            if m.mode in ('gbr', 'gbr0'):
                self.ops.append(Operand('GBR', 'r'))
        for f in flags.split():
            if ':' in f and f.startswith('hard:'):
                slot, what = f[5:].split('=')
                self.flags.setdefault('hard', {})[slot] = what
            elif '=' in f:
                k, v = f.split('=')
                self.flags[k] = v
            elif ':' in f:
                k, v = f.split(':')
                self.flags[k] = v
            else:
                self.flags[f] = True
        # encoding fields
        self.fields = {}
        for ch in 'nmid':
            pos = [15 - i for i, c in enumerate(enc) if c == ch]
            if pos:
                assert pos == list(range(pos[0], pos[-1] - 1, -1)), (name, ch)
                self.fields[ch] = (pos[-1], len(pos))   # (shift, width)
        self.fixed_mask = int(''.join('0' if c in 'nmid' else '1' for c in enc), 2)
        self.fixed_bits = int(''.join(c if c in '01' else '0' for c in enc), 2)
        self.br = self.flags.get('br')
        self.delayed = bool(self.flags.get('delayed'))

    def encode(self, f):
        w = self.fixed_bits
        for ch, (sh, wd) in self.fields.items():
            v = f[ch] & ((1 << wd) - 1)
            w |= v << sh
        # sub-field conventions: DR/XD/FV fields are the register number itself (the encoding pattern already
        # places the 3-bit / 2-bit field), so nothing else to do
        return w

    def is_mem(self):
        return bool(self.mems)

    def __repr__(self):
        return self.name


def _parse():
    forms = []
    for line in FORMS_SRC.strip().split('\n'):
        if line.startswith('#'):
            continue
        cols = [c.strip() for c in line.split('|')]
        name, enc, modes, ops, flags = cols[0], cols[1], cols[2], cols[3], cols[4] if len(cols) > 4 else ''
        md = list(MODES) if modes == '*' else modes.split()
        forms.append(Form(name, enc, md, ops, flags))
    return forms


FORMS = _parse()
BYNAME = {f.name: f for f in FORMS}
assert len(BYNAME) == len(FORMS)

# ---------------------------------------------------------------------------------------------------------------
# physical resources an operand touches, given the field values and the FR bank bit
def fr_name(bank, i):
    return f'f{"ab"[bank]}{i}'


def op_elems(form, op, f, frb):
    """list of physical resource names for operand `op` of `form` with fields `f`; frb = FPSCR.FR while it runs"""
    nm = op.name
    if nm == 'Rn':
        return [f'r{f["n"]}']
    if nm == 'Rm':
        return [f'r{f["m"]}']
    if nm == 'R0':
        return ['r0']
    if nm == 'FRn':
        return [fr_name(frb, f['n'])]
    if nm == 'FRm':
        return [fr_name(frb, f['m'])]
    if nm == 'FR0':
        return [fr_name(frb, 0)]
    if nm == 'DRn':
        return [fr_name(frb, 2 * f['n']), fr_name(frb, 2 * f['n'] + 1)]
    if nm == 'DRm':
        return [fr_name(frb, 2 * f['m']), fr_name(frb, 2 * f['m'] + 1)]
    if nm == 'XDn':
        return [fr_name(1 - frb, 2 * f['n']), fr_name(1 - frb, 2 * f['n'] + 1)]
    if nm == 'XDm':
        return [fr_name(1 - frb, 2 * f['m']), fr_name(1 - frb, 2 * f['m'] + 1)]
    if nm == 'FVn':
        return [fr_name(frb, 4 * f['n'] + k) for k in range(4)]
    if nm == 'FVm':
        return [fr_name(frb, 4 * f['m'] + k) for k in range(4)]
    if nm == 'FIPRn':
        return [fr_name(frb, 4 * f['n'] + 3)]
    if nm == 'XMTRX':
        return [fr_name(1 - frb, k) for k in range(16)]
    if nm == 'BANKn':
        return [f'bk{f["n"]}']
    if nm == 'BANKm':
        return [f'bk{f["m"]}']
    if nm in ('SR',):
        return ['src']
    if nm == 'FRB':
        return ['fpscr']
    if nm == 'SZB':
        return ['fpscr']
    return [nm.lower()]


# field kinds -> which register file the field selects
def field_kinds(form):
    """map field letter -> ('gpr'|'fr'|'dr'|'xd'|'fv'|'bank'|'imm'|'disp') from the operands"""
    k = {}
    for op in form.ops:
        nm = op.name
        for ch in 'nm':
            if nm in (f'R{ch}',):
                k[ch] = 'gpr'
            elif nm == f'FR{ch}':
                k[ch] = 'fr'
            elif nm == f'DR{ch}':
                k[ch] = 'dr'
            elif nm == f'XD{ch}':
                k[ch] = 'xd'
            elif nm in (f'FV{ch}',) or (nm == 'FIPRn' and ch == 'n'):
                k[ch] = 'fv'
            elif nm == f'BANK{ch}':
                k[ch] = 'bank'
    if 'i' in form.fields:
        k['i'] = 'imm'
    if 'd' in form.fields:
        k['d'] = 'disp'
    return k


# ---------------------------------------------------------------------------------------------------------------
# Functional model
class Bad(Exception):
    """the case would do something unsafe or unintended on the console"""


class State:
    """architectural state; values are ints (32-bit) or None (unknown)"""

    def __init__(self):
        self.reg = {}          # resource name -> value
        self.mem = {}          # byte address -> value (None unknown)
        self.mem_ok = []       # list of (lo, hi, kind) address ranges the case may touch
        self.fpscr_mode = 0    # FR/SZ/PR/DN/RM/enables (always known)
        self.trace_r, self.trace_w = set(), set()

    def copy(self):
        s = State.__new__(State)
        s.reg = dict(self.reg)
        s.mem = dict(self.mem)
        s.mem_ok = self.mem_ok
        s.fpscr_mode = self.fpscr_mode
        s.trace_r, s.trace_w = set(), set()
        return s

    # registers
    def rd(self, name):
        self.trace_r.add(name)
        return self.reg.get(name)

    def wr(self, name, v):
        self.trace_w.add(name)
        self.reg[name] = None if v is None else v & M32

    @property
    def frb(self):
        return (self.fpscr_mode >> 21) & 1

    @property
    def sz(self):
        return (self.fpscr_mode >> 20) & 1

    @property
    def pr(self):
        return (self.fpscr_mode >> 19) & 1

    # memory
    def check(self, addr, size, write):
        if addr is None:
            raise Bad('address unknown')
        for lo, hi, kind in self.mem_ok:
            if lo <= addr and addr + size <= hi:
                if write and kind == 'ro':
                    raise Bad(f'write to read-only region {addr:08x}')
                break
        else:
            raise Bad(f'address {addr:08x} outside the case memory')
        if addr % min(size, 8 if size == 8 else size):
            raise Bad(f'misaligned {size}-byte access at {addr:08x}')

    def ld(self, addr, size):
        self.check(addr, size, False)
        self.trace_r.add('mem')
        v = 0
        for k in range(size):
            b = self.mem.get(addr + k, 0)
            if b is None:
                return None
            v |= b << (8 * k)   # SH-4 on the Dreamcast runs little-endian
        return v

    def st(self, addr, size, v):
        self.check(addr, size, True)
        self.trace_w.add('mem')
        for k in range(size):
            self.mem[addr + k] = None if v is None else (v >> (8 * k)) & 0xFF

    def forget_line(self, addr):
        base = addr & ~31
        for k in range(32):
            self.mem[base + k] = None


def _t(v):
    return None if v is None else (1 if v else 0)


def _u(*vals):
    return any(v is None for v in vals)


def shad(rn, rm):
    if rm & 0x80000000 == 0:
        return (rn << (rm & 31)) & M32
    if rm & 31 == 0:
        return M32 if rn & 0x80000000 else 0
    return (s32(rn) >> ((~rm & 31) + 1)) & M32


def shld(rn, rm):
    if rm & 0x80000000 == 0:
        return (rn << (rm & 31)) & M32
    if rm & 31 == 0:
        return 0
    return rn >> ((~rm & 31) + 1)


def div1(st, n, m):
    """DIV1 Rm,Rn, from the manual's C description"""
    rn, rm, t, qm = st.rd(f'r{n}'), st.rd(f'r{m}'), st.rd('t'), st.rd('qm')
    if _u(rn, rm, t, qm):
        st.wr(f'r{n}', None); st.wr('t', None); st.wr('qm', None)
        return
    q, mm = qm & 1, (qm >> 1) & 1
    old_q = q
    q = (rn >> 31) & 1
    rn = ((rn << 1) | t) & M32
    tmp0 = rn
    if old_q == 0:
        if mm == 0:
            rn = (rn - rm) & M32
            tmp1 = rn > tmp0
            q = (1 - tmp1) if q else tmp1
        else:
            rn = (rn + rm) & M32
            tmp1 = rn < tmp0
            q = tmp1 if q else (1 - tmp1)
    else:
        if mm == 0:
            rn = (rn + rm) & M32
            tmp1 = rn < tmp0
            q = (1 - tmp1) if q else tmp1
        else:
            rn = (rn - rm) & M32
            tmp1 = rn > tmp0
            q = tmp1 if q else (1 - tmp1)
    t = 1 if q == mm else 0
    st.wr(f'r{n}', rn); st.wr('t', t); st.wr('qm', q | (mm << 1))


def ea(st, form, mem, f, pc):
    """effective address of memory operand `mem`, and the base register update (name, new value) if any"""
    sz = mem.size
    if mem.mode == 'ind':
        return st.rd(f'r{f[mem.base[1]]}'), None
    if mem.mode == 'post':
        r = f'r{f[mem.base[1]]}'
        a = st.rd(r)
        return a, (r, None if a is None else a + sz)
    if mem.mode == 'pre':
        r = f'r{f[mem.base[1]]}'
        a = st.rd(r)
        a = None if a is None else (a - sz) & M32
        return a, (r, a)
    if mem.mode == 'disp':
        a = st.rd(f'r{f[mem.base[1]]}')
        return (None if a is None else a + f['d'] * sz), None
    if mem.mode == 'r0':
        a, b = st.rd('r0'), st.rd(f'r{f[mem.base[1]]}')
        return (None if _u(a, b) else (a + b) & M32), None
    if mem.mode == 'gbr':
        g = st.rd('gbr')
        return (None if g is None else g + f['d'] * sz), None
    if mem.mode == 'gbr0':
        g, a = st.rd('gbr'), st.rd('r0')
        return (None if _u(g, a) else (g + a) & M32), None
    if mem.mode == 'pc':
        if sz == 2:
            return pc + 4 + f['d'] * 2, None
        return (pc & ~3) + 4 + f['d'] * 4, None
    raise ValueError(mem.mode)


def execute(st, form, f, pc):
    """run one instruction; returns None or (target, delayed) for a branch that is taken"""
    nm = form.name
    n, m = f.get('n'), f.get('m')
    Rn, Rm = f'r{n}', f'r{m}'
    frb = st.frb

    def FR(i):
        return fr_name(frb, i)

    def XF(i):
        return fr_name(1 - frb, i)

    # ---- memory forms: address first
    if form.mems:
        if nm.startswith('MAC.'):
            sz = form.mems[0].size
            am, um = ea(st, form, form.mems[0], f, pc)
            an, un = ea(st, form, form.mems[1], f, pc)
            a = st.ld(an, sz)
            b = st.ld(am, sz)
            st.wr(um[0], um[1]); st.wr(un[0], un[1])
            st.rd('mach'); st.rd('macl'); st.rd('s')
            st.wr('mach', None); st.wr('macl', None)
            return None
        mem = form.mems[0]
        a, upd = ea(st, form, mem, f, pc)
        k = mem.kind
        if k == 'L':
            v = st.ld(a, mem.size)
            if v is not None and mem.size < 4 and not nm.startswith(('LDC', 'LDS', 'TST.B')):
                v = (sx8(v) if mem.size == 1 else sx16(v)) & M32
        elif k in ('S', 'A'):
            pass
        elif k in ('M', 'T'):
            v = st.ld(a, 1)
        elif k in ('P', 'I', 'B', 'W'):
            st.check(a, 1, k == 'I')
        if upd and k != 'S':
            st.wr(upd[0], upd[1])
        # data movement
        if k == 'L':
            if nm.startswith('FMOV'):
                if mem.size == 8:
                    dst = form.ops[0].name
                    base = 2 * n
                    names = [FR(base), FR(base + 1)] if dst == 'DRn' else [XF(base), XF(base + 1)]
                    lo, hi = (None, None) if v is None else (v & M32, v >> 32)
                    # a 64-bit FMOV puts the word at the lower address in the even register
                    st.wr(names[0], lo); st.wr(names[1], hi)
                else:
                    st.wr(FR(n), v)
            elif nm.startswith('LDC.L') or nm.startswith('LDS.L'):
                dst = nm.split(',')[1]
                if dst == 'SR':
                    st.wr('src', v); st.wr('t', None if v is None else v & 1)
                    st.wr('s', None if v is None else (v >> 1) & 1)
                    st.wr('qm', None if v is None else (v >> 8) & 3)
                elif dst == 'Rn_BANK':
                    st.wr(f'bk{n}', v)
                elif dst == 'FPSCR':
                    st.wr('fpscr', v)
                    if v is None:
                        raise Bad('FPSCR loaded with an unknown value')
                    st.fpscr_mode = v
                else:
                    st.wr(dst.lower(), v)
            elif nm.startswith('TST.B'):
                st.rd('r0')
                st.wr('t', None if v is None else int((v & f['i']) == 0))
            elif ',R0' in nm:
                st.wr('r0', v)
            else:
                st.wr(Rn, v)
        elif k == 'S':
            src = form.ops[0].name
            if src == 'Rm':
                v = st.rd(Rm)
            elif src == 'R0':
                v = st.rd('r0')
            elif src in ('FRm',):
                v = st.rd(FR(m))
            elif src in ('DRm', 'XDm'):
                names = [FR(2 * m), FR(2 * m + 1)] if src == 'DRm' else [XF(2 * m), XF(2 * m + 1)]
                lo, hi = st.rd(names[0]), st.rd(names[1])
                v = None if _u(lo, hi) else lo | (hi << 32)
            elif src == 'SR':
                c, t, s, q = st.rd('src'), st.rd('t'), st.rd('s'), st.rd('qm')
                v = None if _u(c, t, s, q) else (c & ~0x303) | t | (s << 1) | (q << 8)
            elif src.startswith('BANK'):
                v = st.rd(f'bk{m}')
            else:
                v = st.rd(src.lower())
            st.st(a, mem.size, v)
            if upd:
                st.wr(upd[0], upd[1])
        elif k == 'A':
            st.st(a, 4, st.rd('r0'))
            st.forget_line(a)       # MOVCA.L allocates the line without fetching it: the rest is garbage
            st.mem[a] = None
        elif k in ('M', 'T'):
            st.rd('r0') if mem.mode == 'gbr0' else None
            i = f.get('i', 0)
            if k == 'T':
                st.wr('t', None if v is None else int(v == 0))
                nv = None if v is None else v | 0x80
            elif nm.startswith('AND.B'):
                nv = None if v is None else v & i
            elif nm.startswith('OR.B'):
                nv = None if v is None else v | i
            else:
                nv = None if v is None else v ^ i
            st.st(a, 1, nv)
        elif k == 'I':
            st.forget_line(a)       # OCBI drops the line: memory may be older than what the cache held
        return None

    # ---- register forms
    if nm == 'MOV #imm,Rn':
        st.wr(Rn, sx8(f['i']))
    elif nm == 'MOVA @(disp,PC),R0':
        st.wr('r0', (pc & ~3) + 4 + f['d'] * 4)
    elif nm == 'MOV Rm,Rn':
        st.wr(Rn, st.rd(Rm))
    elif nm == 'MOVT Rn':
        st.wr(Rn, st.rd('t'))
    elif nm == 'SWAP.B Rm,Rn':
        v = st.rd(Rm)
        st.wr(Rn, None if v is None else (v & 0xFFFF0000) | ((v & 0xFF) << 8) | ((v >> 8) & 0xFF))
    elif nm == 'SWAP.W Rm,Rn':
        v = st.rd(Rm)
        st.wr(Rn, None if v is None else ((v << 16) | (v >> 16)) & M32)
    elif nm == 'XTRCT Rm,Rn':
        a, b = st.rd(Rm), st.rd(Rn)
        st.wr(Rn, None if _u(a, b) else ((a << 16) | (b >> 16)) & M32)
    elif nm in ('ADD Rm,Rn', 'SUB Rm,Rn', 'AND Rm,Rn', 'OR Rm,Rn', 'XOR Rm,Rn'):
        a, b = st.rd(Rm), st.rd(Rn)
        op = nm.split()[0]
        r = None if _u(a, b) else {'ADD': b + a, 'SUB': b - a, 'AND': b & a, 'OR': b | a, 'XOR': b ^ a}[op]
        st.wr(Rn, r)
    elif nm == 'ADD #imm,Rn':
        b = st.rd(Rn)
        st.wr(Rn, None if b is None else b + sx8(f['i']))
    elif nm in ('ADDC Rm,Rn', 'SUBC Rm,Rn'):
        a, b, t = st.rd(Rm), st.rd(Rn), st.rd('t')
        if _u(a, b, t):
            st.wr(Rn, None); st.wr('t', None)
        elif nm.startswith('ADDC'):
            r = b + a + t
            st.wr(Rn, r); st.wr('t', int(r > M32))
        else:
            r = b - a - t
            st.wr(Rn, r); st.wr('t', int(r < 0))
    elif nm in ('ADDV Rm,Rn', 'SUBV Rm,Rn'):
        a, b = st.rd(Rm), st.rd(Rn)
        if _u(a, b):
            st.wr(Rn, None); st.wr('t', None)
        else:
            r = s32(b) + s32(a) if nm.startswith('ADDV') else s32(b) - s32(a)
            st.wr(Rn, r); st.wr('t', int(r > 0x7FFFFFFF or r < -0x80000000))
    elif nm == 'CMP/EQ #imm,R0':
        a = st.rd('r0')
        st.wr('t', None if a is None else int(a == (sx8(f['i']) & M32)))
    elif nm.startswith('CMP/') and ',' in nm:
        a, b = st.rd(Rm), st.rd(Rn)
        if _u(a, b):
            st.wr('t', None)
        else:
            c = nm[4:].split()[0]
            if c == 'EQ':
                t = b == a
            elif c == 'HS':
                t = b >= a
            elif c == 'GE':
                t = s32(b) >= s32(a)
            elif c == 'HI':
                t = b > a
            elif c == 'GT':
                t = s32(b) > s32(a)
            else:   # STR
                x = a ^ b
                t = any(((x >> (8 * k)) & 0xFF) == 0 for k in range(4))
            st.wr('t', int(t))
    elif nm in ('CMP/PZ Rn', 'CMP/PL Rn'):
        b = st.rd(Rn)
        st.wr('t', None if b is None else int(s32(b) >= 0 if 'PZ' in nm else s32(b) > 0))
    elif nm == 'DIV1 Rm,Rn':
        div1(st, n, m)
    elif nm == 'DIV0S Rm,Rn':
        a, b = st.rd(Rm), st.rd(Rn)
        if _u(a, b):
            st.wr('t', None); st.wr('qm', None)
        else:
            q, mm = b >> 31, a >> 31
            st.wr('qm', q | (mm << 1)); st.wr('t', q ^ mm)
    elif nm == 'DIV0U':
        st.wr('t', 0); st.wr('qm', 0)
    elif nm in ('DMULS.L Rm,Rn', 'DMULU.L Rm,Rn'):
        a, b = st.rd(Rm), st.rd(Rn)
        if _u(a, b):
            st.wr('mach', None); st.wr('macl', None)
        else:
            r = (s32(a) * s32(b)) if 'DMULS' in nm else a * b
            r &= (1 << 64) - 1
            st.wr('mach', r >> 32); st.wr('macl', r)
    elif nm == 'DT Rn':
        b = st.rd(Rn)
        r = None if b is None else (b - 1) & M32
        st.wr(Rn, r); st.wr('t', None if r is None else int(r == 0))
    elif nm.startswith(('EXTS', 'EXTU')):
        a = st.rd(Rm)
        if a is None:
            st.wr(Rn, None)
        else:
            st.wr(Rn, {'EXTS.B': sx8(a), 'EXTS.W': sx16(a), 'EXTU.B': a & 0xFF, 'EXTU.W': a & 0xFFFF}[nm.split()[0]])
    elif nm in ('MUL.L Rm,Rn', 'MULS.W Rm,Rn', 'MULU.W Rm,Rn'):
        a, b = st.rd(Rm), st.rd(Rn)
        if _u(a, b):
            st.wr('macl', None)
        elif nm.startswith('MUL.L'):
            st.wr('macl', a * b)
        elif nm.startswith('MULS'):
            st.wr('macl', sx16(a) * sx16(b))
        else:
            st.wr('macl', (a & 0xFFFF) * (b & 0xFFFF))
    elif nm == 'NEG Rm,Rn':
        a = st.rd(Rm)
        st.wr(Rn, None if a is None else -a)
    elif nm == 'NEGC Rm,Rn':
        a, t = st.rd(Rm), st.rd('t')
        if _u(a, t):
            st.wr(Rn, None); st.wr('t', None)
        else:
            r = -a - t
            st.wr(Rn, r); st.wr('t', int(r < 0))
    elif nm in ('AND #imm,R0', 'OR #imm,R0', 'XOR #imm,R0'):
        a = st.rd('r0')
        i = f['i']
        st.wr('r0', None if a is None else {'AND': a & i, 'OR': a | i, 'XOR': a ^ i}[nm.split()[0]])
    elif nm == 'NOT Rm,Rn':
        a = st.rd(Rm)
        st.wr(Rn, None if a is None else ~a)
    elif nm == 'TST Rm,Rn':
        a, b = st.rd(Rm), st.rd(Rn)
        st.wr('t', None if _u(a, b) else int((a & b) == 0))
    elif nm == 'TST #imm,R0':
        a = st.rd('r0')
        st.wr('t', None if a is None else int((a & f['i']) == 0))
    elif nm in ('ROTL Rn', 'ROTR Rn', 'ROTCL Rn', 'ROTCR Rn', 'SHAL Rn', 'SHAR Rn', 'SHLL Rn', 'SHLR Rn'):
        b = st.rd(Rn)
        t = st.rd('t') if 'ROTC' in nm else 0
        if _u(b, t):
            st.wr(Rn, None); st.wr('t', None)
        else:
            op = nm.split()[0]
            if op == 'ROTL':
                r, nt = ((b << 1) | (b >> 31)), b >> 31
            elif op == 'ROTR':
                r, nt = ((b >> 1) | ((b & 1) << 31)), b & 1
            elif op == 'ROTCL':
                r, nt = ((b << 1) | t), b >> 31
            elif op == 'ROTCR':
                r, nt = ((b >> 1) | (t << 31)), b & 1
            elif op in ('SHAL', 'SHLL'):
                r, nt = b << 1, b >> 31
            elif op == 'SHAR':
                r, nt = (b >> 1) | (b & 0x80000000), b & 1
            else:
                r, nt = b >> 1, b & 1
            st.wr(Rn, r); st.wr('t', nt)
    elif nm in ('SHAD Rm,Rn', 'SHLD Rm,Rn'):
        a, b = st.rd(Rm), st.rd(Rn)
        st.wr(Rn, None if _u(a, b) else (shad(b, a) if 'SHAD' in nm else shld(b, a)))
    elif re.match(r'SH[LR][LR](2|8|16) Rn', nm):
        b = st.rd(Rn)
        k = int(re.findall(r'\d+', nm)[0])
        st.wr(Rn, None if b is None else (b << k if nm.startswith('SHLL') else b >> k))
    # ---- branches
    elif form.br:
        return branch(st, form, f, pc)
    # ---- system
    elif nm == 'CLRMAC':
        st.wr('mach', 0); st.wr('macl', 0)
    elif nm in ('CLRS', 'SETS'):
        st.wr('s', int(nm == 'SETS'))
    elif nm in ('CLRT', 'SETT'):
        st.wr('t', int(nm == 'SETT'))
    elif nm == 'NOP':
        pass
    elif nm == 'LDTLB':
        st.rd('pte'); st.wr('tlb', None)
    elif nm.startswith('LDC ') or nm.startswith('LDS '):
        v = st.rd(Rm)
        dst = nm.split(',')[1]
        if dst == 'SR':
            st.wr('src', v); st.wr('t', None if v is None else v & 1)
            st.wr('s', None if v is None else (v >> 1) & 1); st.wr('qm', None if v is None else (v >> 8) & 3)
        elif dst == 'Rn_BANK':
            st.wr(f'bk{n}', v)
        elif dst == 'FPSCR':
            if v is None:
                raise Bad('FPSCR loaded with an unknown value')
            st.wr('fpscr', v)
            st.fpscr_mode = v
        else:
            st.wr(dst.lower(), v)
    elif nm.startswith('STC ') or nm.startswith('STS '):
        src = nm.split()[1].split(',')[0]
        if src == 'SR':
            c, t, s, q = st.rd('src'), st.rd('t'), st.rd('s'), st.rd('qm')
            v = None if _u(c, t, s, q) else (c & ~0x303) | t | (s << 1) | (q << 8)
        elif src == 'Rm_BANK':
            v = st.rd(f'bk{m}')
        elif src == 'FPSCR':
            v = st.rd('fpscr')
        else:
            v = st.rd(src.lower())
        st.wr(Rn, v)
    # ---- FPU
    elif nm in ('FLDI0 FRn', 'FLDI1 FRn'):
        st.wr(FR(n), 0 if 'FLDI0' in nm else 0x3F800000)
    elif nm == 'FMOV FRm,FRn':
        st.wr(FR(n), st.rd(FR(m)))
    elif nm in ('FMOV DRm,DRn', 'FMOV DRm,XDn', 'FMOV XDm,DRn', 'FMOV XDm,XDn'):
        srcf = FR if nm.split()[1].startswith('DR') else XF
        dstf = FR if nm.split(',')[1].startswith('DR') else XF
        lo, hi = st.rd(srcf(2 * m)), st.rd(srcf(2 * m + 1))
        st.wr(dstf(2 * n), lo); st.wr(dstf(2 * n + 1), hi)
    elif nm == 'FLDS FRm,FPUL':
        st.wr('fpul', st.rd(FR(m)))
    elif nm == 'FSTS FPUL,FRn':
        st.wr(FR(n), st.rd('fpul'))
    elif nm in ('FABS FRn', 'FNEG FRn'):
        v = st.rd(FR(n))
        st.wr(FR(n), None if v is None else (v & 0x7FFFFFFF if 'FABS' in nm else v ^ 0x80000000))
    elif nm in ('FABS DRn', 'FNEG DRn'):
        v = st.rd(FR(2 * n))     # the sign lives in the high word = the even register
        st.rd(FR(2 * n + 1))
        st.wr(FR(2 * n), None if v is None else (v & 0x7FFFFFFF if 'FABS' in nm else v ^ 0x80000000))
    elif nm in ('FCMP/EQ FRm,FRn', 'FCMP/GT FRm,FRn', 'FCMP/EQ DRm,DRn', 'FCMP/GT DRm,DRn'):
        dbl = 'DR' in nm
        a = st.rd(FR(2 * m if dbl else m)); b = st.rd(FR(2 * n if dbl else n))
        if dbl:
            st.rd(FR(2 * m + 1)); st.rd(FR(2 * n + 1))
        st.wr('fpscr', None)
        if _u(a, b):
            st.wr('t', None)
        else:
            # only used to steer a following BT / BF; operands here are plain normal numbers
            def key(w):
                return -(w & 0x7FFFFFFF) if w & 0x80000000 else w
            st.wr('t', int(key(a) == key(b) if 'EQ' in nm else key(b) > key(a)))
    elif nm == 'FRCHG':
        st.rd('fpscr'); st.wr('fpscr', None)
        st.fpscr_mode ^= 1 << 21
    elif nm == 'FSCHG':
        st.rd('fpscr'); st.wr('fpscr', None)
        st.fpscr_mode ^= 1 << 20
    else:
        # FP arithmetic: read the operands, write unknown results (+ FPUL / T / FPSCR cause as declared)
        for op in form.ops:
            els = op_elems(form, op, f, frb)
            if 'r' in op.acc:
                for e in els:
                    st.rd(e)
        for op in form.ops:
            if 'w' in op.acc:
                for e in op_elems(form, op, f, frb):
                    st.wr(e, None)
    return None


def branch(st, form, f, pc):
    kind = form.br
    delayed = form.delayed
    if kind in ('cond_t', 'cond_f'):
        t = st.rd('t')
        if t is None:
            raise Bad('conditional branch on an unknown T')
        want = int(form.flags['want_t'])
        if t != want:
            raise Bad(f'{form.name}: T={t}, the form needs T={want}')
        taken = (t == 1) == (kind == 'cond_t')
        if not taken:
            return None
        d = sx8(f['d'])
        return (pc + 4 + d * 2, delayed)
    if kind in ('uncond', 'bsr'):
        d = f['d'] & 0xFFF
        d = d - 0x1000 if d & 0x800 else d
        if kind == 'bsr':
            st.wr('pr', pc + 4)
        return (pc + 4 + d * 2, True)
    if kind in ('braf', 'bsrf'):
        r = st.rd(f'r{f["n"]}')
        if r is None:
            raise Bad('BRAF/BSRF offset unknown')
        if kind == 'bsrf':
            st.wr('pr', pc + 4)
        return ((pc + 4 + r) & M32, True)
    if kind in ('jmp', 'jsr'):
        r = st.rd(f'r{f["n"]}')
        if r is None:
            raise Bad('JMP/JSR target unknown')
        if kind == 'jsr':
            st.wr('pr', pc + 4)
        return (r, True)
    if kind == 'rts':
        r = st.rd('pr')
        if r is None:
            raise Bad('RTS target unknown')
        return (r, True)
    if kind == 'rte':
        spc, ssr = st.rd('spc'), st.rd('ssr')
        if _u(spc, ssr):
            raise Bad('RTE with unknown SPC / SSR')
        st.wr('src', ssr); st.wr('t', ssr & 1); st.wr('s', (ssr >> 1) & 1); st.wr('qm', (ssr >> 8) & 3)
        return (spc, True)
    raise ValueError(kind)


SLOT_ILLEGAL = {f.name for f in FORMS if f.br or f.flags.get('pcrel')} | {'LDC Rm,SR', 'LDC.L @Rm+,SR'}


def forms_in(mode):
    return [f for f in FORMS if mode in f.modes]


def selfcheck():
    """every form: the encoding is unique within each mode (a decode table), and the semantics touch exactly the
    declared operands (traced on a synthetic state)"""
    for mode in MODES:
        seen = {}
        for f in forms_in(mode):
            key = (f.fixed_mask, f.fixed_bits)
            if key in seen and seen[key].name.split(' (')[0] != f.name.split(' (')[0]:
                raise AssertionError(f'{mode}: {f.name} and {seen[key].name} share an encoding')
            seen[key] = f
        for a in forms_in(mode):
            for b in forms_in(mode):
                if a is b or a.name.split(' (')[0] == b.name.split(' (')[0]:
                    continue
                # overlap: some word matches both patterns
                if (a.fixed_bits & b.fixed_mask) == (b.fixed_bits & a.fixed_mask) and \
                   ((a.fixed_bits ^ b.fixed_bits) & a.fixed_mask & b.fixed_mask) == 0:
                    raise AssertionError(f'{mode}: {a.name} / {b.name} overlap')
    return True


if __name__ == '__main__':
    selfcheck()
    for mode in MODES:
        print(mode, len(forms_in(mode)), 'forms')
    print(len(FORMS), 'forms total;', 'excluded:', ', '.join(EXCLUDED))
