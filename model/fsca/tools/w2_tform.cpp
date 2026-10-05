// w2_tform.cpp -- the USER's two hypotheses, retested JOINTLY with a free stored constant.
//   H-A  "t is computed in 2s complement but consumed as 1s complement"  -> t_eff = t + [t<0]
//        (and every relative: the reverse, dx-level, feedback-level, and wrong-width sign extension)
//   H-B  "t carries one extra bit of precision"                          -> dx = (2t+b)<<(w-1)
//        = t<<w + b*2^(w-1); b uniform (+-1) or magnitude-wise (a half-unit away from zero).
// The previous exclusion of these tested them with KC PINNED at 0x4EF145.  Here the operand form,
// the alignment w, both shifts AND the stored constant move together; KCw is scanned exhaustively
// over the rigorous t=342 bracket.  Latch constants K1=K2=2^19 fixed here (free K in w2_land).
//   dx(t) = (t<<w) + p + q*[t<0] ;  I1 = (arr(KCw,dx)+K1)>>s1 (+ r*[t<0]) ;  J = (arr(I1,dx)+K2)>>s2
#include "base2.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024]; static bool have[1024];
static const i64 LMAX=1ll<<21, KCMAX=1ll<<28;
// bracket computed from the ACTUAL operand magnitude |dx| at that offset, so a biased operand gets
// the compensating stored constant scanned too (a half-ULP bias needs KCw ~0.3% away at t=342).
static void brkd(int t,long double d,int s1,int s2,i64&lo,i64&hi){ int i=t+512;
    long double sc=powl(2.0L,(long double)(s1+s2))/(d*d);
    long double slack=((long double)LMAX/powl(2.0L,s2)+2.0L)+((long double)LMAX/powl(2.0L,s1)+2.0L)*d/powl(2.0L,s2);
    long double l=((long double)JL[i]-slack)*sc,h=((long double)JH[i]+slack)*sc;
    lo=(i64)floorl(l)-12; hi=(i64)ceill(h)+12; if(lo<1)lo=1; if(hi>KCMAX)hi=KCMAX; }
struct Var{ const char* name; int pk, qk, r; };   // p = pk-th of PSET, q = qk-th of QSET (both scaled by w)
// p/q are expressed in units of 2^w (the ULP of t) times a rational, encoded as (num,den_log2)
struct Off{ int num, dlog; };                      // value = num * 2^(w-dlog)
static inline i64 off(Off o,int w){ int sh=w-o.dlog; return sh>=0 ? (i64)o.num<<sh : (i64)o.num>>(-sh); }
int main(){
    { FILE*f=fopen("jwin18.txt","r"); char ln[256];
      while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} } fclose(f); }
    struct Cand{ const char* name; Off p, q; int r; };
    std::vector<Cand> cands = {
      {"baseline  t_eff = t",                         {0,0},{0,0},0},
      {"H-A 2sC formed, 1sC used   t<0 -> t+1",       {0,0},{1,0},0},
      {"H-A reversed  1sC formed, 2sC used t<0 -> t-1",{0,0},{-1,0},0},
      {"H-A at dx level            dx<0 -> dx+1",     {0,0},{1,99},0},
      {"H-A at dx level            dx<0 -> dx-1",     {0,0},{-1,99},0},
      {"H-A on the I1 feedback     I1<0 -> I1+1",     {0,0},{0,0},1},
      {"H-A on the I1 feedback     I1<0 -> I1-1",     {0,0},{0,0},-1},
      {"H-A both dx and feedback",                    {0,0},{1,0},1},
      {"H-B extra bit, t_eff = t + 1/2 (uniform)",    {1,1},{0,0},0},
      {"H-B extra bit, t_eff = t - 1/2 (uniform)",    {-1,1},{0,0},0},
      {"H-B extra bit, magnitude + 1/2",              {1,1},{-1,0},0},
      {"H-B extra bit, magnitude - 1/2",              {-1,1},{1,0},0},
      {"H-B extra bit, t_eff = t + 1/4",              {1,2},{0,0},0},
      {"H-B extra bit, t_eff = t - 1/4",              {-1,2},{0,0},0},
      {"H-B extra bit + H-A complement",              {1,1},{1,0},0},
      {"H-B extra bit - , + H-A complement",          {-1,1},{1,0},0},
      {"sign-extension lost 1 bit  t<0 -> t+1024",    {0,0},{1024,0},0},
      {"sign-extension lost 1 bit  t<0 -> t-1024",    {0,0},{-1024,0},0},
    };
    printf("landmarks: J(17)=%lld  J(342)=%lld  J(-342) in [%lld,%lld]\n\n",
        (long long)JL[529],(long long)JL[854],(long long)JL[170],(long long)JH[170]);
    printf("%-46s | triples | any KCw hits t=342 | ...and t=17 | ...and t=-342 | best full 1024\n",
        "operand-formation variant (with KCw FREE)");
    for(auto&C:cands){
        long long tri=0,h342=0,hboth=0,hall=0; int bestfull=-1; int bw=0,bs1=0,bs2=0; i64 bkc=0;
        #pragma omp parallel for schedule(dynamic) collapse(2) reduction(+:tri,h342,hboth,hall)
        for(int w=2;w<=24;w++) for(int s1=4;s1<=40;s1++){
          for(int s2=4;s2<=40;s2++){
            long double nom=157.88L*powl(2.0L,(long double)(s1+s2-2*w)); if(nom<0.5L||nom>=(long double)KCMAX) continue;
            i64 p = C.p.dlog==99 ? (i64)C.p.num : off(C.p,w);
            i64 q = C.q.dlog==99 ? (i64)C.q.num : off(C.q,w);
            long double dp=fabsl((long double)(((i64)342<<w)+p)), dm=fabsl((long double)(((i64)-342<<w)+p+q));
            if(dp<1||dm<1) continue;
            i64 a1,b1,a2,b2; brkd(342,dp,s1,s2,a1,b1); brkd(-342,dm,s1,s2,a2,b2);
            i64 lo=std::min(a1,a2),hi=std::max(b1,b2); if(hi<lo) continue; tri++;
            int f342=0,fb=0,fa=0; int loc_best=-1; i64 loc_kc=0;
            for(i64 kc=lo;kc<=hi;kc++){
                auto J=[&](int t)->i64{ i64 dx=((i64)t<<w)+p+(t<0?q:0);
                    i64 I1=(arr(kc,dx)+K)>>s1; if(t<0) I1+=C.r; return (arr(I1,dx)+K)>>s2; };
                i64 jp=J(342); if(jp<JL[854]||jp>JH[854]) continue; f342=1;
                if(J(17)!=JL[529]) continue; fb=1;
                i64 jm=J(-342); if(jm>=JL[170]&&jm<=JH[170]) fa=1;
                int sc=0; for(int i=0;i<1024;i++) if(have[i]){ int t=i-512; i64 dx=((i64)t<<w)+p+(t<0?q:0);
                    i64 I1=(arr(kc,dx)+K)>>s1; if(t<0) I1+=C.r; i64 j=(arr(I1,dx)+K)>>s2;
                    if(j>=JL[i]&&j<=JH[i])sc++; }
                if(sc>loc_best){loc_best=sc;loc_kc=kc;}
            }
            h342+=f342; hboth+=fb; hall+=fa;
            if(loc_best>0){
              #pragma omp critical
              { if(loc_best>bestfull){bestfull=loc_best;bw=w;bs1=s1;bs2=s2;bkc=loc_kc;} } }
          } }
        printf("%-46s | %7lld | %18lld | %11lld | %13lld | ",C.name,tri,h342,hboth,hall);
        if(bestfull>=0) printf("%d (w=%d s1=%d s2=%d KCw=%lld)\n",bestfull,bw,bs1,bs2,(long long)bkc);
        else printf("-- (no config hit both landmarks)\n");
        fflush(stdout);
    }
    // the rigidity statement, printed as numbers
    printf("\n-- why every sign-dependent variant is inert on the positive branch (baseline geometry) --\n");
    for(auto&C:cands){ int w=13,s1=23,s2=18;
        i64 p=C.p.dlog==99?(i64)C.p.num:off(C.p,w), q=C.q.dlog==99?(i64)C.q.num:off(C.q,w);
        auto J=[&](int t)->i64{ i64 dx=((i64)t<<w)+p+(t<0?q:0); i64 I1=(arr(KC,dx)+K)>>s1; if(t<0) I1+=C.r; return (arr(I1,dx)+K)>>s2; };
        int sc=0; for(int i=0;i<1024;i++) if(have[i]){ int t=i-512; i64 dx=((i64)t<<w)+p+(t<0?q:0);
            i64 I1=(arr(KC,dx)+K)>>s1; if(t<0) I1+=C.r; i64 j=(arr(I1,dx)+K)>>s2; if(j>=JL[i]&&j<=JH[i])sc++; }
        printf("  %-46s J(17)=%8lld (need %lld)  J(342)=%9lld (need %lld)  J(-342)=%9lld  D=%+4lld  score %d/1024\n",
            C.name,(long long)J(17),(long long)JL[529],(long long)J(342),(long long)JL[854],(long long)J(-342),
            (long long)(J(-342)-J(342)),sc); }
    return 0; }
