// v12_len.cpp -- the phase claim presupposes a 1024-wide window.  Check the frame:
//  (1) min/max over phases of the mean certificate for L = 256,512,1024,2048,4096
//  (2) the RMS of the real 1024-phase profile against the planted 1024-wide/boundary-512 model
#include "base2.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>
static i64 MD[65536],WD[65536],RW[65536];
static const int C4[5]={1,4,6,4,1};
static void cert_profile(int A0,int A1,int L,std::vector<i64>&out){
    int nS=A1-L-A0+1; out.assign(nS>0?nS:0,0); if(nS<=0)return;
    std::vector<i64> V; std::vector<int> dq;
    for(int s=1;4*s<=L-1;s++){ int nOff=A1-4*s-A0; if(nOff<=0)break; V.assign(nOff,0);
        for(int i=0;i<nOff;i++){ int off=A0+i; i64 mid=0,sl=0;
            for(int j=0;j<5;j++){int a=off+j*s; mid+=(j&1?-1:1)*(i64)C4[j]*MD[a]; sl+=(i64)C4[j]*WD[a];}
            i64 v=(mid<0?-mid:mid)-sl; V[i]=v>0?v:0; }
        int win=L-4*s; dq.clear(); size_t h=0;
        for(int i=0;i<nOff;i++){ while(dq.size()>h&&V[dq.back()]<=V[i])dq.pop_back(); dq.push_back(i);
            int st=i-win+1; if(st>=0){ while(dq[h]<st)h++; if(st<nS&&V[dq[h]]>out[st])out[st]=V[dq[h]]; } } } }
int main(){ Fsca F; if(!load_fsca(F))return 1;
    for(int a=0;a<65536;a++){ Win wn=decode(F.srz[a],F.srn[a]); i64 lo,hi;
        if(wn.sign>0){lo=wn.lo;hi=wn.hi;}else{lo=-wn.hi;hi=-wn.lo;} MD[a]=lo+hi; WD[a]=hi-lo; RW[a]=hi-lo; }
    printf("min / max over ALL phases of the mean cubic-slack certificate, angles [0,16384):\n");
    printf("   L      min(phase)        max(phase)      ratio\n");
    for(int L : {256,512,1024,2048,4096}){
        std::vector<i64> c; cert_profile(0,16384,L,c);
        double mn=1e18,mx=-1e18; int pmn=0,pmx=0;
        for(int p=0;p<L;p++){ double s=0;int n=0;
            for(int k=0;;k++){int lo=p+L*k; if(lo+L-1>=16384)break; if(lo>=(int)c.size())break; s+=(double)c[lo]/32.0;n++;}
            if(!n)continue; double m=s/n; if(m<mn){mn=m;pmn=p;} if(m>mx){mx=m;pmx=p;} }
        printf("  %5d  %7.3f (p=%4d)  %7.3f (p=%4d)  %5.2f%s\n",L,mn,pmn,mx,pmx,mx/mn,
            L==2048?"   <- N=32 alternative: no cubic fits ANY 2048-wide interval":""); }
    return 0; }
