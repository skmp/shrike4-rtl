// v1_alu_tb -- unit A of v1: rtl/v1/v1_alu.sv (v1_alu_pkg) and rtl/v1/v1_mul.sv (v1_mul_pkg), through tb/v1_alu_tb.sv.
//
// Two references, cross-checked against each other:
//   isa.py  the sweep's functional model (model/timings2/isa.py execute()), as golden vectors written by
//           tb/v1_alu_tb.py: every ALU / MUL form of the ISA table, with the decoder's operand convention.
//   Man     the manual's C pseudo-code (pm.txt Section 10) ported below line for line (long = int32_t,
//           unsigned long = uint32_t), including MAC.W / MAC.L which isa.py leaves 'unknown'.
// Checks (every count is printed; any mismatch fails the run):
//   1. isa.py vectors -> RTL, and -> Man (validates the port before it is trusted as the random reference)
//   2. RTL vs Man, 1M random + edge vectors per ALU op (all outputs: r, t, q, m, wr_t, wr_qm)
//   3. sequences on a register machine stepping the RTL ALU (and Man in lock step):
//        DIV0U/DIV0S + DIV1 + ROTCL long division, the manual's four examples, vs host division
//        ADDC / SUBC / NEGC 128-bit carry chains vs __int128; ADDV / SUBV vs int64 overflow
//        SHAD / SHLD: all 64 (Rm[31], Rm[4:0]) x 16384 Rn, vs a direct formula
//        CMP/STR: all 16 byte-equality patterns x 65536, vs the pattern
//   4. MUL / MAC: RTL vs Man vs an independent __int128 formula, 1M per op and S, with saturation-edge operands
// Options: --vec <vectors.bin> (default ../v1_alu/vectors.bin, relative to the run directory), --n <per-op count>.
#include "Vv1_alu_tb.h"
#include "verilated.h"
#include "v1_alu_ops.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>

static Vv1_alu_tb* dut;
static long g_fail = 0;

// ---------------- deterministic random ----------------
static uint64_t g_rs = 0x9E3779B97F4A7C15ull;
static inline uint64_t rnd64() {   // splitmix64
    uint64_t z = (g_rs += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
static inline uint32_t rnd32() { return (uint32_t)rnd64(); }
static inline uint32_t rndn(uint32_t n) { return (uint32_t)((rnd64() >> 32) * n >> 32); }

static const uint32_t EDGES[] = {
    0, 1, 2, 3, 0x7F, 0x80, 0x81, 0xFF, 0x100, 0x7FFF, 0x8000, 0x8001, 0xFFFF, 0x10000, 0x7FFFFF, 0x800000, 0xFFFFFF,
    0x1000000, 0x7FFFFFFE, 0x7FFFFFFF, 0x80000000, 0x80000001, 0x80000002, 0xFFFFFFFF, 0xFFFFFFFE, 0xFFFFFFFD,
    0xFFFFFF80, 0xFFFFFF7F, 0xFFFF8000, 0xFFFF7FFF, 0xFFFF0000, 0x0000FF00, 0x00FF0000, 0xFF000000, 0x55555555,
    0xAAAAAAAA, 0x12345678, 0x87654321, 0x0F0F0F0F, 0xF0F0F0F0};
static const int NEDGE = sizeof(EDGES) / sizeof(EDGES[0]);

// one operand: uniform, edge +- small, k-bit magnitude of either sign, power of two +- small, byte patterns
static uint32_t gen32() {
    switch (rndn(6)) {
    case 0: case 1: return rnd32();
    case 2: return EDGES[rndn(NEDGE)] + (int32_t)rndn(5) - 2;
    case 3: { uint32_t v = rnd32() >> rndn(32); return rndn(2) ? v : 0u - v; }
    case 4: return (1u << rndn(32)) + (int32_t)rndn(5) - 2;
    default: {
        static const uint8_t B[] = {0x00, 0xFF, 0x80, 0x7F, 0x01};
        uint32_t v = 0;
        for (int k = 0; k < 4; k++) v |= (uint32_t)(rndn(3) ? B[rndn(5)] : (uint8_t)rnd32()) << (8 * k);
        return v;
    }
    }
}
// the Rn-side operand given the Rm-side one: independent, or related (equal, +-1, ~, -, one bit, one byte)
static uint32_t gen_b(uint32_t a) {
    switch (rndn(10)) {
    case 0: return a;
    case 1: return a + (int32_t)rndn(5) - 2;
    case 2: return ~a;
    case 3: return 0u - a;
    case 4: return a ^ (1u << rndn(32));
    case 5: { int k = rndn(4); return (a & ~(0xFFu << (8 * k))) | ((uint32_t)(uint8_t)rnd32() << (8 * k)); }
    default: return gen32();
    }
}

// ---------------- the DUT ----------------
struct AluOut { uint32_t r; int t, q, m, wr_t, wr_qm; };
static AluOut rtl_alu(int op, uint32_t a, uint32_t b, int t, int q, int m) {
    dut->op = op; dut->a = a; dut->b = b; dut->t = t; dut->q = q; dut->m = m;
    dut->eval();
    return {dut->r, dut->ot, dut->oq, dut->om, dut->owr_t, dut->owr_qm};
}
static uint64_t rtl_mul(int op, uint32_t a, uint32_t b, int s, uint64_t acc) {
    dut->mop = op; dut->ma = a; dut->mb = b; dut->ms = s; dut->macc = acc;
    dut->eval();
    return dut->mres;
}

// ---------------- the manual's pseudo-code (pm.txt Section 10) ----------------
// Ported as printed, with long -> int32_t and unsigned long -> uint32_t (the manual's 32-bit long), PC += 2 dropped.
// Deviations, each marked [fix] / [ARCH] at its line: MAC.L's printed MACH sign extension typo, MAC.W S = 1's MACH
// bit 0 (Description + ARCH.md), TAS on a byte held in Rn (memory is outside the unit), MOVT's SR = T.
struct Man {
    uint32_t R[16] = {};
    uint32_t T = 0, Q = 0, M = 0, S = 0, MACH = 0, MACL = 0;

    void ADD(int m, int n) { R[n] += R[m]; }
    void ADDC(int m, int n) {
        uint32_t tmp0, tmp1;
        tmp1 = R[n] + R[m]; tmp0 = R[n]; R[n] = tmp1 + T;
        if (tmp0 > tmp1) T = 1; else T = 0;
        if (tmp1 > R[n]) T = 1;
    }
    void ADDV(int m, int n) {
        int32_t dest, src, ans;
        if ((int32_t)R[n] >= 0) dest = 0; else dest = 1;
        if ((int32_t)R[m] >= 0) src = 0; else src = 1;
        src += dest; R[n] += R[m];
        if ((int32_t)R[n] >= 0) ans = 0; else ans = 1;
        ans += dest;
        if (src == 0 || src == 2) { if (ans == 1) T = 1; else T = 0; } else T = 0;
    }
    void SUB(int m, int n) { R[n] -= R[m]; }
    void SUBC(int m, int n) {
        uint32_t tmp0, tmp1;
        tmp1 = R[n] - R[m]; tmp0 = R[n]; R[n] = tmp1 - T;
        if (tmp0 < tmp1) T = 1; else T = 0;
        if (tmp1 < R[n]) T = 1;
    }
    void SUBV(int m, int n) {
        int32_t dest, src, ans;
        if ((int32_t)R[n] >= 0) dest = 0; else dest = 1;
        if ((int32_t)R[m] >= 0) src = 0; else src = 1;
        src += dest; R[n] -= R[m];
        if ((int32_t)R[n] >= 0) ans = 0; else ans = 1;
        ans += dest;
        if (src == 1) { if (ans == 1) T = 1; else T = 0; } else T = 0;
    }
    void NEG(int m, int n) { R[n] = 0 - R[m]; }
    void NEGC(int m, int n) {
        uint32_t temp;
        temp = 0 - R[m]; R[n] = temp - T;
        if (0 < temp) T = 1; else T = 0;
        if (temp < R[n]) T = 1;
    }
    void AND(int m, int n) { R[n] &= R[m]; }
    void OR(int m, int n) { R[n] |= R[m]; }
    void XOR(int m, int n) { R[n] ^= R[m]; }
    void NOT(int m, int n) { R[n] = ~R[m]; }
    void TST(int m, int n) { if ((R[n] & R[m]) == 0) T = 1; else T = 0; }
    void TAS(int n) {   // [the byte at @Rn is modelled as R[n]'s low byte: the stored byte is R[n][7:0]]
        int32_t temp;
        temp = (int32_t)(R[n] & 0xFF);
        if (temp == 0) T = 1; else T = 0;
        temp |= 0x00000080;
        R[n] = (uint32_t)temp;
    }
    void CMPEQ(int m, int n) { if (R[n] == R[m]) T = 1; else T = 0; }
    void CMPGE(int m, int n) { if ((int32_t)R[n] >= (int32_t)R[m]) T = 1; else T = 0; }
    void CMPGT(int m, int n) { if ((int32_t)R[n] > (int32_t)R[m]) T = 1; else T = 0; }
    void CMPHI(int m, int n) { if ((uint32_t)R[n] > (uint32_t)R[m]) T = 1; else T = 0; }
    void CMPHS(int m, int n) { if ((uint32_t)R[n] >= (uint32_t)R[m]) T = 1; else T = 0; }
    void CMPPL(int n) { if ((int32_t)R[n] > 0) T = 1; else T = 0; }
    void CMPPZ(int n) { if ((int32_t)R[n] >= 0) T = 1; else T = 0; }
    void CMPSTR(int m, int n) {
        uint32_t temp; int32_t HH, HL, LH, LL;
        temp = R[n] ^ R[m];
        HH = (temp & 0xFF000000) >> 24; HL = (temp & 0x00FF0000) >> 16;
        LH = (temp & 0x0000FF00) >> 8;  LL = temp & 0x000000FF;
        HH = HH && HL && LH && LL;
        if (HH == 0) T = 1; else T = 0;
    }
    void DIV0S(int m, int n) {
        if ((R[n] & 0x80000000) == 0) Q = 0; else Q = 1;
        if ((R[m] & 0x80000000) == 0) M = 0; else M = 1;
        T = !(M == Q);
    }
    void DIV0U() { M = Q = T = 0; }
    void DIV1(int m, int n) {
        uint32_t tmp0, tmp2; uint8_t old_q, tmp1;
        old_q = Q;
        Q = (uint8_t)((0x80000000 & R[n]) != 0);
        tmp2 = R[m];
        R[n] <<= 1;
        R[n] |= (uint32_t)T;
        switch (old_q) {
        case 0: switch (M) {
            case 0: tmp0 = R[n]; R[n] -= tmp2; tmp1 = (R[n] > tmp0);
                switch (Q) { case 0: Q = tmp1; break; case 1: Q = (uint8_t)(tmp1 == 0); break; }
                break;
            case 1: tmp0 = R[n]; R[n] += tmp2; tmp1 = (R[n] < tmp0);
                switch (Q) { case 0: Q = (uint8_t)(tmp1 == 0); break; case 1: Q = tmp1; break; }
                break;
            }
            break;
        case 1: switch (M) {
            case 0: tmp0 = R[n]; R[n] += tmp2; tmp1 = (R[n] < tmp0);
                switch (Q) { case 0: Q = tmp1; break; case 1: Q = (uint8_t)(tmp1 == 0); break; }
                break;
            case 1: tmp0 = R[n]; R[n] -= tmp2; tmp1 = (R[n] > tmp0);
                switch (Q) { case 0: Q = (uint8_t)(tmp1 == 0); break; case 1: Q = tmp1; break; }
                break;
            }
            break;
        }
        T = (Q == M);
    }
    void DT(int n) { R[n]--; if (R[n] == 0) T = 1; else T = 0; }
    void EXTSB(int m, int n) { R[n] = R[m]; if ((R[m] & 0x00000080) == 0) R[n] &= 0x000000FF; else R[n] |= 0xFFFFFF00; }
    void EXTSW(int m, int n) { R[n] = R[m]; if ((R[m] & 0x00008000) == 0) R[n] &= 0x0000FFFF; else R[n] |= 0xFFFF0000; }
    void EXTUB(int m, int n) { R[n] = R[m]; R[n] &= 0x000000FF; }
    void EXTUW(int m, int n) { R[n] = R[m]; R[n] &= 0x0000FFFF; }
    void SWAPB(int m, int n) {
        uint32_t temp0, temp1;
        temp0 = R[m] & 0xFFFF0000; temp1 = (R[m] & 0x000000FF) << 8;
        R[n] = (R[m] & 0x0000FF00) >> 8; R[n] = R[n] | temp1 | temp0;
    }
    void SWAPW(int m, int n) { uint32_t temp; temp = (R[m] >> 16) & 0x0000FFFF; R[n] = R[m] << 16; R[n] |= temp; }
    void XTRCT(int m, int n) { uint32_t temp; temp = (R[m] << 16) & 0xFFFF0000; R[n] = (R[n] >> 16) & 0x0000FFFF; R[n] |= temp; }
    void MOV(int m, int n) { R[n] = R[m]; }
    void MOVT(int n) { R[n] = (0x00000001 & T); }   // [SR & 1 is T]
    void ROTL(int n) {
        if ((R[n] & 0x80000000) == 0) T = 0; else T = 1;
        R[n] <<= 1;
        if (T == 1) R[n] |= 0x00000001; else R[n] &= 0xFFFFFFFE;
    }
    void ROTR(int n) {
        if ((R[n] & 0x00000001) == 0) T = 0; else T = 1;
        R[n] >>= 1;
        if (T == 1) R[n] |= 0x80000000; else R[n] &= 0x7FFFFFFF;
    }
    void ROTCL(int n) {
        int32_t temp;
        if ((R[n] & 0x80000000) == 0) temp = 0; else temp = 1;
        R[n] <<= 1;
        if (T == 1) R[n] |= 0x00000001; else R[n] &= 0xFFFFFFFE;
        if (temp == 1) T = 1; else T = 0;
    }
    void ROTCR(int n) {
        int32_t temp;
        if ((R[n] & 0x00000001) == 0) temp = 0; else temp = 1;
        R[n] >>= 1;
        if (T == 1) R[n] |= 0x80000000; else R[n] &= 0x7FFFFFFF;
        if (temp == 1) T = 1; else T = 0;
    }
    void SHAL(int n) { if ((R[n] & 0x80000000) == 0) T = 0; else T = 1; R[n] <<= 1; }
    void SHAR(int n) {
        int32_t temp;
        if ((R[n] & 0x00000001) == 0) T = 0; else T = 1;
        if ((R[n] & 0x80000000) == 0) temp = 0; else temp = 1;
        R[n] >>= 1;
        if (temp == 1) R[n] |= 0x80000000; else R[n] &= 0x7FFFFFFF;
    }
    void SHLL(int n) { if ((R[n] & 0x80000000) == 0) T = 0; else T = 1; R[n] <<= 1; }
    void SHLR(int n) { if ((R[n] & 0x00000001) == 0) T = 0; else T = 1; R[n] >>= 1; R[n] &= 0x7FFFFFFF; }
    void SHLL2(int n) { R[n] <<= 2; }
    void SHLR2(int n) { R[n] >>= 2; R[n] &= 0x3FFFFFFF; }
    void SHLL8(int n) { R[n] <<= 8; }
    void SHLR8(int n) { R[n] >>= 8; R[n] &= 0x00FFFFFF; }
    void SHLL16(int n) { R[n] <<= 16; }
    void SHLR16(int n) { R[n] >>= 16; R[n] &= 0x0000FFFF; }
    void SHAD(int m, int n) {
        int32_t sgn = R[m] & 0x80000000;
        if (sgn == 0) R[n] <<= (R[m] & 0x1F);
        else if ((R[m] & 0x1F) == 0) { if ((R[n] & 0x80000000) == 0) R[n] = 0; else R[n] = 0xFFFFFFFF; }
        else R[n] = (uint32_t)((int32_t)R[n] >> ((~R[m] & 0x1F) + 1));
    }
    void SHLD(int m, int n) {
        int32_t sgn = R[m] & 0x80000000;
        if (sgn == 0) R[n] <<= (R[m] & 0x1F);
        else if ((R[m] & 0x1F) == 0) R[n] = 0;
        else R[n] = (uint32_t)R[n] >> ((~R[m] & 0x1F) + 1);
    }
    void CLRT() { T = 0; }
    void SETT() { T = 1; }

    // ---- multiply (the operands are passed as values; MAC's Read_Word / Read_Long results) ----
    void MULL(int m, int n) { MACL = R[n] * R[m]; }
    void MULS(int m, int n) { MACL = (uint32_t)((int32_t)(int16_t)R[n] * (int32_t)(int16_t)R[m]); }
    void MULU(int m, int n) { MACL = (uint32_t)(uint16_t)R[n] * (uint32_t)(uint16_t)R[m]; }
    void DMULU(int m, int n) {
        uint32_t RnL, RnH, RmL, RmH, Res0, Res1, Res2, temp0, temp1, temp2, temp3;
        RnL = R[n] & 0x0000FFFF; RnH = (R[n] >> 16) & 0x0000FFFF;
        RmL = R[m] & 0x0000FFFF; RmH = (R[m] >> 16) & 0x0000FFFF;
        temp0 = RmL * RnL; temp1 = RmH * RnL; temp2 = RmL * RnH; temp3 = RmH * RnH;
        Res2 = 0; Res1 = temp1 + temp2;
        if (Res1 < temp1) Res2 += 0x00010000;
        temp1 = (Res1 << 16) & 0xFFFF0000; Res0 = temp0 + temp1;
        if (Res0 < temp0) Res2++;
        Res2 = Res2 + ((Res1 >> 16) & 0x0000FFFF) + temp3;
        MACH = Res2; MACL = Res0;
    }
    // the signed 32 x 32 core shared by DMULS.L and MAC.L (the manual repeats it in both)
    static void smul64(int32_t tempn, int32_t tempm, uint32_t& Res2o, uint32_t& Res0o) {
        uint32_t RnL, RnH, RmL, RmH, Res0, Res1, Res2, temp0, temp1, temp2, temp3; int32_t fnLmL;
        if ((int32_t)(tempn ^ tempm) < 0) fnLmL = -1; else fnLmL = 0;
        if (tempn < 0) tempn = (int32_t)(0u - (uint32_t)tempn);
        if (tempm < 0) tempm = (int32_t)(0u - (uint32_t)tempm);
        temp1 = (uint32_t)tempn; temp2 = (uint32_t)tempm;
        RnL = temp1 & 0x0000FFFF; RnH = (temp1 >> 16) & 0x0000FFFF;
        RmL = temp2 & 0x0000FFFF; RmH = (temp2 >> 16) & 0x0000FFFF;
        temp0 = RmL * RnL; temp1 = RmH * RnL; temp2 = RmL * RnH; temp3 = RmH * RnH;
        Res2 = 0; Res1 = temp1 + temp2;
        if (Res1 < temp1) Res2 += 0x00010000;
        temp1 = (Res1 << 16) & 0xFFFF0000; Res0 = temp0 + temp1;
        if (Res0 < temp0) Res2++;
        Res2 = Res2 + ((Res1 >> 16) & 0x0000FFFF) + temp3;
        if (fnLmL < 0) {
            Res2 = ~Res2;
            if (Res0 == 0) Res2++;
            else Res0 = (~Res0) + 1;
        }
        Res2o = Res2; Res0o = Res0;
    }
    void DMULS(int m, int n) { uint32_t Res2, Res0; smul64((int32_t)R[n], (int32_t)R[m], Res2, Res0); MACH = Res2; MACL = Res0; }
    void MACL_(uint32_t memn, uint32_t memm) {   // MAC.L @Rm+,@Rn+ with tempn = (Rn), tempm = (Rm)
        uint32_t Res0, Res2;
        smul64((int32_t)memn, (int32_t)memm, Res2, Res0);
        if (S == 1) {
            Res0 = MACL + Res0;
            if (MACL > Res0) Res2++;
            // [fix] printed: "if (MACH&0x00008000); else Res2+=MACH|0xFFFF0000; Res2+=MACH&0x00007FFF;"
            if (MACH & 0x00008000) Res2 += MACH | 0xFFFF0000;
            else Res2 += MACH & 0x00007FFF;
            if (((int32_t)Res2 < 0) && (Res2 < 0xFFFF8000)) { Res2 = 0xFFFF8000; Res0 = 0x00000000; }
            if (((int32_t)Res2 > 0) && (Res2 > 0x00007FFF)) { Res2 = 0x00007FFF; Res0 = 0xFFFFFFFF; }
            MACH = (Res2 & 0x0000FFFF) | (MACH & 0xFFFF0000);
            MACL = Res0;
        } else {
            Res0 = MACL + Res0;
            if (MACL > Res0) Res2++;
            Res2 += MACH;
            MACH = Res2; MACL = Res0;
        }
    }
    void MACW(uint32_t memn, uint32_t memm) {    // MAC.W @Rm+,@Rn+ with tempn = (Rn), tempm = (Rm), words
        int32_t tempm, tempn, dest, src, ans; uint32_t templ;
        tempn = (int32_t)(uint16_t)memn; tempm = (int32_t)(uint16_t)memm;
        templ = MACL;
        tempm = ((int32_t)(int16_t)tempn * (int32_t)(int16_t)tempm);
        if ((int32_t)MACL >= 0) dest = 0; else dest = 1;
        if ((int32_t)tempm >= 0) { src = 0; tempn = 0; } else { src = 1; tempn = (int32_t)0xFFFFFFFF; }
        src += dest;
        MACL += tempm;
        if ((int32_t)MACL >= 0) ans = 0; else ans = 1;
        ans += dest;
        if (S == 1) {
            if (ans == 1) {
                if (src == 0) MACL = 0x7FFFFFFF;
                if (src == 2) MACL = 0x80000000;
                if (src == 0 || src == 2) MACH |= 1;   // [ARCH] "the LSB of the MACH register is set to 1"
            }
        } else {
            MACH += tempn;
            if (templ > MACL) MACH += 1;
        }
    }

    // one ALU op the way the core presents it: m / n are register numbers (Rm-side, Rn-side)
    void exec(int op, int m, int n) {
        switch (op) {
        case A_ADD: ADD(m, n); break;       case A_ADDC: ADDC(m, n); break;     case A_ADDV: ADDV(m, n); break;
        case A_SUB: SUB(m, n); break;       case A_SUBC: SUBC(m, n); break;     case A_SUBV: SUBV(m, n); break;
        case A_NEG: NEG(m, n); break;       case A_NEGC: NEGC(m, n); break;     case A_AND: AND(m, n); break;
        case A_OR: OR(m, n); break;         case A_XOR: XOR(m, n); break;       case A_NOT: NOT(m, n); break;
        case A_TST: TST(m, n); break;       case A_CMPEQ: CMPEQ(m, n); break;   case A_CMPHS: CMPHS(m, n); break;
        case A_CMPGE: CMPGE(m, n); break;   case A_CMPHI: CMPHI(m, n); break;   case A_CMPGT: CMPGT(m, n); break;
        case A_CMPPZ: CMPPZ(n); break;      case A_CMPPL: CMPPL(n); break;      case A_CMPSTR: CMPSTR(m, n); break;
        case A_DIV0S: DIV0S(m, n); break;   case A_DIV0U: DIV0U(); break;       case A_DIV1: DIV1(m, n); break;
        case A_DT: DT(n); break;            case A_EXTSB: EXTSB(m, n); break;   case A_EXTSW: EXTSW(m, n); break;
        case A_EXTUB: EXTUB(m, n); break;   case A_EXTUW: EXTUW(m, n); break;   case A_SWAPB: SWAPB(m, n); break;
        case A_SWAPW: SWAPW(m, n); break;   case A_XTRCT: XTRCT(m, n); break;   case A_MOV: MOV(m, n); break;
        case A_MOVT: MOVT(n); break;        case A_ROTL: ROTL(n); break;        case A_ROTR: ROTR(n); break;
        case A_ROTCL: ROTCL(n); break;      case A_ROTCR: ROTCR(n); break;      case A_SHAL: SHAL(n); break;
        case A_SHAR: SHAR(n); break;        case A_SHLL: SHLL(n); break;        case A_SHLR: SHLR(n); break;
        case A_SHLL2: SHLL2(n); break;      case A_SHLR2: SHLR2(n); break;      case A_SHLL8: SHLL8(n); break;
        case A_SHLR8: SHLR8(n); break;      case A_SHLL16: SHLL16(n); break;    case A_SHLR16: SHLR16(n); break;
        case A_SHAD: SHAD(m, n); break;     case A_SHLD: SHLD(m, n); break;     case A_CLRT: CLRT(); break;
        case A_SETT: SETT(); break;         case A_TAS: TAS(n); break;
        default: fprintf(stderr, "Man::exec: op %d\n", op); exit(2);
        }
    }
};

// which ops write T / Q+M (the manual's "T Bit" column; Q / M: DIV0S, DIV0U, DIV1) and Rn (Operation writes R[n])
static bool T_WR[A_NUM], QM_WR[A_NUM], RN_WR[A_NUM], BYTE_R[A_NUM];
static void init_tables() {
    for (int o : {A_ADDC, A_ADDV, A_SUBC, A_SUBV, A_NEGC, A_TST, A_CMPEQ, A_CMPHS, A_CMPGE, A_CMPHI, A_CMPGT, A_CMPPZ,
                  A_CMPPL, A_CMPSTR, A_DIV0S, A_DIV0U, A_DIV1, A_DT, A_ROTL, A_ROTR, A_ROTCL, A_ROTCR, A_SHAL, A_SHAR,
                  A_SHLL, A_SHLR, A_CLRT, A_SETT, A_TAS})
        T_WR[o] = true;
    for (int o : {A_DIV0S, A_DIV0U, A_DIV1}) QM_WR[o] = true;
    for (int o = 0; o < A_NUM; o++) RN_WR[o] = true;
    for (int o : {A_TST, A_CMPEQ, A_CMPHS, A_CMPGE, A_CMPHI, A_CMPGT, A_CMPPZ, A_CMPPL, A_CMPSTR, A_DIV0S, A_DIV0U,
                  A_CLRT, A_SETT})
        RN_WR[o] = false;
    BYTE_R[A_TAS] = true;   // only the stored byte r[7:0] is architectural
}

// the reference ALU: Man with Rm-side operand in R2 and Rn-side in R1 (r = R1 afterwards: Rn unchanged = b for the
// flag-only forms, which is also the RTL's contract)
static AluOut ref_alu(int op, uint32_t a, uint32_t b, int t, int q, int m) {
    Man x;
    x.R[2] = a; x.R[1] = b; x.T = t; x.Q = q; x.M = m;
    x.exec(op, 2, 1);
    if (!T_WR[op] && x.T != (uint32_t)t) { fprintf(stderr, "ref: %s changed T outside the table\n", ALU_OP_NAME[op]); exit(2); }
    if (!QM_WR[op] && (x.Q != (uint32_t)q || x.M != (uint32_t)m)) { fprintf(stderr, "ref: %s changed Q/M\n", ALU_OP_NAME[op]); exit(2); }
    if (!RN_WR[op] && x.R[1] != b) { fprintf(stderr, "ref: %s changed Rn\n", ALU_OP_NAME[op]); exit(2); }
    return {x.R[1], (int)x.T, (int)x.Q, (int)x.M, T_WR[op], QM_WR[op]};
}

static bool same(int op, const AluOut& x, const AluOut& y) {
    uint32_t mask = BYTE_R[op] ? 0xFFu : 0xFFFFFFFFu;
    return ((x.r ^ y.r) & mask) == 0 && x.t == y.t && x.q == y.q && x.m == y.m && x.wr_t == y.wr_t && x.wr_qm == y.wr_qm;
}
static void show(const char* what, int op, uint32_t a, uint32_t b, int t, int q, int m, const AluOut& got, const AluOut& exp) {
    printf("  MISMATCH %s %s a=%08x b=%08x tqm=%d%d%d: got r=%08x t%d q%d m%d wt%d wqm%d, want r=%08x t%d q%d m%d wt%d wqm%d\n",
           what, ALU_OP_NAME[op], a, b, t, q, m, got.r, got.t, got.q, got.m, got.wr_t, got.wr_qm, exp.r, exp.t, exp.q,
           exp.m, exp.wr_t, exp.wr_qm);
}

// ---------------- independent 128-bit multiply reference ----------------
static uint64_t formula_mul(int op, uint32_t a, uint32_t b, int s, uint64_t acc) {
    uint32_t mach = acc >> 32, macl = (uint32_t)acc;
    switch (op) {
    case M_MULL: return ((uint64_t)mach << 32) | (uint32_t)(a * b);
    case M_MULSW: return ((uint64_t)mach << 32) | (uint32_t)((int32_t)(int16_t)a * (int32_t)(int16_t)b);
    case M_MULUW: return ((uint64_t)mach << 32) | ((uint32_t)(uint16_t)a * (uint16_t)b);
    case M_DMULS: return (uint64_t)((int64_t)(int32_t)a * (int32_t)b);
    case M_DMULU: return (uint64_t)a * b;
    case M_MACW: {
        int64_t p = (int64_t)(int16_t)a * (int16_t)b;
        if (!s) return acc + (uint64_t)p;
        int64_t v = (int64_t)(int32_t)macl + p;
        if (v > INT32_MAX) return ((uint64_t)(mach | 1) << 32) | 0x7FFFFFFFu;
        if (v < INT32_MIN) return ((uint64_t)(mach | 1) << 32) | 0x80000000u;
        return ((uint64_t)mach << 32) | (uint32_t)v;
    }
    case M_MACL: {
        __int128 p = (__int128)(int32_t)a * (int32_t)b;
        if (!s) return acc + (uint64_t)p;
        __int128 acc48 = (__int128)((int64_t)(acc << 16) >> 16);
        __int128 v = acc48 + p, lo = -((__int128)1 << 47), hi = ((__int128)1 << 47) - 1;
        if (v < lo) v = lo;
        if (v > hi) v = hi;
        return (acc & 0xFFFF000000000000ull) | ((uint64_t)v & 0x0000FFFFFFFFFFFFull);
    }
    }
    return 0;
}
static uint64_t ref_mul(int op, uint32_t a, uint32_t b, int s, uint64_t acc) {
    Man x;
    x.MACH = acc >> 32; x.MACL = (uint32_t)acc; x.S = s; x.R[2] = a; x.R[1] = b;
    switch (op) {
    case M_MULL: x.MULL(2, 1); break;   case M_MULSW: x.MULS(2, 1); break;  case M_MULUW: x.MULU(2, 1); break;
    case M_DMULS: x.DMULS(2, 1); break; case M_DMULU: x.DMULU(2, 1); break;
    case M_MACW: x.MACW(b, a); break;   case M_MACL: x.MACL_(b, a); break;
    }
    return ((uint64_t)x.MACH << 32) | x.MACL;
}

// ---------------- 1. isa.py golden vectors ----------------
struct VecFile { std::vector<std::string> forms; std::vector<uint8_t> alu, mul; uint32_t nalu = 0, nmul = 0; };
static bool load_vectors(const char* path, VecFile& v) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    std::vector<uint8_t> d;
    uint8_t buf[1 << 16]; size_t k;
    while ((k = fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + k);
    fclose(f);
    size_t p = 0;
    auto u32 = [&]() { uint32_t x; memcpy(&x, &d[p], 4); p += 4; return x; };
    if (d.size() < 8 || memcmp(&d[0], "V1AL", 4)) return false;
    p = 4;
    uint32_t nf = u32();
    for (uint32_t i = 0; i < nf; i++) { int len = d[p++]; v.forms.emplace_back((const char*)&d[p], len); p += len; }
    v.nalu = u32(); v.alu.assign(d.begin() + p, d.begin() + p + (size_t)v.nalu * 20); p += (size_t)v.nalu * 20;
    v.nmul = u32(); v.mul.assign(d.begin() + p, d.begin() + p + (size_t)v.nmul * 28);
    return true;
}

static void check_isa(const VecFile& v) {
    printf("== 1. isa.py golden vectors (%u ALU, %u MUL) -> RTL and -> manual C port ==\n", v.nalu, v.nmul);
    std::vector<long> cnt(v.forms.size()), bad_rtl(v.forms.size()), bad_man(v.forms.size());
    long shown = 0, contract = 0;
    for (uint32_t i = 0; i < v.nalu; i++) {
        const uint8_t* e = &v.alu[(size_t)i * 20];
        uint16_t form; uint32_t a, b, r;
        memcpy(&form, e, 2); memcpy(&a, e + 4, 4); memcpy(&b, e + 8, 4); memcpy(&r, e + 12, 4);
        int op = e[2], fin = e[3], fexp = e[16];
        int t = fin & 1, q = (fin >> 1) & 1, m = (fin >> 2) & 1;
        bool wr_r = (fexp >> 5) & 1, rbyte = (fexp >> 6) & 1;
        AluOut exp = {r, fexp & 1, (fexp >> 1) & 1, (fexp >> 2) & 1, (fexp >> 3) & 1, (fexp >> 4) & 1};
        cnt[form]++;
        for (int who = 0; who < 2; who++) {
            AluOut got = who ? ref_alu(op, a, b, t, q, m) : rtl_alu(op, a, b, t, q, m);
            uint32_t mask = rbyte ? 0xFFu : 0xFFFFFFFFu;
            bool ok = (!wr_r || ((got.r ^ exp.r) & mask) == 0) && got.wr_t == exp.wr_t && got.wr_qm == exp.wr_qm &&
                      (!exp.wr_t || got.t == exp.t) && (!exp.wr_qm || (got.q == exp.q && got.m == exp.m));
            // pass-through contract: flags not written keep their value, and a form without a GPR result gives r = b
            bool pass = (exp.wr_t || got.t == t) && (exp.wr_qm || (got.q == q && got.m == m)) && (wr_r || got.r == b);
            if (!pass && !who) contract++;
            if (!ok || !pass) {
                (who ? bad_man : bad_rtl)[form]++;
                if (shown++ < 20) show(who ? "isa/man" : "isa/rtl", op, a, b, t, q, m, got, exp), printf("    form %s\n", v.forms[form].c_str());
            }
        }
    }
    long tot = 0, tbad = 0;
    std::vector<long> mcnt(v.forms.size()), mbad(v.forms.size());
    for (uint32_t i = 0; i < v.nmul; i++) {
        const uint8_t* e = &v.mul[(size_t)i * 28];
        uint16_t form; uint32_t a, b; uint64_t acc, res;
        memcpy(&form, e, 2); memcpy(&a, e + 4, 4); memcpy(&b, e + 8, 4); memcpy(&acc, e + 12, 8); memcpy(&res, e + 20, 8);
        int op = e[2], s = e[3];
        uint64_t g1 = rtl_mul(op, a, b, s, acc), g2 = ref_mul(op, a, b, s, acc);
        mcnt[form]++;
        if (g1 != res || g2 != res) {
            mbad[form]++;
            if (shown++ < 20) printf("  MISMATCH isa %s a=%08x b=%08x s=%d acc=%016llx: rtl %016llx man %016llx want %016llx\n",
                                     MUL_OP_NAME[op], a, b, s, (unsigned long long)acc, (unsigned long long)g1,
                                     (unsigned long long)g2, (unsigned long long)res);
        }
    }
    for (size_t f = 0; f < v.forms.size(); f++) {
        long n = cnt[f] + mcnt[f], b1 = bad_rtl[f] + mbad[f], b2 = bad_man[f];
        printf("  %-22s %7ld vectors  rtl %ld  man %ld mismatches\n", v.forms[f].c_str(), n, b1, b2);
        tot += n; tbad += b1 + b2;
    }
    printf("  total %ld vectors (%zu forms), %ld mismatches (%ld pass-through contract)\n", tot, v.forms.size(), tbad, contract);
    g_fail += tbad;
}

// ---------------- 2. RTL vs the manual port, random + edge, per op ----------------
static void check_random(long per_op) {
    printf("== 2. RTL vs manual C port: %ld random + edge vectors per ALU op ==\n", per_op);
    long tot = 0, tbad = 0;
    for (int op = 0; op < A_NUM; op++) {
        long bad = 0, n = 0;
        // every edge pair with every flag combination first, then random
        for (int i = 0; i < NEDGE; i++)
            for (int j = 0; j < NEDGE; j++)
                for (int f3 = 0; f3 < 8; f3++, n++) {
                    uint32_t a = EDGES[i], b = EDGES[j]; int t = f3 & 1, q = (f3 >> 1) & 1, m = f3 >> 2;
                    AluOut g = rtl_alu(op, a, b, t, q, m), e = ref_alu(op, a, b, t, q, m);
                    if (!same(op, g, e)) { if (bad++ < 5) show("rand", op, a, b, t, q, m, g, e); }
                }
        for (; n < per_op; n++) {
            uint32_t a = gen32(), b = gen_b(a); int t = rndn(2), q = rndn(2), m = rndn(2);
            if (rndn(2)) std::swap(a, b);
            AluOut g = rtl_alu(op, a, b, t, q, m), e = ref_alu(op, a, b, t, q, m);
            if (!same(op, g, e)) { if (bad++ < 5) show("rand", op, a, b, t, q, m, g, e); }
        }
        printf("  %-9s %8ld vectors, %ld mismatches\n", ALU_OP_NAME[op], n, bad);
        tot += n; tbad += bad;
    }
    printf("  total %ld vectors, %ld mismatches\n", tot, tbad);
    g_fail += tbad;
}

// ---------------- 3. sequences ----------------
// a register machine that executes ALU forms through the RTL (the core's write-back rules: Rn if the form writes
// it, T if wr_t, Q / M if wr_qm), and the same machine on the manual port, stepped in lock step
struct RtlMach {
    uint32_t R[16] = {}; int T = 0, Q = 0, M = 0;
    void op(int o, int m, int n) {
        AluOut r = rtl_alu(o, R[m], R[n], T, Q, M);
        if (RN_WR[o]) R[n] = r.r;
        if (r.wr_t) T = r.t;
        if (r.wr_qm) { Q = r.q; M = r.m; }
    }
};
struct Pair {
    RtlMach h; Man s; long steps = 0, diverged = 0;
    void set(int i, uint32_t v) { h.R[i] = v; s.R[i] = v; }
    void op(int o, int m, int n) {
        h.op(o, m, n); s.exec(o, m, n); steps++;
        bool ok = h.T == (int)s.T && h.Q == (int)s.Q && h.M == (int)s.M;
        for (int i = 0; i < 16; i++) ok &= h.R[i] == s.R[i];
        if (!ok) diverged++;
    }
    uint32_t R(int i) const { return h.R[i]; }
    int T() const { return h.T; }
};

static void check_div(long per_ex) {
    printf("== 3a. DIV0U / DIV0S + DIV1 + ROTCL long division (manual 10.19 examples 1-4) vs host division ==\n");
    const int R0 = 0, R1 = 1, R2 = 2, R3 = 3;
    long tb = 0;
    // Example 1: R1 (32 bits) / R0 (16 bits) = R1 (16 bits), unsigned; zero-divisor and overflow checks exercised
    {
        long n = 0, bad = 0, zero = 0, ovf = 0, steps = 0, div = 0;
        for (; n < per_ex; n++) {
            uint32_t dd = gen32(), dv = n < 4 ? 0 : gen32();
            if (rndn(2)) dd = (uint32_t)(((uint64_t)rnd32() * (dv & 0xFFFF)) >> 16) + rndn(3);  // mostly in range
            Pair p; p.set(R0, dv); p.set(R1, dd);
            p.op(A_SHLL16, R0, R0);
            p.op(A_TST, R0, R0);
            bool z = (dv & 0xFFFF) == 0;
            if (p.T() != (int)z) bad++;
            if (p.T()) { zero++; steps += p.steps; div += p.diverged; continue; }
            p.op(A_CMPHS, R0, R1);
            bool o = dd >= ((dv & 0xFFFF) << 16);
            if (p.T() != (int)o) bad++;
            if (p.T()) { ovf++; steps += p.steps; div += p.diverged; continue; }
            p.op(A_DIV0U, 0, 0);
            for (int k = 0; k < 16; k++) p.op(A_DIV1, R0, R1);
            p.op(A_ROTCL, R1, R1);
            p.op(A_EXTUW, R1, R1);
            uint32_t want = dd / (dv & 0xFFFF);
            if (p.R(R1) != want) { if (bad++ < 5) printf("  MISMATCH ex1 %08x / %04x = %08x want %08x\n", dd, dv & 0xFFFF, p.R(R1), want); }
            steps += p.steps; div += p.diverged;
        }
        printf("  ex1 32/16 unsigned  %7ld cases (%ld zero-div, %ld overflow flagged), %8ld ALU steps, %ld wrong, %ld rtl/man divergences\n",
               n, zero, ovf, steps, bad, div);
        tb += bad + div;
    }
    // Example 2: R1:R2 (64 bits) / R0 (32 bits) = R2 (32 bits), unsigned
    {
        long n = 0, bad = 0, zero = 0, ovf = 0, steps = 0, div = 0;
        for (; n < per_ex; n++) {
            uint32_t dv = n < 4 ? 0 : gen32(), hi = gen32(), lo = gen32();
            if (rndn(8)) hi = dv ? (uint32_t)(((uint64_t)rnd32() * dv) >> 32) : 0;   // mostly in range
            if (n % 97 == 5 && dv) hi = dv - 1;                                    // largest quotients
            Pair p; p.set(R0, dv); p.set(R1, hi); p.set(R2, lo);
            p.op(A_TST, R0, R0);
            if (p.T() != (dv == 0)) bad++;
            if (p.T()) { zero++; steps += p.steps; div += p.diverged; continue; }
            p.op(A_CMPHS, R0, R1);
            if (p.T() != (hi >= dv)) bad++;
            if (p.T()) { ovf++; steps += p.steps; div += p.diverged; continue; }
            p.op(A_DIV0U, 0, 0);
            for (int k = 0; k < 32; k++) { p.op(A_ROTCL, R2, R2); p.op(A_DIV1, R0, R1); }
            p.op(A_ROTCL, R2, R2);
            uint64_t dd = ((uint64_t)hi << 32) | lo;
            uint32_t want = (uint32_t)(dd / dv);
            if (p.R(R2) != want) { if (bad++ < 5) printf("  MISMATCH ex2 %016llx / %08x = %08x want %08x\n", (unsigned long long)dd, dv, p.R(R2), want); }
            steps += p.steps; div += p.diverged;
        }
        printf("  ex2 64/32 unsigned  %7ld cases (%ld zero-div, %ld overflow flagged), %8ld ALU steps, %ld wrong, %ld rtl/man divergences\n",
               n, zero, ovf, steps, bad, div);
        tb += bad + div;
    }
    // Example 3: R1 (16 bits) / R0 (16 bits) = R1 (16 bits), signed (upper halves of both inputs are garbage)
    {
        long n = 0, bad = 0, steps = 0, div = 0, skip = 0;
        for (long i = 0; i < per_ex; i++) {
            uint32_t dd = gen32(), dv = gen32();
            if (i < 65536) dv = (dv & 0xFFFF0000) | (uint32_t)(i & 0xFFFF), dd = (dd & 0xFFFF0000) | (EDGES[i % NEDGE] & 0xFFFF);
            int16_t a16 = (int16_t)dd, b16 = (int16_t)dv;
            if (b16 == 0 || (a16 == INT16_MIN && b16 == -1)) { skip++; continue; }
            Pair p; p.set(R0, dv); p.set(R1, dd); p.set(R2, rnd32()); p.set(R3, rnd32());
            p.op(A_SHLL16, R0, R0);
            p.op(A_EXTSW, R1, R1);
            p.op(A_XOR, R2, R2);
            p.op(A_MOV, R1, R3);
            p.op(A_ROTCL, R3, R3);
            p.op(A_SUBC, R2, R1);
            p.op(A_DIV0S, R0, R1);
            for (int k = 0; k < 16; k++) p.op(A_DIV1, R0, R1);
            p.op(A_EXTSW, R1, R1);
            p.op(A_ROTCL, R1, R1);
            p.op(A_ADDC, R2, R1);
            p.op(A_EXTSW, R1, R1);
            uint32_t want = (uint32_t)(int32_t)(a16 / b16);
            n++;
            if (p.R(R1) != want) { if (bad++ < 5) printf("  MISMATCH ex3 %d / %d = %08x want %08x\n", a16, b16, p.R(R1), want); }
            steps += p.steps; div += p.diverged;
        }
        printf("  ex3 16/16 signed    %7ld cases (+%ld /0 or -32768/-1 skipped), %8ld ALU steps, %ld wrong, %ld rtl/man divergences\n",
               n, skip, steps, bad, div);
        tb += bad + div;
    }
    // Example 4: R2 (32 bits) / R0 (32 bits) = R2 (32 bits), signed
    {
        long n = 0, bad = 0, steps = 0, div = 0, skip = 0;
        for (long i = 0; i < per_ex; i++) {
            uint32_t dd = gen32(), dv = gen32();
            if (i < NEDGE * NEDGE) dd = EDGES[i / NEDGE], dv = EDGES[i % NEDGE];
            else if (rndn(4) == 0) dv = gen32() >> rndn(32);                  // small divisors: big quotients
            int32_t a32 = (int32_t)dd, b32 = (int32_t)dv;
            if (b32 == 0 || (a32 == INT32_MIN && b32 == -1)) { skip++; continue; }
            Pair p; p.set(R0, dv); p.set(R1, rnd32()); p.set(R2, dd); p.set(R3, rnd32());
            p.op(A_MOV, R2, R3);
            p.op(A_ROTCL, R3, R3);
            p.op(A_SUBC, R1, R1);
            p.op(A_XOR, R3, R3);
            p.op(A_SUBC, R3, R2);
            p.op(A_DIV0S, R0, R1);
            for (int k = 0; k < 32; k++) { p.op(A_ROTCL, R2, R2); p.op(A_DIV1, R0, R1); }
            p.op(A_ROTCL, R2, R2);
            p.op(A_ADDC, R3, R2);
            uint32_t want = (uint32_t)(a32 / b32);
            n++;
            if (p.R(R2) != want) { if (bad++ < 5) printf("  MISMATCH ex4 %d / %d = %08x want %08x\n", a32, b32, p.R(R2), want); }
            steps += p.steps; div += p.diverged;
        }
        printf("  ex4 32/32 signed    %7ld cases (+%ld /0 or INT_MIN/-1 skipped), %8ld ALU steps, %ld wrong, %ld rtl/man divergences\n",
               n, skip, steps, bad, div);
        tb += bad + div;
    }
    g_fail += tb;
}

static void check_chains(long per) {
    printf("== 3b. ADDC / SUBC / NEGC 128-bit carry chains vs __int128; ADDV / SUBV vs int64 ==\n");
    long tb = 0;
    for (int kind = 0; kind < 3; kind++) {
        long bad = 0, div = 0;
        for (long i = 0; i < per; i++) {
            uint32_t x[4], y[4];
            for (int k = 0; k < 4; k++) {   // limbs with carries that ripple: all-ones / zero limbs are frequent
                x[k] = rndn(3) ? gen32() : (rndn(2) ? 0xFFFFFFFFu : 0);
                y[k] = rndn(3) ? gen32() : (rndn(2) ? 0xFFFFFFFFu : 0);
            }
            if (i < 16) for (int k = 0; k < 4; k++) x[k] = (i & 1) ? 0xFFFFFFFFu : 0, y[k] = (i & 2) ? 0xFFFFFFFFu : (i & 4 ? 1 : 0);
            unsigned __int128 X = 0, Y = 0;
            for (int k = 3; k >= 0; k--) X = (X << 32) | x[k], Y = (Y << 32) | y[k];
            Pair p;
            p.set(15, rnd32());
            p.h.T = p.s.T = rndn(2);
            p.op(A_CLRT, 0, 0);
            unsigned __int128 W; int wt;
            for (int k = 0; k < 4; k++) {
                p.set(4 + k, x[k]); p.set(8 + k, y[k]);
                // ADDC / SUBC y,x: x = x +- y +- T; NEGC y,x: x = 0 - y - T
                p.op(kind == 0 ? A_ADDC : kind == 1 ? A_SUBC : A_NEGC, 8 + k, 4 + k);
            }
            if (kind == 0) { W = X + Y; wt = W < X; }
            else if (kind == 1) { W = X - Y; wt = X < Y; }
            else { W = (unsigned __int128)0 - Y; wt = Y != 0; }
            bool ok = p.T() == wt;
            for (int k = 0; k < 4; k++) ok &= p.R(4 + k) == (uint32_t)(W >> (32 * k));
            if (!ok && bad++ < 5) printf("  MISMATCH chain %d\n", kind);
            div += p.diverged;
        }
        printf("  %s 4-limb chains  %7ld cases, %ld wrong, %ld rtl/man divergences\n",
               kind == 0 ? "ADDC" : kind == 1 ? "SUBC" : "NEGC", per, bad, div);
        tb += bad + div;
    }
    for (int sub = 0; sub < 2; sub++) {
        long bad = 0, n = 0, ovf = 0;
        for (long i = 0; i < 4 * per; i++, n++) {
            uint32_t a = gen32(), b = gen_b(a);
            if (i < NEDGE * NEDGE) a = EDGES[i / NEDGE], b = EDGES[i % NEDGE];
            int64_t v = sub ? (int64_t)(int32_t)b - (int32_t)a : (int64_t)(int32_t)b + (int32_t)a;
            int o = v > INT32_MAX || v < INT32_MIN;
            ovf += o;
            int t = rndn(2);
            AluOut g = rtl_alu(sub ? A_SUBV : A_ADDV, a, b, t, 0, 0);
            if (g.r != (uint32_t)v || g.t != o || !g.wr_t) { if (bad++ < 5) printf("  MISMATCH %s %08x %08x\n", sub ? "SUBV" : "ADDV", a, b); }
        }
        printf("  %s            %7ld cases (%ld overflow), %ld wrong\n", sub ? "SUBV" : "ADDV", n, ovf, bad);
        tb += bad;
    }
    g_fail += tb;
}

static void check_shifts_cmpstr() {
    printf("== 3c. SHAD / SHLD every shift amount; CMP/STR every byte-equality pattern ==\n");
    long tb = 0;
    for (int shad = 0; shad < 2; shad++) {
        long bad = 0, n = 0;
        for (int sg = 0; sg < 2; sg++)
            for (int amt = 0; amt < 32; amt++)
                for (int k = 0; k < 16384; k++, n++) {
                    uint32_t a = (uint32_t)sg << 31 | amt | ((k & 1) ? (rnd32() & 0x7FFFFFE0u) : 0);
                    uint32_t b = k < 2 * NEDGE ? EDGES[k >> 1] : gen32();
                    uint32_t want;
                    if (!sg) want = b << amt;
                    else if (shad) want = amt ? (uint32_t)((int32_t)b >> (32 - amt)) : (uint32_t)((int32_t)b >> 31);
                    else want = amt ? b >> (32 - amt) : 0;
                    int t = rndn(2);
                    int op = shad ? A_SHAD : A_SHLD;
                    AluOut g = rtl_alu(op, a, b, t, 0, 0), e = ref_alu(op, a, b, t, 0, 0);
                    if (g.r != want || g.t != t || g.wr_t || !same(op, g, e)) {
                        if (bad++ < 5) printf("  MISMATCH %s a=%08x b=%08x got %08x want %08x\n", shad ? "SHAD" : "SHLD", a, b, g.r, want);
                    }
                }
        printf("  %s 64 amounts x 16384  %8ld vectors, %ld mismatches\n", shad ? "SHAD" : "SHLD", n, bad);
        tb += bad;
    }
    {
        long bad = 0, n = 0, hits[5] = {};
        for (int mask = 0; mask < 16; mask++)
            for (int k = 0; k < 65536; k++, n++) {
                uint32_t a = gen32(), d = 0;
                for (int by = 0; by < 4; by++)
                    if (!((mask >> by) & 1)) d |= (uint32_t)(rndn(2) ? (1u << rndn(8)) : 1 + rndn(255)) << (8 * by);
                uint32_t b = a ^ d;
                int t = rndn(2);
                AluOut g = rtl_alu(A_CMPSTR, a, b, t, 0, 0);
                int want = mask != 0;
                hits[__builtin_popcount(mask)]++;
                if (g.t != want || !g.wr_t || g.r != b) { if (bad++ < 5) printf("  MISMATCH CMP/STR a=%08x b=%08x T=%d\n", a, b, g.t); }
            }
        printf("  CMP/STR 16 patterns x 65536    %8ld vectors (0..4 equal bytes: %ld %ld %ld %ld %ld), %ld mismatches\n",
               n, hits[0], hits[1], hits[2], hits[3], hits[4], bad);
        tb += bad;
    }
    g_fail += tb;
}

// ---------------- 4. multiply / MAC ----------------
static void check_mul(long per) {
    printf("== 4. MUL / DMUL / MAC: RTL vs manual C port vs __int128 formula ==\n");
    long tot = 0, tbad = 0;
    for (int op = 0; op < M_NUM; op++)
        for (int s = 0; s < 2; s++) {
            bool mac = op == M_MACW || op == M_MACL;
            long bad = 0, n = 0, sat = 0;
            for (long i = 0; i < per; i++, n++) {
                uint32_t a = gen32(), b = gen32();
                uint64_t acc = ((uint64_t)rnd32() << 32) | rnd32();
                if (i < NEDGE * NEDGE) a = EDGES[i / NEDGE], b = EDGES[i % NEDGE];
                else if (mac && rndn(2)) {
                    // aim the sum at a limit +- 3: the S = 1 saturation bounds, or for S = 0 the MACL carry into
                    // MACH (MAC.W) and the 64-bit wrap (MAC.L)
                    if (op == M_MACL && rndn(2)) a = (uint32_t)((int32_t)a >> rndn(32));   // small products too
                    int64_t p = op == M_MACW ? (int64_t)(int16_t)a * (int16_t)b : (int64_t)(int32_t)a * (int32_t)b;
                    int64_t d = (int64_t)rndn(7) - 3;
                    if (op == M_MACW) {
                        int64_t bound = rndn(2) ? INT32_MAX : INT32_MIN;
                        acc = (acc & 0xFFFFFFFF00000000ull) | (uint32_t)(bound - p + d);
                    } else if (s) {
                        int64_t bound = rndn(2) ? ((int64_t)1 << 47) - 1 : -((int64_t)1 << 47);
                        int64_t acc48 = bound - p + d;
                        if (acc48 >= ((int64_t)1 << 47) || acc48 < -((int64_t)1 << 47)) acc48 = bound + d;  // |p| large
                        acc = (acc & 0xFFFF000000000000ull) | ((uint64_t)acc48 & 0x0000FFFFFFFFFFFFull);
                    } else {
                        acc = (rndn(2) ? 0x7FFFFFFFFFFFFFFFull : 0x8000000000000000ull) - (uint64_t)p + (uint64_t)d;
                    }
                }
                uint64_t g = rtl_mul(op, a, b, s, acc), e1 = ref_mul(op, a, b, s, acc), e2 = formula_mul(op, a, b, s, acc);
                if (mac && s) {
                    if (op == M_MACW) {
                        int64_t v = (int64_t)(int32_t)(uint32_t)acc + (int64_t)(int16_t)a * (int16_t)b;
                        sat += v > INT32_MAX || v < INT32_MIN;
                    } else {
                        __int128 v = (__int128)((int64_t)(acc << 16) >> 16) + (__int128)(int32_t)a * (int32_t)b;
                        sat += v >= ((__int128)1 << 47) || v < -((__int128)1 << 47);
                    }
                }
                if (g != e1 || g != e2) {
                    if (bad++ < 5) printf("  MISMATCH %s s=%d a=%08x b=%08x acc=%016llx: rtl %016llx man %016llx formula %016llx\n",
                                          MUL_OP_NAME[op], s, a, b, (unsigned long long)acc, (unsigned long long)g,
                                          (unsigned long long)e1, (unsigned long long)e2);
                }
            }
            if (mac && s) printf("  %-8s S=%d %8ld vectors (%ld saturated), %ld mismatches\n", MUL_OP_NAME[op], s, n, sat, bad);
            else printf("  %-8s S=%d %8ld vectors, %ld mismatches\n", MUL_OP_NAME[op], s, n, bad);
            tot += n; tbad += bad;
        }
    printf("  total %ld vectors, %ld mismatches\n", tot, tbad);
    g_fail += tbad;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    const char* vec = "../v1_alu/vectors.bin";
    long per_op = 1000000;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--vec") && i + 1 < argc) vec = argv[++i];
        else if (!strcmp(argv[i], "--n") && i + 1 < argc) per_op = atol(argv[++i]);
    }
    dut = new Vv1_alu_tb;
    init_tables();
    VecFile v;
    if (!load_vectors(vec, v)) { fprintf(stderr, "cannot read %s (run tb/v1_alu_tb.py)\n", vec); return 2; }
    check_isa(v);
    check_random(per_op);
    check_div(per_op / 4);
    check_chains(per_op / 4);
    check_shifts_cmpstr();
    check_mul(per_op);
    printf("v1_alu_tb: %s (%ld mismatches)\n", g_fail ? "FAIL" : "PASS", g_fail);
    delete dut;
    return g_fail ? 1 : 0;
}
