// w2_mono.cpp -- branch 2 groundwork:
//  (1) reproduce the baseline J-rule score against jwin18.txt
//  (2) measure the array's loss law  arr(rec,mul) - rec*mul  over the operand ranges this branch uses
//  (3) VERIFY the monotonicity assumption the exhaustive KCw binary search relies on:
//      is J(t) non-decreasing in KCw, for every t, over a wide constant range and many alignments?
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024]; static bool have[1024];
static void loadwin(){ FILE*f=fopen("jwin18.txt","r"); if(!f){printf("no jwin18.txt\n");exit(1);} char ln[256];
  while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
    if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} } fclose(f); }
// generic chain: dx=t<<w ; I1=(arr(KCw,dx)+K1)>>s1 ; J=(arr(I1,dx)+K2)>>s2   (floor shifts, as in base2 model)
static inline i64 chainJ(int t,int w,int s1,int s2,i64 KCw,i64 K1,i64 K2){
    i64 dx=(i64)t<<w; i64 I1=(arr(KCw,dx)+K1)>>s1; return (arr(I1,dx)+K2)>>s2; }
int main(){
    loadwin();
    // ---- (1) baseline
    int sc=0; for(int i=0;i<1024;i++) if(have[i]){ i64 j=chainJ(i-512,13,23,18,KC,K,K); if(j>=JL[i]&&j<=JH[i])sc++; }
    printf("baseline (w=13,s1=23,s2=18,KCw=0x4ef145,K1=K2=2^19): score %d/1024\n",sc);
    printf("  J(17)=%lld need [%lld,%lld]\n",(long long)chainJ(17,13,23,18,KC,K,K),(long long)JL[529],(long long)JH[529]);
    printf("  J(342)=%lld need [%lld,%lld]\n",(long long)chainJ(342,13,23,18,KC,K,K),(long long)JL[854],(long long)JH[854]);
    printf("  J(-342)=%lld need [%lld,%lld]\n",(long long)chainJ(-342,13,23,18,KC,K,K),(long long)JL[170],(long long)JH[170]);
    // ---- (2) loss law: arr(rec,mul) - rec*mul, over the ranges used
    printf("\n-- loss = arr(rec,mul) - rec*mul --\n");
    for(int w=2;w<=20;w+=2){ i64 lmin=1ll<<60,lmax=-(1ll<<60); std::vector<i64> vals;
      for(i64 rec=0;rec<(1ll<<24);rec+=104729){ for(int t=-512;t<=511;t+=7){ i64 mul=(i64)t<<w;
          i64 L=arr(rec,mul)-rec*mul; lmin=std::min(lmin,L); lmax=std::max(lmax,L);} }
      printf("  w=%2d (mul=t<<w): loss in [%lld,%lld]  (2^16=%d)\n",w,(long long)lmin,(long long)lmax,1<<16); }
    // ---- (3) monotonicity of J in KCw
    printf("\n-- monotonicity of J(t) in KCw (step +1), per alignment --\n");
    for(int w=2;w<=24;w+=1){
        // nominal KCw = KC * 2^(s1+s2-15-2w) with s1+s2 chosen near 41+2(w-13)
        int tot=41+2*(w-13); if(tot<8) tot=8; int s1=tot/2+5, s2=tot-s1; if(s2<1){s2=1;s1=tot-1;} if(s1<1)s1=1;
        long double nom=(long double)KC*powl(2.0L,(long double)(s1+s2-15-2*w));
        i64 kc0=(i64)nom; if(kc0<1)kc0=1;
        i64 lo=kc0>4000?kc0-2000:1, hi=kc0+2000;
        long long viol=0, tested=0; i64 worst=0;
        for(int t=-512;t<=511;t+=13){ i64 prev=chainJ(t,w,s1,s2,lo,K,K);
            for(i64 c=lo+1;c<=hi;c++){ i64 v=chainJ(t,w,s1,s2,c,K,K); tested++;
                if(v<prev){ viol++; if(prev-v>worst) worst=prev-v; } prev=v; } }
        printf("  w=%2d s1=%2d s2=%2d nomKCw=%lld : %lld/%lld decreasing steps, worst drop %lld\n",
            w,s1,s2,(long long)kc0,viol,tested,(long long)worst);
    }
    return 0; }
