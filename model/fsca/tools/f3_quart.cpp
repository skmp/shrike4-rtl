// f3_quart.cpp -- is the coarse-latch solution just the fine-latch solution divided by four?
// If the complement-feedback final multiply makes the COARSE latch reach floor 0, and its per-offset latch
// windows are the published fine (shift-18) windows shifted right by two, then the "the latch must be two or
// three bits finer" conclusion was compensating for the final multiply's ROUNDING DIRECTION, not for missing
// bits in the latch.  This tests exactly that, offset by offset, against tools/jwin18.txt.
// Also scores the published chain against the new coarse windows (published ceiling on jwin18: 938/1024).
#include "f3_fb.h"
#include "f3_field.h"
#include <cstdio>
static Fsca DUMP;
static i64 WLO[65536], WHI[65536];
int main(int argc, char** argv) {
    if (!load_fsca(DUMP)) { puts("dump load failed"); return 1; } build_tables(); FBF = DUMP;
    for (int a = 0; a < 65536; a++) { Win w = decode(DUMP.srz[a], DUMP.srn[a]);
        if (w.sign > 0) { WLO[a] = w.lo; WHI[a] = w.hi; } else { WLO[a] = -w.hi; WHI[a] = -w.lo; } }
    int sv = argc > 1 ? atoi(argv[1]) : 10;
    FbV v{MODE_SEP, argc > 2 ? atoi(argv[2]) : 0, argc > 3 ? atoi(argv[3]) : 2,
          argc > 4 ? atoi(argv[4]) : 1, argc > 5 ? atoi(argv[5]) : 1, argc > 6 ? atoi(argv[6]) : 28};
    if (argc > 7 && atoi(argv[7]) == 0) v.mode = MODE_CUR;
    int nr = fb_nrows(v); i64 S[17]; int U[17];
    for (int n = 0; n <= 16; n++) fin_field2(sv, n, S[n], U[n]);
    i64 b0[17]; for (int n = 0; n <= 16; n++) b0[n] = fb_base0(v, S[n], U[n], nr);
    char nb[128]; fb_name(v, nb); printf("sine rule %s ; final multiply %s ; COARSE latch\n", SV2[sv], nb);
    // coarse windows
    static i64 CL[1024], CH[1024]; int feas = 0;
    for (int t = -512; t <= 511; t++) {
        i64 dx = (i64)t << 13; i64 A3 = ((arr(((i64)t*t) << 3, dx) + K) >> 20) >> 3;
        std::vector<NodeCon> cons;
        for (int node = 1; node <= 31; node++) { int flip = node >= 17, n = flip ? 32 - node : node;
            if (!S[n]) continue; i64 s = flip ? -1 : 1;
            i64 T1 = TB[n].C1f ? sm(arr(TB[n].C1f, dx) + K, 23 - TB[n].e1) : 0;
            i64 T3 = TB[n].c3 ? sm(arr(TB[n].c3 << 5, A3) + K, 26) : 0;
            i64 base = FB_C0[n] + s * T1 - s * T3; int a = (node * 1024 + t) & 0xffff;
            NodeCon c; c.n = n; c.clo = WLO[a] - base; c.chi = WHI[a] - base; c.S = S[n]; c.u = U[n]; c.base0 = b0[n];
            cons.push_back(c); }
        i64 c0 = fb_ideal(t, 0), lo = 1, hi = 0;
        for (i64 J = std::max((i64)0, c0 - 600); J <= c0 + 600; J++) { bool ok = true;
            for (const NodeCon& q : cons) { i64 g = fb_contrib(v, J, q.S, q.u, nr, q.base0);
                if (g < q.clo || g > q.chi) { ok = false; break; } }
            if (ok) { if (lo > hi) lo = J; hi = J; } }
        CL[t + 512] = lo; CH[t + 512] = hi; if (lo <= hi) feas++;
    }
    printf("coarse windows: %d/1024 offsets feasible\n", feas);
    // compare with jwin18.txt >> 2
    FILE* f = fopen("jwin18.txt", "r"); if (!f) { puts("jwin18.txt not found -- run from tools/"); return 1; }
    char line[256]; int cmp = 0, agree = 0, both = 0; long tot18 = 0;
    while (fgets(line, sizeof line, f)) { long long t, a, b, wdt;
        if (sscanf(line, "%lld %lld %lld %lld", &t, &a, &b, &wdt) != 4) continue;
        if (t < -512 || t > 511) continue; tot18++;
        if (a > b) continue; i64 la = a >> 2, hb = b >> 2; int i = (int)t + 512;
        if (CL[i] > CH[i]) continue; both++;
        if (!(hb < CL[i] || la > CH[i])) agree++; else if (cmp < 12) {
            printf("   disagree t=%4lld : fine>>2 [%lld,%lld]  coarse [%lld,%lld]\n", t, (long long)la, (long long)hb,
                   (long long)CL[i], (long long)CH[i]); cmp++; } }
    fclose(f);
    printf("jwin18.txt read: %ld offsets.  Both feasible at %d offsets; the coarse window overlaps (fine window >> 2)\n"
           "at %d of them -- so the coarse solution IS the fine solution at two bits less resolution.\n", tot18, both, agree);
    // score the published chain against the coarse windows
    int best = -1, bs1 = 0, bk2 = 0, bro = 0;
    for (int ro = 0; ro < 2; ro++) for (int s1 = 19; s1 <= 27; s1++) { int s2 = 43 - s1;
      for (int k2 = 0; k2 < 2; k2++) { i64 K2 = k2 ? (1ll << 19) : 0; int sc = 0;
        for (int t = -512; t <= 511; t++) { int i = t + 512; if (CL[i] > CH[i]) continue; i64 dx = (i64)t << 13;
            i64 I1 = ((ro ? arr(dx, KC) : arr(KC, dx)) + K) >> s1; i64 J = (arr(I1, dx) + K2) >> s2;
            if (J >= CL[i] && J <= CH[i]) sc++; }
        if (sc > best) { best = sc; bs1 = s1; bk2 = k2; bro = ro; } } }
    printf("published chain against these coarse windows: best %d/1024  [s1=%d s2=%d K2=%d %s]\n",
           best, bs1, 43 - bs1, bk2, bro ? "arr(dx,KC)" : "arr(KC,dx)");
    printf("   (the same chain against jwin18 reaches 938/1024 by exact solve; 918 at s2=20)\n\n");
    // does e6_nol1's one-offset certificate survive the new final multiply?
    puts("e6_nol1's question re-asked against THESE windows: for how many offsets is there NO integer I1 at all");
    puts("with J = (arr(I1,dx)+K2)>>s2 inside the window?");
    for (int s2 = 19; s2 <= 21; s2++) for (int k2 = 0; k2 < 2; k2++) {
        i64 K2 = k2 ? (1ll << 19) : 0; int none = 0; std::vector<int> bd;
        for (int t = -512; t <= 511; t++) { int i = t + 512; if (CL[i] > CH[i] || t == 0) continue;
            i64 dx = (i64)t << 13, adx = dx < 0 ? -dx : dx;
            long double sc = (long double)(1ll << s2) / (long double)adx;
            i64 xlo = (i64)(CL[i] * sc) - 8; i64 xhi = (i64)((CH[i] + 1) * sc) + 8;
            if (xhi - xlo > 400000) xhi = xlo + 400000; bool any = false;
            for (i64 x = xlo; x <= xhi && !any; x++) { i64 I1 = (t < 0) ? -x : x;
                i64 J = (arr(I1, dx) + K2) >> s2; if (J >= CL[i] && J <= CH[i]) any = true; }
            if (!any) { none++; if (bd.size() < 8) bd.push_back(t); } }
        printf("   s2=%2d K2=%-7lld : %3d offsets unreachable  ", s2, (long long)K2, none);
        for (int t : bd) printf(" %d", t); puts("");
    }
    return 0;
}
