// v52_biv.cpp -- B(iv) alignment scan, done two ways.
//  (a) w5's slice: s1=23, s2=18 fixed, KC' swept around KC*2^(26-2w).
//  (b) the FULL family: every split s1+s2 = 15+2w, KC' swept around the nominal for that split.
#include "v52_common.h"
#include <algorithm>
static JW W;
static inline int score_cfg(int w,int s1,int s2,i64 kc,int order){
    int s=0; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<w;
        i64 I1 = order ? (arr(dx,kc)+K)>>s1 : (arr(kc,dx)+K)>>s1;
        i64 j  = (arr(I1,dx)+K)>>s2;
        if(j>=W.lo[t+512]&&j<=W.hi[t+512])s++; }
    return s; }
static inline i64 Jat(int t,int w,int s1,int s2,i64 kc,int order){ i64 dx=(i64)t<<w;
    i64 I1 = order ? (arr(dx,kc)+K)>>s1 : (arr(kc,dx)+K)>>s1; return (arr(I1,dx)+K)>>s2; }
int main(){ if(!load_jwin(W)){puts("no jwin");return 1;} build_tables();
    puts("(a) w5's slice reproduced independently: s1=23, s2=18, KC' around KC<<(26-2w), +-4096");
    long long cfgs=0;
    for(int w=10;w<=16;w++){
        long long nom = (w<=13)? ((long long)KC<<(26-2*w)) : ((long long)KC>>(2*w-26));
        int best=-1; long long bk=0;
        for(long long kc=std::max(1LL,nom-4096);kc<=nom+4096;kc++){ cfgs++; int s=score_cfg(w,23,18,kc,1); if(s>best){best=s;bk=kc;} }
        printf("  w=%2d nom=%11lld : best %4d/1024 at KC'=%lld  J(17)=%lld J(342)=%lld J(-342)=%lld%s\n",
            w,nom,best,bk,(long long)Jat(17,w,23,18,bk,1),(long long)Jat(342,w,23,18,bk,1),
            (long long)Jat(-342,w,23,18,bk,1), w==13?"  <- published":""); }
    printf("  configs in slice (a): %lld\n",cfgs);
    puts("(b) FULL family: all splits s1+s2 = 15+2w, KC' around nominal 2^(s1)*KC/2^(20+?) -- nominal derived so that");
    puts("    I1 ~ KC*dx/2^s1 keeps J ~ KC*t^2/2^15 ; sweep +-4096 ; both operand orders");
    { int gbest=-1,gw=0,gs1=0,gs2=0,go=0; long long gkc=0; long long cfg2=0;
      for(int w=9;w<=18;w++) for(int s1=10;s1<=34;s1++){ int s2=15+2*w-s1; if(s2<8||s2>34)continue;
        // nominal KC' so that KC'*2^(2w)/2^(s1+s2) == KC*2^26/2^41  ->  KC' = KC * 2^(26-2w)
        long long nom = (2*w<=26)? ((long long)KC<<(26-2*w)) : ((long long)KC>>(2*w-26));
        if(nom<1) continue;
        for(int o=0;o<2;o++){ int best=-1; long long bk=0;
          for(long long kc=std::max(1LL,nom-4096);kc<=nom+4096;kc++){ cfg2++; int s=score_cfg(w,s1,s2,kc,o); if(s>best){best=s;bk=kc;} }
          if(best>gbest){gbest=best;gw=w;gs1=s1;gs2=s2;go=o;gkc=bk;}
          if(best>918) printf("    BEATS BASELINE: w=%d s1=%d s2=%d order=%d KC'=%lld -> %d/1024  J(17)=%lld J(342)=%lld J(-342)=%lld\n",
              w,s1,s2,o,bk,best,(long long)Jat(17,w,s1,s2,bk,o),(long long)Jat(342,w,s1,s2,bk,o),(long long)Jat(-342,w,s1,s2,bk,o)); } }
      printf("  global best over %lld configs: %d/1024 at w=%d s1=%d s2=%d order=%d KC'=%lld\n",cfg2,gbest,gw,gs1,gs2,go,gkc); }
    return 0; }
