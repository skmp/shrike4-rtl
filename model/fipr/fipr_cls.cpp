// classify low-16 values by hardware E relative to the per-row model Em (from g?_iv.bin intervals)
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include "fipr_vecgen.h"
typedef int64_t i64;
static inline int booth_digit(uint32_t x, int k) { int b_1 = k ? (x >> (2*k-1)) & 1 : 0, b0 = (x >> (2*k)) & 1, b1 = (x >> (2*k+1)) & 1; return -2*b1 + b0 + b_1; }
int main(int argc, char** argv) {
    int g = atoi(argv[1]); char fn[200]; snprintf(fn, 200, "/tmp/g%d_iv.bin", g);
    std::vector<i64> LO(65536), HI(65536); FILE* f = fopen(fn, "rb"); fread(LO.data(), 8, 65536, f); fread(HI.data(), 8, 65536, f); fclose(f);
    std::vector<int> cls(65536); long hist[8] = {0}; long bit1[16] = {0}, bitn[16] = {0};
    for (uint32_t v = 0; v < 65536; v++) {
        i64 drop = 0; for (int k = 0; k < 8; k++) { int d = g ? booth_digit(v, k) : booth_digit(FIPR_K0 | 0x800000, k); i64 pp = (i64)d * (i64)(g ? 0xd55555 : v) << (2*k); drop += pp - ((pp >> 16) << 16); }
        i64 Em = (1ll << 19) - drop; i64 dlo = LO[v] - Em;      // hardware E in (LO, HI]; class by (LO-Em)/65536
        int c = (int)((dlo + 3 * 65536) / 65536) - 3; if (c < -3) c = -3; if (c > 3) c = 3; cls[v] = c; hist[c + 3]++;
        for (int i = 0; i < 16; i++) if ((v >> i) & 1) { bitn[i]++; if (c >= 0) bit1[i]++; }
    }
    printf("G%d class histogram ((LO-Em) div 65536, -3..3):", g); for (int i = 0; i < 7; i++) printf(" %d:%ld", i - 3, hist[i]); printf("\n");
    printf("  P(class>=0 | bit i set):"); for (int i = 0; i < 16; i++) printf(" b%d:%.2f", i, (double)bit1[i] / bitn[i]); printf("\n");
    printf("  classes v=0..127: "); for (int v = 0; v < 128; v++) printf("%c", cls[v] < 0 ? '-' : cls[v] == 0 ? '0' : '+'); printf("\n");
    printf("  classes v=0x8000..0x807f: "); for (int v = 0x8000; v < 0x8080; v++) printf("%c", cls[v] < 0 ? '-' : cls[v] == 0 ? '0' : '+'); printf("\n");
    // dependence on low bits pair pattern for G0: print class vs v&0xf and v>>12
    printf("  count class<0 by v&15:"); for (int m = 0; m < 16; m++) { long c = 0; for (uint32_t v = m; v < 65536; v += 16) c += cls[v] < 0; printf(" %d:%ld", m, c); } printf("\n");
    printf("  count class<0 by v>>12:"); for (int m = 0; m < 16; m++) { long c = 0; for (uint32_t v = m << 12; v < (uint32_t)((m + 1) << 12); v++) c += cls[v] < 0; printf(" %d:%ld", m, c); } printf("\n");
    return 0;
}
