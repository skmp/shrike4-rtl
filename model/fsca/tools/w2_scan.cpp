// w2_scan.cpp -- BRANCH 2: joint (operand alignment w, stored constant KCw) scan.
//   dx = t<<w ; I1 = (arr(KCw,dx)+K1)>>s1 ; J = (arr(I1,dx)+K2)>>s2
// KCw is NOT held at 0x4EF145: at a different alignment the hardware stores a different constant.
// EXHAUSTIVENESS: instead of binary search (monotonicity in KCw FAILS for w<=10, see w2_mono),
// each (w,s1,s2) gets a RIGOROUS bracket of KCw derived from the array loss bound L and the
// latch slack, then the bracket is scanned DENSELY.  Two brackets are used:
//   B17   : every KCw that could possibly make J(17)=45617
//   Bfull : every KCw that could possibly satisfy any t with |t|>=256  (a config scoring >=919
//           must satisfy >=408 of the 513 such t, so it must lie in this union)
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cmath>
static i64 JL[1024],JH[1024]; static bool have[1024];
static const i64 LMAX = 1ll<<21;          // safe bound on |arr(a,b)-a*b| (measured max 483984)
static const i64 KCMAX = 1ll<<28;         // the Booth array recodes 14 digits = bits 0..27
static void loadwin(){ FILE*f=fopen("jwin18.txt","r"); if(!f){printf("no jwin18.txt\n");exit(1);} char ln[256];
  while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
    if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} } fclose(f); }
static inline i64 chainJ(int t,int w,int s1,int s2,i64 KCw,i64 K1,i64 K2){
    i64 dx=(i64)t<<w; i64 I1=(arr(KCw,dx)+K1)>>s1; return (arr(I1,dx)+K2)>>s2; }
// rigorous KCw bracket for offset t
static void bracket(int t,int w,int s1,int s2,i64&lo,i64&hi){
    long double d=fabsl((long double)t)*powl(2.0L,w); int i=t+512;
    long double sc=powl(2.0L,(long double)(s1+s2))/(d*d);
    long double slack=( (long double)LMAX/powl(2.0L,s2)+2.0L ) + ( (long double)LMAX/powl(2.0L,s1)+2.0L )*d/powl(2.0L,s2);
    long double l=((long double)JL[i]-slack)*sc, h=((long double)JH[i]+slack)*sc;
    lo=(i64)floorl(l)-8; hi=(i64)ceill(h)+8; if(lo<1)lo=1; if(hi>KCMAX)hi=KCMAX; }
struct Res{ int w,s1,s2; i64 kc17,kcbest; int best; int hit17,hit342,hitm342; long long scanned; int capped; };
int main(int argc,char**argv){
    loadwin();
    long long CAP = argc>1? atoll(argv[1]) : 400000;      // max KCw values scanned per (w,s1,s2)
    std::vector<Res> out; std::vector<int> ws;
    for(int w=2;w<=24;w++) ws.push_back(w);
    long long tot_tri=0, tot_scan=0, tri_with17=0, tri_with17and342=0, tri_all3=0, capped=0;
    std::vector<Res> results;
    #pragma omp parallel for schedule(dynamic) collapse(2)
    for(int wi=0;wi<23;wi++) for(int s1=4;s1<=40;s1++){
        int w=ws[wi];
        for(int s2=4;s2<=40;s2++){
            // scale sanity: some integer KCw in [1,2^28) must be able to produce J~157.88*t^2
            long double nom=157.88L*powl(2.0L,(long double)(s1+s2-2*w));
            if(nom<0.5L||nom>=(long double)KCMAX) continue;
            i64 lo17,hi17; bracket(17,w,s1,s2,lo17,hi17);
            i64 lof=lo17,hif=hi17;
            for(int t=-512;t<=511;t++){ if(abs(t)<256||!have[t+512]) continue; i64 a,b; bracket(t,w,s1,s2,a,b);
                if(a<lof)lof=a; if(b>hif)hif=b; }
            bool cap = (hif-lof+1)>CAP; if(cap){ hif=lof+CAP-1; }
            int best=-1; i64 kcbest=0, kc17=-1; int h17=0,h342=0,hm=0; i64 kc342=-1;
            for(i64 kc=lof;kc<=hif;kc++){
                i64 j17=chainJ(17,w,s1,s2,kc,K,K);
                if(j17==JL[529]){ h17=1; if(kc17<0)kc17=kc;
                    i64 j342=chainJ(342,w,s1,s2,kc,K,K);
                    if(j342>=JL[854]&&j342<=JH[854]){ h342=1; if(kc342<0)kc342=kc;
                        i64 jm=chainJ(-342,w,s1,s2,kc,K,K); if(jm>=JL[170]&&jm<=JH[170]) hm=1; } }
                // full score only where it can matter: cheap prefilter on t=511
                i64 j511=chainJ(511,w,s1,s2,kc,K,K);
                if(j511<JL[1023]-4000||j511>JH[1023]+4000) continue;
                int sc=0; for(int i=0;i<1024;i++) if(have[i]){ i64 j=chainJ(i-512,w,s1,s2,kc,K,K); if(j>=JL[i]&&j<=JH[i])sc++; }
                if(sc>best){best=sc;kcbest=kc;}
            }
            #pragma omp critical
            { results.push_back({w,s1,s2,kc17,kcbest,best,h17,h342,hm,hif-lof+1,cap?1:0});
              tot_tri++; tot_scan+=hif-lof+1; tri_with17+=h17; tri_with17and342+=h342; tri_all3+=hm; capped+=cap; }
        }
    }
    printf("triples (w,s1,s2) with a scale-admissible KCw: %lld\n",tot_tri);
    printf("total KCw values densely scanned:              %lld\n",tot_scan);
    printf("triples where the bracket hit the %lld cap:     %lld\n",CAP,capped);
    printf("triples where SOME KCw gives J(17)=45617:      %lld\n",tri_with17);
    printf("  ... and the same KCw also gives J(342)=18466836: %lld\n",tri_with17and342);
    printf("  ... and also J(-342) in window:                  %lld\n",tri_all3);
    std::sort(results.begin(),results.end(),[](const Res&a,const Res&b){return a.best>b.best;});
    printf("\ntop full-1024 scores (K1=K2=2^19):\n  score |  w  s1  s2 |  KCw      | h17 h342 hm | bracket\n");
    for(size_t i=0;i<results.size()&&i<25;i++){ Res&r=results[i];
        printf("  %4d  | %2d  %2d  %2d | %9lld | %d   %d    %d  | %lld%s\n",
            r.best,r.w,r.s1,r.s2,(long long)r.kcbest,r.hit17,r.hit342,r.hitm342,r.scanned,r.capped?" CAP":""); }
    printf("\ntriples that hit t=17, with their score:\n");
    int n=0; for(auto&r:results) if(r.hit17){ if(n++<40) printf("  w=%2d s1=%2d s2=%2d  KCw(first hitting 17)=%lld  h342=%d hm=%d  best full=%d\n",
        r.w,r.s1,r.s2,(long long)r.kc17,r.hit342,r.hitm342,r.best); }
    printf("  (total %d)\n",n);
    return 0; }
