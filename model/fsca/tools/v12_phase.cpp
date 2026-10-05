// v12_phase.cpp -- ADVERSARIAL re-test of the "partition phase read off the dump" claim.
//
// Claim under test (branch "table-size", observable (b)):
//   "Sliding a 1024-wide cubic-fit window over all 32 phases gives a unique sharp minimum at p=512
//    (0.896 mean / 1.000 worst); p=0 gives 1.850 / 3.062 and the worst phase 2.692 / 4.375."
//
// Statistic: 4th-divided-difference certificate.  For equally spaced a0..a4 (spacing s) any cubic p
// satisfies sum (-1)^i C(4,i) p(a_i) == 0, so with dump windows [VL,VH]
//      max |x - p|  >=  ( |sum (-1)^i C(4,i) (VL+VH)| - sum C(4,i) (VH-VL) ) / 32 .
// All decisive arithmetic is i64; the /32 is display only.
//
// What this tool adds over w1_phase.cpp:
//   (A) ALL 1024 phases at step 1, not 32 phases at step 32   -> tests "unique" and "sharp"
//   (B) the identical statistic run on the UPPER half (nodes 17..31), which w1 never touched
//   (C) PLANTED-PARTITION CONTROLS: synthetic piecewise-cubic data with a KNOWN boundary phase and
//       the real dump's own window widths + a noise level tuned to the real within-node certificate.
//       If the statistic cannot recover a planted q, its verdict on the real dump means nothing.
//   (D) equal-coverage variant (every phase scored on the same angle span) to remove the |sin| drift.
#include "base2.h"
#include <vector>
#include <algorithm>
#include <cstring>
#include <cmath>

static i64 MD[65536], WD[65536];          // doubled mid (VL+VH), full width (VH-VL)
static i64 RW[65536];                     // real dump width, kept for the synthetic controls
static const int C4[5]={1,4,6,4,1};

// cert[start] = max over spacings s and stencils inside [start, start+L-1]  (doubled-by-32 units)
static void cert_profile(int A0,int A1,int L,std::vector<i64>&out){
    int nS = A1 - L - A0 + 1; out.assign(nS>0?nS:0, 0); if(nS<=0) return;
    std::vector<i64> V; std::vector<int> dq;
    for(int s=1; 4*s<=L-1; s++){
        int nOff = A1 - 4*s - A0;                 // off in [A0, A1-1-4s]
        if(nOff<=0) break; V.assign(nOff,0);
        for(int i=0;i<nOff;i++){ int off=A0+i; i64 mid=0,sl=0;
            for(int j=0;j<5;j++){ int a=off+j*s; mid += (j&1?-1:1)*(i64)C4[j]*MD[a]; sl += (i64)C4[j]*WD[a]; }
            i64 v=(mid<0?-mid:mid)-sl; V[i]= v>0? v:0; }
        int win = L - 4*s;                        // stencils starting in [start, start+win-1]
        dq.clear(); size_t h=0;
        for(int i=0;i<nOff;i++){
            while(dq.size()>h && V[dq.back()]<=V[i]) dq.pop_back();
            dq.push_back(i);
            int start = i - win + 1;
            if(start>=0){ while(dq[h] < start) h++;
                if(start<nS && V[dq[h]]>out[start]) out[start]=V[dq[h]]; } }
    }
}

struct Stat{ double mean, worst; };
static Stat phase_stat(const std::vector<i64>&cert,int A0,int p,int L,int lo_lim,int hi_lim){
    double sum=0; int cnt=0; i64 worst=0;
    for(int k=0;;k++){ int lo=p+L*k; if(lo<lo_lim) continue; if(lo+L-1>=hi_lim) break;
        int idx=lo-A0; if(idx<0||idx>=(int)cert.size()) break;
        sum+=(double)cert[idx]/32.0; cnt++; if(cert[idx]>worst)worst=cert[idx]; }
    return Stat{ cnt? sum/cnt : -1.0, (double)worst/32.0 };
}

// ---------- synthetic planted-partition generator (long double only for DATA GENERATION) ----------
static uint32_t hsh(uint32_t x){ x^=x>>16; x*=0x7feb352dU; x^=x>>15; x*=0x846ca68bU; x^=x>>16; return x; }
static void plant(int q,int L,int noise_amp){
    const long double TWOPI = 6.283185307179586476925286766559L;
    const long double w = TWOPI/65536.0L, SC = 67108864.0L;
    for(int a=0;a<65536;a++){
        long double A=(long double)a;
        long long k = (long long)std::floor((A-q)/(long double)L);
        long double c = q + (long double)L*k + L/2.0L;   // node centre
        long double th = w*c, t = A-c;
        long double f0=sinl(th)*SC, f1=w*cosl(th)*SC, f2=-w*w*sinl(th)*SC, f3=-w*w*w*cosl(th)*SC;
        long double v = f0 + f1*t + f2*t*t/2.0L + f3*t*t*t/6.0L;
        long long iv = (long long)llroundl(v);
        if(noise_amp){ int r=(int)(hsh((uint32_t)a*2654435761u)%(2u*noise_amp+1u))-noise_amp; iv+=r; }
        MD[a]=2*iv; WD[a]=RW[a];                         // same window widths as the real dump
    }
}
int main(int argc,char**argv){
    Fsca F; if(!load_fsca(F)){printf("no dump\n");return 1;}
    for(int a=0;a<65536;a++){ Win wn=decode(F.srz[a],F.srn[a]); i64 lo,hi;
        if(wn.sign>0){lo=wn.lo;hi=wn.hi;} else {lo=-wn.hi;hi=-wn.lo;}
        MD[a]=lo+hi; WD[a]=hi-lo; RW[a]=hi-lo; }
    const int L=1024;

    // ---------- (A) real dump, lower half, ALL 1024 phases ----------
    std::vector<i64> cert; cert_profile(0,16384,L,cert);
    printf("== (A) REAL DUMP, angles [0,16384), all 1024 phases at step 1 ==\n");
    std::vector<std::pair<double,int>> rank;
    double m0=0,w0=0,m512=0,w512=0, worstm=0; int worstp=0;
    for(int p=0;p<L;p++){ Stat st=phase_stat(cert,0,p,L,0,16384); rank.push_back({st.mean,p});
        if(p==0){m0=st.mean;w0=st.worst;} if(p==512){m512=st.mean;w512=st.worst;}
        if(st.mean>worstm){worstm=st.mean;worstp=p;} }
    std::sort(rank.begin(),rank.end());
    printf("   p=0   mean %.3f worst %.3f     p=512 mean %.3f worst %.3f\n",m0,w0,m512,w512);
    printf("   worst phase p=%d mean %.3f\n",worstp,worstm);
    printf("   12 lowest phases: "); for(int i=0;i<12;i++) printf("p=%d(%.3f) ",rank[i].second,rank[i].first); printf("\n");
    printf("   multiples of 32 only (what w1_phase sampled): ");
    { std::vector<std::pair<double,int>> r32; for(int p=0;p<L;p+=32){Stat st=phase_stat(cert,0,p,L,0,16384); r32.push_back({st.mean,p});}
      std::sort(r32.begin(),r32.end()); for(int i=0;i<4;i++) printf("p=%d(%.3f) ",r32[i].second,r32[i].first); printf("\n"); }
    // how many phases are within 25% / 50% of the minimum
    { int n25=0,n50=0; double mn=rank[0].first; for(auto&r:rank){ if(r.first<=mn*1.25)n25++; if(r.first<=mn*1.5)n50++; }
      printf("   phases within 25%% of min: %d   within 50%%: %d   (of 1024)\n",n25,n50); }

    // ---------- (B) upper half, independent data ----------
    { std::vector<i64> c2; cert_profile(16384,32768,L,c2);
      printf("== (B) REAL DUMP, angles [16384,32768) -- nodes 17..31, independent of (A) ==\n");
      std::vector<std::pair<double,int>> rk; double a0=0,a512=0;
      for(int p=0;p<L;p++){ Stat st=phase_stat(c2,16384,16384+p,L,16384,32768); rk.push_back({st.mean,p});
          if(p==0)a0=st.mean; if(p==512)a512=st.mean; }
      std::sort(rk.begin(),rk.end());
      printf("   p=0 mean %.3f   p=512 mean %.3f\n",a0,a512);
      printf("   8 lowest phases: "); for(int i=0;i<8;i++) printf("p=%d(%.3f) ",rk[i].second,rk[i].first); printf("\n"); }

    // ---------- (C) PLANTED CONTROLS ----------
    printf("== (C) PLANTED-PARTITION CONTROLS (synthetic piecewise cubic, real window widths) ==\n");
    for(int noise : {0,1,2}) for(int q : {0,256,512,768}){
        plant(q,L,noise); std::vector<i64> cs; cert_profile(0,16384,L,cs);
        std::vector<std::pair<double,int>> rk;
        double atq=0,at0=0,at512=0;
        for(int p=0;p<L;p++){ Stat st=phase_stat(cs,0,p,L,0,16384); rk.push_back({st.mean,p});
            if(p==q)atq=st.mean; if(p==0)at0=st.mean; if(p==512)at512=st.mean; }
        std::sort(rk.begin(),rk.end());
        printf("   noise +-%d  planted q=%3d : argmin p=%d (%.3f)  | at q %.3f | at 0 %.3f | at 512 %.3f | %s\n",
            noise,q,rk[0].second,rk[0].first,atq,at0,at512, rk[0].second==q?"RECOVERED":"*** WRONG ***");
    }
    return 0; }
