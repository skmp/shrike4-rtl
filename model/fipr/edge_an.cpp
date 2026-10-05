#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <map>
#include <string>
#include "fipr_vecgen_edge.h"
typedef int64_t i64; typedef uint64_t u64;
static int cls(uint32_t x) { int e = (x >> 23) & 0xff; uint32_t m = x & 0x7fffff; if (e == 255) return m ? ((m & 0x400000) ? 3 : 4) : 2; if (e == 0) return m ? 1 : 0; return 5; }   // 0 zero 1 denorm 2 inf 3 qnan 4 snan 5 normal
int main() { u64 N = FIPRE_N; FILE* f = fopen("kos_fipr_edge/fipr_edge_rnrz.bin", "rb"); std::vector<uint32_t> D(N * 2); if (fread(D.data(), 8, N, f) != N) return 1; fclose(f); uint32_t a[4], b[4];
    // A: G0 by exact result exponent: product exponent E = ea+eb-127 (biased of the product before normalisation); hw rn/rz classes
    std::map<int, std::map<std::string, u64>> A; 
    for (u64 i = 0; i < (1u << 20); i++) { fipre_vec((uint32_t)i, a, b); int ea = (a[0] >> 23) & 0xff, eb = (b[0] >> 23) & 0xff; int E = ea + eb - 127; u64 ma = (a[0] & 0x7fffff) | 0x800000, mb = (b[0] & 0x7fffff) | 0x800000; u64 P = ma * mb; int top = P >> 47 ? 1 : 0; int Eres = E + top; int sign = ((a[0] ^ b[0]) >> 31) & 1;
        uint32_t rn = D[2*i], rz = D[2*i+1]; char buf[64]; const char* cn[] = { "zero", "den", "inf", "qnan", "snan", "norm" }; snprintf(buf, 64, "rn=%s%s rz=%s%s", (rn >> 31) ? "-" : "+", cn[cls(rn)], (rz >> 31) ? "-" : "+", cn[cls(rz)]);
        int key = Eres <= 0 ? (Eres < -30 ? -99 : Eres) : Eres >= 255 ? (Eres > 260 ? 999 : Eres) : 100; A[key][std::string(sign ? "neg " : "pos ") + buf]++; }
    printf("A: G0 hw output classes by result exponent (100 = normal range, -99 = deep underflow, 999 = deep overflow):\n"); for (auto& kv : A) { printf("  E=%4d:", kv.first); for (auto& kv2 : kv.second) printf("  [%s]x%llu", kv2.first.c_str(), (unsigned long long)kv2.second); printf("\n"); }
    // overflow value details: print a few RZ results for overflow
    printf("  overflow samples:"); int sh = 0; for (u64 i = 0; i < (1u << 20) && sh < 4; i++) { fipre_vec((uint32_t)i, a, b); int E = ((a[0] >> 23) & 0xff) + ((b[0] >> 23) & 0xff) - 127; if (E >= 256) { sh++; printf(" (a=%08x b=%08x rn=%08x rz=%08x)", a[0], b[0], D[2*i], D[2*i+1]); } } printf("\n");
    printf("  underflow-boundary samples (E in -3..1):"); sh = 0; for (u64 i = 0; i < (1u << 20) && sh < 6; i++) { fipre_vec((uint32_t)i, a, b); int E = ((a[0] >> 23) & 0xff) + ((b[0] >> 23) & 0xff) - 127; if (E >= -3 && E <= 1) { sh++; printf(" (a=%08x b=%08x rn=%08x rz=%08x)", a[0], b[0], D[2*i], D[2*i+1]); } } printf("\n");
    // B: G2 by input class combination
    std::map<std::string, std::map<std::string, u64>> B;
    for (u64 i = 2u << 20; i < (3u << 20); i++) { fipre_vec((uint32_t)i, a, b); int anyN = 0, anyS = 0, anyI = 0, inf0 = 0; for (int j = 0; j < 4; j++) { int ca = cls(a[j]), cb = cls(b[j]); if (ca == 3 || cb == 3) anyN = 1; if (ca == 4 || cb == 4) anyS = 1; if (ca == 2 || cb == 2) anyI = 1; if ((ca == 2 && cb == 0) || (ca == 0 && cb == 2)) inf0 = 1; }
        std::string k = std::string(anyS ? "sNaN " : "") + (anyN ? "qNaN " : "") + (inf0 ? "inf*0 " : anyI ? "inf " : "") ; if (k.empty()) k = "finite"; uint32_t rn = D[2*i]; char buf[32]; const char* cn[] = { "zero", "den", "inf", "qnan", "snan", "norm" }; snprintf(buf, 32, "%s%s(%08x)", (rn >> 31) ? "-" : "+", cn[cls(rn)], cls(rn) >= 3 ? rn : 0u); B[k][buf]++; }
    printf("B: G2 hw RN output by input class:\n"); for (auto& kv : B) { printf("  %-18s:", kv.first.c_str()); int c = 0; for (auto& kv2 : kv.second) { if (c++ < 6) printf("  %s x%llu", kv2.first.c_str(), (unsigned long long)kv2.second); } printf("\n"); }
    // inf-only cases: does the hw give inf? print a few
    printf("  inf-only samples:"); sh = 0; for (u64 i = 2u << 20; i < (3u << 20) && sh < 5; i++) { fipre_vec((uint32_t)i, a, b); int anyN = 0, anyI = 0, inf0 = 0; for (int j = 0; j < 4; j++) { int ca = cls(a[j]), cb = cls(b[j]); if (ca >= 3 || cb >= 3) anyN = 1; if (ca == 2 || cb == 2) anyI = 1; if ((ca == 2 && cb == 0) || (ca == 0 && cb == 2)) inf0 = 1; } if (anyI && !anyN && !inf0) { sh++; printf(" [a=%08x %08x %08x %08x b=%08x %08x %08x %08x -> %08x]", a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3], D[2*i]); } } printf("\n");
    // C: G3: count vectors where all products involve a denormal (-> expected zero) and hw sign
    std::map<std::string, u64> C; for (u64 i = 3u << 20; i < (4u << 20); i++) { fipre_vec((uint32_t)i, a, b); int nden = 0; for (int j = 0; j < 4; j++) if (cls(a[j]) == 1 || cls(b[j]) == 1) nden++; uint32_t rn = D[2*i]; char buf[48]; snprintf(buf, 48, "%d den-products, hw %s", nden, cls(rn) == 0 ? ((rn >> 31) ? "-0" : "+0") : "nonzero"); C[buf]++; }
    printf("C: G3:"); for (auto& kv : C) printf("  [%s]x%llu", kv.first.c_str(), (unsigned long long)kv.second); printf("\n");
    return 0; }
