// f3_full.cpp -- the full model, end to end, nodes 0..31, for every feedback / placement / sine-rule variant.
// Reports the window-violation count (baseline 147) and the guard nodes 0,1,2,16 (+ mirrors 30,31) separately,
// exactly as the ground rules demand.  The square chain is the published one,
//     I1 = (arr(dx,KC) + K1) >> s1 ,  J = (arr(I1,dx) + K2) >> s2 ,   s1 + s2 = 43 - du
// swept over s1 and the two rounding constants; the FINAL multiply is the variant under test.
#include "f3_fb.h"
#include "f3_field.h"
#include <cstdio>
#include <string>
static Fsca DUMP;
struct Res { int viol, guard, s1, s2, k1, k2, ro, sv; FbV v; };
int main(int argc, char** argv) {
    if (!load_fsca(DUMP)) { puts("dump load failed"); return 1; } build_tables(); FBF = DUMP;
    // precompute the windows once
    static i64 WLO[65536], WHI[65536];
    for (int a = 0; a < 65536; a++) { Win w = decode(DUMP.srz[a], DUMP.srn[a]);
        if (w.sign > 0) { WLO[a] = w.lo; WHI[a] = w.hi; } else { WLO[a] = -w.hi; WHI[a] = -w.lo; } }
    for (int du = 0; du <= 2; du += 2) {
        std::vector<FbV> vs;
        for (int role = 0; role < 2; role++) for (int k3 = 0; k3 < 3; k3++) vs.push_back(FbV{MODE_CUR,0,0,role,k3,0});
        for (int comp = 0; comp < 2; comp++) for (int tm = 0; tm < 3; tm++) for (int role = 0; role < 2; role++)
            for (int k3 = 0; k3 < 3; k3++) for (int w = 23 + du; w <= 38; w++) {
                FbV v{MODE_SEP, comp, tm, role, k3, w}; if (fb_valid(v, du)) vs.push_back(v); }
        for (int comp = 0; comp < 2; comp++) for (int role = 0; role < 2; role++) for (int k3 = 0; k3 < 3; k3++) {
            FbV v{MODE_MERGED, comp, 0, role, k3, 33 + du}; if (fb_valid(v, du)) vs.push_back(v); }
        int NV = (int)vs.size(), NT = NSV2 * NV;
        printf("\n===== FULL MODEL, latch shift %d : %d sine rules x %d final-multiply variants =====\n", 20 - du, NSV2, NV);
        std::vector<Res> best(NT);
        #pragma omp parallel for schedule(dynamic)
        for (int idx = 0; idx < NT; idx++) {
            int sv = idx / NV, i = idx % NV; const FbV& v = vs[i]; int nr = fb_nrows(v);
            i64 S[17]; int U[17]; for (int n = 0; n <= 16; n++) { fin_field2(sv, n, S[n], U[n]); U[n] += du; }
            i64 b0[17]; for (int n = 0; n <= 16; n++) b0[n] = fb_base0(v, S[n], U[n], nr);
            Res bst; bst.viol = 1 << 30; bst.guard = 1 << 30; bst.sv = sv; bst.v = v;
            // the published chain, plus the two shift splits the exact solves single out, plus K2 = 0
            const int S1[3] = { 23, 24, 26 };
            for (int ro = 1; ro >= 0; ro--) for (int si = 0; si < 3; si++) { int s1 = S1[si], s2 = 43 - du - s1; if (s2 < 10) continue;
              for (int k1 = 1; k1 < 2; k1++) for (int k2 = 0; k2 < 2; k2++) {
                i64 K1 = k1 ? (1ll << 19) : 0, K2 = k2 ? (1ll << 19) : 0; int viol = 0, guard = 0;
                for (int t = -512; t <= 511; t++) { i64 dx = (i64)t << 13;
                    i64 A3 = ((arr(((i64)t*t) << 3, dx) + K) >> 20) >> 3;
                    i64 I1 = ((ro ? arr(dx, KC) : arr(KC, dx)) + K1) >> s1;
                    i64 J = (arr(I1, dx) + K2) >> s2; if (J < 0) J = 0;
                    for (int node = 0; node <= 31; node++) { int flip = node >= 17, n = flip ? 32 - node : node;
                        i64 s = flip ? -1 : 1;
                        i64 T1 = TB[n].C1f ? sm(arr(TB[n].C1f, dx) + K, 23 - TB[n].e1) : 0;
                        i64 T3 = TB[n].c3 ? sm(arr(TB[n].c3 << 5, A3) + K, 26) : 0;
                        i64 V = s * T1 - s * T3 + fb_contrib(v, J, S[n], U[n], nr, b0[n]);
                        if (v.mode != MODE_MERGED) V += FB_C0[n];
                        else if (!S[n]) V += FB_C0[n];                 // node 0: sin = 0, no product to carry it
                        int a = (node * 1024 + t) & 0xffff;
                        if (V < WLO[a] || V > WHI[a]) { viol++; if (n == 0 || n == 1 || n == 2 || n == 16) guard++; } } }
                if (guard < bst.guard || (guard == bst.guard && viol < bst.viol)) {
                    bst.viol = viol; bst.guard = guard; bst.s1 = s1; bst.s2 = s2; bst.k1 = k1; bst.k2 = k2; bst.ro = ro; }
              } }
            best[idx] = bst;
        }
        std::vector<int> ord(NT); for (int i = 0; i < NT; i++) ord[i] = i;
        std::sort(ord.begin(), ord.end(), [&](int a, int b) {
            if (best[a].guard != best[b].guard) return best[a].guard < best[b].guard;
            return best[a].viol < best[b].viol; });
        puts("best (guard, then violations) over the chain sweep; guard = nodes 0,1,2,16 and mirrors");
        printf("%-4s %-34s %-44s %-8s %-6s %s\n", "rank", "sine rule", "final multiply", "guard", "viol", "chain");
        for (int r = 0; r < 14 && r < NT; r++) { const Res& b = best[ord[r]]; char nb[128]; fb_name(b.v, nb);
            printf("%-4d %-34s %-44s %-8d %-6d s1=%d s2=%d K1=%d K2=%d %s\n", r, SV2[b.sv], nb, b.guard, b.viol,
                   b.s1, b.s2, b.k1, b.k2, b.ro ? "arr(dx,KC)" : "arr(KC,dx)"); }
        // and the published model for reference
        for (int i = 0; i < NT; i++) { const Res& b = best[i];
            if (b.sv == 0 && b.v.mode == MODE_CUR && b.v.role == 0 && b.v.k3 == 1) { char nb[128]; fb_name(b.v, nb);
                printf("REF  %-34s %-44s %-8d %-6d s1=%d s2=%d K1=%d K2=%d %s\n", SV2[b.sv], nb, b.guard, b.viol,
                       b.s1, b.s2, b.k1, b.k2, b.ro ? "arr(dx,KC)" : "arr(KC,dx)"); } }
    }
    return 0;
}
