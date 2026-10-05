// fv_certificate_a.cpp -- independent re-measurement of the facts the adversarial report rests on.
// Written from the description alone; includes only base2.h (the model) and fsca_common.h (the dump).
// Everything integer.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdio>
static Fsca F;
static const i64 C0PUB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2PUB[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}}

int main(){
    if(!load_fsca(F)){puts("cannot load dump");return 1;} build_tables();
    // ---- 0. the derived tables vs the published literals
    int c0bad=0,u2bad=0; for(int n=0;n<=16;n++){ if(TB[n].C0!=C0PUB[n])c0bad++; if(TB[n].u2!=U2PUB[n])u2bad++; }
    printf("[0] derived C0 vs published literals: %d mismatches ; u2: %d mismatches\n",c0bad,u2bad);

    // ---- 1. baseline, per node, both operand roles
    for(int ro=0;ro<2;ro++){
        int per[32]; int tot=0;
        for(int node=0;node<32;node++){ int flip=node>=17,n=flip?32-node:node; i64 s=flip?-1:1; int v=0;
            for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
                i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
                i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
                i64 I1=((ro? arr(dx,KC):arr(KC,dx))+K)>>23, J=(arr(I1,dx)+K)>>20;
                i64 T2=TB[n].S? sm(arr(J,TB[n].S)+K,TB[n].u2):0;
                i64 V=C0PUB[n]+s*T1-T2-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
                if(V<lo||V>hi)v++; }
            per[node]=v; tot+=v; }
        printf("[1] role arr(%s): total %d violations over nodes 0..31\n",ro?"dx,KC":"KC,dx",tot);
        printf("    guard: node0=%d node1=%d node2=%d node16=%d  (mirrors: 31=%d 30=%d 16 shared)\n",
               per[0],per[1],per[2],per[16],per[31],per[30]);
        printf("    per-node:"); for(int n=0;n<32;n++) if(per[n]) printf(" %d:%d",n,per[n]); puts("");
    }

    // ---- 2. arr() granularity: multiple of 2^16 ?
    { long long n=0,bad=0;
      for(int g=8;g<=14;g++) for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<(g);
        for(i64 v=-2100;v<=2100;v++){ i64 a=arr(v,dx),b=arr(dx,v); n+=2; if(a&0xffff)bad++; if(b&0xffff)bad++; } }
      for(i64 v=5173000;v<=5174000;v++) for(int t=-512;t<=511;t+=37){ i64 dx=(i64)t<<13; n+=2;
        if(arr(v,dx)&0xffff)bad++; if(arr(dx,v)&0xffff)bad++; }
      printf("[2] arr granularity: %lld samples, %lld not a multiple of 2^16\n",n,bad); }

    // ---- 3. deficit envelope E = arr(rec,mul) - rec*mul over the FSCA-relevant domain (both roles)
    { i64 emin=1ll<<60,emax=-(1ll<<60); long long nover=0,ntot=0; i64 wrec=0,wmul=0;
      for(int g=8;g<=14;g++) for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<g;
        for(i64 v=-4200;v<=4200;v++){ i64 e=arr(v,dx)-v*dx; ntot++; if(e>0)nover++; if(e<emin){emin=e;wrec=v;wmul=dx;} if(e>emax)emax=e;
                                      i64 f=arr(dx,v)-dx*v; ntot++; if(f>0)nover++; if(f<emin){emin=f;wrec=dx;wmul=v;} if(f>emax)emax=f; } }
      // and around the real I1 magnitudes
      for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
        for(i64 v=3400000;v<=3400400;v++){ i64 e=arr(v,dx)-v*dx; ntot++; if(e>0)nover++; if(e<emin){emin=e;wrec=v;wmul=dx;} if(e>emax)emax=e; } }
      printf("[3] deficit E=arr(rec,mul)-rec*mul over %lld samples: min %lld max %lld ; overshoots(E>0): %lld\n",
             ntot,(long long)emin,(long long)emax,nover);
      printf("    worst deficit at rec=%lld mul=%lld\n",(long long)wrec,(long long)wmul); }

    // ---- 4. monotonicity of arr(v,dx) in v (role 0) and arr(dx,v) in v (role 1)
    { long long back0=0,back1=0; i64 worst0=0,worst1=0,dev0=0,dev1=0;
      for(int g=8;g<=14;g++) for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<g; i64 p0=arr(-2200,dx),p1=arr(dx,-2200);
        for(i64 v=-2199;v<=2200;v++){ i64 a=arr(v,dx),b=arr(dx,v);
          i64 d0=a-p0,d1=b-p1; if(d0<0){back0++; if(d0<worst0)worst0=d0;} if(d1<0){back1++; if(d1<worst1)worst1=d1;}
          i64 x0=d0-dx; if(x0<0)x0=-x0; if(x0>dev0)dev0=x0; i64 x1=d1-dx; if(x1<0)x1=-x1; if(x1>dev1)dev1=x1;
          p0=a;p1=b; } }
      printf("[4] unit-step in the recoded operand: role0 backward steps %lld (worst %lld), max |step-mul| %lld\n",back0,(long long)worst0,(long long)dev0);
      printf("    role1 (arr(dx,v), v is the multiplicand) backward steps %lld (worst %lld), max |step-mul| %lld\n",back1,(long long)worst1,(long long)dev1); }

    // ---- 5. is arr(J,S) monotone in J ?  (validates every binary-search-built latch dataset)
    { i64 LIM=12000000ll<<2; long long bad=0; i64 worst=0; int badnode=-1;
      #pragma omp parallel for schedule(dynamic)
      for(int n=1;n<=16;n++){ i64 S=TB[n].S; if(!S) continue; i64 p=arr(0,S); long long b=0; i64 w=0;
        for(i64 J=1;J<=LIM;J++){ i64 a=arr(J,S); if(a<p){b++; if(a-p<w)w=a-p;} p=a; }
        #pragma omp critical
        { bad+=b; if(w<worst){worst=w;badnode=n;} } }
      printf("[5] arr(J,S) over J in [0,%lld] for all nodes: %lld backward steps (worst %lld, node %d)\n",
             (long long)LIM,bad,(long long)worst,badnode);
      puts("    -> if 0, binary-search inversion of the final multiply is sound and the datasets are intervals"); }
    return 0; }
