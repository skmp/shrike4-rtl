// w4_wscan.cpp -- BRANCH 4 (a)/(c): the JOINT (alignment w, KC) scan.
// NOTES.md 2026-09-18 leaves exactly this gap open: "a different operand alignment w changes the lattice
// spacing ... that is exactly the joint (w, KC) scan -- the one gap still open, since the earlier width
// scan held KC fixed."
//   chain:  I1 = (arr(KC,dx) + K1) >> s1 ;  J = (arr(I1,dx) + K2) >> s2 ,  dx = t << w
//   scale law: KC ~ 4*pi^2 * 2^(s1+s2-2w+2)   (w=13,s1=23,s2=18 -> 4pi^2*2^17 = the real KC)
// Free: w, s1, s2, both operand roles, three rounding constants each, KC in ideal +- 4096.
// Phase 0: capability map (envelope with loss AND constants free) -- which (w,s1,s2) can even reach the pins.
// Phase 1: exact scan, filter on J(17)==45617, then |342|, then the full 1024 score.
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
#include <array>
#include <cmath>
#include <omp.h>
static i64 JL[1024],JH[1024]; static bool have[1024];
static void load(){ FILE*f=fopen("jwin18.txt","r"); char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} } fclose(f); }
static const i64 LMAX=1ll<<16, LMIN=-655360;
struct Cfg{int w,s1,s2,r1,r2;i64 K1,K2,KC;};
static inline i64 Jof(const Cfg&c,int t){ i64 dx=(i64)t<<c.w;
    i64 A1 = c.r1? arr(dx,c.KC) : arr(c.KC,dx); i64 I1=(A1+c.K1)>>c.s1;
    i64 A2 = c.r2? arr(dx,I1) : arr(I1,dx); return (A2+c.K2)>>c.s2; }
int main(){ load();
    long double PI4=39.478417604357434475L;     // 4*pi^2
    // ---------- phase 0: capability envelope ----------
    printf("=== phase 0: CAPABILITY (loss L1,L2 free in [%lld,%lld], K1,K2 free) ===\n",(long long)LMIN,(long long)LMAX);
    printf("   listing (w,s1,s2) whose generous envelope contains ALL THREE pinned targets\n");
    int ncap=0; std::vector<std::array<int,3>> caps;
    for(int w=2;w<=24;w++) for(int s1=2;s1<=44;s1++) for(int s2=2;s2<=44;s2++){
        int E=s1+s2-2*w+2; if(E<6||E>22) continue;
        long double kcf=PI4*powl(2.0L,(long double)E); if(kcf>=(long double)(1ll<<27)) continue;
        i64 KCi=(i64)(kcf+0.5L); if(KCi<64) continue;
        // I1 must fit the array's 28-bit recoded operand
        long double i1max=kcf*512*powl(2.0L,(long double)w)/powl(2.0L,(long double)s1);
        if(i1max>=(long double)(1ll<<27)) continue;
        if(512.0L*powl(2.0L,(long double)w)>=(long double)(1ll<<27)) continue;
        bool all=true;
        for(int t : {17,342,-342}){ int i=t+512; i64 dx=(i64)t<<w; i64 P1=(i64)KCi*dx;
            i64 I1lo=(P1+LMIN)>>s1, I1hi=(P1+LMAX+((1ll<<s1)-1))>>s1;
            i64 a=I1lo*dx,b=I1hi*dx; i64 Plo=std::min(a,b),Phi=std::max(a,b);
            i64 Jlo=(Plo+LMIN)>>s2, Jhi=(Phi+LMAX+((1ll<<s2)-1))>>s2;
            if(JH[i]<Jlo||JL[i]>Jhi){all=false;break;} }
        if(all){ ncap++; if(ncap<=40) printf("   w=%2d s1=%2d s2=%2d  E=%2d KC~%lld (%d bits)  lattice step at |t|=342: %.3f\n",
            w,s1,s2,E,(long long)KCi,64-__builtin_clzll((unsigned long long)KCi),(double)(342.0*pow(2.0,(double)(w-s2))));
            caps.push_back(std::array<int,3>{w,s1,s2}); } }
    printf("   capable (w,s1,s2) configurations: %d\n",ncap);
    // ---------- phase 1: exact scan ----------
    printf("\n=== phase 1: exact scan, KC in ideal+-4096, roles x 9 constant pairs ===\n");
    long long tried=0, pass17=0, pass342=0; int best=0; Cfg bestc{}; 
    std::vector<Cfg> hits;
    #pragma omp parallel
    {
      long long ltried=0,lp17=0,lp342=0; int lbest=0; Cfg lbestc{}; std::vector<Cfg> lhits;
      #pragma omp for schedule(dynamic)
      for(int w=2;w<=24;w++){
        if(512.0*pow(2.0,w)>=(double)(1ll<<27)) continue;
        for(int s1=2;s1<=44;s1++) for(int s2=2;s2<=44;s2++){
          int E=s1+s2-2*w+2; if(E<6||E>22) continue;
          double kcf=39.478417604357434*pow(2.0,(double)E); if(kcf>=(double)(1ll<<27)) continue;
          i64 KCi=(i64)(kcf+0.5); if(KCi<64) continue;
          if(kcf*512*pow(2.0,w)/pow(2.0,s1) >= (double)(1ll<<27)) continue;
          for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++)
          for(int ka=0;ka<3;ka++) for(int kb=0;kb<3;kb++){
            i64 K1 = ka==0? (1ll<<19) : ka==1? (s1>=1?(1ll<<(s1-1)):0) : 0;
            i64 K2 = kb==0? (1ll<<19) : kb==1? (s2>=1?(1ll<<(s2-1)):0) : 0;
            for(i64 KC=KCi-4096;KC<=KCi+4096;KC++){
              if(KC<8) continue;
              // cheap exact-product estimate to skip hopeless KC (margin covers the whole loss envelope)
              __int128 dx17=(__int128)17<<w; __int128 p1=(__int128)KC*dx17;
              __int128 i1=(p1+K1)>>s1; __int128 j=((i1*dx17)+K2)>>s2;
              i64 marg = (i64)(( (__int128)655360>>s2 ) + (((__int128)17<<w)>>s2) + ((__int128)655360*((__int128)17<<w)>>(s1+s2)) + 4);
              if(j < 45617-marg || j > 45617+marg) continue;
              ltried++;
              Cfg c{w,s1,s2,r1,r2,K1,K2,KC};
              if(Jof(c,17)!=45617) continue;
              lp17++;
              i64 ja=Jof(c,342), jb=Jof(c,-342);
              if(ja<JL[512+342]||ja>JH[512+342]) continue;
              if(jb<JL[512-342]||jb>JH[512-342]) continue;
              lp342++;
              int sc=0; for(int i=0;i<1024;i++) if(have[i]){ i64 v=Jof(c,i-512); if(v>=JL[i]&&v<=JH[i]) sc++; }
              if(sc>lbest){lbest=sc;lbestc=c;} if(sc>=900) lhits.push_back(c);
            } }
        } }
      #pragma omp critical
      { tried+=ltried; pass17+=lp17; pass342+=lp342; if(lbest>best){best=lbest;bestc=lbestc;}
        for(auto&h:lhits) hits.push_back(h); }
    }
    printf("   configs whose estimate was in range (arr() actually evaluated): %lld\n",tried);
    printf("   passing J(17)==45617 : %lld\n",pass17);
    printf("   ALSO passing both |342| windows : %lld\n",pass342);
    if(pass342){ printf("   best full score %d/1024 at w=%d s1=%d s2=%d r=(%d,%d) K1=%lld K2=%lld KC=%lld\n",
        best,bestc.w,bestc.s1,bestc.s2,bestc.r1,bestc.r2,(long long)bestc.K1,(long long)bestc.K2,(long long)bestc.KC);
      int n=0; for(auto&c:hits){ if(n++>=10)break;
        printf("     >=900: w=%d s1=%d s2=%d r=(%d,%d) K1=%lld K2=%lld KC=%lld\n",c.w,c.s1,c.s2,c.r1,c.r2,(long long)c.K1,(long long)c.K2,(long long)c.KC);} }
    return 0; }
