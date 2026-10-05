#include <cstdio>
#include <cstdint>
#include <vector>
#include <map>
#include "fipr_vecgen_edge.h"
typedef uint64_t u64;
static int cls(uint32_t x) { int e = (x >> 23) & 0xff; uint32_t m = x & 0x7fffff; if (e == 255) return m ? 3 : 2; if (e == 0) return 0; return 5; }
int main() { u64 N = FIPRE_N; FILE* f = fopen("kos_fipr_edge/fipr_edge_rnrz.bin", "rb"); std::vector<uint32_t> D(N * 2); if (fread(D.data(), 8, N, f) != N) return 1; fclose(f); uint32_t a[4], b[4];
    std::map<int, std::map<uint32_t, u64>> M;   // key: npos*16 + nneg + 256*(has inf*0 or nan) ; value: hw result
    for (u64 i = 2u << 20; i < (3u << 20); i++) { fipre_vec((uint32_t)i, a, b); int np = 0, nn = 0, bad = 0; int lastsign = -1; for (int j = 0; j < 4; j++) { int ca = cls(a[j]), cb = cls(b[j]); if (ca == 3 || cb == 3) bad = 1; if ((ca == 2 && cb == 0) || (cb == 2 && ca == 0)) bad = 1; if (ca == 2 || cb == 2) { int s = ((a[j] ^ b[j]) >> 31) & 1; if (s) nn++; else np++; lastsign = s; } }
        if (bad || (np + nn) == 0) continue; M[np * 16 + nn][D[2*i]]++; }
    printf("inf products only: (n+inf, n-inf) -> hw results\n"); for (auto& kv : M) { printf("  (+%d, -%d):", kv.first / 16, kv.first % 16); for (auto& r : kv.second) printf("  %08x x%llu", r.first, (unsigned long long)r.second); printf("\n"); }
    // for (1,1),(2,0),(0,2) etc: does position/order matter? check (+1,-1) results split by which comes first
    std::map<int, std::map<uint32_t, u64>> O; for (u64 i = 2u << 20; i < (3u << 20); i++) { fipre_vec((uint32_t)i, a, b); int np = 0, nn = 0, bad = 0, first = -1, last = -1; for (int j = 0; j < 4; j++) { int ca = cls(a[j]), cb = cls(b[j]); if (ca == 3 || cb == 3) bad = 1; if ((ca == 2 && cb == 0) || (cb == 2 && ca == 0)) bad = 1; if (ca == 2 || cb == 2) { int s = ((a[j] ^ b[j]) >> 31) & 1; if (s) nn++; else np++; if (first < 0) first = s; last = s; } } if (bad || np + nn != 2) continue; O[np * 16 + nn + 256 * first + 512 * last][D[2*i]]++; }
    printf("two inf products by (first sign, last sign):\n"); for (auto& kv : O) { printf("  (+%d,-%d) first %s last %s:", (kv.first % 256) / 16, kv.first % 16, (kv.first >> 8) & 1 ? "-" : "+", (kv.first >> 9) & 1 ? "-" : "+"); for (auto& r : kv.second) printf("  %08x x%llu", r.first, (unsigned long long)r.second); printf("\n"); }
    return 0; }
