// w4_wscan2.cpp -- BRANCH 4 (a): the joint (alignment w, KC) family scored on ALL 1024 offsets, via a
// cascade: exact-product estimate at t=342 -> 8 sampled offsets -> 64 sampled offsets -> full 1024.
// Also counts, exactly, how many configurations hit each pinned landmark.
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
#include <omp.h>
static i64 JL[1024],JH[1024]; static bool have[1024];
static void load(){ FILE*f=fopen("jwin18.txt","r"); char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} } fclose(f); }
struct Cfg{int w,s1,s2,r1,r2;i64 K1,K2,KC;int sc;};
static inline i64 Jof(const Cfg&c,int t){ i64 dx=(i64)t<<c.w;
    i64 A1 = c.r1? arr(dx,c.KC) : arr(c.KC,dx); i64 I1=(A1+c.K1)>>c.s1;
    i64 A2 = c.r2? arr(dx,I1) : arr(I1,dx); return (A2+c.K2)>>c.s2; }
static const int S8[8]={-500,-341,-200,-61,37,150,283,451};
static const int S64[64]={-509,-497,-471,-450,-433,-412,-399,-377,-360,-343,-329,-311,-298,-277,-260,-241,
 -229,-211,-197,-180,-163,-149,-131,-117,-99,-83,-67,-51,-37,-23,-11,-3,5,13,27,41,55,71,89,103,119,137,
 151,167,183,199,217,233,251,269,287,301,319,337,353,371,389,407,423,441,459,477,493,507};
int main(){ load();
    long long est=0,p8=0,p64=0,full=0,hit17=0,hit342=0,hitm342=0,hitall=0;
    int gbest=0; Cfg gbc{}; std::vector<int> perw(25,0);
    #pragma omp parallel
    { long long lest=0,l8=0,l64=0,lfull=0,h17=0,h342=0,hm342=0,hall=0; int lbest=0; Cfg lbc{}; std::vector<int> lperw(25,0);
      #pragma omp for schedule(dynamic)
      for(int w=2;w<=24;w++){
        if(512.0*pow(2.0,w)>=(double)(1ll<<27)) continue;
        for(int s1=2;s1<=44;s1++) for(int s2=2;s2<=44;s2++){
          int E=s1+s2-2*w+2; if(E<6||E>22) continue;
          double kcf=39.478417604357434*pow(2.0,(double)E); if(kcf>=(double)(1ll<<27)) continue;
          i64 KCi=(i64)(kcf+0.5); if(KCi<64) continue;
          if(kcf*512*pow(2.0,w)/pow(2.0,s1) >= (double)(1ll<<27)) continue;
          i64 marg = (i64)(( (__int128)655360>>s2 ) + (((__int128)342<<w)>>s2) + ((__int128)655360*((__int128)342<<w)>>(s1+s2)) + 4);
          for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++)
          for(int ka=0;ka<3;ka++) for(int kb=0;kb<3;kb++){
            i64 K1 = ka==0? (1ll<<19) : ka==1? (s1>=1?(1ll<<(s1-1)):0) : 0;
            i64 K2 = kb==0? (1ll<<19) : kb==1? (s2>=1?(1ll<<(s2-1)):0) : 0;
            for(i64 KC=KCi-4096;KC<=KCi+4096;KC++){
              if(KC<8) continue;
              __int128 dx=(__int128)342<<w; __int128 i1=((__int128)KC*dx+K1)>>s1; __int128 j=((i1*dx)+K2)>>s2;
              if(j < (__int128)JL[512+342]-marg || j > (__int128)JH[512+342]+marg) continue;
              lest++;
              Cfg c{w,s1,s2,r1,r2,K1,K2,KC,0};
              i64 a17=Jof(c,17), a342=Jof(c,342), am=Jof(c,-342);
              bool b17=(a17==45617), b342=(a342>=JL[854]&&a342<=JH[854]), bm=(am>=JL[170]&&am<=JH[170]);
              if(b17)h17++; if(b342)h342++; if(bm)hm342++; if(b17&&b342&&bm)hall++;
              int s=0; for(int k=0;k<8;k++){ int i=S8[k]+512; i64 v=Jof(c,S8[k]); if(v>=JL[i]&&v<=JH[i])s++; }
              if(s<7) continue; l8++;
              s=0; for(int k=0;k<64;k++){ int i=S64[k]+512; i64 v=Jof(c,S64[k]); if(v>=JL[i]&&v<=JH[i])s++; }
              if(s<56) continue; l64++;
              int sc=0; for(int i=0;i<1024;i++) if(have[i]){ i64 v=Jof(c,i-512); if(v>=JL[i]&&v<=JH[i])sc++; }
              lfull++;
              if(sc>lperw[w]) lperw[w]=sc;
              if(sc>lbest){ lbest=sc; lbc=c; lbc.sc=sc; }
            } } } }
      #pragma omp critical
      { est+=lest;p8+=l8;p64+=l64;full+=lfull;hit17+=h17;hit342+=h342;hitm342+=hm342;hitall+=hall;
        if(lbest>gbest){gbest=lbest;gbc=lbc;} for(int w=0;w<25;w++) if(lperw[w]>perw[w]) perw[w]=lperw[w]; }
    }
    printf("configs reaching arr() evaluation : %lld\n",est);
    printf("  hitting J(17)=45617            : %lld\n",hit17);
    printf("  hitting the J(342) window      : %lld\n",hit342);
    printf("  hitting the J(-342) window     : %lld\n",hitm342);
    printf("  hitting ALL THREE              : %lld\n",hitall);
    printf("cascade: >=7/8 -> %lld   >=56/64 -> %lld   full-scored -> %lld\n",p8,p64,full);
    printf("best per alignment w (max full score reached by the cascade):\n");
    for(int w=2;w<=24;w++) if(perw[w]) printf("   w=%2d : %4d/1024%s\n",w,perw[w],w==13?"   <- the model's alignment (baseline 918)":"");
    printf("\nGLOBAL BEST %d/1024  w=%d s1=%d s2=%d roles=(%d,%d) K1=%lld K2=%lld KC=%lld\n",
        gbest,gbc.w,gbc.s1,gbc.s2,gbc.r1,gbc.r2,(long long)gbc.K1,(long long)gbc.K2,(long long)gbc.KC);
    if(gbest) printf("  landmarks there: J(17)=%lld (need 45617)  J(342)=%lld (need 18466836)  J(-342)=%lld (need [18466858,18466867])\n",
        (long long)Jof(gbc,17),(long long)Jof(gbc,342),(long long)Jof(gbc,-342));
    return 0; }
