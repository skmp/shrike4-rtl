// v12_shift.cpp -- localize the partition with the WHOLE 1024-phase profile, not just its argmin.
// The planted-partition construction is translation covariant, so the profile for a plant at q is the
// profile for a plant at q=0 shifted by q.  Cross-correlating the REAL profile against the q=0 plant
// profile therefore estimates the real partition phase using all 1024 numbers instead of one argmin.
// Validated by feeding plants at known q through the identical procedure.
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
static void profile(int L,std::vector<double>&P,int A1=16384){ std::vector<i64> c; cert_profile(0,A1,L,c);
    P.assign(L,0); for(int p=0;p<L;p++){ double s=0;int n=0;
        for(int k=0;;k++){int lo=p+L*k; if(lo+L-1>=A1)break; int i=lo; if(i>=(int)c.size())break; s+=(double)c[i]/32.0;n++;} P[p]=n?s/n:0; } }
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
// best shift q minimising affine-fitted L2 distance  ||target - (alpha*ref_shifted + beta)||
static int best_shift(const std::vector<double>&tgt,const std::vector<double>&ref,double*outr=nullptr){
    int L=tgt.size(); double bb=1e18; int bq=0; std::vector<double> sc(L);
    for(int q=0;q<L;q++){ double sx=0,sy=0,sxx=0,sxy=0;
        for(int p=0;p<L;p++){ double x=ref[((p-q)%L+L)%L], y=tgt[p]; sx+=x;sy+=y;sxx+=x*x;sxy+=x*y; }
        double n=L, den=n*sxx-sx*sx; double al=den!=0?(n*sxy-sx*sy)/den:0, be=(sy-al*sx)/n, e=0;
        for(int p=0;p<L;p++){ double x=ref[((p-q)%L+L)%L], d=tgt[p]-(al*x+be); e+=d*d; }
        sc[q]=e; if(e<bb){bb=e;bq=q;} }
    if(outr){ double s2=1e18; for(int q=0;q<L;q++){int d=std::abs(q-bq);d=std::min(d,L-d); if(d<=8)continue; if(sc[q]<s2)s2=sc[q];} *outr=s2/bb; }
    return bq; }
int main(){ Fsca F; if(!load_fsca(F))return 1;
    for(int a=0;a<65536;a++){ Win wn=decode(F.srz[a],F.srn[a]); i64 lo,hi;
        if(wn.sign>0){lo=wn.lo;hi=wn.hi;}else{lo=-wn.hi;hi=-wn.lo;} MD[a]=lo+hi; WD[a]=hi-lo; RW[a]=hi-lo; }
    std::vector<i64> RMD(MD,MD+65536), RWD(WD,WD+65536);
    std::vector<double> preal; profile(1024,preal);
    plant(0,30,777); std::vector<double> ref; profile(1024,ref);
    printf("PROCEDURE VALIDATION (plant at known q, matched against the q=0 reference profile):\n");
    for(int q : {0,64,137,256,433,512,700,900}){ plant(q,30,31337); std::vector<double> pp; profile(1024,pp);
        double m; int bq=best_shift(pp,ref,&m); int d=((bq-q+512)&1023)-512;
        printf("   planted q=%4d -> estimated %4d (err %+4d)  margin x%.2f %s\n",q,bq,d,m,std::abs(d)<=2?"OK":"MISS"); }
    printf("\nREAL DUMP matched against the same reference:\n");
    { double m; int bq=best_shift(preal,ref,&m);
      printf("   estimated partition phase = %d   (margin over best shift >8 away: x%.2f)\n",bq,m);
      // how sharp: list the 8 best shifts
      int L=1024; std::vector<std::pair<double,int>> rk;
      for(int q=0;q<L;q++){ double sx=0,sy=0,sxx=0,sxy=0;
        for(int p=0;p<L;p++){double x=ref[((p-q)%L+L)%L],y=preal[p];sx+=x;sy+=y;sxx+=x*x;sxy+=x*y;}
        double n=L,den=n*sxx-sx*sx,al=(n*sxy-sx*sy)/den,be=(sy-al*sx)/n,e=0;
        for(int p=0;p<L;p++){double x=ref[((p-q)%L+L)%L],d=preal[p]-(al*x+be);e+=d*d;} rk.push_back({e,q}); }
      std::sort(rk.begin(),rk.end());
      printf("   8 best shifts: "); for(int i=0;i<8;i++) printf("%d(%.3f) ",rk[i].second,rk[i].first); printf("\n");
      printf("   shift 0 residual %.3f  vs best %.3f\n", rk[0].second==0?rk[0].first:[&]{for(auto&r:rk)if(r.second==0)return r.first;return 0.0;}(), rk[0].first); }
    // same on the upper half, independent data
    { std::memcpy(MD,RMD.data(),sizeof MD); std::memcpy(WD,RWD.data(),sizeof WD);
      std::vector<i64> c; cert_profile(16384,32768,1024,c); std::vector<double> pu(1024);
      for(int p=0;p<1024;p++){ double s=0;int n=0; for(int k=0;;k++){int lo=16384+p+1024*k; if(lo+1023>=32768)break; int i=lo-16384; if(i>=(int)c.size())break; s+=(double)c[i]/32.0;n++;} pu[p]=n?s/n:0; }
      double m; int bq=best_shift(pu,ref,&m);
      printf("   upper half [16384,32768) estimated phase = %d  margin x%.2f\n",bq,m); }
    return 0; }
