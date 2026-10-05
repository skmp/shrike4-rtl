// rt22_epscdf.cpp -- empirical distribution of the square-product error eps = (hardware product) - (model product), in
// 2^-26 units, read off the windows.  For a sample whose model product has fraction f (just above an integer):
//    A: model V outside the window, V+1 inside  -> hardware T2 = model-1  -> eps < -f
//    B: model V inside, V+1 outside             -> hardware T2 = model    -> eps >= -f
//    (both inside: undetermined, skipped)
// so  P(A | f) = P(eps < -f).  Symmetrically near the upper boundary with g = 1-f:  P(eps >= g).
#include "base2.h"
static Fsca F;
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
int main(){ if(!load_fsca(F))return 1; build_tables();
    static i64 Jc[1024]; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; Jc[t+512]=(arr(I1,dx)+K)>>20; }
    const double edges[]={0,0.002,0.004,0.006,0.008,0.012,0.016,0.02,0.03,0.04,0.06,0.10}; const int NB=11;
    const char* gname[]={"words 3..15 (k<=1, generic)","word 16 (S all ones)","words 1,2 (k=3,2; exact windows)"};
    for(int grp=0;grp<3;grp++){ long A[NB]={0},B[NB]={0},A2[NB]={0},B2[NB]={0};
        for(int n=1;n<=16;n++){ int g = (n>=3&&n<=15)?0 : (n==16?1:2); if(g!=grp)continue; const Tab&T=TB[n];
            for(int i=0;i<(n==16?1:2);i++){ int m=i?32-n:n; int flip=m>=17; i64 s=flip?-1:1;
                for(int t=-512;t<=511;t++){ if(!t)continue; i64 dx=(i64)t<<13; i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0; i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
                    i64 P=arr(Jc[t+512],T.S)+K; i64 T2=P>>T.u2; double f=(double)(P&((1ll<<T.u2)-1))/(double)(1ll<<T.u2);
                    int a=(m*1024+t)&0xffff; Win w=decode(F.srz[a],F.srn[a]); i64 lo,hi; if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;}
                    i64 V=C0TAB[n]+s*T1-s*T3-T2; bool in0=V>=lo&&V<=hi, inP=(V+1)>=lo&&(V+1)<=hi, inM=(V-1)>=lo&&(V-1)<=hi;
                    for(int b=0;b<NB;b++){ if(f>=edges[b]&&f<edges[b+1]){ if(!in0&&inP)A[b]++; else if(in0&&!inP)B[b]++; }
                                           double gg=1-f; if(gg>=edges[b]&&gg<edges[b+1]){ if(!in0&&inM)A2[b]++; else if(in0&&!inM)B2[b]++; } } } } }
        printf("\n%s\n   product fraction just ABOVE an integer: f in [a,b)   hw=model-1   hw=model   P(eps < -f)   ||  just BELOW: g=1-f   hw=model+1   hw=model   P(eps >= g)\n",gname[grp]);
        for(int b=0;b<NB;b++) printf("        [%.3f,%.3f)                                  %5ld      %5ld      %5.1f%%       ||                      %5ld       %5ld      %5.1f%%\n",edges[b],edges[b+1],A[b],B[b],A[b]+B[b]?100.0*A[b]/(A[b]+B[b]):0.0,A2[b],B2[b],A2[b]+B2[b]?100.0*A2[b]/(A2[b]+B2[b]):0.0); }
    return 0; }
