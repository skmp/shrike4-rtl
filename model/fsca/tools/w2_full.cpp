// w2_full.cpp -- BRANCH 2, full-1024 pass + a no-scan exhaustive answer for the single-offset questions.
// Part 1 : for every (w,s1,s2), is t=17 reachable AT ALL (over every KCw and every K1,K2)?  Decided
//          without scanning: J(17) depends on KCw only through I1(17), so enumerate the admissible I1.
//          Same for t=342.  Then measure, for triples where both are individually reachable, the KCw
//          each one demands -- in ppm of the nominal constant.
// Part 2 : best full 1024 score.  Any config scoring >=919 must satisfy one of the 106 largest-|t|
//          offsets, so its KCw lies in the union of those rigorous brackets; that union is scanned densely.
#include "base2.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024]; static bool have[1024];
static const i64 LMAX=1ll<<21, KCMAX=1ll<<28;
static void brk(int t,int w,int s1,int s2,i64&lo,i64&hi){ long double d=fabsl((long double)t)*powl(2.0L,w); int i=t+512;
    long double sc=powl(2.0L,(long double)(s1+s2))/(d*d);
    long double slack=((long double)LMAX/powl(2.0L,s2)+2.0L)+((long double)LMAX/powl(2.0L,s1)+2.0L)*d/powl(2.0L,s2);
    long double l=((long double)JL[i]-slack)*sc,h=((long double)JH[i]+slack)*sc;
    lo=(i64)floorl(l)-8; hi=(i64)ceill(h)+8; if(lo<1)lo=1; if(hi>KCMAX)hi=KCMAX; }
// admissible I1 magnitudes at offset t with K2 free: J in {q,q+1}, q = arr(I1,dx)>>s2.
// The lattice {m*|dx|} has spacing |dx| and the target band width is ~2^s2, so when |dx| <= 2^s2 a hit
// appears within a handful of candidates.  At most CAPN candidates are tried; unresolved is reported.
static const i64 CAPN=200000;
static int adm_i1(int t,int w,int s2,i64&found){ i64 dx=(i64)t<<w; int i=t+512; if(dx==0) return 0;
    i64 ad=llabs(dx);
    i64 c0=(((i64)JL[i]-2)<<s2)/ad-4; if(c0<0)c0=0;
    i64 c1=(((i64)JH[i]+3)<<s2)/ad+4;
    i64 n=c1-c0+1, lim = n>CAPN?CAPN:n;
    for(i64 k=0;k<lim;k++){ i64 m=c0+k; i64 I1 = dx>0? m : -m; i64 q=arr(I1,dx)>>s2;
        if((q>=JL[i]&&q<=JH[i])||(q+1>=JL[i]&&q+1<=JH[i])){ found=m; return 1; } }
    return n>CAPN ? -1 : 0; }
int main(int argc,char**argv){
    { FILE*f=fopen("jwin18.txt","r"); char ln[256];
      while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} } fclose(f); }
    // ================= Part 1 =================
    long long tri=0, r17=0, r342=0, rboth_sep=0, unres=0;
    long double worst_ppm_gap=1e18L, best_ppm_gap=-1e18L;
    struct G{ int w,s1,s2; long double p17,p342,gap; };
    std::vector<G> gaps;
    for(int w=2;w<=24;w++) for(int s1=4;s1<=40;s1++) for(int s2=4;s2<=40;s2++){
        long double nom=157.88L*powl(2.0L,(long double)(s1+s2-2*w)); if(nom<0.5L||nom>=(long double)KCMAX) continue;
        tri++;
        i64 f17=-1,f342=-1; int e17=adm_i1(17,w,s2,f17), e342=adm_i1(342,w,s2,f342);
        if(e17<0||e342<0) unres++;
        // reachability of an I1 magnitude m: need KCw in [1,2^28) and K1 in [0,2^s1) with |I1| = m
        i64 dx17=(i64)17<<w, dx342=(i64)342<<w;
        i64 m17max=(KCMAX*dx17)>>s1, m342max=(KCMAX*dx342)>>s1;
        bool ok17 = (e17==1 && f17>=0 && f17<=m17max), ok342 = (e342==1 && f342>=0 && f342<=m342max);
        long double kc17 = ok17 ? (long double)f17*powl(2.0L,s1)/(long double)dx17 : 0;
        long double kc342 = ok342 ? (long double)f342*powl(2.0L,s1)/(long double)dx342 : 0;
        r17+=ok17; r342+=ok342;
        if(ok17&&ok342){ rboth_sep++;
            // the KCw each offset demands, normalised: relative difference in ppm
            long double g=(kc17-kc342)/kc342*1e6L;
            gaps.push_back({w,s1,s2,kc17,kc342,g});
            if(fabsl(g)<fabsl(worst_ppm_gap)) worst_ppm_gap=g; if(fabsl(g)>fabsl(best_ppm_gap)) best_ppm_gap=g; }
    }
    printf("== part 1: single-offset reachability, decided without scanning ==\n");
    printf("  scale-admissible (w,s1,s2) triples            : %lld\n",tri);
    printf("  triples where SOME (KCw,K1,K2) gives J(17)=%lld : %lld\n",(long long)JL[529],r17);
    printf("  triples where SOME (KCw,K1,K2) gives J(342) ok : %lld\n",r342);
    printf("  triples where each is reachable SEPARATELY     : %lld\n",rboth_sep);
    printf("  triples left unresolved by the candidate cap  : %lld\n",unres);
    std::sort(gaps.begin(),gaps.end(),[](const G&a,const G&b){return fabsl(a.gap)<fabsl(b.gap);});
    printf("  smallest |gap| between the KCw that t=17 wants and the KCw that t=342 wants:\n");
    for(size_t i=0;i<gaps.size()&&i<12;i++) printf("    w=%2d s1=%2d s2=%2d : KCw(17)=%.3Lf  KCw(342)=%.3Lf  gap=%+.2Lf ppm\n",
        gaps[i].w,gaps[i].s1,gaps[i].s2,gaps[i].p17,gaps[i].p342,gaps[i].gap);
    // ================= Part 2 =================
    printf("\n== part 2: best full 1024 score, K1=K2=2^19, exhaustive over KCw ==\n");
    struct R{int w,s1,s2;i64 kc;int sc;};
    std::vector<R> res; long long scanned=0;
    #pragma omp parallel for schedule(dynamic) collapse(2) reduction(+:scanned)
    for(int w=2;w<=24;w++) for(int s1=4;s1<=40;s1++){
      for(int s2=4;s2<=40;s2++){
        long double nom=157.88L*powl(2.0L,(long double)(s1+s2-2*w)); if(nom<0.5L||nom>=(long double)KCMAX) continue;
        i64 L=KCMAX,H=1;
        for(int t=459;t<=511;t++){ i64 a,b; brk(t,w,s1,s2,a,b); if(a<L)L=a; if(b>H)H=b; }
        for(int t=-512;t<=-460;t++){ i64 a,b; brk(t,w,s1,s2,a,b); if(a<L)L=a; if(b>H)H=b; }
        if(H<L) continue; scanned+=H-L+1;
        int best=-1; i64 bkc=0;
        for(i64 kc=L;kc<=H;kc++){
            i64 d5=(i64)511<<w, dm=(i64)-512<<w;
            i64 j5=(arr((arr(kc,d5)+K)>>s1,d5)+K)>>s2;
            i64 jm=(arr((arr(kc,dm)+K)>>s1,dm)+K)>>s2;
            if((j5<JL[1023]-3000||j5>JH[1023]+3000)&&(jm<JL[0]-3000||jm>JH[0]+3000)) continue;
            int sc=0; for(int i=0;i<1024;i++) if(have[i]){ i64 dx=(i64)(i-512)<<w;
                i64 j=(arr((arr(kc,dx)+K)>>s1,dx)+K)>>s2; if(j>=JL[i]&&j<=JH[i])sc++; }
            if(sc>best){best=sc;bkc=kc;} }
        if(best>=900){ 
          #pragma omp critical
          res.push_back({w,s1,s2,bkc,best}); }
      } }
    printf("  KCw values densely scanned: %lld\n",scanned);
    std::sort(res.begin(),res.end(),[](const R&a,const R&b){return a.sc>b.sc;});
    printf("  score |  w  s1  s2 |  KCw\n");
    for(size_t i=0;i<res.size()&&i<20;i++) printf("  %4d  | %2d  %2d  %2d | %lld\n",res[i].sc,res[i].w,res[i].s1,res[i].s2,(long long)res[i].kc);
    printf("  (%zu triples scored >=900)\n",res.size());
    return 0; }
