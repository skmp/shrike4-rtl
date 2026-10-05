// wc_enum.cpp -- full re-enumeration of the square path's multiplication orders, scored against the RAW
// window data (all 31 node instances, C0 refitted per table entry), NOT against the decoupled J intervals.
// Covers structures where the node-dependent coefficient enters early as well as last.
//
// forms   (A = global constant KC = 0x4ef145 ; C = per-node coefficient ; D = dx = t<<13 ; Q = t^2 latch)
//   0 CHAIN4   perm of {A,C,D,D}, chained left to right           (C = normalised sine field S)
//   1 TREE_AC  (A*C) * (D*D)                                      (C = S)
//   2 TREE_AD  (A*D) * (C*D)                                      (C = S)
//   3 CHAIN3Q  perm of {A,C,Q}, chained                           (C = S)
//   4 CHAIN3C  perm of {C2,D,D}, chained                          (C2 = STORED square coefficient)
//   5 CHAIN2Q  C2 * Q                                             (C2 stored)
//   6 SQW      w = latch(B*D) ; J = latch(w*w) ; T2 = J*S         (B = 2pi field, scanned)
#include "wc_core.h"
#include <cstdio>
#include <vector>
#include <algorithm>
#include <omp.h>
static inline i64 MUL(i64 a,i64 b,int role){ return role? arr(b,a) : arr(a,b); }
struct Cfg { short form,order,s0,s1,bq,du; short c0,c1,c2,r0,r1,ro0,ro1,ro2,cv; int Bc; };
static const int PERM4[12][4] = {{0,1,2,3},{0,2,1,3},{0,2,3,1},{1,0,2,3},{2,0,1,3},{2,0,3,1},
                                 {1,2,0,3},{2,1,0,3},{2,3,0,1},{1,2,3,0},{2,1,3,0},{2,3,1,0}};
// PERM4 rows are positions: value v at index i means "slot i holds item v" with items 0=A,1=C,2=D,3=D
static i64 eval(const Cfg&c,int node,int t){
    int n = node>=17?32-node:node; Coef cf = CF[c.cv][n]; if(!cf.v) return 0;
    i64 dx=(i64)t<<13; i64 A=KC, C=cf.v; int u;
    i64 K0=c.c0?K:0, K1=c.c1?K:0, K2=c.c2?K:0;
    switch(c.form){
    case 0: { u=cf.ub+c.du; i64 it[4]={A,C,dx,dx}, o[4];
        for(int i=0;i<4;i++) o[i]=it[PERM4[c.order][i]];
        i64 acc=o[0];
        acc=wlatch(MUL(acc,o[1],c.ro0)+K0,c.s0,c.r0);
        acc=wlatch(MUL(acc,o[2],c.ro1)+K1,c.s1,c.r1);
        return sm(MUL(acc,o[3],c.ro2)+K2,u); }
    case 1: { u=cf.ub+c.du; i64 x=wlatch(MUL(A,C,c.ro0)+K0,c.s0,c.r0), y=wlatch(MUL(dx,dx,c.ro1)+K1,c.s1,c.r1);
        return sm(MUL(x,y,c.ro2)+K2,u); }
    case 2: { u=cf.ub+c.du; i64 x=wlatch(MUL(A,dx,c.ro0)+K0,c.s0,c.r0), y=wlatch(MUL(C,dx,c.ro1)+K1,c.s1,c.r1);
        return sm(MUL(x,y,c.ro2)+K2,u); }
    case 3: { u=cf.ub+c.du; i64 Q=wlatch(arr(dx,dx)+K,c.bq,0);
        i64 it[3]={A,C,Q}, o[3]; int ia=c.order/2, ic=c.order%2;      // 6 orders of 3 distinct items
        { int rem[2],k=0; for(int i=0;i<3;i++) if(i!=ia) rem[k++]=i; o[0]=it[ia]; o[1]=it[rem[ic]]; o[2]=it[rem[1-ic]]; }
        i64 acc=wlatch(MUL(o[0],o[1],c.ro0)+K0,c.s0,c.r0);
        return sm(MUL(acc,o[2],c.ro1)+K1,u); }
    case 4: { u=cf.ub+c.du; i64 it[3]={C,dx,dx}, o[3];
        if(c.order==0){o[0]=C;o[1]=dx;o[2]=dx;} else if(c.order==1){o[0]=dx;o[1]=C;o[2]=dx;} else {o[0]=dx;o[1]=dx;o[2]=C;}
        (void)it; i64 acc=wlatch(MUL(o[0],o[1],c.ro0)+K0,c.s0,c.r0);
        return sm(MUL(acc,o[2],c.ro1)+K1,u); }
    case 5: { u=cf.ub+26-c.bq+c.du; i64 Q=wlatch(arr(dx,dx)+K,c.bq,0);
        return sm(MUL(C,Q,c.ro0)+K0,u); }
    default:{ u=cf.ub+c.du; i64 w=wlatch(MUL((i64)c.Bc,dx,c.ro0)+K0,c.s0,c.r0);
        i64 J=wlatch(arr(w,w)+K1,c.s1,c.r1);
        return sm(MUL(J,C,c.ro2)+K2,u); } } }
// cheap prefilter: 6 samples on the two most J-sensitive node instances
static const int PF_N[6]={16,16,9,9,13,13}; static const int PF_T[6]={511,-512,400,-400,-300,301};
static inline bool prefilter(const Cfg&c){
    for(int i=0;i<6;i++){ int node=PF_N[i],t=PF_T[i]; int n=node>=17?32-node:node;
        i64 V=C0B[n]+LT[node][t+512]-eval(c,node,t);
        if(V<WLO[node][t+512]-4||V>WHI[node][t+512]+4) return false; }
    return true; }
int main(int argc,char**argv){ wc_init();
    int only = argc>1?atoi(argv[1]):-1;
    std::vector<Cfg> cf;
    Cfg z{}; 
    // form 0 CHAIN4 : s0+s1 = 43-du
    for(int du=-1;du<=1;du++) for(int s0=0;s0<=43-du;s0++){ int s1=43-du-s0; if(s1<0) continue;
      for(int order=0;order<12;order++) for(int ro0=0;ro0<2;ro0++) for(int ro1=0;ro1<2;ro1++) for(int ro2=0;ro2<2;ro2++)
      for(int q0=0;q0<2;q0++) for(int q1=0;q1<2;q1++) for(int q2=0;q2<2;q2++)
      for(int r0=0;r0<3;r0++) for(int r1=0;r1<3;r1++){ z={}; z.form=0;z.order=order;z.s0=s0;z.s1=s1;z.du=du;
        z.c0=q0;z.c1=q1;z.c2=q2;z.r0=r0;z.r1=r1;z.ro0=ro0;z.ro1=ro1;z.ro2=ro2;z.cv=0; cf.push_back(z); }
    }
    // forms 1,2 TREE : s0+s1 = 43-du
    for(int form=1;form<=2;form++) for(int du=-1;du<=1;du++) for(int s0=0;s0<=43-du;s0++){ int s1=43-du-s0; if(s1<0)continue;
      for(int ro0=0;ro0<2;ro0++) for(int ro1=0;ro1<2;ro1++) for(int ro2=0;ro2<2;ro2++)
      for(int q0=0;q0<2;q0++) for(int q1=0;q1<2;q1++) for(int q2=0;q2<2;q2++)
      for(int r0=0;r0<3;r0++) for(int r1=0;r1<3;r1++){ z={}; z.form=form;z.s0=s0;z.s1=s1;z.du=du;
        z.c0=q0;z.c1=q1;z.c2=q2;z.r0=r0;z.r1=r1;z.ro0=ro0;z.ro1=ro1;z.ro2=ro2;z.cv=0; cf.push_back(z); }
    }
    // form 3 CHAIN3Q : s0 = 43-bq-du
    for(int du=-1;du<=1;du++) for(int bq=14;bq<=26;bq++){ int s0=43-bq-du; if(s0<0) continue;
      for(int order=0;order<6;order++) for(int ro0=0;ro0<2;ro0++) for(int ro1=0;ro1<2;ro1++)
      for(int q0=0;q0<2;q0++) for(int q1=0;q1<2;q1++) for(int r0=0;r0<3;r0++){ z={}; z.form=3;z.order=order;
        z.s0=s0;z.bq=bq;z.du=du;z.c0=q0;z.c1=q1;z.r0=r0;z.ro0=ro0;z.ro1=ro1;z.cv=0; cf.push_back(z); }
    }
    // form 4 CHAIN3C : s0 = 26-du,  cv in {1,2}
    for(int cv=1;cv<=2;cv++) for(int du=-3;du<=3;du++){ int s0=26-du; if(s0<0)continue;
      for(int order=0;order<3;order++) for(int ro0=0;ro0<2;ro0++) for(int ro1=0;ro1<2;ro1++)
      for(int q0=0;q0<2;q0++) for(int q1=0;q1<2;q1++) for(int r0=0;r0<3;r0++){ z={}; z.form=4;z.order=order;
        z.s0=s0;z.du=du;z.c0=q0;z.c1=q1;z.r0=r0;z.ro0=ro0;z.ro1=ro1;z.cv=cv; cf.push_back(z); }
    }
    // form 5 CHAIN2Q
    for(int cv=1;cv<=2;cv++) for(int du=-3;du<=3;du++) for(int bq=14;bq<=26;bq++)
      for(int ro0=0;ro0<2;ro0++) for(int q0=0;q0<2;q0++){ z={}; z.form=5;z.bq=bq;z.du=du;z.c0=q0;z.ro0=ro0;z.cv=cv; cf.push_back(z); }
    // form 6 SQW : 2*s0+s1 = 66-du ; B = RN(2pi*2^20) and a small scan
    for(int du=-1;du<=1;du++) for(int s0=0;s0<=33;s0++){ int s1=66-du-2*s0; if(s1<0||s1>40)continue;
      for(int dB=-4;dB<=4;dB++) for(int ro0=0;ro0<2;ro0++) for(int ro2=0;ro2<2;ro2++)
      for(int q0=0;q0<2;q0++) for(int q1=0;q1<2;q1++) for(int q2=0;q2<2;q2++)
      for(int r0=0;r0<3;r0++) for(int r1=0;r1<3;r1++){ z={}; z.form=6;z.s0=s0;z.s1=s1;z.du=du;z.Bc=6588397+dB;
        z.c0=q0;z.c1=q1;z.c2=q2;z.r0=r0;z.r1=r1;z.ro0=ro0;z.ro2=ro2;z.cv=0; cf.push_back(z); }
    }
    printf("configs: %zu\n",cf.size());
    // pass 1: prefilter
    std::vector<char> keep(cf.size(),0);
    #pragma omp parallel for schedule(dynamic,4096)
    for(size_t i=0;i<cf.size();i++){ if(only>=0&&cf[i].form!=only) continue; keep[i]=prefilter(cf[i]); }
    std::vector<size_t> surv; for(size_t i=0;i<cf.size();i++) if(keep[i]) surv.push_back(i);
    printf("survived prefilter: %zu\n",surv.size());
    // pass 2: full score
    std::vector<int> sc(surv.size(),1<<30), gd(surv.size(),1<<30);
    #pragma omp parallel for schedule(dynamic,8)
    for(size_t i=0;i<surv.size();i++){ const Cfg&c=cf[surv[i]]; int g=0;
        sc[i]=wc_score([&](int node,int t){ return eval(c,node,t); },nullptr,&g); gd[i]=g; }
    std::vector<size_t> idx(surv.size()); for(size_t i=0;i<idx.size();i++) idx[i]=i;
    std::sort(idx.begin(),idx.end(),[&](size_t a,size_t b){ if(gd[a]!=gd[b]) return gd[a]<gd[b]; return sc[a]<sc[b]; });
    puts("\nbest by (guard, total).  guard = violations on nodes 0,1,2,16");
    puts("tot guard form ord  s0  s1  bq du cv  ro0 ro1 ro2  c0 c1 c2  r0 r1   B");
    int shown=0; for(size_t k=0;k<idx.size()&&shown<40;k++){ size_t i=idx[k]; const Cfg&c=cf[surv[i]];
        printf("%3d %5d %4d %3d %3d %3d %3d %2d %2d   %d   %d   %d   %d  %d  %d   %d  %d  %d\n",
            sc[i],gd[i],c.form,c.order,c.s0,c.s1,c.bq,c.du,c.cv,c.ro0,c.ro1,c.ro2,c.c0,c.c1,c.c2,c.r0,c.r1,c.Bc); shown++; }
    // also: best with guard == 0
    puts("\nbest with guard == 0:");
    shown=0; for(size_t k=0;k<idx.size()&&shown<40;k++){ size_t i=idx[k]; if(gd[i]) break; const Cfg&c=cf[surv[i]];
        printf("%3d %5d %4d %3d %3d %3d %3d %2d %2d   %d   %d   %d   %d  %d  %d   %d  %d  %d\n",
            sc[i],gd[i],c.form,c.order,c.s0,c.s1,c.bq,c.du,c.cv,c.ro0,c.ro1,c.ro2,c.c0,c.c1,c.c2,c.r0,c.r1,c.Bc); shown++; }
    // per-form best
    puts("\nper-form best total (any guard) / best with guard 0:");
    for(int f=0;f<7;f++){ int b=1<<30,b0=1<<30; for(size_t i=0;i<surv.size();i++){ if(cf[surv[i]].form!=f)continue;
        if(sc[i]<b)b=sc[i]; if(gd[i]==0&&sc[i]<b0)b0=sc[i]; }
        printf("  form %d: %d  /  %d\n",f,b==(1<<30)?-1:b,b0==(1<<30)?-1:b0); }
    return 0; }
