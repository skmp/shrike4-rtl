// fv_wiring_6.cpp -- chase the one number that did not reproduce: "the published chain scores 914/1024
// against its [new] coarse windows".  Sweep the small representation choices the claim leaves open.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdio>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,
                            47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static inline void win_of(int a,i64&lo,i64&hi){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}}
static int kof(int n){ if(n==0)return 0; i128 s=sin_(PI_()*n/32); int k=0; while(s*((i128)1<<(k+1))<QONE)k++; return k; }
static i64 S21[17]; static int U21[17];
// cm 0 = two's complement 2^w-J ; 1 = one's complement 2^w-1-J ; role 0 = sine recoded, 1 = field recoded
static inline i64 fb(i64 J,i64 S,int u,int w,int cm,int role){ if(!S)return 0;
    i64 M=(1ll<<w)-(cm?1:0);
    i64 p0=role?arr(M,S):arr(S,M), p1=role?arr(M-J,S):arr(S,M-J);
    return (p0-p1+K)>>u; }
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int n=0;n<=16;n++){ int k=kof(n); i128 s=sin_(PI_()*n/32);
        S21[n]=n?std::min(rn_scale(s,21+k),(i64)((1ll<<21)-1)):0; U21[n]=n?28+k:30; }
    puts("published chain scored against the REGENERATED coarse windows, over the claim's open choices");
    puts("  w   cmpl  role | feasible |  chain score for each (first multiply, K2)");
    puts("                 |          |  arr(dx,KC)/2^19  arr(KC,dx)/2^19  arr(dx,KC)/0  arr(KC,dx)/0");
    for(int w=22;w<=26;w++) for(int cm=0;cm<2;cm++) for(int role=0;role<2;role++){
      i64 JL[1024],JH[1024]; int feas=0;
      for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=12000000ll;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
        for(int node=1;node<=31;node++){int flip=node>=17,n=flip?32-node:node; if(!S21[n])continue;
          i64 s=flip?-1:1; i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
          i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
          i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 wl,wh; win_of(a,wl,wh);
          i64 t2lo=base-wh,t2hi=base-wl;
          i64 a1=0,b1=12000001ll; while(a1<b1){i64 m=(a1+b1)/2; if(fb(m,S21[n],U21[n],w,cm,role)>=t2lo)b1=m;else a1=m+1;}
          i64 a2=-1,b2=12000000ll; while(a2<b2){i64 m=(a2+b2+1)/2; if(fb(m,S21[n],U21[n],w,cm,role)<=t2hi)a2=m;else b2=m-1;}
          if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        JL[t+512]=gl; JH[t+512]=gh; if(gl<=gh)feas++; }
      int sc[4]={0,0,0,0};
      for(int t=-512;t<=511;t++){ if(JL[t+512]>JH[t+512])continue; i64 dx=(i64)t<<13;
        for(int v=0;v<4;v++){ int ro=v&1; i64 K2=(v<2)?K:0;
          i64 I1=((ro?arr(KC,dx):arr(dx,KC))+K)>>23; i64 J=(arr(I1,dx)+K2)>>20;
          if(J>=JL[t+512]&&J<=JH[t+512])sc[v]++; } }
      printf("  %2d   %s   %s  |  %4d    |   %4d             %4d             %4d          %4d%s\n",
        w,cm?"1sC":"2sC",role?"fld":"sin",feas,sc[0],sc[1],sc[2],sc[3],
        (sc[0]==914||sc[1]==914||sc[2]==914||sc[3]==914)?"   <== 914 here":""); }
    return 0; }
