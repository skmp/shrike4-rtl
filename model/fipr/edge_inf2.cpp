#include <cstdio>
#include <cstdint>
#include <vector>
#include <map>
#include <string>
#include "fipr_vecgen_edge.h"
typedef uint64_t u64;
static int cls(uint32_t x) { int e = (x >> 23) & 0xff; uint32_t m = x & 0x7fffff; if (e == 255) return m ? 3 : 2; if (e == 0) return 0; return 5; }
int main() { u64 N = FIPRE_N; FILE* f = fopen("kos_fipr_edge/fipr_edge_rnrz.bin", "rb"); std::vector<uint32_t> D(N * 2); if (fread(D.data(), 8, N, f) != N) return 1; fclose(f); uint32_t a[4], b[4];
    std::map<std::string, std::pair<u64,u64>> M; int shown = 0;
    for (u64 i = 2u << 20; i < (3u << 20); i++) { fipre_vec((uint32_t)i, a, b); int np = 0, nn = 0, bad = 0; int pos[4], npos = 0; int pe[4]; for (int j = 0; j < 4; j++) { int ca = cls(a[j]), cb = cls(b[j]); if (ca == 3 || cb == 3) bad = 1; if ((ca == 2 && cb == 0) || (cb == 2 && ca == 0)) bad = 1; if (ca == 2 || cb == 2) { int s = ((a[j] ^ b[j]) >> 31) & 1; if (s) nn++; else np++; pos[npos] = j; uint32_t partner = ca == 2 ? b[j] : a[j]; pe[npos] = cls(partner) == 2 ? 255 : (partner >> 23) & 0xff; npos++; } }
        if (bad || np + nn != 2 || nn) continue;   // two +inf products
        int flipped = (D[2*i] >> 31) & 1; char key[96]; int others = 0; for (int j = 0; j < 4; j++) { bool isinf = (j == pos[0] || j == pos[1]); if (!isinf && cls(a[j]) == 5 && cls(b[j]) == 5) others++; }
        snprintf(key, 96, "pos %d,%d partner exps %d,%d finite-others %d", pos[0], pos[1], pe[0], pe[1], others); auto& e = M[key]; e.first++; e.second += flipped;
        if (flipped && shown < 5) { shown++; printf("  flipped: a=%08x %08x %08x %08x b=%08x %08x %08x %08x\n", a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3]); } }
    printf("two +inf products: key -> (n, flipped to -inf)\n"); for (auto& kv : M) printf("  %-48s n=%4llu flipped=%4llu (%.2f)\n", kv.first.c_str(), (unsigned long long)kv.second.first, (unsigned long long)kv.second.second, (double)kv.second.second / kv.second.first);
    return 0; }
