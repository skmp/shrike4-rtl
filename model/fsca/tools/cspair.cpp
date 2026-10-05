// (C) carry-save pair truncation of the node-0 linear product: rows -> CSA tree -> (S, C) -> T = (S>>r) + (C>>r) [+ injected round row]
#include "fsca_common.h"
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
static inline int bd(uint32_t x, int k) { int b_1 = k ? (x >> (2*k-1)) & 1 : 0, b0 = (x >> (2*k)) & 1, b1 = (x >> (2*k+1)) & 1; return -2*b1 + b0 + b_1; }
static void csa(u64& s, u64& c, u64 r) { u64 ns = s ^ c ^ r, nc = ((s & c) | (s & r) | (c & r)) << 1; s = ns; c = nc; }
int main() { Fsca F; if (!load_fsca(F)) return 1; double k = 2 * M_PI / 65536; i64 m1 = (i64)floor(k * pow(2, 37)); i64 T1hw[512]; g_K = 1ll << 19;
    for (int a = 1; a <= 511; a++) { i64 V = decode(F.srz[a], F.srn[a]).lo; u64 a3 = (u64)a * a * a; Op cu { (uint32_t)(a3 >> 4), 27 }, c3 { 0xA56u << 2, -54 + 23 - 2 }; T1hw[a] = V + prod_units(c3, cu); }
    struct R { int ok; std::string name; }; std::vector<R> res;
    for (int rowmode = 0; rowmode < 3; rowmode++) for (int sp = 0; sp <= 14; sp++) for (int tree = 0; tree < 2; tree++) for (int inj = 0; inj < 4; inj++) { int r = 11 + sp; int ok = 0;
        for (int a = 1; a <= 511; a++) { std::vector<u64> rows; u64 tt = (u64)a << sp;
            if (rowmode == 0) { for (int kk = 0; kk < 13; kk++) { int d = bd((uint32_t)m1, kk); if (d) rows.push_back((u64)((i64)d * (i64)tt) << (2*kk)); } }          // Booth on C1, multiplicand t<<sp
            else if (rowmode == 1) { for (int kk = 0; kk < 13; kk++) { int d = bd((uint32_t)tt, kk); if (d) rows.push_back((u64)((i64)d * m1) << (2*kk)); } }    // Booth on t<<sp, multiplicand C1
            else { for (int b = 0; b < 24; b++) if ((tt >> b) & 1) rows.push_back((u64)m1 << b); }                                                             // AND array
            u64 injv = inj == 0 ? 0 : inj == 1 ? (1ull << (r - 1)) : inj == 2 ? (1ull << (r - 2)) : (1ull << (r - 1)) + (1ull << (r - 3)); if (injv) rows.push_back(injv);
            if (rows.empty()) rows.push_back(0); u64 S = 0, C = 0;
            if (tree == 0) { S = rows[0]; C = 0; for (size_t j = 1; j < rows.size(); j++) csa(S, C, rows[j]); }
            else { std::vector<u64> cur = rows; while (cur.size() > 2) { std::vector<u64> nxt; size_t j = 0; for (; j + 2 < cur.size() + 0 && j + 3 <= cur.size(); j += 3) { u64 s = cur[j], c = 0; csa(s, c, cur[j+1]); u64 s2 = s, c2 = c; s2 = s ^ c ^ cur[j+2]; c2 = ((s & c) | (s & cur[j+2]) | (c & cur[j+2])) << 1; nxt.push_back(s2); nxt.push_back(c2); } for (; j < cur.size(); j++) nxt.push_back(cur[j]); cur = nxt; } S = cur[0]; C = cur.size() > 1 ? cur[1] : 0; }
            i64 T = ((i64)S >> r) + ((i64)C >> r); ok += (T == T1hw[a]); }
        char nm[96]; snprintf(nm, 96, "%s t<<%2d %s inj=%s", rowmode == 0 ? "Booth(C1)  " : rowmode == 1 ? "Booth(t)   " : "AND-array  ", sp, tree ? "wallace" : "chain  ", inj == 0 ? "none" : inj == 1 ? "1/2u" : inj == 2 ? "1/4u" : "5/8u"); res.push_back({ ok, nm }); }
    std::sort(res.begin(), res.end(), [](const R& x, const R& y) { return x.ok > y.ok; }); printf("(C) carry-save pair truncation, node 0 linear (hits of 511; exact+80/2048 gives 511):\n"); for (int i = 0; i < 12; i++) printf("  %d  %s\n", res[i].ok, res[i].name.c_str()); return 0; }
