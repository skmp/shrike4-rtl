// fv_certificate_d.cpp -- the t=17 budget, decided EXACTLY rather than by interval relaxation.
// Required: J(17) = 45617, i.e. the square path must lose D = KC*17^2/2^15 - 45617 = 24293/32768 + 11
// latch units.  Question: what D can a two-latch chain actually produce?
// All arithmetic integer; D is carried as an exact numerator over 2^15.
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
int main(){ build_tables();
    const int t=17;
    i64 num_exact=(i64)KC*t*t;                 // KC*t^2 ; exact J target scale is /2^15
    printf("exact KC*t^2/2^15 = %lld/32768 = %.6f ; required J = 45617 -> D = %lld/32768 = %.6f\n",
        (long long)num_exact,(double)num_exact/32768.0,(long long)(num_exact-45617ll*32768),
        (double)(num_exact-45617ll*32768)/32768.0);

    // ---- A. the published-style chain: K1=K2=2^19 fixed, s1+s2 = 2g+15, both roles at both multiplies
    printf("\n[A] EXACT chain, K1=K2=2^19, s1+s2=2g+15, all 4 role combinations:\n");
    { i64 bestnum=-(1ll<<62); int bg=0,bs1=0,bs2=0,br1=0,br2=0; int hit=0; int ncell=0;
      for(int g=8;g<=14;g++) for(int s2=g-2;s2<=g+9;s2++){ int s1=2*g+15-s2; if(s1<1||s1>60) continue; ncell++;
        i64 dx=(i64)t<<g;
        for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++){
          i64 A1=r1?arr(KC,dx):arr(dx,KC); i64 I1=(A1+K)>>s1;
          i64 A2=r2?arr(dx,I1):arr(I1,dx); i64 J=(A2+K)>>s2;
          i64 d=num_exact-J*32768;              // D * 2^15
          if(J==45617)hit++;
          if(d>bestnum){bestnum=d;bg=g;bs1=s1;bs2=s2;br1=r1;br2=r2;} } }
      printf("    %d (g,s2) cells x 4 roles ; J==45617 hit %d times\n",ncell,hit);
      printf("    largest D achieved: %lld/32768 = %.6f   at g=%d s1=%d s2=%d roles(%d,%d)\n",
             (long long)bestnum,(double)bestnum/32768.0,bg,bs1,bs2,br1,br2); }

    // ---- B. *** SUPERSEDED AND WRONG -- kept only as a record of the trap ***
    //   This block assumes the latch takes BOTH floor(A/2^s) and floor(A/2^s)+1 as K sweeps a period.
    //   That is false: arr() always returns a multiple of 2^16, so for s <= 16 the constant cannot move
    //   the latch at all, and the "+1" case needs K >= 2^s, i.e. an integer bias, not a rounding constant.
    //   fv_certificate_h.cpp redoes this with exact breakpoints; its answer is 0, not 44.
    // free K1 and K2 over FULL PERIODS (a stored rounding constant, no integer bias),
    //         s1 and s2 unconstrained, both roles: the exact reachable J set at t=17.
    printf("\n[B] K1 in [0,2^s1), K2 in [0,2^s2) exhausted (every value; the latch then takes 2 values each),\n");
    printf("    s1 in 8..34, s2 in 8..26, g in 8..14, both roles at both multiplies:\n");
    { i64 bestnum=-(1ll<<62); int bg=0,bs1=0,bs2=0; long long hit=0; long long cells=0;
      i64 bestJ=0;
      for(int g=8;g<=14;g++){ i64 dx=(i64)t<<g;
        for(int r1=0;r1<2;r1++){ i64 A1=r1?arr(KC,dx):arr(dx,KC);
          for(int s1=8;s1<=34;s1++){ i64 base=A1>>s1;                   // K1 sweep gives base or base+1
            for(int e1=0;e1<2;e1++){ i64 I1=base+e1;
              for(int r2=0;r2<2;r2++){ i64 A2=r2?arr(dx,I1):arr(I1,dx);
                for(int s2=8;s2<=26;s2++){ i64 b2=A2>>s2; cells++;
                  for(int e2=0;e2<2;e2++){ i64 J=b2+e2; i64 d=num_exact-J*32768;
                    if(J==45617)hit++;
                    if(d>bestnum && J>=0){bestnum=d;bg=g;bs1=s1;bs2=s2;bestJ=J;} } } } } } } }
      printf("    %lld configurations ; J==45617 reachable %lld times\n",cells,hit);
      printf("    largest D (over configs with J>=0): %.6f at g=%d s1=%d s2=%d (J=%lld)\n",
             (double)bestnum/32768.0,bg,bs1,bs2,(long long)bestJ); }

    // ---- C. what the terms can actually contribute, measured (not relaxed):
    //         a1 = dx*KC - arr(dx,KC) ; a2 = I1*dx - arr(I1,dx) ; d1,d2 the latch residues
    printf("\n[C] the term budget at t=17, measured per (g,s2) with s1=2g+15-s2, K1=K2=2^19:\n");
    printf("      g  s1  s2 |    a1      a2   d1*w/2^s2   a2/2^s2    d2   -K2/2^s2   (a1-K1)w/2^S |    D\n");
    for(int g=12;g<=14;g++) for(int s2=g-2;s2<=g+3;s2++){ int s1=2*g+15-s2; i64 dx=(i64)t<<g;
        i64 A1=arr(dx,KC); i64 a1=dx*KC-A1; i64 I1=(A1+K)>>s1;
        long double d1=(long double)((A1+K)-((A1+K)>>s1<<s1))/(long double)(1ll<<s1);
        i64 A2=arr(I1,dx); i64 a2=I1*dx-A2; i64 J=(A2+K)>>s2;
        long double d2=(long double)((A2+K)-((A2+K)>>s2<<s2))/(long double)(1ll<<s2);
        long double w=(long double)dx, S=(long double)s1+s2;
        long double T1=(long double)(a1-(i64)K)*w/powl(2.0L,S), T2=d1*w/powl(2.0L,(long double)s2),
                    T3=(long double)a2/powl(2.0L,(long double)s2), T4=-(long double)K/powl(2.0L,(long double)s2);
        printf("     %2d  %2d  %2d | %7lld %7lld  %9.4Lf %9.4Lf %6.4Lf %9.4Lf %12.6Lf | %8.4f\n",
            g,s1,s2,(long long)a1,(long long)a2,T2,T3,d2,T4,T1,(double)(num_exact-J*32768)/32768.0); }

    // ---- D. the SUP of the relaxation the report used: a1,a2 free in [0,Amax], d1,d2 free in [0,1)
    printf("\n[D] sup of the relaxed budget (a1,a2 free in [0,Amax], d1,d2 in [0,1)), K1=K2=2^19:\n");
    { i64 Amax=677198;   // structural bound on the array deficit (see fv_certificate_a: measured max 393216)
      long double best=-1e18; int bg=0,bs2=0; int nfeas=0,ncell=0;
      for(int g=8;g<=14;g++) for(int s2=g-2;s2<=g+9;s2++){ int s1=2*g+15-s2; if(s1<1)continue; ncell++;
        long double w=(long double)((i64)t<<g);
        long double sup=(long double)(Amax-(i64)K)*w/powl(2.0L,(long double)(s1+s2))
                       + w/powl(2.0L,(long double)s2) + (long double)Amax/powl(2.0L,(long double)s2)
                       + 1.0L - (long double)K/powl(2.0L,(long double)s2);
        if(sup>=11.7413635L) nfeas++;
        if(sup>best){best=sup;bg=g;bs2=s2;} }
      printf("    %d cells ; sup >= 11.741363 in %d of them ; largest sup %.6Lf at g=%d s2=%d\n",
             ncell,nfeas,best,bg,bs2); }
    return 0; }
