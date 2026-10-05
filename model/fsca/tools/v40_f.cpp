// v40_f.cpp -- spot-checks of the branch-4 headline configurations, and the ENVELOPE with the
// rounding constant's INTEGER PART free (the dimension both w4_loss.cpp and my first pass dropped).
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
static i64 JLv[1024],JHv[1024];
static bool loadwin(){ FILE*f=fopen("jwin18.txt","r"); if(!f)return false; char ln[256]; int n=0;
  while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,a,b,w; if(sscanf(ln,"%lld %lld %lld %lld",&t,&a,&b,&w)!=4)continue; JLv[t+512]=a;JHv[t+512]=b;n++; } fclose(f); return n==1024; }
int main(){ if(!loadwin()){printf("FATAL\n");return 1;}
    // ---- 1. the claimed family best: w=7 s1=15 s2=16 roles=(0,0) K1=2^14 K2=2^15 KC=20694296
    { int w=7,s1=15,s2=16; i64 kc=20694296,k1=1ll<<14,k2=1ll<<15; int sc=0; i64 j17=0,j342=0,jm=0;
      for(int t=-512;t<512;t++){ i64 dx=(i64)t<<w; i64 I1=(arr(kc,dx)+k1)>>s1; i64 J=(arr(I1,dx)+k2)>>s2;
        if(J>=JLv[t+512]&&J<=JHv[t+512])sc++; if(t==17)j17=J; if(t==342)j342=J; if(t==-342)jm=J; }
      printf("[f1] claimed family best (w=7,s1=15,s2=16,KC=20694296,K1=2^14,K2=2^15): score %d/1024 (claimed 936) ; J(17)=%lld J(342)=%lld J(-342)=%lld\n",sc,(long long)j17,(long long)j342,(long long)jm); }
    // ---- 2. exp2: operand shifted relative to fixed columns
    { int best=0,bj1=0,bj2=0;
      for(int j1=-6;j1<=6;j1++)for(int j2=-6;j2<=6;j2++){ int s1=23+j1,s2=18+j2; if(s1<1||s2<1)continue;
        int sc=0; for(int t=-512;t<512;t++){ i64 d1=(i64)t<<(13+j1), d2=(i64)t<<(13+j2);
          i64 I1=(arr(KC,d1)+K)>>s1; i64 J=(arr(I1,d2)+K)>>s2; if(J>=JLv[t+512]&&J<=JHv[t+512])sc++; }
        if(sc>best){best=sc;bj1=j1;bj2=j2;} }
      printf("[f2] exp2 (dx=t<<(13+j), shift compensated, KC held): best %d/1024 at j1=%+d j2=%+d (claimed 925 at 0,+1)\n",best,bj1,bj2); }
    // ---- 3. ENVELOPE with the rounding constant's INTEGER PART free  (c = K2 >> s2, a shared J offset)
    { const i64 LO=-655360, HI=65536; i64 elo[1024],ehi[1024];
      for(int t=-512;t<512;t++){ i64 dx=(i64)t<<13; __int128 base=(__int128)KC*dx;
        __int128 nlo=base+LO, nhi=base+HI+((1ll<<23)-1);
        i64 I1lo=(i64)((nlo>=0?nlo:nlo-((1ll<<23)-1))/(1ll<<23)), I1hi=(i64)((nhi>=0?nhi:nhi-((1ll<<23)-1))/(1ll<<23));
        __int128 a=(__int128)I1lo*dx,b=(__int128)I1hi*dx, plo=std::min(a,b), phi=std::max(a,b);
        __int128 qlo=plo+LO, qhi=phi+HI+((1ll<<18)-1);
        elo[t+512]=(i64)((qlo>=0?qlo:qlo-((1ll<<18)-1))/(1ll<<18)); ehi[t+512]=(i64)((qhi>=0?qhi:qhi-((1ll<<18)-1))/(1ll<<18)); }
      auto cr=[&](int t,i64&lo,i64&hi){ lo=JLv[t+512]-ehi[t+512]; hi=JHv[t+512]-elo[t+512]; };
      i64 a1,b1,a2,b2,a3,b3; cr(17,a1,b1); cr(342,a2,b2); cr(-342,a3,b3);
      printf("[f3] shared integer offset c on J (the part of K2 above 2^s2, which the published envelope silently fixed at 0):\n");
      printf("     t=17   needs c in [%lld,%lld] ;  t=342 needs c in [%lld,%lld] ;  t=-342 needs c in [%lld,%lld]\n",
          (long long)a1,(long long)b1,(long long)a2,(long long)b2,(long long)a3,(long long)b3);
      i64 lo=std::max(a1,std::max(a2,a3)), hi=std::min(b1,std::min(b2,b3));
      printf("     intersection: %s -> the three landmarks stay jointly unreachable even with the constant's integer part free\n", lo<=hi?"NON-EMPTY (!!)":"EMPTY");
      int bestout=1<<30,bestc=0; for(i64 c=-64;c<=64;c++){ int out=0; for(int t=-512;t<512;t++)
          if(ehi[t+512]+c<JLv[t+512]||elo[t+512]+c>JHv[t+512]) out++; if(out<bestout){bestout=out;bestc=(int)c;} }
      printf("     best single c over all 1024 offsets: c=%d leaves %d offsets outside the envelope (c=0 leaves 34)\n",bestc,bestout); }
    // ---- 4. capability map at the canonical split s=(23+(w-13), 18+(w-13)), KC rescaled by the scale law
    { const i64 LO=-655360, HI=65536; printf("[f4] canonical-split t=17 envelope per alignment (KC rescaled to hold the scale):\n");
      for(int w=2;w<=18;w++){ int s1=23+(w-13), s2=18+(w-13); if(s1<2||s2<2) continue;
        long double kcf=157.884620L*powl(2.0L,(long double)(s1+s2-2*w)); i64 kc=(i64)(kcf+0.5L);
        i64 dx=(i64)17<<w; __int128 base=(__int128)kc*dx;
        __int128 nlo=base+LO, nhi=base+HI+((1ll<<s1)-1);
        i64 I1lo=(i64)((nlo>=0?nlo:nlo-((1ll<<s1)-1))/(1ll<<s1)), I1hi=(i64)((nhi>=0?nhi:nhi-((1ll<<s1)-1))/(1ll<<s1));
        __int128 a=(__int128)I1lo*dx,b=(__int128)I1hi*dx, plo=std::min(a,b), phi=std::max(a,b);
        i64 Jlo=(i64)((plo+LO)/(1ll<<s2)), Jhi=(i64)((phi+HI+((1ll<<s2)-1))/(1ll<<s2));
        printf("     w=%2d s=(%2d,%2d) KC=%lld : t=17 envelope [%lld,%lld]  %s 45617\n",w,s1,s2,(long long)kc,
            (long long)Jlo,(long long)Jhi,(Jlo<=45617&&45617<=Jhi)?"CONTAINS":"excludes"); } }
    return 0; }
