// w5_model.h -- branch-5 general FSCA evaluator over ALL 65536 angles, with a parameterised
// node rule and t-formation.  Baseline params reproduce the published model exactly.
#pragma once
#include "base2.h"
#include <cstdio>
static Fsca W5F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void w5_win(int a,i64&lo,i64&hi){ Win w=decode(W5F.srz[a],W5F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} }
struct P5 {
    int rho=512;          // node = (angle + rho) >> 10
    int dlin=0,dsq=0,dcub=0;   // t-level offset applied when t<0 (hypothesis A), per term
    int e=0;              // hypothesis B half-step: dx = (2*t + e) << 12   (e=0 == t<<13)
    int halfcub=0;        // 1: cubic's square factor uses ((2t+e)^2)>>2 instead of t*t
    int order=1;          // 1: arr(dx,KC)   0: arr(KC,dx)
    int qlin=0,qsq=0,qcub=0;   // dx-LEVEL offset applied when t<0 (1s-vs-2s at the dx node)
    int rfb=0;            // offset on the I1 feedback latch when t<0
    int shw=13;           // dx = t << shw (with the compensating scale law when shw != 13)
};
// dx for a given (possibly offset) t value
static inline i64 w5_dx(i64 tv,const P5&P){ return P.e? ((2*tv+P.e)<<(P.shw-1)) : (tv<<P.shw); }
static inline i64 w5_V(int n,int t,int flip,const P5&P){
    const Tab&T=TB[n];
    i64 tl = t + (t<0?P.dlin:0), ts = t + (t<0?P.dsq:0), tc = t + (t<0?P.dcub:0);
    i64 dxl=w5_dx(tl,P)+(t<0?P.qlin:0), dxs=w5_dx(ts,P)+(t<0?P.qsq:0), dxc=w5_dx(tc,P)+(t<0?P.qcub:0);
    int extra = 2*(P.shw-13);
    i64 T1=T.C1f? sm(arr(T.C1f,dxl)+K,23-T.e1+(P.shw-13)):0;
    i64 sq = P.halfcub? (((2*tc+P.e)*(2*tc+P.e))>>2) : (tc*tc);
    i64 A3=((arr(sq<<3,dxc)+K)>>20)>>3;
    i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
    i64 I1=P.order? ((arr(dxs,KC)+K)>>23) : ((arr(KC,dxs)+K)>>23);
    if(t<0) I1+=P.rfb;
    i64 I2=(arr(I1,dxs)+K)>>20;
    i64 T2=T.S? sm(arr(I2,T.S)+K,U2TAB[n]+extra):0;
    i64 s=flip?-1:1; return C0TAB[n]+s*T1-T2-s*T3; }
// violations over all 65536 angles; also splits by sign of t
static inline int w5_score(const P5&P,int*vpos=nullptr,int*vneg=nullptr,int*maxabs_t=nullptr){
    int v=0,vp=0,vn=0,mt=0;
    for(int a=0;a<65536;a++){
        int nr=(a+P.rho)>>10; int node=nr&63; int t=a-1024*nr;
        if(t>mt)mt=t; if(-t>mt)mt=-t;
        int so=1,m=node; if(m>=32){so=-1;m-=32;}
        int flip=m>=17,n=flip?32-m:m;
        i64 V=so*w5_V(n,t,flip,P);
        i64 lo,hi; w5_win(a,lo,hi);
        if(V<lo||V>hi){v++; if(t>=0)vp++; else vn++;} }
    if(vpos)*vpos=vp; if(vneg)*vneg=vn; if(maxabs_t)*maxabs_t=mt; return v; }
