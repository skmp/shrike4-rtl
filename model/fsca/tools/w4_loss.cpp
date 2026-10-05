// w4_loss.cpp -- BRANCH 4 (a): is the residual an ARRAY-LOSS effect?
//   1. exact analytic bound on loss(), verified against arr()
//   2. how many bits of t reach each multiply's loss, at every alignment w in [2,24]
//   3. the ENVELOPE test: with loss1 and loss2 FREE over the whole loss range (and the rounding
//      constants free too), what J values can the chain reach at each t?  If the required window
//      lies outside that envelope, no array-loss / truncated-column effect can explain the residual.
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>
#include <algorithm>
#include <random>

static i64 JL[1024],JH[1024]; static bool have[1024];
static void load(){ FILE*f=fopen("jwin18.txt","r"); char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} } fclose(f); }

static inline i64 lossof(i64 rec,i64 mul){ return arr(rec,mul) - rec*mul; }

// analytic decomposition, to prove the bound rather than sample it
static i64 loss_analytic(i64 rec,i64 mul){
    i64 R[14]; i64 trunc=0, hotmiss=0;
    for(int k=0;k<14;k++){ auto bit=[&](int b)->int{ return b<0?0:(int)((rec>>(b>62?62:b))&1); };
        int d=-2*bit(2*k+1)+bit(2*k)+bit(2*k-1); i64 mag=(i64)(d<0?-d:d)*mul; i64 row=d<0?~mag:mag;
        R[k]=row*(1ll<<(2*k)); trunc += R[k]-((R[k]>>16)<<16); if(d<0 && 2*k<18) hotmiss += 1ll<<(2*k); }
    int m15=((R[0]>>15)&1)+((R[1]>>15)&1)+((R[2]>>15)&1);
    int p16=((R[6]>>16)^(R[7]>>16)^(R[8]>>16))&1;
    return -trunc - hotmiss + (1ll<<16)*((m15>=2)?1:0) - (1ll<<16)*p16;
}

int main(){ build_tables(); load();
    printf("=========== 1. the loss law and its EXACT range ===========\n");
    { std::mt19937_64 rng(12345); int bad=0;
      for(int i=0;i<2000000;i++){ i64 a=(i64)(rng()%(1ull<<24)), b=(i64)(rng()%(1ull<<24));
        if(rng()&1)a=-a; if(rng()&2)b=-b; if(lossof(a,b)!=loss_analytic(a,b)) bad++; }
      printf("analytic decomposition  loss = -sum(R[k] mod 2^16) - sum(missing hot ones) + 2^16*(g1-g2)\n");
      printf("  verified against arr() on 2,000,000 random pairs, all four sign quadrants: %d mismatches\n",bad); }
    const i64 LMAX = 1ll<<16;                       // +2^16 from the m15 group rule
    const i64 LMIN = -((8ll<<16) - 21845 + 87381 + 65536);
    printf("  analytic bound: loss in [%lld, %lld]   (|loss| <= 10*2^16 = %lld)\n",
        (long long)LMIN,(long long)LMAX,(long long)(10ll<<16));
    { i64 mn=0,mx=0; std::mt19937_64 rng(999);
      for(int i=0;i<4000000;i++){ i64 a=(i64)(rng()%(1ull<<24)), b=(i64)(rng()%(1ull<<24));
        if(rng()&1)a=-a; if(rng()&2)b=-b; i64 l=lossof(a,b); mn=std::min(mn,l); mx=std::max(mx,l); }
      printf("  observed over 4,000,000 random pairs:      [%lld, %lld]\n",(long long)mn,(long long)mx); }
    { i64 mn=0,mx=0;                                 // the actual square-path calls
      for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 l1=lossof(KC,dx);
          i64 I1=(arr(KC,dx)+K)>>23; i64 l2=lossof(I1,dx);
          mn=std::min(mn,std::min(l1,l2)); mx=std::max(mx,std::max(l1,l2)); }
      printf("  observed over the 2048 square-path calls:   [%lld, %lld]\n",(long long)mn,(long long)mx); }

    printf("\n=========== 2. how many BITS OF t reach each multiply's loss, per alignment w ===========\n");
    printf("  loss depends only on (rec mod 2^18, mul mod 2^16).  mul = dx = t<<w.\n");
    printf("  w   dxmod-bits  mul1-distinct  bits-of-t-that-change-loss1   loss2-distinct  bits-that-change-loss2\n");
    for(int w=2;w<=24;w++){
        // distinct loss1 values, and which bits of t matter (flip bit b of t, does loss change for some t?)
        std::set<i64> d1,d2; int mask1=0,mask2=0;
        // use the ideal KC at this alignment so the chain is at the right scale (s1 chosen below)
        // for the bit-reach question only dx matters for loss1 (KC fixed), so keep KC as the real one
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<w; d1.insert(lossof(KC,dx)); }
        // s1 at this w for an in-scale I1: I1 ~ 157.9*t*2^(s2-w); use the canonical split s1=23+(w-13), s2=18+(w-13)
        int s1=23+(w-13), s2=18+(w-13); if(s1<1)s1=1; if(s2<1)s2=1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<w; i64 I1=(arr(KC,dx)+K)>>s1; d2.insert(lossof(I1,dx)); }
        for(int b=0;b<10;b++){ bool ch1=false,ch2=false;
            for(int t=-512;t<=511 && !(ch1&&ch2);t++){ int t2 = b==9 ? (t<0? t+512 : t-512) : (t^(1<<b)); if(t2<-512||t2>511)continue;
                i64 dx=(i64)t<<w, dy=(i64)t2<<w;
                if(lossof(KC,dx)!=lossof(KC,dy)) ch1=true;
                i64 A=(arr(KC,dx)+K)>>s1, B=(arr(KC,dy)+K)>>s1;
                if(lossof(A,dx)!=lossof(B,dy)) ch2=true; }
            if(ch1) mask1|=1<<b; if(ch2) mask2|=1<<b; }
        printf("  %2d      %2d          %3zu         %2d bits (mask 0x%03x)          %4zu        %2d bits (mask 0x%03x)%s\n",
            w, w<16?16-w:0, d1.size(), __builtin_popcount(mask1), mask1, d2.size(), __builtin_popcount(mask2), mask2,
            w==13?"   <- the model":"");
    }

    printf("\n=========== 3. the ENVELOPE at w=13: what can ANY loss behaviour reach? ===========\n");
    printf("  I1 = (KC*dx + L1 + K1) >> 23 ;  J = (I1*dx + L2 + K2) >> 18,  with L1,L2 free in the loss\n");
    printf("  range above and K1 in [0,2^23), K2 in [0,2^18) ALSO free (deliberately over-generous).\n");
    int outside=0, inside=0; i64 worst=0; int worstt=0;
    for(int i=0;i<1024;i++){ if(!have[i])continue; int t=i-512; i64 dx=(i64)t<<13; i64 P1=(i64)KC*dx;
        i64 I1lo=(P1+LMIN)>>23, I1hi=(P1+LMAX+(1ll<<23)-1)>>23;
        // J is monotone in I1 only through I1*dx, whose sign depends on dx
        i64 a=I1lo*dx, b=I1hi*dx; i64 Plo=std::min(a,b), Phi=std::max(a,b);
        i64 Jlo=(Plo+LMIN)>>18, Jhi=(Phi+LMAX+(1ll<<18)-1)>>18;
        bool ok = !(JH[i]<Jlo || JL[i]>Jhi);
        if(ok) inside++; else { outside++; i64 g = JL[i]>Jhi ? JL[i]-Jhi : Jlo-JH[i]; if(g>worst){worst=g;worstt=t;} } }
    printf("  offsets whose required window INTERSECTS the envelope: %d / 1024   outside: %d\n",inside,outside);
    printf("  worst offset t=%d, window %lld units outside the envelope\n",worstt,(long long)worst);
    for(int t : {17,-17,342,-342,376,-376}){ int i=t+512; i64 dx=(i64)t<<13; i64 P1=(i64)KC*dx;
        i64 I1lo=(P1+LMIN)>>23, I1hi=(P1+LMAX+(1ll<<23)-1)>>23;
        i64 a=I1lo*dx,b=I1hi*dx; i64 Plo=std::min(a,b),Phi=std::max(a,b);
        i64 Jlo=(Plo+LMIN)>>18, Jhi=(Phi+LMAX+(1ll<<18)-1)>>18;
        i64 Jm=(arr((arr(KC,dx)+K)>>23,dx)+K)>>18;
        printf("   t=%+4d  model J=%lld  envelope [%lld,%lld] (width %lld)  required [%lld,%lld]  %s\n",
            t,(long long)Jm,(long long)Jlo,(long long)Jhi,(long long)(Jhi-Jlo),(long long)JL[i],(long long)JH[i],
            (JH[i]<Jlo||JL[i]>Jhi)?"*** UNREACHABLE ***":"reachable"); }

    printf("\n=========== 3b. budget accounting at the pinned offsets (w=13) ===========\n");
    printf("   t   required dJ   max dJ from loss2 (=%lld/2^18)   dJ per unit of I1 (=dx/2^18)   max I1 shift from loss1\n",
        (long long)(-LMIN));
    for(int t : {17,342,-342}){ int i=t+512; i64 dx=(i64)t<<13;
        i64 Jm=(arr((arr(KC,dx)+K)>>23,dx)+K)>>18; i64 need = (JL[i]+JH[i])/2 - Jm;
        double l2u=(double)(-LMIN)/262144.0, iu=(double)(dx<0?-dx:dx)/262144.0;
        double i1sh=(double)(-LMIN)/8388608.0;
        printf("   %+4d   %+6lld        %.2f                         %.3f                       %.4f\n",
            t,(long long)need,l2u,iu,i1sh); }
    printf("  => total achievable |dJ| at t=17 is about %.2f latch units; the requirement is 13.\n",
        (double)(-LMIN)/262144.0 + (double)(17ll<<13)/262144.0*1.0 + 1.0);
    return 0; }
