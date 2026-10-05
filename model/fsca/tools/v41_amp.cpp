// v41_amp.cpp -- the amplitude budget per offset, DIRECTIONAL and exact.
// NOTE: a naive "budget = I1 granularity + loss + rounding" is WRONG, because the model's I1 usually
// sits at one END of the reachable I1 range, so that part of the budget is available in ONE direction
// only.  The exact reachable envelope is computed here instead.
#include "base2.h"
#include <cstdio>
#include <algorithm>
static inline i64 fdv(i64 a,i64 b){ i64 q=a/b,r=a%b; if(r&&((r<0)!=(b<0)))q--; return q; }
static i64 JL[1024],JH[1024];
static void load_win(){ FILE* f=fopen("jwin18.txt","r"); char l[512];
  while(fgets(l,sizeof l,f)){ if(l[0]=='#')continue; long long t,a,b,w; if(sscanf(l,"%lld %lld %lld %lld",&t,&a,&b,&w)!=4)continue; JL[t+512]=a; JH[t+512]=b;} fclose(f);}
int main(){ build_tables(); load_win();
    const i64 LMIN=-655360,LMAX=65536; const int w=13,s1=23,s2=18;
    printf("   t    J_model    required delta    I1 reachable (model marked *)      exact J envelope        verdict\n");
    int T[7]={17,342,-342,-469,100,511,-17};
    for(int i=0;i<7;i++){ int t=T[i]; i64 dx=(i64)t<<w;
        i64 I1m=fdv(arr(KC,dx)+K,(i64)1<<s1); i64 J=fdv(arr(I1m,dx)+K,(i64)1<<s2);
        i64 a=fdv((i64)KC*dx+LMIN,(i64)1<<s1), b=fdv((i64)KC*dx+LMAX+((i64)1<<s1)-1,(i64)1<<s1);
        i64 jl=(i64)1<<62, jh=-((i64)1<<62);
        for(i64 I=a;I<=b;I++){ i64 e=I*dx; jl=std::min(jl,fdv(e+LMIN,(i64)1<<s2)); jh=std::max(jh,fdv(e+LMAX+((i64)1<<s2)-1,(i64)1<<s2)); }
        printf(" %5d %10lld     [%4lld,%4lld]      %lld..%lld (model %lld%s)   [%10lld,%10lld]   %s\n",
            t,(long long)J,(long long)(JL[t+512]-J),(long long)(JH[t+512]-J),(long long)a,(long long)b,
            (long long)I1m, I1m==a?"=lowest":(I1m==b?"=highest":""),
            (long long)jl,(long long)jh,
            (jh<JL[t+512]||jl>JH[t+512])?"UNREACHABLE":"reachable");
    }
    // how often does the model's I1 sit at the bottom of its reachable range? (= downward budget lost)
    int atlow=0,athigh=0; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<w;
        i64 I1m=fdv(arr(KC,dx)+K,(i64)1<<s1);
        i64 a=fdv((i64)KC*dx+LMIN,(i64)1<<s1), b=fdv((i64)KC*dx+LMAX+((i64)1<<s1)-1,(i64)1<<s1);
        if(I1m==a)atlow++; if(I1m==b)athigh++; }
    printf("\nover all 1024 offsets: model I1 is at the LOW end of its reachable range %d times, at the HIGH end %d times\n",atlow,athigh);
    printf("(so the first latch's granularity is mostly a ONE-SIDED, upward, freedom -- it cannot supply the negative delta t=17 needs)\n");
    return 0; }
