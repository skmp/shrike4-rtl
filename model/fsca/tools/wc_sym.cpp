// wc_sym.cpp -- a handful of SPECIFIC square structures, scored end-to-end on nodes 0..31 and against
// the J windows.  Purpose: measure the fine symmetric J (predicted to floor near the t-even bound 34)
// and compare with the coarse chain (148).
#include "wc_core.h"
#include <vector>
#include <algorithm>
static i64 JLo[1024],JHi[1024]; static char JOK[1024];
static void build_jwin(){ // b12's inversion, using the baseline final multiply arr(J,S)+K >> u2
    for(int t=-512;t<=511;t++){ i64 gl=0,gh=12000000;
        for(int node=1;node<=31;node++){ int n=node>=17?32-node:node; if(!TB[n].S)continue; const Tab&T=TB[n];
            i64 base=C0B[n]+LT[node][t+512];
            i64 t2lo=base-WHI[node][t+512], t2hi=base-WLO[node][t+512]; int u=T.u2;
            i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        JLo[t+512]=gl;JHi[t+512]=gh;JOK[t+512]=gl<=gh; } }
static int jscore(i64(*J)(int)){ int s=0; for(int t=-512;t<=511;t++){ if(!JOK[t+512])continue;
    i64 j=J(t); if(j>=JLo[t+512]&&j<=JHi[t+512])s++; } return s; }
static i64 Jchain(int t){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>20; }
static i64 Jfine (int t){ i64 Q=(arr((i64)t<<13,(i64)t<<13)+K)>>26; return (arr(KC,Q)+K)>>17; }
static i64 JfineR(int t){ i64 Q=(arr((i64)t<<13,(i64)t<<13)+K)>>26; return (arr(Q,KC)+K)>>17; }
static i64 JfineN(int t){ i64 Q=(arr((i64)t<<13,(i64)t<<13)+K)>>26; return (arr(KC,Q))>>17; }
static i64 Jq20  (int t){ i64 Q=(arr((i64)t<<13,(i64)t<<13)+K)>>20; return (arr(KC,Q)+K)>>23; }
static i64 Jq18  (int t){ i64 Q=(arr((i64)t<<13,(i64)t<<13)+K)>>18; return (arr(KC,Q)+K)>>25; }
static i64 Jq16  (int t){ i64 Q=(arr((i64)t<<13,(i64)t<<13)+K)>>16; return (arr(KC,Q)+K)>>27; }
static i64 Jtt   (int t){ return (arr(KC,(i64)t*t)+K)>>17; }
int main(){ wc_init(); build_jwin();
    int nf=0; for(int t=-512;t<=511;t++) nf+=JOK[t+512]; printf("feasible t: %d\n",nf);
    struct E{const char*n; i64(*f)(int);} es[]={{"chain I1=(KC*dx)>>23, J=(I1*dx)>>20",Jchain},
        {"Q=(dx*dx+K)>>26 ; J=(KC*Q+K)>>17      ",Jfine},{"Q=... ; J=(Q*KC+K)>>17 (roles swapped)",JfineR},
        {"Q=... ; J=(KC*Q)>>17  (no 2nd const)  ",JfineN},{"Q=(dx*dx+K)>>20 ; J=(KC*Q+K)>>23     ",Jq20},
        {"Q=(dx*dx+K)>>18 ; J=(KC*Q+K)>>25     ",Jq18},{"Q=(dx*dx+K)>>16 ; J=(KC*Q+K)>>27     ",Jq16},
        {"J=(arr(KC,t*t)+K)>>17 (exact t^2)     ",Jtt}};
    for(auto&e:es){ int js=jscore(e.f);
        int g=0; int tot=wc_score([&](int node,int t)->i64{ int n=node>=17?32-node:node; if(!TB[n].S)return 0;
            return sm(arr(e.f(t),TB[n].S)+K,TB[n].u2); },nullptr,&g);
        printf("%-40s jwin %4d/%d   model %4d  guard %d\n",e.n,js,nf,tot,g); }
    // how far off is the fine symmetric J at the failures?
    puts("\nfine-J failures (Q>>26, J=(KC*Q+K)>>17):  t  JL JH  J  err  |  mirror err");
    int nn=0; for(int t=-512;t<=511&&nn<50;t++){ if(!JOK[t+512])continue; i64 j=Jfine(t);
        if(j>=JLo[t+512]&&j<=JHi[t+512])continue; i64 e=j<JLo[t+512]?j-JLo[t+512]:j-JHi[t+512];
        printf("%5d %9lld %9lld %9lld %+4lld\n",t,(long long)JLo[t+512],(long long)JHi[t+512],(long long)j,(long long)e); nn++; }
    return 0; }
