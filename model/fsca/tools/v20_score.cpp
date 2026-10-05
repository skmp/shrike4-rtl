// v20_score.cpp -- independent jwin18 scoring of every configuration the w-kc-joint branch claims,
// plus the user's two hypotheses H-A (2s computed / 1s consumed) and H-B (one extra bit on t).
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
static i64 JL[1024],JH[1024];
static void load(){ FILE*f=fopen("jwin18.txt","r");
    char ln[512]; long long t,l,h; while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue;
    if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){JL[t+512]=l;JH[t+512]=h;} } fclose(f); }
struct Cfg{ const char*name; int w,s1,s2; i64 kc,K1,K2; };
static inline i64 Jc(const Cfg&c,int t){ i64 dx=(i64)t<<c.w; i64 I1=(arr(c.kc,dx)+c.K1)>>c.s1; return (arr(I1,dx)+c.K2)>>c.s2; }
static int score(const Cfg&c,i64 bias=0){ int s=0; for(int t=-512;t<=511;t++){ i64 J=Jc(c,t)+bias; if(J>=JL[t+512]&&J<=JH[t+512]) s++; } return s; }
static void rep(const Cfg&c,i64 bias=0){
    printf("%-46s w=%2d s1=%2d s2=%2d KCw=%-10lld K1=%-10lld K2=%-10lld  score=%4d   J(17)=%lld J(342)=%lld J(-342)=%lld\n",
        c.name,c.w,c.s1,c.s2,(long long)c.kc,(long long)c.K1,(long long)c.K2,score(c,bias),
        (long long)(Jc(c,17)+bias),(long long)(Jc(c,342)+bias),(long long)(Jc(c,-342)+bias));
}
// ---- generalised chain with the user's operand transforms
// H-A: t consumed as 1s complement -> value used is t+1 for t<0, at one of several placements
// H-B: t carries one extra bit -> dx = (2t+eps)<<(w-1)
static inline i64 JgenA(int w,int s1,int s2,i64 kc,int place,int d){
    // place 0: t level  1: dx level  2: I1 feedback operand  3: both dx and feedback
    i64 tt;  // handled by caller through dx
    (void)tt; return 0; }
int main(){
    load();
    puts("=== configurations claimed by branch w-kc-joint (baseline 918) ===");
    Cfg base{"baseline (structural model)",13,23,18,KC,K,K};                     rep(base);
    Cfg c925a{"claimed 925/1024 (fixed K) #1",14,27,19,41388583,K,K};            rep(c925a);
    Cfg c925b{"claimed 925/1024 (fixed K) #2",14,28,19,82777166,K,K};            rep(c925b);
    Cfg c937 {"claimed 937/1024 (free K in period)",12,26,17,82777165,5963776,65536}; rep(c937);
    Cfg c940 {"claimed 940/1024 (K2 outside period)",16,30,21,82777165,85590016,-2293760}; rep(c940);
    printf("   [K1 in [0,2^%d)? %s ;  K2 in [0,2^%d)? %s]\n",c937.s1,(c937.K1>=0&&c937.K1<((i64)1<<c937.s1))?"yes":"NO",
           c937.s2,(c937.K2>=0&&c937.K2<((i64)1<<c937.s2))?"yes":"NO");
    printf("   [c940: K1 in [0,2^%d)? %s ;  K2 in [0,2^%d)? %s]\n",c940.s1,(c940.K1>=0&&c940.K1<((i64)1<<c940.s1))?"yes":"NO",
           c940.s2,(c940.K2>=0&&c940.K2<((i64)1<<c940.s2))?"yes":"NO");
    // nominal constants at those alignments
    for(Cfg c : {c925a,c925b,c937,c940}){ int e=c.s1+c.s2-2*c.w-15;
        long double nom = (long double)KC * powl(2.0L,(long double)e);
        printf("   %-38s nominal KCw = %.2Lf   claimed %lld   delta %.3f ppm\n",c.name,nom,(long long)c.kc,
            (double)((c.kc-nom)/nom*1e6)); }
    puts("\n=== is 925 really the fixed-K ceiling at those alignments? bounded rescan of KCw ===");
    for(Cfg c : {c925a,c925b}){
        int best=0; i64 bk=0;
        for(i64 d=-3000; d<=3000; d++){ Cfg x=c; x.kc=c.kc+d; if(x.kc<1) continue; int s=score(x); if(s>best){best=s;bk=x.kc;} }
        printf("  w=%d s1=%d s2=%d : best over KCw+-3000 = %d at KCw=%lld\n",c.w,c.s1,c.s2,best,(long long)bk);
    }
    puts("\n=== additive free bias c on J (their point 5): best over the 918-baseline family ===");
    { int best=0; i64 bb=0; for(i64 b=-600;b<=600;b++){ int s=score(base,b); if(s>best){best=s;bb=b;} }
      printf("  baseline + best constant bias: %d at c=%lld\n",best,(long long)bb);
      printf("  required difference J(342)-J(17) = %lld ; baseline gives %lld\n",
        (long long)(JL[854]-JL[529]), (long long)(Jc(base,342)-Jc(base,17))); }
    return 0;
}
