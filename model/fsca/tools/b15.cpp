// b15.cpp -- the t=+-17 conflict is direct evidence that one of the NON-square parts is a hair off.
// Enumerate structural variants of the linear/cubic sign conventions, the sine field and the final rounding
// constant; for each, rebuild the allowed-J dataset and report how many t are FEASIBLE (1024 = the square
// path can then explain everything) and the residual violations with a free J.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
// flip conventions for a sign-magnitude term whose coefficient is negative
static inline i64 term(i64 field,i64 dx,int u,int conv,int flip){
    if(!field) return 0;
    if(!flip) return sm(arr(field,dx)+K,u);
    switch(conv){ case 0: return -sm(arr(field,dx)+K,u);                 // negate the magnitude afterwards
                  case 1: return sm(arr(field,-dx)+K,u);                 // negate the multiplicand
                  default: return sm(~arr(field,dx)+K,u); } }            // one's-complement the product
int main(){ if(!load_fsca(F))return 1; build_tables();
    // per-node t=0 windows pin C0 candidates
    i64 c0lo[17],c0hi[17];
    for(int n=0;n<=16;n++){ i64 lo,hi;bool ex; win_of(n*1024,lo,hi,ex); c0lo[n]=lo;c0hi[n]=hi; }
    printf("conv1 conv3 Svar K3 | feasible t | free-J violations | note\n");
    int bestfeas=0;
    for(int conv1=0;conv1<3;conv1++) for(int conv3=0;conv3<3;conv3++) for(int Svar=0;Svar<4;Svar++) for(int k3=0;k3<3;k3++){
        // sine field variant
        i64 SV[17]; int UV[17];
        for(int n=0;n<=16;n++){ if(!TB[n].S){SV[n]=0;UV[n]=30;continue;}
            int k=TB[n].u2-30; i128 s=sin_(PI_()*n/32);
            i64 f23r=rn_scale(s,23+k), f24r=rn_scale(s,24+k);
            i64 f23t=(i64)((s<<(23+k))/QONE), f24t=(i64)((s<<(24+k))/QONE);
            switch(Svar){ case 0: SV[n]=std::min(f23r,(i64)((1ll<<23)-1)); UV[n]=30+k; break;
                          case 1: SV[n]=std::min(f23t,(i64)((1ll<<23)-1)); UV[n]=30+k; break;
                          case 2: SV[n]=std::min(f24r,(i64)((1ll<<24)-1)); UV[n]=31+k; break;
                          default:SV[n]=std::min(f24t,(i64)((1ll<<24)-1)); UV[n]=31+k; break; } }
        // choose C0 per node to maximise feasibility: try both endpoints, keep the better
        i64 C0[17]; for(int n=0;n<=16;n++) C0[n]=c0lo[n];
        int feas=0,resid=0;
        for(int pass=0;pass<2;pass++){
          feas=0;resid=0;
          for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            // gather constraints for every candidate C0 assignment is too big; use the current C0[] and
            // additionally allow each node its own endpoint choice by taking the UNION of its two windows
            i64 gl=0,gh=12000000; int nbad=0;
            struct CC{ i64 lo,hi,S; int u; }; std::vector<CC> cc;
            for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!SV[n])continue;
                i64 T1=term(TB[n].C1f,dx,23-TB[n].e1,conv1,flip);
                i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=term(TB[n].c3<<5,A3,26,conv3,flip);
                i64 base=C0[n]+T1-T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                cc.push_back({base-hi,base-lo,SV[n],UV[n]}); }
            i64 K3 = k3==0?(1ll<<19): k3==1?0 : 0;      // 2 = per-node 2^(u-1), handled below
            for(auto&c:cc){ i64 kk = k3==2? (1ll<<(c.u-1)) : K3;
                i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,c.S)+kk,c.u)>=c.lo)b1=m;else a1=m+1;}
                i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,c.S)+kk,c.u)<=c.hi)a2=m;else b2=m-1;}
                if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
            if(gl<=gh) feas++;
            else { // residual: best single J
                int bv=1<<30; std::vector<i64> cand; for(auto&c:cc){ cand.push_back(gl); cand.push_back(gh); }
                cand.push_back(gl-1);cand.push_back(gh+1);
                for(i64 j : cand){ if(j<0)continue; int v=0; for(auto&c:cc){ i64 kk=k3==2?(1ll<<(c.u-1)):K3;
                    i64 T2=sm(arr(j,c.S)+kk,c.u); if(T2<c.lo||T2>c.hi)v++; } bv=std::min(bv,v); }
                resid+=bv; }
            (void)nbad; }
          if(pass==0){ for(int n=0;n<=16;n++) C0[n]=c0hi[n]; }   // second pass with the other endpoint
        }
        printf("  %d     %d     %d   %d  | %4d       | %3d\n",conv1,conv3,Svar,k3,feas,resid);
        bestfeas=std::max(bestfeas,feas);
    }
    printf("best feasible-t count: %d (1024 would mean the square latch can explain everything)\n",bestfeas);
    return 0; }
