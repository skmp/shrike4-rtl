// v12_final.cpp -- consolidated numbers actually quoted in the verdict.
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
static uint32_t hsh(uint32_t x){x^=x>>16;x*=0x7feb352dU;x^=x>>15;x*=0x846ca68bU;x^=x>>16;return x;}
static void plant(int q,int nlev,int seed){
    const long double TWOPI=6.283185307179586476925286766559L,w=TWOPI/65536.0L,SC=67108864.0L,H=512.0L;
    for(int a=0;a<65536;a++){ long double A=a; long long k=(long long)std::floor((A-q)/1024.0L);
        long double c=q+1024.0L*k+H, th=w*c, t=A-c;
        long double f0=sinl(th)*SC,f1=w*cosl(th)*SC,f2=-w*w*sinl(th)*SC,f3=-w*w*w*cosl(th)*SC,f4=w*w*w*w*sinl(th)*SC;
        long double qc=f4/24.0L, A0=f0-qc*H*H*H*H/8.0L, A1=f1, A2=f2/2.0L+qc*H*H, A3=f3/6.0L;
        long long iv=(long long)llroundl(A0+A1*t+A2*t*t+A3*t*t*t);
        if(nlev){uint32_t r=hsh((uint32_t)a*2654435761u+seed); if((int)(r%100u)<nlev) iv+=(r&0x10000)?1:-1;}
        MD[a]=2*iv; WD[a]=RW[a]; } }
int main(){ Fsca F; if(!load_fsca(F))return 1;
    for(int a=0;a<65536;a++){ Win wn=decode(F.srz[a],F.srn[a]); i64 lo,hi;
        if(wn.sign>0){lo=wn.lo;hi=wn.hi;}else{lo=-wn.hi;hi=-wn.lo;} MD[a]=lo+hi; WD[a]=hi-lo; RW[a]=hi-lo; }
    std::vector<i64> RMD(MD,MD+65536);
    std::vector<i64> c; cert_profile(0,16384,1024,c);
    std::vector<double> pm(1024),pw(1024);
    for(int p=0;p<1024;p++){ double s=0;int n=0; i64 wr=0;
        for(int k=0;;k++){int lo=p+1024*k; if(lo+1023>=16384)break; s+=(double)c[lo]/32.0;n++; if(c[lo]>wr)wr=c[lo];}
        pm[p]=s/n; pw[p]=(double)wr/32.0; }
    // agent's 15-window convention for p=0 (their k<15 cap)
    { double s=0; for(int k=0;k<15;k++) s+=(double)c[1024*k]/32.0; printf("p=0, 15 windows (their cap): mean %.3f\n",s/15); }
    printf("p=0, all 16 windows          : mean %.3f worst %.3f\n",pm[0],pw[0]);
    printf("p=512                        : mean %.3f worst %.3f\n",pm[512],pw[512]);
    double bm=1e9,bw=0; int pbm=0,pbw=0;
    for(int p=0;p<1024;p++){ if(pm[p]<bm){bm=pm[p];pbm=p;} if(pm[p]>bw){bw=pm[p];pbw=p;} }
    double wworst=0; int pww=0; for(int p=0;p<1024;p++) if(pw[p]>wworst){wworst=pw[p];pww=p;}
    printf("step-1 best mean %.3f at p=%d ; worst mean %.3f at p=%d ; worst 'worst' %.3f at p=%d\n",bm,pbm,bw,pbw,wworst,pww);
    printf("p=768 (their worst)          : mean %.3f worst %.3f\n",pm[768],pw[768]);
    // RMS of real profile vs planted q=512 profile, affine-fitted
    plant(512,40,12345); std::vector<i64> c2; cert_profile(0,16384,1024,c2);
    std::vector<double> qm(1024);
    for(int p=0;p<1024;p++){ double s=0;int n=0; for(int k=0;;k++){int lo=p+1024*k; if(lo+1023>=16384)break; s+=(double)c2[lo]/32.0;n++;} qm[p]=s/n; }
    double e=0,mx=0; for(int p=0;p<1024;p++){ double d=pm[p]-qm[p]; e+=d*d; if(std::fabs(d)>mx)mx=std::fabs(d); }
    printf("real vs planted(q=512,econ,n40) profile: RMS %.4f  max dev %.4f  (profile range %.3f..%.3f)\n",
        std::sqrt(e/1024),mx,bm,bw);
    return 0; }
