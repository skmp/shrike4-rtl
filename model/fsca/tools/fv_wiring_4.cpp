// fv_wiring_4.cpp -- corrected certificate + attribution.
//  (C') "no integer I1 at all": parametrised so the array output is INCREASING in the scan variable
//       (for t<0 the first-latch value is negative and arr(I1,dx) DECREASES in I1 -- the naive scan
//        direction silently fails every negative offset).
//  (D)  attribution: role swap vs rounding constant vs complement feedback, at the coarse latch.
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
static void sfield(int sv,int n,i64&S,int&u){ int k=kof(n); i128 s=sin_(PI_()*n/32);
    if(sv==0){S=std::min(rn_scale(s,23+k),(i64)((1ll<<23)-1));u=30+k;}
    else      {S=std::min(rn_scale(s,21+k),(i64)((1ll<<21)-1));u=28+k;}
    if(n==0){S=0;u=30;} }
static i64 S21[17],S23[17]; static int U21[17],U23[17];
static const int WW=24; static const i64 MM=1ll<<WW;
// final-multiply variants: 0 published sm(arr(J,S)+K3,u) ; 1 plain (arr(S,J)+K3)>>u ; 2 feedback
static inline i64 fvv(int form,i64 J,i64 S,int u,i64 K3){ if(!S)return 0;
    if(form==0) return sm(arr(J,S)+K3,u);
    if(form==1) return (arr(S,J)+K3)>>u;
    return (arr(S,MM)-arr(S,MM-J)+K3)>>u; }
static int build(int form,int sv,int du,i64 K3,i64*JL,i64*JH){
    i64*S=sv?S21:S23; int*U=sv?U21:U23; i64 LIM=12000000ll<<du; int feas=0;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=LIM;
      i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
      for(int node=1;node<=31;node++){int flip=node>=17,n=flip?32-node:node; if(!S[n])continue;
        i64 s=flip?-1:1; i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
        i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
        i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 wl,wh; win_of(a,wl,wh);
        i64 t2lo=base-wh,t2hi=base-wl; int uu=U[n]+du;
        i64 a1=0,b1=LIM+1; while(a1<b1){i64 m=(a1+b1)/2; if(fvv(form,m,S[n],uu,K3)>=t2lo)b1=m;else a1=m+1;}
        i64 a2=-1,b2=LIM;  while(a2<b2){i64 m=(a2+b2+1)/2; if(fvv(form,m,S[n],uu,K3)<=t2hi)a2=m;else b2=m-1;}
        if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
      JL[t+512]=gl; JH[t+512]=gh; if(gl<=gh)feas++; }
    return feas; }
int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables();
    for(int n=0;n<=16;n++){sfield(1,n,S21[n],U21[n]); sfield(0,n,S23[n],U23[n]);}
    int part=argc>1?atoi(argv[1]):0;
    if(part==0){
      puts("=== (D) ATTRIBUTION at the COARSE latch: what actually makes t=+-17 feasible? ===");
      puts("    feasible offsets /1024 ; 'J(17)' is the coarse window at t=17");
      struct R{int form;int sv;i64 K3;const char*nm;};
      std::vector<R> rs={
        {0,0,K,"PUBLISHED  sm(arr(J,S)+2^19,u), RN23 field"},
        {0,1,K,"           sm(arr(J,S)+2^19,u), RN21 field"},
        {1,0,K,"role swap  (arr(S,J)+2^19)>>u,  RN23 field"},
        {1,1,K,"role swap  (arr(S,J)+2^19)>>u,  RN21 field"},
        {2,0,K,"feedback   w=24 2sC,            RN23 field"},
        {2,1,K,"feedback   w=24 2sC,            RN21 field  <== THE CLAIM"},
        {1,1,655360,"role swap  (arr(S,J)+655360)>>u, RN21 field  [fitted constant]"},
        {1,1,K+65536,"role swap  (arr(S,J)+2^19+2^16)>>u, RN21"},
        {1,1,K+196608,"role swap (arr(S,J)+2^19+3*2^16)>>u, RN21"},
        {0,1,655360,"           sm(arr(J,S)+655360,u), RN21 field"},
      };
      i64 JL[1024],JH[1024];
      for(auto&r:rs){ int f=build(r.form,r.sv,0,r.K3,JL,JH);
        printf("    %-52s feasible %4d/1024  J(17)=[%lld,%lld]%s\n",r.nm,f,
          (long long)JL[17+512],(long long)JH[17+512],JL[17+512]>JH[17+512]?" INFEAS":""); }
      puts("");
      puts("    fine K3 sweep, plain role-swapped product, RN21 field, coarse latch:");
      for(i64 K3=0;K3<=1048576;K3+=65536){ int f=build(1,1,0,K3,JL,JH);
        printf("      K3=%7lld (2^19%+lld*2^16): feasible %4d/1024%s\n",(long long)K3,
               (long long)((K3-K)/65536),f, f==1024?"   <== complete":""); }
    }
    if(part==1){
      puts("=== (C') 'no integer I1 at all', scanned in a variable the array is INCREASING in ===");
      const char*dn[3]={"published FINE (u=32+k, latch shift 18)","published COARSE (u=30+k, shift 20)",
                        "NEW COARSE (claim's final multiply, u=28+k)"};
      for(int ds=0;ds<3;ds++){
        int du=(ds==0)?2:0, sv=(ds==2)?1:0, form=(ds==2)?2:0;
        i64 JL[1024],JH[1024]; build(form,sv,du,K,JL,JH);
        for(int s2b=0;s2b<2;s2b++) for(int k2c=0;k2c<2;k2c++){
          int s2=(ds==0)?(s2b?18:17):(s2b?21:20); i64 K2=k2c?K:0;
          std::vector<int> bad; long nonmono=0;
          for(int t=-512;t<=511;t++){ if(JL[t+512]>JH[t+512])continue;
            i64 dx=(i64)t<<13; int sg = t>=0?1:-1;
            auto g=[&](i64 m)->i64{ return arr(sg*m,dx); };      // increasing in m for both signs of t
            if(t==0){ bool ok=false; for(i64 m=0;m<=8;m++){i64 J=(g(m)+K2)>>s2; if(J>=JL[512]&&J<=JH[512])ok=true;}
                      if(!ok)bad.push_back(0); continue; }
            i64 Plo=(JL[t+512]<<s2)-K2, Phi=((JH[t+512]+1)<<s2)-1-K2;
            i64 hi=1; while(g(hi)<Phi) hi*=2;                     // bracket
            i64 a=0,b=hi; while(a<b){i64 m=a+(b-a)/2; if(g(m)>=Plo)b=m;else a=m+1;}
            i64 c=0,d=hi; while(c<d){i64 m=c+(d-c+1)/2; if(g(m)<=Phi)c=m;else d=m-1;}
            i64 s=std::max((i64)0,a-3000), e=c+3000; bool ok=false; i64 prev=g(s);
            for(i64 m=s;m<=e;m++){ i64 v=g(m); if(m>s&&v<prev)nonmono++; prev=v;
              i64 J=(v+K2)>>s2; if(J>=JL[t+512]&&J<=JH[t+512]){ok=true;break;} }
            if(!ok) bad.push_back(t); }
          printf("    %-42s s2=%d K2=%-4s : %zu unreachable",dn[ds],s2,K2?"2^19":"0",bad.size());
          if(bad.size()<=8){printf(" :"); for(int t:bad)printf(" %d",t);} 
          printf("   (descents in the scan variable: %ld)\n",nonmono); }
        puts(""); } }
    return 0; }
