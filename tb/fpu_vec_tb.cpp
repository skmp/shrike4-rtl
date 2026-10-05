// fpu_vec_tb -- drives FIPR / FSCA / FSRRA through the shared datapath (rtl/fpu_vec) and checks:
//   FIPR : vs model/fipr/fipr_ref.h (bit-exact to 32M + 5.2M hardware vectors) on a sample of fipr_vecgen.h
//          vectors, back-to-back at pitch 1, RN and RZ; and vs the hardware dump itself where present.
//   FSCA : all 65536 angles, RN/RZ, sine/cosine: zero mismatches against fsca_ref2.h AND hardware.
//   FSRRA: stride over [1,4) against fsrra_ref.h and RZ/RN captures: zero mismatches.
// Also checks the latencies: FIPR result at +4, FSCA sine +6 / cosine +7, FSRRA +6 (cycle 0 = issue).
#include "Vfpu_vec_tb_top.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>
#include "../model/fipr/fipr_ref.h"
#include "../model/fipr/fipr_vecgen.h"
#include "../model/fsca/sh4_fsca.h"
#include "../model/fsrra/sh4_fsrra_words.h"

static Vfpu_vec_tb_top* dut;
static long g_cycle = 0;
static void tick() { dut->clk = 0; dut->eval(); dut->clk = 1; dut->eval(); g_cycle++; }
static const char* DUMP_DIR = "../../model/fipr/";

// ---------------- FSRRA structural reference (model/fsrra/tools/rr14_exact.cpp: bit-exact on all of [1,4)) ----------------
// W = C0 - T1 + T2 - T3, dx = t << 4, ONE square latch sq = (arr(dx,dx) + K) >> 23 feeding both the square product
// (14-bit FIXED-POINT field C2F << 9, aligner shift 29) and the cubic chain (9-bit field C3F << 8, I = >> 23, then dx x I).
static int64_t arr_ref(int64_t rec, int64_t mul) { return sh4_fsca__arr(rec, mul); }
static inline int64_t sm_ref(int64_t P, int u) { return sh4_fsca__sm(P, u); }
static int g_sqmode = 1;   // 1 = square through the array (what the RTL does); 0 = exact dx*dx (j1_base.cpp mode 0)
static uint32_t fsrra_ref(uint32_t x) {
    const int64_t K = 1ll << 19;
    uint32_t sign = x >> 31, exp = (x >> 23) & 0xff, man = x & 0x7fffff;
    if (exp == 0xff) { if (man) return 0x7fbfffffu; return sign ? 0x7fbfffffu : 0; }
    if (exp == 0) return sign << 31 | 0x7f800000u;
    if (sign) return 0x7fbfffffu;
    int e = (int)exp - 127, p = e & 1, k = (e - p) / 2;
    int64_t m = man, node = (m + (1 << 18)) >> 19, t = m - (node << 19);
    const FsrraWord& T = FSRRA_WORDS[p * 17 + node];
    int64_t dx = t * 16, sqraw = g_sqmode ? arr_ref(dx, dx) : dx * dx, sq = (sqraw + K) >> 23;
    int64_t W = T.C0 - sm_ref(arr_ref(T.REC1, dx) + K, 26 - T.E1) + sm_ref(arr_ref((int64_t)T.C2F << 9, sq) + K, 29)
                     - sm_ref(arr_ref(dx, (arr_ref((int64_t)T.C3F << 8, sq) + K) >> 23) + K, 27);
    // 2^-26 grid, value W * 2^-26 * 2^-k -> float (RNE with sticky; the tb only runs FSRRA in RN like the dumps)
    uint64_t mag = (uint64_t)W; int top = 63 - __builtin_clzll(mag), sh = top - 23; uint64_t q;
    if (sh > 0) { uint64_t r = mag & ((1ull << sh) - 1), h = 1ull << (sh - 1); q = mag >> sh; q += (r > h) || (r == h && (q & 1)); if (q >> 24) { q >>= 1; top++; } }
    else q = mag << (-sh);
    int re = top + (254 - k) - 280 + 127;
    return ((uint32_t)re << 23) | ((uint32_t)q & 0x7fffff);
}
static uint32_t fsrra_ref_rz(uint32_t x) {   // same with truncation (for the RZ dump)
    const int64_t K = 1ll << 19;
    uint32_t exp = (x >> 23) & 0xff, man = x & 0x7fffff;
    int e = (int)exp - 127, p = e & 1, k = (e - p) / 2;
    int64_t m = man, node = (m + (1 << 18)) >> 19, t = m - (node << 19);
    const FsrraWord& T = FSRRA_WORDS[p * 17 + node];
    int64_t dx = t * 16, sqraw = g_sqmode ? arr_ref(dx, dx) : dx * dx, sq = (sqraw + K) >> 23;
    int64_t W = T.C0 - sm_ref(arr_ref(T.REC1, dx) + K, 26 - T.E1) + sm_ref(arr_ref((int64_t)T.C2F << 9, sq) + K, 29)
                     - sm_ref(arr_ref(dx, (arr_ref((int64_t)T.C3F << 8, sq) + K) >> 23) + K, 27);
    uint64_t mag = (uint64_t)W; int top = 63 - __builtin_clzll(mag), sh = top - 23; uint64_t q = sh > 0 ? mag >> sh : mag << (-sh);
    int re = top + (254 - k) - 280 + 127;
    return ((uint32_t)re << 23) | ((uint32_t)q & 0x7fffff);
}

// ---------------- FSCA reference: model/fsca/sh4_fsca.h (bit-exact vs hardware; the RTL microprogram is this arithmetic) ----------------
static uint32_t fsca_rtl_sin(uint32_t fpul, int rz) { return sh4_fsca_sin(fpul, rz); }
static uint32_t fsca_rtl_cos(uint32_t fpul, int rz) { return sh4_fsca_cos(fpul, rz); }

// ---------------- driving ----------------
struct Res { uint32_t v; int second; long cycle; };
static std::vector<Res> results;
static void drain(int want) {
    // run until `want` results have been collected (max 16 cycles)
    for (int c = 0; c < 16 && (int)results.size() < want; c++) {
        dut->start = 0; tick();
        if (dut->res_valid) results.push_back({dut->res, dut->res_second, g_cycle});
    }
}
static long issue(int op, int rz, const uint32_t* a, const uint32_t* b, uint32_t fpul, uint32_t frn) {
    dut->op = op; dut->rz = rz;
    dut->fva0 = a ? a[0] : 0; dut->fva1 = a ? a[1] : 0; dut->fva2 = a ? a[2] : 0; dut->fva3 = a ? a[3] : 0;
    dut->fvb0 = b ? b[0] : 0; dut->fvb1 = b ? b[1] : 0; dut->fvb2 = b ? b[2] : 0; dut->fvb3 = b ? b[3] : 0;
    dut->fpul = fpul; dut->frn = frn;
    dut->start = 1; long issued = g_cycle; tick();   // cycle 0 (RR) ends here
    if (dut->res_valid) results.push_back({dut->res, dut->res_second, g_cycle});
    dut->start = 0;
    return issued;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    dut = new Vfpu_vec_tb_top;
    dut->clk = 0; dut->rst = 1; dut->start = 0; tick(); tick(); dut->rst = 0; tick();
    int fails = 0;

    // ============================ FIPR: one at a time, latency + value ============================
    {
        long bad = 0, n = 0, lat_bad = 0;
        for (uint32_t idx = 0; idx < FIPR_N; idx += 4099) {
            uint32_t a[4], b[4]; fipr_vec(idx, a, b);
            for (int rz = 0; rz < 2; rz++) {
                results.clear(); long t0 = issue(0, rz, a, b, 0, 0); drain(1);
                uint32_t ref = fipr_ref(a, b, rz);
                if (results.size() != 1 || results[0].v != ref) { if (bad < 5) printf("FIPR idx %u rz %d: rtl %08x ref %08x\n", idx, rz, results.empty() ? 0 : results[0].v, ref); bad++; }
                else if (results[0].cycle - t0 != 4) lat_bad++;
                n++;
            }
        }
        printf("FIPR single-issue: %ld vectors, %ld value mismatches vs fipr_ref, %ld latency != 4\n", n, bad, lat_bad);
        fails += bad != 0 || lat_bad != 0;
    }
    // ============================ FIPR: pitch-1 back-to-back ============================
    {
        const int N = 20000; std::vector<uint32_t> refs; results.clear();
        uint64_t s = 12345;
        for (int i = 0; i < N; i++) {
            uint32_t idx = (uint32_t)(fipr_mix(s++) % FIPR_N); uint32_t a[4], b[4]; fipr_vec(idx, a, b); int rz = (int)(fipr_mix(s) & 1);
            refs.push_back(fipr_ref(a, b, rz));
            issue(0, rz, a, b, 0, 0);
        }
        drain(N);
        long bad = 0; for (int i = 0; i < N && i < (int)results.size(); i++) bad += results[i].v != refs[i];
        printf("FIPR pitch-1 stream: %d issued, %zu results, %ld mismatches\n", N, results.size(), bad);
        fails += bad != 0 || (int)results.size() != N;
    }
    // ============================ FIPR: hardware dump sample ============================
    {
        std::string p = std::string(DUMP_DIR) + "kos_fipr_dump/fipr_rnrz.bin"; FILE* f = fopen(p.c_str(), "rb");
        if (!f) printf("FIPR hardware dump not found (%s), skipped\n", p.c_str());
        else {
            long bad = 0, n = 0;
            for (uint32_t idx = 0; idx < FIPR_N; idx += 2503) {
                uint32_t d[2]; fseek(f, (long)idx * 8, SEEK_SET); if (fread(d, 4, 2, f) != 2) break;
                uint32_t a[4], b[4]; fipr_vec(idx, a, b);
                for (int rz = 0; rz < 2; rz++) { results.clear(); issue(0, rz, a, b, 0, 0); drain(1);
                    if (results.size() != 1 || results[0].v != d[rz]) { if (bad < 5) printf("FIPR HW idx %u rz %d: rtl %08x hw %08x\n", idx, rz, results.empty() ? 0 : results[0].v, d[rz]); bad++; } n++; }
            }
            fclose(f);
            printf("FIPR vs hardware dump: %ld samples, %ld mismatches\n", n, bad);
            fails += bad != 0;
        }
    }
    // ============================ FSCA: all angles, both modes ============================
    {
        std::vector<uint32_t> hw(65536 * 4); bool have_hw = false;
        { std::string p = std::string(DUMP_DIR) + "kos_fpu_dump/fsca_rnrz.bin"; FILE* f = fopen(p.c_str(), "rb");
          if (f) { have_hw = fread(hw.data(), 4, hw.size(), f) == hw.size(); fclose(f); } }
        long bad_model = 0, bad_hw[2][2] = {{0,0},{0,0}}, lat_bad = 0;
        for (int rz = 0; rz < 2; rz++) for (uint32_t ang = 0; ang < 65536; ang++) {
            results.clear(); long t0 = issue(1, rz, 0, 0, ang | 0xabcd0000u, 0); drain(2);
            uint32_t rs = fsca_rtl_sin(ang, rz), rc = fsca_rtl_cos(ang, rz);
            if (results.size() != 2 || results[0].second != 0 || results[1].second != 1) { if (bad_model < 5) printf("FSCA ang %04x: bad result sequence (%zu)\n", ang, results.size()); bad_model++; continue; }
            if (results[0].v != rs || results[1].v != rc) { if (bad_model < 5) printf("FSCA ang %04x rz %d: rtl %08x %08x model %08x %08x\n", ang, rz, results[0].v, results[1].v, rs, rc); bad_model++; }
            if (results[0].cycle - t0 != 6 || results[1].cycle - t0 != 7) lat_bad++;
            if (have_hw) { uint32_t hs = hw[4*ang + (rz ? 2 : 0)], hc = hw[4*ang + (rz ? 3 : 1)]; bad_hw[rz][0] += results[0].v != hs; bad_hw[rz][1] += results[1].v != hc; }
        }
        printf("FSCA all 65536 angles x RN/RZ: %ld mismatches vs the FSCA exact model, %ld latency != (6,7)\n", bad_model, lat_bad);
        if (have_hw) printf("FSCA vs hardware dump: sin RN %ld RZ %ld, cos RN %ld RZ %ld  (must be zero)\n", bad_hw[0][0], bad_hw[1][0], bad_hw[0][1], bad_hw[1][1]);
        fails += bad_model != 0 || lat_bad != 0;
        if (have_hw) fails += (bad_hw[0][0] || bad_hw[0][1] || bad_hw[1][0] || bad_hw[1][1]);
    }
    // ============================ FSRRA: stride over [1,4) ============================
    {
        const uint32_t NA = 1u << 24; const int STRIDE = getenv("FSRRA_STRIDE") ? atoi(getenv("FSRRA_STRIDE")) : 61;   // FSRRA_STRIDE=1: every input
        std::vector<uint32_t> hwrz, hwrn; bool have_hw = false;
        { std::string p1 = std::string(DUMP_DIR) + "kos_fpu_dump/fsrra_rz.bin", p2 = std::string(DUMP_DIR) + "kos_fpu_dump/fsrra_rn_mantissa_1to4.bin";
          FILE* f1 = fopen(p1.c_str(), "rb"), *f2 = fopen(p2.c_str(), "rb");
          if (f1 && f2) { hwrz.resize(NA); hwrn.resize(NA); have_hw = fread(hwrz.data(), 4, NA, f1) == NA && fread(hwrn.data(), 4, NA, f2) == NA; }
          if (f1) fclose(f1); if (f2) fclose(f2); }
        long bad_model = 0, bad_rz = 0, bad_rn = 0, n = 0, lat_bad = 0;
        for (uint32_t i = 0; i < NA; i += STRIDE) {
            uint32_t x = 0x3f800000u + i;   // [1,4): exponents 127, 128
            for (int rz = 0; rz < 2; rz++) {
                results.clear(); long t0 = issue(2, rz, 0, 0, 0, x); drain(1);
                uint32_t ref = rz ? fsrra_ref_rz(x) : fsrra_ref(x);
                if (results.size() != 1 || results[0].v != ref) { if (bad_model < 5) printf("FSRRA x %08x rz %d: rtl %08x model %08x\n", x, rz, results.empty() ? 0 : results[0].v, ref); bad_model++; }
                else if (results[0].cycle - t0 != 6) lat_bad++;
                if (have_hw && !results.empty()) {
                    if (rz) bad_rz += (results[0].v & 0x7fffff) != (hwrz[i] & 0x7fffff) || ((results[0].v >> 23) != (hwrz[i] >> 23));
                    else    bad_rn += (results[0].v & 0x7fffff) != (hwrn[i] & 0x7fffff);   // RN file holds mantissas only
                }
            }
            n++;
        }
        printf("FSRRA stride %d over [1,4): %ld samples x RN/RZ, %ld mismatches vs the structural model, %ld latency != 6\n", STRIDE, n, bad_model, lat_bad);
        if (have_hw) printf("FSRRA vs hardware: RZ %ld (%.3f%%), RN mantissa %ld (%.3f%%)  (the model is bit-exact: must be 0)\n", bad_rz, 100.0 * bad_rz / n, bad_rn, 100.0 * bad_rn / n);
        fails += bad_model != 0 || lat_bad != 0 || bad_rz != 0 || bad_rn != 0;
    }
    printf(fails ? "FAIL\n" : "PASS\n");
    delete dut;
    return fails ? 1 : 0;
}
