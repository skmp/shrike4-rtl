// w5_base.cpp -- branch 5 baseline harness.
//  (1) full-model window violations over nodes 0..31 with FIXED C0TAB/U2TAB (no refit), both operand orders
//  (2) jwin18 score of the baseline square chain at fine shift 18, split into t>=0 and t<0
//  (3) the CEILING for any purely sign-dependent hypothesis: (in-window t>=0) + 512
#include "base2.h"
#include <cstdio>
#include <cstring>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 JL[1024],JH[1024]; static bool have[1024];
static void load_jwin(){ FILE*f=fopen("jwin18.txt","r"); char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} } fclose(f); }
static inline i64 Vof(int n,int t,int flip,int order){ const Tab&T=TB[n]; i64 dx=(i64)t<<13;
    i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
    i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
    i64 I1=order? ((arr(dx,KC)+K)>>23) : ((arr(KC,dx)+K)>>23);
    i64 I2=(arr(I1,dx)+K)>>20; i64 T2=T.S? sm(arr(I2,T.S)+K,U2TAB[n]):0;
    i64 s=flip?-1:1; return C0TAB[n]+s*T1-T2-s*T3; }
static int full_score(int order){ int v=0;
    for(int node=0;node<32;node++){ int flip=node>=17,n=flip?32-node:node;
        for(int t=-512;t<=511;t++){ int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            i64 V=Vof(n,t,flip,order); if(V<lo||V>hi)v++; } } return v; }
int main(){ if(!load_fsca(F))return 1; build_tables(); load_jwin();
    printf("FULL MODEL violations (fixed C0TAB/U2TAB): arr(KC,dx)=%d  arr(dx,KC)=%d\n",full_score(0),full_score(1));
    int sc=0,pos=0,neg=0,npos=0,nneg=0;
    for(int i=0;i<1024;i++){ if(!have[i])continue; int t=i-512; i64 dx=(i64)t<<13;
        i64 I1=(arr(KC,dx)+K)>>23; i64 J=(arr(I1,dx)+K)>>18;
        bool ok = J>=JL[i]&&J<=JH[i]; if(ok)sc++;
        if(t>=0){npos++; if(ok)pos++;} else {nneg++; if(ok)neg++;} }
    printf("jwin18 baseline score %d/1024   (t>=0: %d/%d in window ; t<0: %d/%d)\n",sc,pos,npos,neg,nneg);
    printf("CEILING for ANY purely sign-dependent change (t>=0 untouched): %d/1024\n",pos+nneg);
    for(int t : {17,-17,342,-342}){ int i=t+512; i64 dx=(i64)t<<13;
        i64 I1=(arr(KC,dx)+K)>>23; i64 J=(arr(I1,dx)+K)>>18;
        printf("  t=%4d  model J=%lld   window [%lld,%lld]  %s\n",t,(long long)J,(long long)JL[i],(long long)JH[i],
            (J>=JL[i]&&J<=JH[i])?"IN":"OUT"); }
    return 0; }
