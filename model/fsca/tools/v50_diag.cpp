// v50_diag.cpp -- spot-check the remaining branch-5 numbers I have not yet reproduced:
//  (1) per-term A-variants (t_eff = t+1 for t<0 applied to ONE term at a time), full 65536
//  (2) the labelled fitted diagnostic: joint sign-dependent dx-level offsets (qsq, r)
#include "v50_core.h"
static int fullmodel(int qa,int qb,int r,int ql,int qc,int ord){
    int viol=0;
    for(int node=0;node<=63;node++){ int hb=node>=32,nd=node&31,flip=nd>=17,n=flip?32-nd:nd;
        i64 s=flip?-1:1,g=hb?-1:1; const Tab&T=TB[n];
        for(int t=-512;t<=511;t++){
            i64 dx=(i64)t<<13; int neg = t<0;
            i64 dl = dx + (neg?ql:0), dc = dx + (neg?qc:0);
            i64 da = dx + (neg?qa:0), db = dx + (neg?qb:0);
            i64 T1=T.C1f? sm(arr(T.C1f,dl)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dc)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 I1=((ord?arr(da,KC):arr(KC,da))+K)>>23; I1 += (neg?r:0);
            i64 J=(arr(I1,db)+K)>>20;
            i64 T2=T.S? sm(arr(J,T.S)+K,V_U2TAB[n]):0;
            i64 V=g*(V_C0TAB[n]+s*T1-T2-s*T3);
            int a=(node*1024+t)&0xffff; i64 lo,hi; v50_win(a,lo,hi);
            if(V<lo||V>hi)viol++; } }
    return viol; }
// per-term t_eff variant: which TERMS see t_eff = t + d (for t<0)
static int fullterm(int d,bool lin,bool sq,bool cub){
    int viol=0;
    for(int node=0;node<=63;node++){ int hb=node>=32,nd=node&31,flip=nd>=17,n=flip?32-nd:nd;
        i64 s=flip?-1:1,g=hb?-1:1; const Tab&T=TB[n];
        for(int t=-512;t<=511;t++){ int te=t+((t<0)?d:0);
            i64 dxo=(i64)t<<13, dxe=(i64)te<<13;
            i64 T1=T.C1f? sm(arr(T.C1f,lin?dxe:dxo)+K,23-T.e1):0;
            int tc = cub? te : t;
            i64 A3=((arr(((i64)tc*tc)<<3,cub?dxe:dxo)+K)>>20)>>3;
            i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 ds = sq? dxe : dxo;
            i64 I1=(arr(KC,ds)+K)>>23, J=(arr(I1,ds)+K)>>20;
            i64 T2=T.S? sm(arr(J,T.S)+K,V_U2TAB[n]):0;
            i64 V=g*(V_C0TAB[n]+s*T1-T2-s*T3);
            int a=(node*1024+t)&0xffff; i64 lo,hi; v50_win(a,lo,hi);
            if(V<lo||V>hi)viol++; } }
    return viol; }
int main(){
    if(!load_fsca(VF)){puts("no dump");return 1;} build_tables();
    printf("baseline ord=0 %d   ord=1 %d\n\n",fullmodel(0,0,0,0,0,0),fullmodel(0,0,0,0,0,1));
    puts("(1) per-term: t_eff = t+1 for t<0 applied to ONE term (full 65536)");
    printf("   lin-only %d\n   sq-only  %d\n   cub-only %d\n   all      %d\n\n",
        fullterm(1,true,false,false),fullterm(1,false,true,false),
        fullterm(1,false,false,true),fullterm(1,true,true,true));
    puts("(2) fitted diagnostic: sign-dependent dx-level offsets, 5^4=625 combos (qa=qb=qsq, r, ql, qc)");
    int best=1<<30,bq=0,br=0,bl=0,bc=0;
    for(int q=-2;q<=2;q++) for(int r=-2;r<=2;r++) for(int l=-2;l<=2;l++) for(int cc=-2;cc<=2;cc++){
        int v=fullmodel(q,q,r,l,cc,0);
        if(v<best){best=v;bq=q;br=r;bl=l;bc=cc;} }
    printf("   best %d at qsq=%+d r=%+d qlin=%+d qcub=%+d  (baseline 296)\n",best,bq,br,bl,bc);
    printf("   the specific claimed point qsq=+1 r=-1 (others 0): %d  [ord=0]  %d  [ord=1]\n",
           fullmodel(1,1,-1,0,0,0),fullmodel(1,1,-1,0,0,1));
    return 0; }
