// p1_sqfield.cpp -- TRANSPLANT FSRRA's square-path STRUCTURE into FSCA.
// FSRRA (verified to 0.021% refit): sq = ((a*a)>>SQS<<SQS) + SQK ; quad = trunc(C2*sq/2^..)
//   i.e. a REDUCED-PRECISION square field with an ADDITIVE COMPENSATION CONSTANT, then a per-node coef.
// FSCA's model instead: I1=(arr(dx,KC)+K)>>23 ; J=(arr(I1,dx)+K)>>20 ; T2=sm(arr(J,S)+K,u2).
// Here: sq = ((arr(dx,dx)+K) >> (26+g)) + SQK ;  T2 = sm(arr(sq, C2_n) + K, u)
// C2_n is SOLVED EXACTLY per node (a labelled diagnostic: if no C2_n exists the structure is dead;
// if one does, the next question is whether the solved values follow a derived rule).
#include "base2.h"
#include "fsca_common.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>
static Fsca F;
static const int64_t C0T[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
struct Cell { int64_t lo,hi; bool live; };
int main(){ if(!load_fsca(F)) return 1; build_tables();
    // per-angle: hardware V interval, and C0 + s*T1 - s*T3 (everything except T2)
    struct A { int m,t; int64_t rest, Vlo,Vhi; };
    std::vector<A> AS;
    for(int a=0;a<32768;a++){
        int q=(int)((a+512u)>>10), m=q&63, t=a-1024*q, flip=0; if(t<-512)t+=65536;
        if(m>=17){ m=32-m; flip=1; } if(m<0||m>16) continue;
        int64_t s=flip?-1:1, dx=(int64_t)t<<13; const Tab&T=TB[m];
        int64_t T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
        int64_t A3=((arr(((int64_t)t*t)<<3,dx)+K)>>20)>>3;
        int64_t T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
        Win w=decode(F.srz[a],F.srn[a]); int64_t lo=w.sign>0?w.lo:-w.hi, hi=w.sign>0?w.hi:-w.lo;
        AS.push_back({m,t,C0T[m]+s*T1-s*T3,lo,hi});
    }
    printf("angles usable: %zu   (FSCA baseline for this structure's competitor: 147 violations)\n\n",AS.size());
    printf("  g   u   SQK   nodes with a feasible C2   worst node   (17 = complete structural fit)\n");
    int bestn=-1; int bg=0,bu=0; int64_t bk=0;
    for(int g=0;g<=6;g++) for(int u=24;u<=40;u++){
      for(int64_t SQK : {(int64_t)0,(int64_t)1024,(int64_t)2048,(int64_t)2730,(int64_t)3072,(int64_t)4096}){
        int nodesok=0; int worst=-1;
        for(int n=0;n<17;n++){
            int64_t clo=0, chi=(1LL<<24)-1; bool live=true; int nc=0;
            for(const A&x : AS){ if(x.m!=n||!live) continue; if(x.t==0) continue;
                int64_t dx=(int64_t)x.t<<13;
                int64_t sq=((arr(dx,dx)+K)>>(26+g))+SQK; if(sq<=0) continue;
                int64_t T2lo=x.rest-x.Vhi, T2hi=x.rest-x.Vlo;     // T2 must land here
                // arr(sq,C2) monotone in C2 (sq>0): binary search the C2 range giving T2 in [T2lo,T2hi]
                auto t2of=[&](int64_t C2){ return sm(arr(sq,C2)+K,u); };
                if(t2of(chi)<T2lo || t2of(clo)>T2hi){ live=false; break; }
                int64_t a1=clo,b1=chi; while(a1<b1){ int64_t mid=(a1+b1)/2; if(t2of(mid)>=T2lo) b1=mid; else a1=mid+1; }
                int64_t nlo=a1;
                a1=clo;b1=chi; while(a1<b1){ int64_t mid=(a1+b1+1)/2; if(t2of(mid)<=T2hi) a1=mid; else b1=mid-1; }
                int64_t nhi=a1;
                clo=std::max(clo,nlo); chi=std::min(chi,nhi); nc++;
                if(clo>chi){ live=false; break; }
            }
            if(live&&clo<=chi) nodesok++; else if(worst<0) worst=n;
        }
        if(nodesok>bestn){ bestn=nodesok; bg=g; bu=u; bk=SQK;
            printf("  %d  %2d  %4lld            %2d                  %2d\n",g,u,(long long)SQK,nodesok,worst); }
      } }
    printf("\nBEST: %d of 17 nodes admit a C2 at g=%d u=%d SQK=%lld\n",bestn,bg,bu,(long long)bk);
    if(bestn<17) printf("  -> the FSRRA square-field structure does NOT fit FSCA as-is.\n");
    return 0; }
