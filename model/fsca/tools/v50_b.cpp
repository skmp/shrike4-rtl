// v50_b.cpp -- HYPOTHESIS B: "t carries one more bit of precision".
//  B1 half-step node base : dx = (2t+e)<<12
//  B4 alignment           : *** THIS SECTION IS WRONG AND SUPERSEDED BY v50_b4a.cpp ***
//                           it applies BOTH the shift rescale (s1+s2=2w+15) AND the constant
//                           rescale KC<<(26-2w); only one of the two is correct, so every w != 13
//                           row here is off-scale by 2^(26-2w) and its failure means nothing.
//                           The B1 and B3 sections below ARE valid.
//  B3 node rounding rule  : node = (angle+rho)>>10
#include "v50_core.h"
int main(){
    if(!load_fsca(VF)){puts("no dump");return 1;} build_tables(); if(!v50_load_jwin())return 1;
    VCfg c; c.s2=18; VCfg c20;
    printf("baseline: jwin18 %d/1024, full65536 %d\n\n",
        v50_score([&](int t){return v50_J(dx_id(t),dx_id(t),c);}),
        v50_full64([&](int t){return v50_J(dx_id(t),dx_id(t),c20);},20,dx_id,dx_id,nullptr));
    // ---------------- B1 ----------------
    puts("== B1 half-LSB node base: dx = (2t+e)<<12 ==");
    for(int e=-4;e<=4;e++){
        auto dxe=[&](int t){ return (i64)(2*t+e)<<12; };
        auto jf =[&](int t){ return v50_J(dxe(t),dxe(t),c); };
        auto j20=[&](int t){ return v50_J(dxe(t),dxe(t),c20); };
        // cubic square factor consistent with the half-step: ((2t+e)^2)>>2
        int f1=v50_full64(j20,20,dxe,dxe,nullptr);
        printf("   e=%+d : jwin18 %4d/1024  full65536 %6d   J(17)=%lld\n",e,v50_score(jf),f1,(long long)jf(17));
    }
    // ---------------- B4 ----------------
    puts("\n== B4 alignment dx = t<<w, scale law s1+s2 = 2w+15, KC swept +-4096 EFFECTIVE (w=13 units) ==");
    printf("   %-3s %-3s %-3s %-12s %-10s %-9s %s\n","w","s1","s2","KC nominal","best","at KC","J(17) J(342) J(-342)");
    for(int w=9;w<=17;w++){
      for(int s1=20;s1<=26;s1++){
        int s2=2*w+15-s1; if(s2<10||s2>30) continue;
        // nominal constant so that J = KC13 * t^2 / 2^15 ; KC' = KC13 * 2^(41-2w) / 2^(s1+s2-2w) ... = KC13 * 2^(26-2w)
        long long nom = (w<=13)? ((long long)KC<<(26-2*w)) : ((long long)KC>>(2*w-26));
        long long step = (w<=13)? (1LL<<(26-2*w)) : 1;              // 1 effective unit
        long long span = 4096;                                      // +-4096 EFFECTIVE units
        int best=-1; long long bk=0;
        for(long long q=-span;q<=span;q++){
            long long kc = nom + q*step; if(kc<1) continue;
            int s=0;
            for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<w;
                i64 I1=(arr(kc,dx)+K)>>s1; i64 j=(arr(I1,dx)+K)>>s2;
                if(j>=VJL[t+512]&&j<=VJH[t+512])s++; }
            if(s>best){best=s;bk=kc;} }
        if(best<850 && w!=13) continue;
        i64 j17,j342,jm; { for(int t:{17,342,-342}){ i64 dx=(i64)t<<w; i64 I1=(arr(bk,dx)+K)>>s1; i64 j=(arr(I1,dx)+K)>>s2;
              if(t==17)j17=j; else if(t==342)j342=j; else jm=j; } }
        printf("   %-3d %-3d %-3d %-12lld %4d/1024  %-9lld %lld %lld %lld%s\n",
               w,s1,s2,nom,best,bk,(long long)j17,(long long)j342,(long long)jm,
               (w==13&&s1==23&&s2==18)?"   <- published":"");
      } }
    printf("   targets                                          45617 18466836 18466858..67\n");
    // ---------------- B3 ----------------
    puts("\n== B3 node rounding rule: node = (angle+rho)>>10, t = angle - 1024*node ==");
    for(int rho : {0,256,448,504,511,512,513,520,576,768,1023}){
        int viol=0;
        for(int a=0;a<65536;a++){
            int node=((a+rho)>>10)&63; int t=a-1024*node; if(t>32768)t-=65536; if(t<-32768)t+=65536;
            int hb=node>=32,nd=node&31,flip=nd>=17,n=flip?32-nd:nd; i64 s=flip?-1:1,g=hb?-1:1; const Tab&T=TB[n];
            i64 dx=(i64)t<<13;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 I1=(arr(KC,dx)+K)>>23, J=(arr(I1,dx)+K)>>20;
            i64 T2=T.S? sm(arr(J,T.S)+K,V_U2TAB[n]):0;
            i64 V=g*(V_C0TAB[n]+s*T1-T2-s*T3);
            i64 lo,hi; v50_win(a,lo,hi); if(V<lo||V>hi)viol++; }
        printf("   rho=%4d : %6d violations%s\n",rho,viol,rho==512?"   <- published":"");
    }
    return 0; }
