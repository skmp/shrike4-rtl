// wa_lp.cpp -- DECISIVE feasibility test for the whole two-multiply chain family.
// Relaxation (a strict SUPERSET of the family: real slope, real rounding offsets per sign of t,
// real second constant, and the array's exact deviation replaced by its measured bound):
//     I1(t) = floor(c*t + a_s + e1),  e1 in [-d1,0]     (s = sign of t; a_p/a_m independent
//                                                        => covers K1 and every latch rule)
//     J(t)  = floor(I1(t)*t*2^e + k + e2), e2 in [-d2,0] (=> covers K2 and every latch rule)
// Necessary conditions:  n=I1(t) in [nlo,nhi] with
//     t>0: n >= (JL-k)/(t*u), n < (JH+1+d2-k)/(t*u)      u = 2^e
//     t<0: mirrored
//     and  nlo <= c*t + a_s < nhi+1+d1
// The (c,a_p),(c,a_m) systems are 2-var LPs; F(c)=min(U-ct)-max(L-ct) is concave -> exact interval.
#include "wa_common.h"
#include <omp.h>
#include <cmath>
static Windows W;
typedef long double ld;
static int NT; static int TL[1024];
struct Prob { ld L[1024],U[1024]; };
// returns 1 if feasible, 0 if not; fills the conflicting t when infeasible
static int side_interval(const ld*Lv,const ld*Uv,const int*ts,int n,ld c0,ld&clo,ld&chi){
    auto F=[&](ld c)->ld{ ld mx=-1e30L,mn=1e30L; for(int i=0;i<n;i++){ ld ct=c*(ld)ts[i];
            ld a=Lv[i]-ct, b=Uv[i]-ct; if(a>mx)mx=a; if(b<mn)mn=b; } return mn-mx; };
    ld lo=c0-0.05L, hi=c0+0.05L;
    for(int it=0;it<200;it++){ ld m1=lo+(hi-lo)/3, m2=hi-(hi-lo)/3; if(F(m1)<F(m2)) lo=m1; else hi=m2; }
    ld cs=(lo+hi)/2; if(F(cs)<0) return 0;
    ld a=c0-0.05L,b=cs; for(int it=0;it<200;it++){ ld m=(a+b)/2; if(F(m)>=0) b=m; else a=m; } clo=b;
    a=cs; b=c0+0.05L; for(int it=0;it<200;it++){ ld m=(a+b)/2; if(F(m)>=0) a=m; else b=m; } chi=a;
    return 1; }
int main(int argc,char**argv){ build_tables(); if(!load_windows(W)){puts("no wa_jwin.txt");return 1;}
    for(int t=-512;t<=511;t++) if(W.ok[t+512]&&t!=0) TL[NT++]=t;
    printf("t used: %d\n",NT);
    ld L1=6.0L*65536, L2=8.0L*65536;      // measured bounds on the array deviation (magnitude)
    puts("\n e   s1 s2   d1      d2     | feasible kappa windows (relaxed family)   | c range");
    for(int e=-8;e<=-3;e++){
      // most generous: smallest s1 and s2 in the allowed g range -> largest relaxation
      for(int g1=8;g1<=15;g1++) for(int g2=8;g2<=15;g2++){
        int s1=g1+17+e, s2=g2-e; if(s1<4||s2<4) continue;
        if(!(g1==8&&g2==8) && !(g1==13&&g2==13) && !(g1==8&&g2==15) && !(g1==15&&g2==8)) continue;
        ld d1=L1/powl(2.0L,s1), d2=L2/powl(2.0L,s2), u=powl(2.0L,e);
        ld c0=(ld)KC/powl(2.0L,17+e);
        // kappa breakpoints
        std::vector<ld> kb; ld kmin=-2.0L,kmax=3.0L;
        for(int i=0;i<NT;i++){ int t=TL[i]; ld tu=(ld)t*u;
            for(int which=0;which<2;which++){ ld base = which? (ld)W.JH[t+512]+1+d2 : (ld)W.JL[t+512];
                // kappa = base - m*tu  in [kmin,kmax]
                ld mlo=(base-kmax)/tu, mhi=(base-kmin)/tu; if(mlo>mhi)std::swap(mlo,mhi);
                long long a=(long long)floorl(mlo)-1,b=(long long)ceill(mhi)+1;
                if(b-a>4000) continue;
                for(long long m=a;m<=b;m++){ ld k=base-(ld)m*tu; if(k>kmin&&k<kmax) kb.push_back(k); } } }
        kb.push_back(kmin+1e-9L); std::sort(kb.begin(),kb.end());
        int nfeas=0; ld bk=0,bclo=0,bchi=0; size_t nk=kb.size();
        std::vector<char> feas(nk,0);
        #pragma omp parallel for schedule(dynamic,32)
        for(size_t ki=0;ki<nk;ki++){
            ld k = ki+1<nk ? (kb[ki]+kb[ki+1])/2 : kb[ki]+1e-9L;
            static thread_local std::vector<ld> Lp,Up,Lm,Um; static thread_local std::vector<int> tp,tm;
            Lp.clear();Up.clear();Lm.clear();Um.clear();tp.clear();tm.clear();
            bool bad=false;
            for(int i=0;i<NT&&!bad;i++){ int t=TL[i]; ld tu=(ld)t*u;
                ld A=((ld)W.JL[t+512]-k)/tu, B=((ld)W.JH[t+512]+1+d2-k)/tu;   // n in [A,B) for t>0
                ld nlo,nhi;
                if(t>0){ nlo=ceill(A); nhi=ceill(B)-1; } else { nlo=floorl(B)+1; nhi=floorl(A); }
                if(nlo>nhi){ bad=true; break; }
                if(t>0){ tp.push_back(t); Lp.push_back(nlo); Up.push_back(nhi+1+d1); }
                else    { tm.push_back(t); Lm.push_back(nlo); Um.push_back(nhi+1+d1); } }
            if(bad) continue;
            ld p1,p2,m1,m2;
            if(!side_interval(Lp.data(),Up.data(),tp.data(),(int)tp.size(),c0,p1,p2)) continue;
            if(!side_interval(Lm.data(),Um.data(),tm.data(),(int)tm.size(),c0,m1,m2)) continue;
            if(std::max(p1,m1)<=std::min(p2,m2)) feas[ki]=1;
        }
        for(size_t ki=0;ki<nk;ki++) if(feas[ki]) nfeas++;
        printf("%2d  %2d %2d  %7.4f %7.4f | g1=%2d g2=%2d  feasible kappa intervals: %d / %zu\n",
            e,s1,s2,(double)d1,(double)d2,g1,g2,nfeas,nk);
        fflush(stdout);
      }
    }
    return 0; }
