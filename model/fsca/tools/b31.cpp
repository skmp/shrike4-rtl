// b31.cpp -- the exact slice the asymmetry ladder predicts: sA = 24, sB + u3 = 49, Kq = KC.
// The sign asymmetry is then delta(t) * t/64 latch units, which is the only ladder step that can produce the
// D in [6,7] that |t| = 342 forces.  Print per-node scores for every role / rounding-constant combination.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 CLO[32][1024],CHI[32][1024]; static int TABN[32];
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; TABN[node]=n; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            CLO[node][t+512]=base-hi; CHI[node][t+512]=base-lo; } }
    printf("sA sB u3 r1 r2 r3 k1k2k3 Kqd | tot | n1 n2 n16 n31 | nodes 3..15\n");
    struct Row{int tot;char s[400];}; std::vector<Row> rows;
    for(int sA=23;sA<=25;sA++) for(int sB=20;sB<=24;sB++){ int u3=49-sB; if(sA+sB+u3!=73) { }
        for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++) for(int r3=0;r3<2;r3++)
          for(int k1=0;k1<2;k1++) for(int k2=0;k2<2;k2++) for(int k3=0;k3<2;k3++) for(int dq=-3;dq<=3;dq++){
            i64 kq=KC*(1ll<<(sA+sB+u3-73))+dq; if(sA+sB+u3<73) kq=KC/(1ll<<(73-sA-sB-u3))+dq;
            i64 K1=k1?(1ll<<19):0,K2=k2?(1ll<<19):0,K3=k3?(1ll<<19):0;
            int per[32]={0}; int tot=0;
            for(int node=1;node<=31;node++){ int n=TABN[node]; int kk=TB[n].u2-30; int v=0; i64 Fs=TB[n].S; int shB=sB+kk;
                for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 T2;
                    if(!Fs) T2=0; else { i64 B=((r1? arr(dx,Fs):arr(Fs,dx))+K2)>>shB;
                        i64 A=((r2? arr(dx,kq):arr(kq,dx))+K1)>>sA;
                        i64 P=r3? arr(B,A):arr(A,B); T2=sm(P+K3,u3); }
                    if(T2<CLO[node][t+512]||T2>CHI[node][t+512]) v++; }
                per[node]=v; tot+=v; }
            Row r; r.tot=tot; int o=snprintf(r.s,sizeof r.s,"%2d %2d %2d  %d  %d  %d  %d%d%d %+2d | %4d | %d %d %d %d |",
                sA,sB,u3,r1,r2,r3,k1,k2,k3,dq,tot,per[1],per[2],per[16],per[31]);
            for(int n=3;n<=15;n++) o+=snprintf(r.s+o,sizeof(r.s)-o," %d",per[n]);
            rows.push_back(r); } }
    std::sort(rows.begin(),rows.end(),[](const Row&a,const Row&b){return a.tot<b.tot;});
    for(int i=0;i<18&&i<(int)rows.size();i++) printf("%s\n",rows[i].s);
    return 0; }
