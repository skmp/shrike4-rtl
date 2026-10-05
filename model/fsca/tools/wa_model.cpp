// wa_model.cpp -- b2.cpp end-to-end with a substitutable square-latch rule J(t).
// Usage: wa_model [mode]   0 = current chain (baseline), 1 = best chain found by wa_scan2/3,
//                          2 = free per-t J from the windows (floor), 3 = custom via argv
#include "wa_common.h"
static Fsca F; static Windows W;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 JT[1024];
static inline i64 Vof(int n,int t,int flip,i64 C0,int u2){ const Tab&T=TB[n]; i64 dx=(i64)t<<13;
    i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
    i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
    i64 T2=T.S? sm(arr(JT[t+512],T.S)+K,u2):0;
    i64 s=flip?-1:1; return C0+s*T1-T2-s*T3; }
int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables(); load_windows(W);
    int mode=argc>1?atoi(argv[1]):0;
    Cfg c; i64 K1,K2; const char*name;
    if(mode==0){ c=Cfg{0x4ef145,13,23,0,0,0,0,13,20,0,0}; K1=1ll<<19; K2=1ll<<19; name="baseline chain (K1=K2=2^19)"; }
    else if(mode==1){ c=Cfg{-5173574,10,19,1,3,1,1,13,21,1,6}; K1=0; K2=-786431; name="best chain found (wa_scan2)"; }
    else if(mode==3){ c=Cfg{atoll(argv[2]),atoi(argv[3]),atoi(argv[4]),atoi(argv[5]),atoi(argv[6]),atoi(argv[7]),
                            atoi(argv[8]),atoi(argv[9]),atoi(argv[10]),atoi(argv[11]),atoi(argv[12])};
                      K1=atoll(argv[13]); K2=atoll(argv[14]); name="custom"; }
    else { c=Cfg{0,0,0,0,0,0,0,0,0,0,0}; K1=K2=0; name="free per-t J (lower bound)"; }
    for(int t=-512;t<=511;t++) JT[t+512] = (mode==2)? W.JL[t+512] : Jof(c,K1,K2,t);
    if(mode==2){ /* free J: pick, per t, the value minimising violations later; JL is a decent proxy */ }
    printf("J rule: %s\n",name);
    if(mode!=2){ int in=0; for(int t=-512;t<=511;t++) if(W.ok[t+512]&&JT[t+512]>=W.JL[t+512]&&JT[t+512]<=W.JH[t+512])in++;
        printf("window score against jwin: %d/1022\n",in); }
    i64 bestC0[17]; int bestU[17]; int grand=0; int exact_nodes=0;
    puts("tab  C0        d(RN26) u2   viol(n)  viol(32-n)  total");
    for(int n=0;n<=16;n++){ int nodes[2]={n,32-n}; int nn=(n==0||n==16)?1:2;
        i64 lo0,hi0; bool ex0; win_of(n*1024,lo0,hi0,ex0);
        int bv=1<<30; i64 bc=lo0; int bu=TB[n].u2; int bper[2]={0,0};
        for(i64 cc=lo0-3;cc<=hi0+3;cc++) for(int u=TB[n].u2-1;u<=TB[n].u2+1;u++){
            int v=0,per[2]={0,0};
            for(int i=0;i<nn;i++){ int m=nodes[i]; int flip=(m>=17); int tn=flip?32-m:m;
                for(int t=-512;t<=511;t++){ int a=(m*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                    i64 V=Vof(tn,t,flip,cc,u); if(V<lo||V>hi){v++;per[i]++;} } }
            if(v<bv){bv=v;bc=cc;bu=u;bper[0]=per[0];bper[1]=per[1];} }
        bestC0[n]=bc;bestU[n]=bu;grand+=bv;
        printf("%2d  %9lld  %+4lld    %2d   %5d     %5d      %5d\n",n,(long long)bc,(long long)(bc-TB[n].sin26),bu,bper[0],bper[1],bv); }
    printf("TOTAL window violations over nodes 0..31: %d\n",grand);
    // guard nodes
    printf("guard nodes exact (0,1,2,16 -> node instances 0,1,2,16,30,31): ");
    for(int m : {0,1,2,16,30,31}){ int flip=(m>=17); int tn=flip?32-m:m; int v=0;
        for(int t=-512;t<=511;t++){ int a=(m*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            i64 V=Vof(tn,t,flip,bestC0[tn],bestU[tn]); if(V<lo||V>hi)v++; }
        printf("n%d:%d ",m,v); }
    puts("");
    return 0; }
