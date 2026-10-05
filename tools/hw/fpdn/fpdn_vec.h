/* fpdn_vec.h -- the FPSCR.DN = 0 probe's operations and operand generators (SOC.md 9.17, WI-FPDN).  Integer-only C99,
 * shared by the console program (tools/hw/fpdn/fpdn.c) and the host side (tb/v1_fpdn_tb.cpp), so the operands of
 * every record can be regenerated exactly from its indices.
 *
 * Part 1 (class table): every operation on every combination of a 14-value palette (normals, zeros, infinities, SH-4
 *   quiet / signalling NaNs, denormals, min / max normal) under the FPSCR settings P1_CFG.
 * Part 2 (values): operands whose exact result lies in or near the denormal range (gradual underflow, tininess at the
 *   minimum normal, deep underflow, cancellation), plus FCMP on denormals, under P2_CFG (DN = 0 RN / RZ, DN = 1 RN).
 *
 * Register use of every operation (the console stub loads FR0..FR3 and FPUL, runs the opcode, stores them back):
 *   single binary : FR0 = a (FRn), FR1 = b (FRm), opcode "op FR1,FR0"  (FR0 = FR0 op FR1; FCMP: T = FR0 == / > FR1)
 *   FMAC          : FR0 = a, FR1 = b, FR2 = c, "FMAC FR0,FR1,FR2"      (FR2 = FR0 * FR1 + FR2)
 *   single unary  : FR0 = a                                             (FTRC: FPUL = (int)FR0)
 *   double binary : DR0 = a (FR0 = high word), DR2 = b, "op DR2,DR0"   (DR0 = DR0 op DR2)
 *   double unary  : DR0 = a                                             (FTRC / FCNVDS: FPUL)
 *   FCNVSD        : FPUL = a (single)                                   (DR0 = (double)FPUL)
 */
#ifndef FPDN_VEC_H
#define FPDN_VEC_H
#include <stdint.h>

enum { K_BIN_S, K_MAC, K_UN_S, K_BIN_D, K_UN_D, K_CNVSD };
typedef struct { const char *name; uint16_t opw; uint8_t pr, kind; } fpdn_op_t;

/* part 1 operations */
enum { P_FADD_S, P_FSUB_S, P_FMUL_S, P_FDIV_S, P_FCMPEQ_S, P_FCMPGT_S, P_FMAC_S, P_FSQRT_S, P_FTRC_S, P_FABS_S, P_FNEG_S,
       P_FSRRA_S, P_FADD_D, P_FSUB_D, P_FMUL_D, P_FDIV_D, P_FCMPEQ_D, P_FCMPGT_D, P_FSQRT_D, P_FTRC_D, P_FABS_D, P_FNEG_D,
       P_FCNVDS, P_FCNVSD, P_NOPS };
static const fpdn_op_t FPDN_OPS[P_NOPS] = {
    {"FADD", 0xF010, 0, K_BIN_S},  {"FSUB", 0xF011, 0, K_BIN_S},  {"FMUL", 0xF012, 0, K_BIN_S},
    {"FDIV", 0xF013, 0, K_BIN_S},  {"FCMP/EQ", 0xF014, 0, K_BIN_S}, {"FCMP/GT", 0xF015, 0, K_BIN_S},
    {"FMAC", 0xF21E, 0, K_MAC},    {"FSQRT", 0xF06D, 0, K_UN_S},  {"FTRC", 0xF03D, 0, K_UN_S},
    {"FABS", 0xF05D, 0, K_UN_S},   {"FNEG", 0xF04D, 0, K_UN_S},   {"FSRRA", 0xF07D, 0, K_UN_S},
    {"FADD.D", 0xF020, 1, K_BIN_D}, {"FSUB.D", 0xF021, 1, K_BIN_D}, {"FMUL.D", 0xF022, 1, K_BIN_D},
    {"FDIV.D", 0xF023, 1, K_BIN_D}, {"FCMP/EQ.D", 0xF024, 1, K_BIN_D}, {"FCMP/GT.D", 0xF025, 1, K_BIN_D},
    {"FSQRT.D", 0xF06D, 1, K_UN_D}, {"FTRC.D", 0xF03D, 1, K_UN_D}, {"FABS.D", 0xF05D, 1, K_UN_D},
    {"FNEG.D", 0xF04D, 1, K_UN_D},  {"FCNVDS", 0xF0BD, 1, K_UN_D}, {"FCNVSD", 0xF0AD, 1, K_CNVSD} };

#define FPDN_NPAL 14
static const uint32_t FPDN_PAL_S[FPDN_NPAL] = {
    0x3FC00000u, 0xC0400000u, 0x00000000u, 0x80000000u, 0x7F800000u, 0xFF800000u, 0x7F800001u, 0x7FC00000u,
    0x00000001u, 0x00400000u, 0x007FFFFFu, 0x80400000u, 0x00800000u, 0x7F7FFFFFu };
static const uint64_t FPDN_PAL_D[FPDN_NPAL] = {
    0x3FF8000000000000ull, 0xC008000000000000ull, 0x0000000000000000ull, 0x8000000000000000ull,
    0x7FF0000000000000ull, 0xFFF0000000000000ull, 0x7FF0000000000001ull, 0x7FF8000000000000ull,
    0x0000000000000001ull, 0x0008000000000000ull, 0x000FFFFFFFFFFFFFull, 0x8008000000000000ull,
    0x0010000000000000ull, 0x7FEFFFFFFFFFFFFFull };
static const char *const FPDN_PAL_NAME[FPDN_NPAL] = {
    "+1.5", "-3", "+0", "-0", "+inf", "-inf", "qNaN", "sNaN", "+dmin", "+dmid", "+dmax", "-dmid", "+minN", "+max" };
static inline int fpdn_pal_den(int i) { return i >= 8 && i <= 11; }

/* part 1 FPSCR settings (PR is added for the double forms) */
#define FPDN_P1_NCFG 7
static const uint32_t FPDN_P1_CFG[FPDN_P1_NCFG] = {
    0x00000000u,   /* 0: DN = 0, no enables, RN */
    0x00000800u,   /* 1: DN = 0, EN.V */
    0x00000400u,   /* 2: DN = 0, EN.Z */
    0x00000080u,   /* 3: DN = 0, EN.I */
    0x0003F07Cu,   /* 4: DN = 0, every cause and flag preset */
    0x00040000u,   /* 5: DN = 1 (control) */
    0x00000001u }; /* 6: DN = 0, RZ */
/* the operand indices of part 1 row r of an operation: FMAC has 14^3 rows, binary forms 14^2, unary 14 */
static inline int fpdn_p1_rows(int op) {
    int k = FPDN_OPS[op].kind;
    return k == K_MAC ? FPDN_NPAL * FPDN_NPAL * FPDN_NPAL : (k == K_BIN_S || k == K_BIN_D) ? FPDN_NPAL * FPDN_NPAL : FPDN_NPAL;
}
/* sentinels in the registers an operation does not read */
#define FPDN_SENT_FR2 0x12345678u
#define FPDN_SENT_FR3 0x9ABCDEF0u
#define FPDN_SENT_UL  0x5A5A5A5Au

/* ---- the machine inputs of one operation: in[0..3] = FR0..FR3, in[4] = FPUL ------------------------------------- */
static inline void fpdn_load_s(uint32_t in[5], int kind, uint32_t a, uint32_t b, uint32_t c) {
    in[0] = FPDN_SENT_FR3; in[1] = FPDN_SENT_FR2; in[2] = FPDN_SENT_FR2; in[3] = FPDN_SENT_FR3; in[4] = FPDN_SENT_UL;
    if (kind == K_CNVSD) { in[4] = a; return; }
    in[0] = a;
    if (kind == K_BIN_S || kind == K_MAC) in[1] = b;
    if (kind == K_MAC) in[2] = c;
}
static inline void fpdn_load_d(uint32_t in[5], int kind, uint64_t a, uint64_t b) {
    in[0] = (uint32_t)(a >> 32); in[1] = (uint32_t)a; in[2] = FPDN_SENT_FR2; in[3] = FPDN_SENT_FR3; in[4] = FPDN_SENT_UL;
    if (kind == K_BIN_D) { in[2] = (uint32_t)(b >> 32); in[3] = (uint32_t)b; }
}
static inline void fpdn_p1_inputs(int op, int row, uint32_t in[5]) {
    int k = FPDN_OPS[op].kind, ia = row % FPDN_NPAL, ib = (row / FPDN_NPAL) % FPDN_NPAL, ic = row / (FPDN_NPAL * FPDN_NPAL);
    if (k == K_BIN_D || k == K_UN_D) fpdn_load_d(in, k, FPDN_PAL_D[ia], FPDN_PAL_D[ib]);
    else fpdn_load_s(in, k, FPDN_PAL_S[ia], FPDN_PAL_S[ib], FPDN_PAL_S[ic]);
}

/* ---- part 2: gradual-underflow value vectors ------------------------------------------------------------------------ */
enum { V_FMUL_S, V_FDIV_S, V_FMAC_S, V_FADD_S, V_FSUB_S, V_FCNVDS, V_FMUL_D, V_FDIV_D, V_FADD_D, V_FSUB_D,
       V_FCMPEQ_S, V_FCMPGT_S, V_FCMPEQ_D, V_FCMPGT_D, V_NOPS };
static const uint8_t FPDN_VOP[V_NOPS] = { P_FMUL_S, P_FDIV_S, P_FMAC_S, P_FADD_S, P_FSUB_S, P_FCNVDS, P_FMUL_D, P_FDIV_D,
                                          P_FADD_D, P_FSUB_D, P_FCMPEQ_S, P_FCMPGT_S, P_FCMPEQ_D, P_FCMPGT_D };
#define FPDN_P2_N 4096
#define FPDN_P2_NCFG 3
static const uint32_t FPDN_P2_CFG[FPDN_P2_NCFG] = { 0x00000000u, 0x00000001u, 0x00040000u };   /* DN0 RN, DN0 RZ, DN1 RN */

static inline uint64_t fpdn_mix(uint64_t z) {
    z += 0x9e3779b97f4a7c15ull; z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull; z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}
static inline uint32_t fpdn_s(uint32_t s, uint32_t e, uint32_t m) { return (s << 31) | ((e & 0xFFu) << 23) | (m & 0x7FFFFFu); }
static inline uint64_t fpdn_d(uint32_t s, uint32_t e, uint64_t m) {
    return ((uint64_t)s << 63) | ((uint64_t)(e & 0x7FFu) << 52) | (m & 0xFFFFFFFFFFFFFull);
}
/* mantissa shapes: random, one bit, low ones, small, high ones */
static inline uint32_t fpdn_man_s(uint64_t r, int style) {
    switch (style % 5) {
    case 0: return (uint32_t)r & 0x7FFFFFu;
    case 1: return 1u << (uint32_t)(r % 23);
    case 2: return 0x7FFFFFu >> (uint32_t)(r % 23);
    case 3: return (uint32_t)r & 0xFFu;
    default: return (0x7FFFFFu << (uint32_t)(r % 23)) & 0x7FFFFFu;
    }
}
static inline uint64_t fpdn_man_d(uint64_t r, int style) {
    const uint64_t M = 0xFFFFFFFFFFFFFull;
    switch (style % 5) {
    case 0: return r & M;
    case 1: return 1ull << (r % 52);
    case 2: return M >> (r % 52);
    case 3: return r & 0xFFFFull;
    default: return (M << (r % 52)) & M;
    }
}
/* one value from a class mix (FCMP): random denormal, signed zero, small normal, or a near neighbour of `near` */
static inline uint32_t fpdn_cls_s(uint64_t r) {
    uint32_t s = (uint32_t)(r >> 63), k = (uint32_t)(r >> 60) & 7;
    if (k < 4) { uint32_t m = fpdn_man_s(r >> 8, (int)((r >> 4) & 7)); return fpdn_s(s, 0, m ? m : 1); }
    if (k == 4) return s << 31;
    return fpdn_s(s, 1 + (uint32_t)((r >> 40) & 1), fpdn_man_s(r >> 8, (int)((r >> 4) & 7)));
}
static inline uint64_t fpdn_cls_d(uint64_t r) {
    uint32_t s = (uint32_t)(r >> 63), k = (uint32_t)(r >> 60) & 7;
    uint64_t m = fpdn_man_d(fpdn_mix(r), (int)((r >> 4) & 7));
    if (k < 4) return fpdn_d(s, 0, m ? m : 1);
    if (k == 4) return (uint64_t)s << 63;
    return fpdn_d(s, 1 + (uint32_t)((r >> 40) & 1), m);
}

/* the machine inputs of part 2 vector idx of value operation v */
static inline void fpdn_p2_inputs(int v, uint32_t idx, uint32_t in[5]) {
    uint64_t r0 = fpdn_mix(((uint64_t)(v + 1) << 40) ^ idx), r1 = fpdn_mix(r0), r2 = fpdn_mix(r1), r3 = fpdn_mix(r2);
    uint32_t mode = idx & 3, sa = (uint32_t)(r1 >> 63), sb = (uint32_t)(r2 >> 63), sc = (uint32_t)(r3 >> 63);
    int kind = FPDN_OPS[FPDN_VOP[v]].kind;
    uint32_t a = 0, b = 0, c = 0; uint64_t da = 0, db = 0;
    switch (v) {
    case V_FMUL_S: case V_FMAC_S: {
        /* product 1.ma * 1.mb * 2^(ea + eb - 254): the biased result exponent er = ea + eb - 127 (+1 on a carry) */
        int er, ea, eb;
        uint32_t ma = fpdn_man_s(r1 >> 8, (int)(r0 % 5)), mb = fpdn_man_s(r2 >> 8, (int)((r0 >> 8) % 5));
        if (mode == 2) {                                        /* just below the minimum normal: (2 - k ulp) x 2^-127 */
            ea = 1 + (int)(r3 % 126); eb = 127 - ea;
            ma = 0x7FFFFFu - (uint32_t)((r1 >> 8) & 0xFF); mb = ((r2 >> 8) & 3) ? 0 : (uint32_t)((r2 >> 16) & 7);
        } else {
            er = mode == 3 ? -40 + (int)(r3 % 21) : -26 + (int)(r3 % 30);
            if (v == V_FMAC_S && mode == 1) er = 1 + (int)(r3 % 20);           /* a normal product for the cancellation */
            ea = 1 + (int)((r3 >> 16) % (uint32_t)(er + 126 < 254 ? er + 126 : 253));
            eb = er + 127 - ea;
            if (eb < 1) eb = 1;
            if (eb > 254) eb = 254;
        }
        a = fpdn_s(sa, (uint32_t)ea, ma); b = fpdn_s(sb, (uint32_t)eb, mb);
        if (v == V_FMAC_S) {
            if (mode == 1) {                                    /* c = -(the product's top 24 bits) +- k ulps: cancellation */
                uint64_t P = (uint64_t)(0x800000u | ma) * (0x800000u | mb);
                int e = ea + eb - 127; uint32_t top;
                if (P >> 47) { top = (uint32_t)(P >> 24); e++; } else top = (uint32_t)(P >> 23);
                top = (uint32_t)((int32_t)top + (int32_t)(r2 % 5) - 2);
                if (!(top & 0x800000u)) top = 0x800000u;
                if (top > 0xFFFFFFu) top = 0xFFFFFFu;
                c = fpdn_s(!(sa ^ sb), (uint32_t)(e < 1 ? 1 : e > 254 ? 254 : e), top & 0x7FFFFFu);
            } else if (mode == 0) c = sc << 31;                 /* +-0: the fused product alone */
            else c = fpdn_s(sc, 1 + (uint32_t)((r3 >> 32) % 3), fpdn_man_s(r3 >> 8, (int)((r3 >> 40) % 5)));
        }
        break;
    }
    case V_FDIV_S: {
        int er, ea, eb;
        uint32_t ma = fpdn_man_s(r1 >> 8, (int)(r0 % 5)), mb = fpdn_man_s(r2 >> 8, (int)((r0 >> 8) % 5));
        if (mode == 2) {                                        /* quotients just below the minimum normal */
            if ((r3 >> 20) & 1) { ea = 1 + (int)(r3 % 126); eb = ea + 127; ma = 0x7FFFFFu - (uint32_t)((r1 >> 8) & 0xFF); mb = (uint32_t)((r2 >> 8) & 3); }
            else                { ea = 1 + (int)(r3 % 126); eb = ea + 126; ma = (uint32_t)((r1 >> 8) & 3); mb = 1 + (uint32_t)((r2 >> 8) & 0xFF); }
        } else {
            er = mode == 3 ? -40 + (int)(r3 % 21) : -26 + (int)(r3 % 30);
            ea = 1 + (int)((r3 >> 16) % (uint32_t)(127 + er));
            eb = ea + 127 - er;
        }
        a = fpdn_s(sa, (uint32_t)ea, ma); b = fpdn_s(sb, (uint32_t)eb, mb);
        break;
    }
    case V_FADD_S: case V_FSUB_S: {
        uint32_t ea = 1 + (uint32_t)(r3 % (mode == 3 ? 30 : 4)), ma = fpdn_man_s(r1 >> 8, (int)(r0 % 5));
        a = fpdn_s(sa, ea, ma);
        if (mode == 0) {                                        /* -(a +- k ulps): an exact tiny difference */
            uint32_t mag = (a & 0x7FFFFFFFu) + (uint32_t)(r2 % 15) - 7u;
            b = (mag & 0x7FFFFFFFu) | ((sa ^ 1u) << 31);
        } else if (mode == 2) {
            a = fpdn_s(sa, 1, ma); b = fpdn_s(sa ^ 1u, 1 + (uint32_t)((r2 >> 40) & 1), fpdn_man_s(r2 >> 8, (int)((r0 >> 8) % 5)));
        } else {
            uint32_t eb = mode == 3 ? (ea > 2 ? ea - (uint32_t)(r2 % 3) : ea) : 1 + (uint32_t)(r2 % 3);
            b = fpdn_s(mode == 3 ? sa ^ 1u : sb, eb, fpdn_man_s(r2 >> 8, (int)((r0 >> 8) % 5)));
        }
        if (v == V_FSUB_S) b ^= 0x80000000u;                    /* FSUB: the same differences */
        break;
    }
    case V_FCNVDS: {
        int er = mode == 3 ? -40 + (int)(r3 % 21) : mode == 2 ? 0 : -26 + (int)(r3 % 29);   /* the single's biased exponent */
        uint64_t m = fpdn_man_d(r1, (int)(r0 % 5));
        if (mode == 2) m = 0xFFFFFE0000000ull | ((r1 >> 8) & 0x1FFFFFFFull) | ((uint64_t)((r1 >> 40) & 1) << 28);
        da = fpdn_d(sa, (uint32_t)(er - 127 + 1023), m);
        break;
    }
    case V_FMUL_D: case V_FDIV_D: {
        int er, ea, eb;
        uint64_t ma = fpdn_man_d(r1, (int)(r0 % 5)), mb = fpdn_man_d(r2, (int)((r0 >> 8) % 5));
        er = mode == 3 ? -70 + (int)(r3 % 31) : -55 + (int)(r3 % 59);
        if (mode == 2) {
            er = v == V_FMUL_D ? 0 : 1;
            if (v == V_FMUL_D) { ma = 0xFFFFFFFFFFFFFull - ((r1 >> 8) & 0xFF); mb = ((r2 >> 8) & 3) ? 0 : ((r2 >> 16) & 7); }
            else               { ma = (r1 >> 8) & 3; mb = 1 + ((r2 >> 8) & 0xFF); }
        }
        if (v == V_FMUL_D) { ea = 1 + (int)((r3 >> 16) % (uint32_t)(er + 1022)); eb = er + 1023 - ea; }
        else               { ea = 1 + (int)((r3 >> 16) % (uint32_t)(1023 + er)); eb = ea + 1023 - er; }
        if (eb < 1) eb = 1;
        if (eb > 2046) eb = 2046;
        da = fpdn_d(sa, (uint32_t)ea, ma); db = fpdn_d(sb, (uint32_t)eb, mb);
        break;
    }
    case V_FADD_D: case V_FSUB_D: {
        uint32_t ea = 1 + (uint32_t)(r3 % (mode == 3 ? 30 : 4));
        uint64_t ma = fpdn_man_d(r1, (int)(r0 % 5));
        da = fpdn_d(sa, ea, ma);
        if (mode == 0) db = (((da & 0x7FFFFFFFFFFFFFFFull) + (r2 % 15) - 7ull) & 0x7FFFFFFFFFFFFFFFull) | ((uint64_t)(sa ^ 1u) << 63);
        else if (mode == 2) { da = fpdn_d(sa, 1, ma); db = fpdn_d(sa ^ 1u, 1 + (uint32_t)((r2 >> 40) & 1), fpdn_man_d(r2, (int)((r0 >> 8) % 5))); }
        else db = fpdn_d(mode == 3 ? sa ^ 1u : sb, mode == 3 ? (ea > 2 ? ea - (uint32_t)(r2 % 3) : ea) : 1 + (uint32_t)(r2 % 3),
                         fpdn_man_d(r2, (int)((r0 >> 8) % 5)));
        if (v == V_FSUB_D) db ^= 0x8000000000000000ull;
        break;
    }
    case V_FCMPEQ_S: case V_FCMPGT_S:
        a = fpdn_cls_s(r1);
        b = mode == 0 ? a : mode == 1 ? a + (uint32_t)(r2 % 3) - 1u : fpdn_cls_s(r2);
        break;
    default:   /* V_FCMPEQ_D, V_FCMPGT_D */
        da = fpdn_cls_d(r1);
        db = mode == 0 ? da : mode == 1 ? da + (r2 % 3) - 1ull : fpdn_cls_d(r2);
        break;
    }
    if (kind == K_BIN_D || kind == K_UN_D) fpdn_load_d(in, kind, da, db);
    else fpdn_load_s(in, kind, a, b, c);
}


/* ---- part 3 (run 2): the double forms on a palette that separates the two words (a fraction only in the low word,
 * only in the high word, or both) -- the double classes turned out to depend on the high word (fpdn_run1) ---------- */
#define FPDN_NPAL3 21
static const uint64_t FPDN_PAL3[FPDN_NPAL3] = {
    0x3FF8000000000000ull, 0xC008000000000000ull, 0x0000000000000000ull, 0x8000000000000000ull,
    0x7FF0000000000000ull, 0xFFF0000000000000ull, 0x7FF0000000000001ull, 0x7FF4000000000000ull,
    0x7FF8000000000000ull, 0x0000000000000001ull, 0x0000000080000000ull, 0x8000000000000001ull,
    0x0008000000000000ull, 0x0000000100000001ull, 0x8008000000000000ull, 0x3FF0000000000001ull,
    0x0010000000000000ull, 0xFFF0000080000000ull, 0x7FF8000000000001ull, 0x7FEFFFFFFFFFFFFFull,
    0x7FF0000100000001ull };
static const char *const FPDN_PAL3_NAME[FPDN_NPAL3] = {
    "+1.5", "-3", "+0", "-0", "+inf", "-inf", "qNaNlo", "qNaNhi", "sNaN", "+L1", "+L2", "-L1", "+H", "+HL", "-H",
    "1+ulp", "+minN", "-qNaNlo", "sNaNlo", "+max", "qNaNhl" };
#define FPDN_P3_NOPS 9
static const uint8_t FPDN_P3_OP[FPDN_P3_NOPS] = { P_FADD_D, P_FSUB_D, P_FMUL_D, P_FDIV_D, P_FCMPEQ_D, P_FCMPGT_D,
                                                  P_FSQRT_D, P_FTRC_D, P_FCNVDS };
#define FPDN_P3_NCFG 6
static const uint32_t FPDN_P3_CFG[FPDN_P3_NCFG] = { 0x00000000u, 0x00000800u, 0x00000400u, 0x00000080u, 0x00040000u,
                                                    0x00000001u };
static inline int fpdn_p3_rows(int op) { return FPDN_OPS[op].kind == K_BIN_D ? FPDN_NPAL3 * FPDN_NPAL3 : FPDN_NPAL3; }
static inline void fpdn_p3_inputs(int op, int row, uint32_t in[5]) {
    fpdn_load_d(in, FPDN_OPS[op].kind, FPDN_PAL3[row % FPDN_NPAL3], FPDN_PAL3[row / FPDN_NPAL3]);
}

/* one record of either part: the registers after the operation, FPSCR after, T, and the trap */
typedef struct {
    uint32_t fr[4], fpul, fpscr, t;   /* t: T after (the stub sets T = 1 before the operation) */
    uint32_t trap;                    /* 0 none, else the exception code (0x120 FPU, 0x180 illegal) */
} fpdn_rec_t;
#endif
