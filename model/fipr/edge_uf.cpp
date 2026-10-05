#include <cstdio>
#include <cstdint>
#include <vector>
#include <map>
#include "fipr_vecgen_edge.h"
typedef uint64_t u64;
int main() { u64 N = FIPRE_N; FILE* f = fopen("kos_fipr_edge/fipr_edge_rnrz.bin", "rb"); std::vector<uint32_t> D(N * 2); if (fread(D.data(), 8, N, f) != N) return 1; fclose(f); uint32_t a[4], b[4];
    std::map<int, std::pair<u64,u64>> M;   // key: ea+eb ; (neg->+0 count, neg->-0 count)
    for (u64 i = 0; i < (1u << 20); i++) { fipre_vec((uint32_t)i, a, b); int ea = (a[0] >> 23) & 0xff, eb = (b[0] >> 23) & 0xff; int sign = ((a[0] ^ b[0]) >> 31) & 1; if (!sign) continue; uint32_t rn = D[2*i]; if ((rn & 0x7fffffff) != 0) continue; int key = ea + eb; if (rn >> 31) M[key].second++; else M[key].first++; }
    printf("negative single product underflow: ea+eb -> (+0 count, -0 count)\n"); for (auto& kv : M) if (kv.second.first || kv.second.second) printf("  ea+eb=%3d: +0 %llu  -0 %llu\n", kv.first, (unsigned long long)kv.second.first, (unsigned long long)kv.second.second);
    return 0; }
