// fv_final-fields_9_verify.cpp -- end-to-end verification of the guard-clean (field, chain) pair that the
// report's two separate sweeps could not see, because each held the other half at the published value.
//   field : S_n = CEIL(sin(n*pi/32) * 2^23) stored fixed-point, left-shifted by the octave k_n by a
//           normalising shifter; alignment u_n = 30 + k_n  (ONE rule for all nodes, no per-node fitting)
//   chain : I1 = (arr(KC,dx) + 2^19) >> 23 ; J = (arr(I1,dx) + 0) >> 20      <-- K2 = 0
// Coefficient tables C1f / e1 / c3 / C0 are the frozen published ones.  Everything integer.
#include "base2.h"
#include <cstring>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,
                            51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static i64 WLO[65536],WHI[65536];
static inline i64 ceil_scale(i128 v,int sh){ i128 num=v<<sh; i128 q=num/QONE,r=num%QONE; if(r>0)++q; return (i64)q; }
static i64 SS[17]; static int UU[17];
// variant: 0 = published baseline, 1 = the fixed-ROM + normalising shifter field above
static void mkfield(int variant){ for(int n=0;n<=16;n++){ int k=TB[n].u2-30; i128 s=sin_(PI_()*n/32);
    if(n==0){SS[n]=0;UU[n]=30;continue;}
    if(variant==0){ SS[n]=TB[n].S; UU[n]=TB[n].u2; }
    else { i64 rom=std::min(ceil_scale(s,23),(i64)((1ll<<23)-1)); SS[n]=rom<<k; UU[n]=30+k; } } }
static inline i64 Vof(int n,int t,int flip,int ro,int s1,int s2,i64 K1,i64 K2){
    const Tab&T=TB[n]; i64 dx=(i64)t<<13;
    i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
    i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
    i64 I1=((ro?arr(dx,KC):arr(KC,dx))+K1)>>s1, J=(arr(I1,dx)+K2)>>s2;
    i64 T2=SS[n]? ((arr(J,SS[n])+K)>>UU[n]) : 0;
    i64 s=flip?-1:1; return C0TAB[n]+s*T1-T2-s*T3; }
static void report(const char*tag,int variant,int ro,int s1,int s2,i64 K1,i64 K2){
    mkfield(variant); int tot=0; int per[32];
    for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; int v=0;
        for(int t=-512;t<=511;t++){ int a=(node*1024+t)&0xffff; i64 V=Vof(n,t,flip,ro,s1,s2,K1,K2);
            if(V<WLO[a]||V>WHI[a])v++; }
        per[node]=v; tot+=v; }
    printf("%s\n   nodes 0..31 window violations = %d   guards: n0=%d n1=%d n2=%d n16=%d n30=%d n31=%d\n",
        tag,tot,per[0],per[1],per[2],per[16],per[30],per[31]);
    printf("   per-node:"); for(int i=0;i<32;i++) if(per[i]) printf(" n%d:%d",i,per[i]); printf("\n");
    int rn=0,rz=0,wv=0;
    for(int a=0;a<65536;a++){ int q=(a+512)>>10,m=q&63,t=a-1024*q; if(t<-512)t+=65536;
        int neg=0,flip=0,mm=m; if(mm>=33){mm-=32;neg=1;} if(mm>=17){mm=32-mm;flip=1;}
        i64 V=Vof(mm,t,flip,ro,s1,s2,K1,K2); if(neg)V=-V;
        if(to_float(V,0)!=F.srn[a])rn++; if(to_float(V,1)!=F.srz[a])rz++;
        if(V<WLO[a]||V>WHI[a])wv++; }
    printf("   whole 65536-angle dump: RN %d / RZ %d float mismatches, %d window violations\n\n",rn,rz,wv); }
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int a=0;a<65536;a++){ Win w=decode(F.srz[a],F.srn[a]);
        if(w.sign>0){WLO[a]=w.lo;WHI[a]=w.hi;} else {WLO[a]=-w.hi;WHI[a]=-w.lo;} }
    mkfield(0); printf("published field  S_n:"); for(int n=0;n<=16;n++)printf(" %lld",(long long)SS[n]);
    printf("\n                 u_n:"); for(int n=0;n<=16;n++)printf(" %d",UU[n]); printf("\n");
    mkfield(1); printf("fixed-ROM+shift  S_n:"); for(int n=0;n<=16;n++)printf(" %lld",(long long)SS[n]);
    printf("\n                 u_n:"); for(int n=0;n<=16;n++)printf(" %d",UU[n]);
    printf("\n   (ROM entries, before the shifter:");
    for(int n=0;n<=16;n++){ i128 s=sin_(PI_()*n/32); printf(" %lld",(long long)(n?std::min(ceil_scale(s,23),(i64)((1ll<<23)-1)):0)); }
    printf(")\n\n");
    report("A. BASELINE  : published field, chain (23,20,arr(dx,KC),K1=2^19,K2=2^19)",0,1,23,20,1ll<<19,1ll<<19);
    report("B. published field, chain (23,20,arr(KC,dx),K1=2^19,K2=0)  [the known 140, guard-breaking]",0,0,23,20,1ll<<19,0);
    report("C. fixed-ROM+shifter ceil field, chain (23,20,arr(KC,dx),K1=2^19,K2=0)  [joint sweep winner]",1,0,23,20,1ll<<19,0);
    report("D. fixed-ROM+shifter ceil field, chain (23,20,arr(dx,KC),K1=2^19,K2=0)",1,1,23,20,1ll<<19,0);
    report("E. fixed-ROM+shifter ceil field, PUBLISHED chain (K2=2^19)",1,1,23,20,1ll<<19,1ll<<19);
    return 0; }
