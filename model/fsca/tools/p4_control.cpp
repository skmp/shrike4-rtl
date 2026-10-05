// p4_control.cpp -- THE CONTROL.  p2/p3 scored the FSRRA-style structure with a per-node SOLVED C2
// against the current model's DERIVED S. That is not a fair comparison. Here both structures get a
// per-node solved coefficient, by the same max-overlap method.
//   A) current  : T2 = sm(arr(J, S_n) + K, u2_n)      J = (arr(I1,dx)+K)>>20, I1 = (arr(dx,KC)+K)>>23
//   B) FSRRA-ish: T2 = sm(arr(sq, C2_n) + K, u)       sq = (arr(dx,dx)+K) >> 26
#include "base2.h"
#include "fsca_common.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>
static Fsca F;
static const int64_t C0T[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
struct A{int m,t;int64_t rest,Vlo,Vhi;}; static std::vector<A> AS;
static const int64_t CMAX=(1LL<<28)-1;
// generic: given per-angle "operand" value, solve the per-node coefficient by max overlap
static int solve(int n,int u,int64_t(*opnd)(int),int&ntot){
    std::vector<std::pair<int64_t,int>> ev; int nt=0;
    for(const A&x:AS){ if(x.m!=n||x.t==0) continue; int64_t o=opnd(x.t); if(o<=0) continue; nt++;
        int64_t T2lo=x.rest-x.Vhi,T2hi=x.rest-x.Vlo;
        auto t2=[&](int64_t C){ return sm(arr(o,C)+K,u); };
        if(t2(CMAX)<T2lo||t2(0)>T2hi) continue;
        int64_t a=0,b=CMAX; while(a<b){int64_t m2=(a+b)/2; if(t2(m2)>=T2lo)b=m2; else a=m2+1;} int64_t L=a;
        a=0;b=CMAX; while(a<b){int64_t m2=(a+b+1)/2; if(t2(m2)<=T2hi)a=m2; else b=m2-1;} int64_t R=a;
        if(L<=R){ev.push_back({L,+1});ev.push_back({R+1,-1});} }
    std::sort(ev.begin(),ev.end()); int cur=0,mx=0; for(auto&e:ev){cur+=e.second;mx=std::max(mx,cur);}
    ntot=nt; return nt-mx;
}
static int64_t op_cur(int t){ int64_t dx=(int64_t)t<<13; int64_t I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>20; }
static int64_t op_sq (int t){ int64_t dx=(int64_t)t<<13; return (arr(dx,dx)+K)>>26; }
int main(){ if(!load_fsca(F)) return 1; build_tables();
    for(int a=0;a<32768;a++){ int q=(int)((a+512u)>>10),m=q&63,t=a-1024*q,flip=0; if(t<-512)t+=65536;
        if(m>=17){m=32-m;flip=1;} if(m<0||m>16) continue;
        int64_t s=flip?-1:1,dx=(int64_t)t<<13; const Tab&T=TB[m];
        int64_t T1=T.C1f?sm(arr(T.C1f,dx)+K,23-T.e1):0;
        int64_t A3=((arr(((int64_t)t*t)<<3,dx)+K)>>20)>>3;
        int64_t T3=T.c3?sm(arr(T.c3<<5,A3)+K,26):0;
        Win w=decode(F.srz[a],F.srn[a]); int64_t lo=w.sign>0?w.lo:-w.hi,hi=w.sign>0?w.hi:-w.lo;
        AS.push_back({m,t,C0T[m]+s*T1-s*T3,lo,hi}); }
    printf("Both structures with a per-node SOLVED coefficient (fitted-vs-fitted, the fair control).\n");
    printf("Current model with its DERIVED S and u2 = 147 violations of 32768.\n\n");
    for(int which=0;which<2;which++){
        const char* nm = which? "B) FSRRA-ish  sq=(arr(dx,dx)+K)>>26, coef per node" : "A) CURRENT    J-chain, coef per node";
        // global u
        int bg=1<<30,bu=0;
        for(int u=20;u<=40;u++){ int tot=0,nt; for(int n=0;n<17;n++) tot+=solve(n,u,which?op_sq:op_cur,nt);
            if(tot<bg){bg=tot;bu=u;} }
        // per-node u (the current model HAS a per-node u2, so this is the like-for-like version)
        int pn=0; std::vector<int> us(17,0);
        for(int n=0;n<17;n++){ int b=1<<30,bb=0; for(int u=20;u<=40;u++){ int nt,v=solve(n,u,which?op_sq:op_cur,nt); if(v<b){b=v;bb=u;} } pn+=b; us[n]=bb; }
        printf("  %s\n",nm);
        printf("      global shift : best %4d violations at u=%d\n",bg,bu);
        printf("      per-node shift: best %4d violations   u_n =",pn);
        for(int n=0;n<17;n++) printf(" %d",us[n]); printf("\n\n");
    }
    return 0; }
