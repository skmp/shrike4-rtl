// wc_best.cpp -- the full (23,20) slice: every role / rounding-constant / latch-rule combination of
// I1 = latch(Kc x dx, 23) ; J = latch(I1 x dx, 20) ; T2 = sm(arr(J,S)+K,u2), with both jwin and the
// end-to-end model score, so the ties and the trade-off between them are explicit.
#include "wc_core.h"
#include <vector>
#include <algorithm>
static i64 JLo[1024],JHi[1024]; static char JOK[1024];
static void build_jwin(){
    for(int t=-512;t<=511;t++){ i64 gl=0,gh=12000000;
        for(int node=1;node<=31;node++){ int n=node>=17?32-node:node; if(!TB[n].S)continue; const Tab&T=TB[n];
            i64 base=C0B[n]+LT[node][t+512];
            i64 t2lo=base-WHI[node][t+512], t2hi=base-WLO[node][t+512]; int u=T.u2;
            i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        JLo[t+512]=gl;JHi[t+512]=gh;JOK[t+512]=gl<=gh; } }
static inline i64 MUL(i64 a,i64 b,int r){ return r?arr(b,a):arr(a,b); }
int main(){ wc_init(); build_jwin();
    int nf=0; for(int i=0;i<1024;i++)nf+=JOK[i];
    puts("ro0 ro1 ro2 c0 c1 c2 r0 r1 | jwin   model guard  (ro0=1 means arr(dx,Kc), i.e. dx recoded)");
    struct Row{int j,m,g,ro0,ro1,ro2,c0,c1,c2,r0,r1;}; std::vector<Row> rows;
    for(int ro0=0;ro0<2;ro0++) for(int ro1=0;ro1<2;ro1++) for(int ro2=0;ro2<2;ro2++)
    for(int c0=0;c0<2;c0++) for(int c1=0;c1<2;c1++) for(int c2=0;c2<2;c2++)
    for(int r0=0;r0<3;r0++) for(int r1=0;r1<3;r1++){
        i64 J[1024];
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 I=wlatch(MUL((i64)KC,dx,ro0)+(c0?K:0),23,r0);
            J[t+512]=wlatch(MUL(I,dx,ro1)+(c1?K:0),20,r1); }
        int js=0; for(int t=-512;t<=511;t++){ if(!JOK[t+512])continue;
            if(J[t+512]>=JLo[t+512]&&J[t+512]<=JHi[t+512])js++; }
        int g=0; int m=wc_score([&](int nd,int t)->i64{ int n=nd>=17?32-nd:nd; if(!TB[n].S)return 0;
            return sm(MUL(J[t+512],TB[n].S,ro2)+(c2?K:0),TB[n].u2); },nullptr,&g);
        rows.push_back({js,m,g,ro0,ro1,ro2,c0,c1,c2,r0,r1}); }
    std::sort(rows.begin(),rows.end(),[](const Row&a,const Row&b){ if(a.m!=b.m) return a.m<b.m; return a.j>b.j; });
    for(int i=0;i<24;i++){ Row&r=rows[i];
        printf(" %d   %d   %d  %d  %d  %d  %d  %d | %4d/%d %4d  %d\n",r.ro0,r.ro1,r.ro2,r.c0,r.c1,r.c2,r.r0,r.r1,r.j,nf,r.m,r.g); }
    puts("\nbest by jwin:");
    std::sort(rows.begin(),rows.end(),[](const Row&a,const Row&b){ if(a.j!=b.j) return a.j>b.j; return a.m<b.m; });
    for(int i=0;i<10;i++){ Row&r=rows[i];
        printf(" %d   %d   %d  %d  %d  %d  %d  %d | %4d/%d %4d  %d\n",r.ro0,r.ro1,r.ro2,r.c0,r.c1,r.c2,r.r0,r.r1,r.j,nf,r.m,r.g); }
    // per-node breakdown of the best guard-clean one
    for(auto&r:rows){ if(r.g) continue;
        int per[32]={0},g=0;
        i64 J[1024]; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 I=wlatch(MUL((i64)KC,dx,r.ro0)+(r.c0?K:0),23,r.r0);
            J[t+512]=wlatch(MUL(I,dx,r.ro1)+(r.c1?K:0),20,r.r1); }
        int m=wc_score([&](int nd,int t)->i64{ int n=nd>=17?32-nd:nd; if(!TB[n].S)return 0;
            return sm(MUL(J[t+512],TB[n].S,r.ro2)+(r.c2?K:0),TB[n].u2); },per,&g);
        printf("\nbest guard-clean: ro=(%d,%d,%d) c=(%d,%d,%d) rules=(%d,%d) jwin %d model %d\nper node:",
            r.ro0,r.ro1,r.ro2,r.c0,r.c1,r.c2,r.r0,r.r1,r.j,m);
        for(int i=0;i<32;i++) printf(" %d:%d",i,per[i]); puts("");
        break; }
    return 0; }
