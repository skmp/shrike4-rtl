// e6_nol1.cpp -- verify the sharpest claim in the wc_ report: for some t there is NO integer I1 AT ALL that
// puts J inside its window under J = (arr(I1,dx)+K2)>>s2.  If true, the second multiply's FORM is wrong, not
// just its parameters -- and that is a much narrower target than "the square path".
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 JL[1024],JH[1024];
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int DU=0;DU<=2;DU+=2){
      i64 LIM=12000000ll<<DU;
      for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=LIM;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue; i64 s=flip?-1:1;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            i64 t2lo=base-hi,t2hi=base-lo; int u=U2TAB[n]+DU;
            i64 a1=0,b1=LIM+1; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,TB[n].S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=LIM;  while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,TB[n].S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        JL[t+512]=gl; JH[t+512]=gh; }
      printf("=== latch shift %d (u2 = 30+k+%d) ===\n",20-DU,DU);
      for(int s2v=0;s2v<3;s2v++){ int s2=(DU==0?20:18)+s2v-1;
        for(int k2=0;k2<2;k2++){ i64 K2=k2?(1ll<<19):0;
          int none=0; std::vector<int> bad;
          for(int t=-512;t<=511;t++){ int i=t+512; if(JL[i]>JH[i]) continue; if(t==0) continue;
            i64 dx=(i64)t<<13; bool any=false;
            // arr(v,dx) is monotone in v (increasing for dx>0, decreasing for dx<0) -> bracket by binary search
            i64 lim=8000000;
            bool inc = ((arr(1000,dx)+K2)>>s2) > ((arr(0,dx)+K2)>>s2);
            auto Jof=[&](i64 v){ return (arr(v,dx)+K2)>>s2; };
            i64 a1,b1,a2,b2;
            if(inc){ a1=-lim;b1=lim+1; while(a1<b1){i64 m=a1+((b1-a1)>>1); if(Jof(m)>=JL[i])b1=m;else a1=m+1;}
                     a2=-lim-1;b2=lim; while(a2<b2){i64 m=a2+((b2-a2+1)>>1); if(Jof(m)<=JH[i])a2=m;else b2=m-1;}
                     any=(a1<=a2); }
            else   { a1=-lim;b1=lim+1; while(a1<b1){i64 m=a1+((b1-a1)>>1); if(Jof(m)<=JH[i])b1=m;else a1=m+1;}
                     a2=-lim-1;b2=lim; while(a2<b2){i64 m=a2+((b2-a2+1)>>1); if(Jof(m)>=JL[i])a2=m;else b2=m-1;}
                     any=(a1<=a2); }
            if(!any){ none++; if((int)bad.size()<12) bad.push_back(t); } }
          printf("  s2=%2d K2=%-7lld : %3d t have NO valid I1 at all", s2,(long long)K2,none);
          if(none){ printf("  ->"); for(int t:bad) printf(" %d",t); }
          printf("\n"); } }
    }
    puts("\nIf this count is nonzero the second multiply's FORM is wrong for those offsets: no parameter choice");
    puts("of the first latch can rescue them, because no integer input to arr(.,dx) produces the required J.");
    return 0; }
