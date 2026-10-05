// p2_sqfield.cpp -- as p1 but FAIR: C2 up to 2^28 (nominal for node 16 at g=0,u=26 is ~2.07e7, above
// p1's 2^24 cap), and scored by VIOLATIONS (baseline 147) via exact max-overlap of the per-t allowed
// C2 intervals, rather than demanding zero violations.
#include "base2.h"
#include "fsca_common.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>
static Fsca F;
static const int64_t C0T[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
struct A { int m; int t; int64_t rest,Vlo,Vhi; };
static std::vector<A> AS;
static const int64_t CMAX=(1LL<<28)-1;
int main(){ if(!load_fsca(F)) return 1; build_tables();
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
    printf("FSRRA-style square field transplanted into FSCA:\n");
    printf("  sq = ((arr(dx,dx)+K) >> (26+g)) + SQK ;  T2 = sm(arr(sq, C2_n) + K, u)   C2_n solved per node\n");
    printf("  (C2 searched to 2^28; scored by violations, baseline model = 147 of 32768)\n\n");
    int best=1<<30,bg=0,bu=0; int64_t bk=0; std::vector<int64_t> bC2;
    for(int g=0;g<=4;g++) for(int u=22;u<=38;u++) for(int64_t SQK : {(int64_t)0,(int64_t)2730,(int64_t)3072}){
        int tot=0; std::vector<int64_t> C2s(17,0);
        for(int n=0;n<17;n++){
            std::vector<std::pair<int64_t,int>> ev; int nt=0;
            for(const A&x : AS){ if(x.m!=n) continue; if(x.t==0) continue;
                int64_t dx=(int64_t)x.t<<13;
                int64_t sq=((arr(dx,dx)+K)>>(26+g))+SQK; if(sq<=0) continue;
                nt++;
                int64_t T2lo=x.rest-x.Vhi, T2hi=x.rest-x.Vlo;
                auto t2=[&](int64_t C){ return sm(arr(sq,C)+K,u); };
                if(t2(CMAX)<T2lo||t2(0)>T2hi) continue;            // no C2 works for this t
                int64_t a1=0,b1=CMAX; while(a1<b1){int64_t m2=(a1+b1)/2; if(t2(m2)>=T2lo)b1=m2; else a1=m2+1;}
                int64_t L=a1; a1=0;b1=CMAX; while(a1<b1){int64_t m2=(a1+b1+1)/2; if(t2(m2)<=T2hi)a1=m2; else b1=m2-1;}
                int64_t Rr=a1; if(L>Rr) continue;
                ev.push_back({L,+1}); ev.push_back({Rr+1,-1});
            }
            std::sort(ev.begin(),ev.end());
            int cur=0,mx=0; int64_t arg=0;
            for(size_t i=0;i<ev.size();i++){ cur+=ev[i].second; if(cur>mx){mx=cur;arg=ev[i].first;} }
            tot += nt-mx; C2s[n]=arg;
        }
        if(tot<best){ best=tot;bg=g;bu=u;bk=SQK;bC2=C2s;
            printf("  g=%d u=%2d SQK=%4lld -> %5d violations%s\n",g,u,(long long)SQK,tot,tot<147?"   *** BEATS BASELINE ***":""); }
    }
    printf("\nBEST %d violations at g=%d u=%d SQK=%lld   (baseline 147)\n",best,bg,bu,(long long)bk);
    printf("  solved C2 per node:"); for(int n=0;n<17;n++) printf(" %lld",(long long)bC2[n]); printf("\n");
    printf("  ratio C2_n / C2_16 vs sin(n*pi/32) (a derived rule would make these match):\n   ");
    for(int n=0;n<17;n++){ double r=bC2[16]?(double)bC2[n]/bC2[16]:0, s=__builtin_sin(n*3.14159265358979/32);
        printf(" %d:%.4f/%.4f",n,r,s); if(n%5==4)printf("\n   "); }
    printf("\n");
    return 0; }
