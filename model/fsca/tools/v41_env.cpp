// v41_env.cpp -- INDEPENDENT test of the "loss-alignment" envelope claim.
// Claim under test: at w=13, (s1,s2)=(23,18), with BOTH array losses free over [-655360,+65536]
// and BOTH rounding constants free, the reachable J(17) set is exactly [45626,45630].
// Everything exact integer. Enumerates the reachable I1 set rather than reasoning about it.
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
static i64 JL[1024], JH[1024];
static void load_win(){ FILE* f=fopen("jwin18.txt","r"); char l[512];
  while(fgets(l,sizeof l,f)){ if(l[0]=='#')continue; long long t,a,b,w; if(sscanf(l,"%lld %lld %lld %lld",&t,&a,&b,&w)!=4)continue; JL[t+512]=a; JH[t+512]=b;} fclose(f);}
static inline i64 fdiv(i64 a, i64 b){ i64 q=a/b, r=a%b; if(r&&((r<0)!=(b<0)))q--; return q; }

static const i64 LMIN = -655360, LMAX = 65536;

// reachable J interval at offset t, given loss ranges [LMIN,LMAX] at both multiplies and
// rounding constants free in [0, 2^s).  Exact; both branches of the union enumerated.
static void env(int t, int w, int s1, int s2, i64 kc, i64& jlo, i64& jhi, i64& i1lo, i64& i1hi) {
    i64 dx = (i64)t << w;
    i64 ex1 = kc * dx;
    i64 P1 = 1ll << s1, P2 = 1ll << s2;
    i1lo = fdiv(ex1 + LMIN + 0,        P1);
    i1hi = fdiv(ex1 + LMAX + (P1 - 1), P1);
    jlo = (i64)1<<62; jhi = -((i64)1<<62);
    for (i64 I1 = i1lo; I1 <= i1hi; I1++) {
        i64 ex2 = I1 * dx;
        i64 a = fdiv(ex2 + LMIN + 0,        P2);
        i64 b = fdiv(ex2 + LMAX + (P2 - 1), P2);
        jlo = std::min(jlo, a); jhi = std::max(jhi, b);
    }
}

int main(int argc, char** argv) {
    build_tables(); load_win();
    int w = 13, s1 = 23, s2 = 18; i64 kc = KC;
    printf("=== ENVELOPE at w=%d, (s1,s2)=(%d,%d), KC=0x%llx, loss in [%lld,%lld], K free in [0,2^s) ===\n",
           w, s1, s2, (long long)kc, (long long)LMIN, (long long)LMAX);

    int land[3] = {17, 342, -342};
    for (int i = 0; i < 3; i++) {
        int t = land[i]; i64 jl, jh, il, ih; env(t, w, s1, s2, kc, jl, jh, il, ih);
        i64 dx = (i64)t << w;
        i64 I1m = (arr(kc,dx)+K)>>s1; i64 Jm = (arr(I1m,dx)+K)>>s2;
        printf("t=%5d  I1 reachable [%lld,%lld] (model %lld, %lld values)  J envelope [%lld,%lld]  model J %lld  required [%lld,%lld]  %s\n",
            t, (long long)il, (long long)ih, (long long)I1m, (long long)(ih-il+1),
            (long long)jl, (long long)jh, (long long)Jm, (long long)JL[t+512], (long long)JH[t+512],
            (jh < JL[t+512] || jl > JH[t+512]) ? "UNREACHABLE" : "reachable");
    }
    // absolute floor at t=17: everything pushed to its minimum, no interval reasoning
    {
        int t = 17; i64 dx = (i64)t<<w;
        i64 I1min = fdiv(kc*dx + LMIN, 1ll<<s1);
        i64 Jmin  = fdiv(I1min*dx + LMIN, 1ll<<s2);
        printf("\nhand check t=17: KC*dx = %lld = %lld*2^23 + %lld ; I1min=%lld ; I1min*dx = %lld*2^18 + %lld ; Jmin=%lld ; need 45617 (short by %lld)\n",
            (long long)(kc*dx), (long long)((kc*dx)>>s1), (long long)((kc*dx)&((1ll<<s1)-1)),
            (long long)I1min, (long long)((I1min*dx)>>s2), (long long)((I1min*dx)&((1ll<<s2)-1)),
            (long long)Jmin, (long long)(Jmin-45617));
    }
    // full sweep: how many offsets lie outside the envelope
    int out = 0, worst_t = 0; i64 worst_d = 0;
    for (int t = -512; t <= 511; t++) {
        i64 jl, jh, il, ih; env(t, w, s1, s2, kc, jl, jh, il, ih);
        if (jh < JL[t+512]) { out++; i64 d = JL[t+512]-jh; if (d>worst_d){worst_d=d;worst_t=t;} }
        else if (jl > JH[t+512]) { out++; i64 d = jl-JH[t+512]; if (d>worst_d){worst_d=d;worst_t=t;} }
    }
    printf("\noffsets whose window is entirely OUTSIDE the free-loss envelope: %d / 1024 ; worst t=%d by %lld units\n",
        out, worst_t, (long long)worst_d);

    // Stress the claim: make the freedoms absurdly generous and see when 45617 becomes reachable.
    printf("\n--- how much extra freedom would t=17 need? ---\n");
    for (i64 mult = 1; mult <= 64; mult *= 2) {
        int t = 17; i64 dx=(i64)t<<w;
        i64 I1min = fdiv(kc*dx + LMIN*mult, 1ll<<s1);
        i64 Jmin  = fdiv(I1min*dx + LMIN*mult, 1ll<<s2);
        printf("  loss range scaled x%-3lld (|loss| <= %lld = %lld*2^16): I1min=%lld J(17)min=%lld  %s\n",
            (long long)mult, (long long)(-LMIN*mult), (long long)((-LMIN*mult)>>16), (long long)I1min, (long long)Jmin,
            Jmin<=45617?"<-- 45617 reachable":"");
    }
    return 0;
}
