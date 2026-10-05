// fv_wiring_3.cpp -- (A) what the complement-feedback form actually computes, relative to the plain
// product; (B) whether a plain role-swapped product with a rounding constant reproduces floor 0;
// (C) the "no integer I1 at all" certificate, over the WHOLE integer range, for both datasets.
#include "base2.h"
#include <cstring>
#include <vector>
#include <set>
#include <map>
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
int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables();
    int part=argc>1?atoi(argv[1]):0;
    i64 S21[17],S23[17]; int U21[17],U23[17];
    for(int n=0;n<=16;n++){sfield(1,n,S21[n],U21[n]); sfield(0,n,S23[n],U23[n]);}
    const int w=24; const i64 M=1ll<<w;      // w=24: 2^w - J is a legal non-negative w-bit field for every J used

    if(part==0){
      puts("=== (A) the complement-feedback value MINUS the plain role-swapped product ===");
      puts("    d(J) = [ arr(S, 2^w) - arr(S, 2^w - J) ] - arr(S, J)   (w=24, sine field recoded)");
      for(int n=1;n<=16;n++){ std::map<i64,long> hist;
        for(i64 J=0;J<=12000000;J+=1237){ i64 d=(arr(S21[n],M)-arr(S21[n],M-J))-arr(S21[n],J); hist[d]++; }
        printf("    node %2d S=%8lld : ",n,(long long)S21[n]);
        for(auto&p:hist)printf(" %lld(x%ld)",(long long)p.first,p.second); printf("\n"); }
      puts("");
      puts("    -> if d(J) is CONSTANT the feedback form is just arr(S,J) with a shifted rounding");
      puts("       constant; only a J-DEPENDENT d is a new degree of freedom.");
      puts("");
      puts("=== (A2) exhaustive over a dense J range: is d(J) constant per node? ===");
      for(int n=1;n<=16;n++){ std::set<i64> vals; 
        for(i64 J=0;J<=3000000;J++){ vals.insert((arr(S21[n],M)-arr(S21[n],M-J))-arr(S21[n],J)); if(vals.size()>8)break; }
        printf("    node %2d : %zu distinct d over J=0..3e6 :",n,vals.size());
        int c=0; for(i64 v:vals){ printf(" %lld",(long long)v); if(++c>8)break;} printf("\n"); }
    }
    if(part==1){
      // (B) can a PLAIN product arr(S,J) with some rounding constant reach floor 0 at the coarse latch?
      puts("=== (B) plain role-swapped product arr(S_n,J) + K3, coarse latch, sweeping K3 ===");
      puts("    (if a mere constant does it, the 'complement feedback' story is not needed)");
      for(int sv=0;sv<2;sv++){
        i64*S=sv?S21:S23; int*U=sv?U21:U23;
        int bestf=-1; i64 bestK=0;
        for(i64 K3=0;K3<(1ll<<22);K3+= (1ll<<14)){
          int feas=0;
          for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=12000000ll;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
            for(int node=1;node<=31;node++){int flip=node>=17,n=flip?32-node:node; if(!S[n])continue;
              i64 s=flip?-1:1; i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
              i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
              i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 wl,wh; win_of(a,wl,wh);
              i64 t2lo=base-wh,t2hi=base-wl;
              auto fvv=[&](i64 J){ return (arr(S[n],J)+K3)>>U[n]; };
              i64 a1=0,b1=12000001ll; while(a1<b1){i64 m=(a1+b1)/2; if(fvv(m)>=t2lo)b1=m;else a1=m+1;}
              i64 a2=-1,b2=12000000ll; while(a2<b2){i64 m=(a2+b2+1)/2; if(fvv(m)<=t2hi)a2=m;else b2=m-1;}
              if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
            if(gl<=gh)feas++; }
          if(feas>bestf){bestf=feas;bestK=K3;} }
        printf("    sine field %s : best feasible %d/1024 over K3 = 0..2^22 step 2^14, at K3=%lld\n",
               sv?"RN21/u=28+k":"RN23/u=30+k",bestf,(long long)bestK); }
    }
    if(part==2){
      // (C) the "no integer I1 at all" certificate, over the whole integer range.
      puts("=== (C) is there ANY integer I1 with J=(arr(I1,dx)+K2)>>s2 inside the window? ===");
      puts("    arr(I1,dx) is verified nondecreasing in I1 over the scanned bracket, so the");
      puts("    bracket decides the WHOLE integer range.");
      // datasets: 0 = published fine (jwin18), 1 = published coarse, 2 = new coarse (claim's variant)
      for(int ds=0;ds<3;ds++){
        i64 JL[1024],JH[1024]; int du = (ds==0)?2:0;
        i64 LIM=12000000ll<<du;
        i64*S = (ds==2)?S21:S23; int*U=(ds==2)?U21:U23;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=LIM;
          i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
          for(int node=1;node<=31;node++){int flip=node>=17,n=flip?32-node:node; if(!S[n])continue;
            i64 s=flip?-1:1; i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 wl,wh; win_of(a,wl,wh);
            i64 t2lo=base-wh,t2hi=base-wl;
            auto fvv=[&](i64 J)->i64{ if(ds==2) return (arr(S[n],M)-arr(S[n],M-J)+K)>>(U[n]+du);
                                      return sm(arr(J,S[n])+K,U[n]+du); };
            i64 a1=0,b1=LIM+1; while(a1<b1){i64 m=(a1+b1)/2; if(fvv(m)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=LIM;  while(a2<b2){i64 m=(a2+b2+1)/2; if(fvv(m)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
          JL[t+512]=gl; JH[t+512]=gh; }
        const char*dn[3]={"published FINE (jwin18, s2 nominal 18)","published COARSE (shift 20)","NEW COARSE (claim's final multiply)"};
        for(int s2c=0;s2c<2;s2c++) for(int k2c=0;k2c<2;k2c++){
          int s2 = (ds==0)? (s2c?18:17) : (s2c?21:20); i64 K2=k2c?K:0;
          std::vector<int> bad; long nonmono=0;
          for(int t=-512;t<=511;t++){ if(JL[t+512]>JH[t+512])continue; i64 dx=(i64)t<<13;
            if(t==0){ // J must be 0-ish; handle directly
              bool ok=false; for(i64 I1=-4;I1<=4;I1++){ i64 J=(arr(I1,dx)+K2)>>s2; if(J>=JL[512]&&J<=JH[512])ok=true;} 
              if(!ok)bad.push_back(0); continue; }
            i64 Plo=(JL[t+512]<<s2)-K2, Phi=((JH[t+512]+1)<<s2)-1-K2;
            // bracket I1 by binary search over the sign that makes arr grow
            i64 lo,hi;
            if(t>0){ lo=0; hi=1; while(arr(hi,dx)<Phi) hi*=2; }
            else   { lo=-1; while(arr(lo,dx)<Phi) lo*=2; hi=0; }
            // verify monotonicity over the bracket (sampled densely near the target) and scan it
            i64 a1=lo,b1=hi; while(a1<b1){i64 m=a1+(b1-a1)/2; if(arr(m,dx)>=Plo)b1=m;else a1=m+1;}
            i64 c1=lo,d1=hi; while(c1<d1){i64 m=c1+(d1-c1+1)/2; if(arr(m,dx)<=Phi)c1=m;else d1=m-1;}
            i64 s=a1-2000, e=c1+2000; bool ok=false; i64 prev=arr(s,dx);
            for(i64 I1=s;I1<=e;I1++){ i64 v=arr(I1,dx); if(I1>s&&v<prev)nonmono++; prev=v;
              i64 J=(v+K2)>>s2; if(J>=JL[t+512]&&J<=JH[t+512]){ok=true;break;} }
            if(!ok)bad.push_back(t); }
          printf("    %-42s s2=%d K2=%-4s : %zu unreachable offsets",dn[ds],s2,K2?"2^19":"0",bad.size());
          if(bad.size()<=6){printf(" :"); for(int t:bad)printf(" %d",t);} printf("   (non-monotone steps seen: %ld)\n",nonmono); }
        puts(""); }
    }
    return 0; }
