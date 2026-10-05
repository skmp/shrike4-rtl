// wv_chain_mask.cpp -- which SUBSETS of the 8 tightest J windows are jointly achievable by a
// two-multiply chain?  Exact: K1 by breakpoint segmentation, K2 by interval sweep (every mask).
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
static i64 WL[1024],WH[1024]; static int WF[1024];
static void loadw(){ FILE*f=fopen("jwin.txt","r"); for(int i=0;i<1024;i++){int t,fe;long long a,b;
  if(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&fe)!=4) exit(1); WL[i]=a;WH[i]=b;WF[i]=fe;} fclose(f);}
static inline i64 RULE(int r,i64 P,i64 Kc,int s){ i64 x=P+Kc; switch(r){
  case 0: return x>>s; case 1: return -((-x)>>s); case 2: return x>=0?(x>>s):-((-x)>>s);
  case 3: return x>=0?(x>>s):-((~x)>>s); default: return P>=0?((P+Kc)>>s):-(((~P)+Kc)>>s);} }
static inline i64 MASKV(int m,i64 v){ if(m==0)return v; if(m==1)return v&~(i64)1;
  i64 a=v<0?-v:v; a&=~(i64)1; return v<0?-a:a; }
static inline bool k2range(int rule,int sgn,int s,i64 P2,i64 A,i64 B,i64&L,i64&H){
  i64 xl,xh,base=P2;
  if(!sgn){ switch(rule){ case 0: case 2: case 3: xl=A<<s; xh=((B+1)<<s)-1; break;
      case 1: xl=((A-1)<<s)+1; xh=B<<s; break;
      default: if(P2>=0){xl=A<<s;xh=((B+1)<<s)-1;} else {base=~P2; xl=(-B)<<s; xh=((-A+1)<<s)-1;} } }
  else { switch(rule){ case 0: xl=(-B)<<s; xh=((-A+1)<<s)-1; break;
      case 1: xl=((-B-1)<<s)+1; xh=(-A)<<s; break;
      case 2: xl=-(((B+1)<<s)-1); xh=-(A<<s); break;
      case 3: xl=-((B+1)<<s); xh=-(A<<s)-1; break;
      default: if(P2>=0){xl=(-B)<<s;xh=((-A+1)<<s)-1;} else {base=~P2; xl=A<<s; xh=((B+1)<<s)-1;} } }
  L=xl-base; H=xh-base; return L<=H; }
static const int NG=8;
static int GT[NG]={-376,-40,258,342,376,40,-303,-342};
int main(){
  build_tables(); loadw();
  std::vector<i64> CST; std::vector<int> CJ;
  for(int d=-8;d<=8;d++) for(int j=0;j<3;j++){ i64 c=((i64)0x4EF145+d)<<j;
    CST.push_back(c);CJ.push_back(j); CST.push_back(-c);CJ.push_back(j); }
  std::vector<char> ach(1<<NG,0); long long tried=0;
  #pragma omp parallel
  { std::vector<char> la(1<<NG,0);
    #pragma omp for schedule(dynamic) reduction(+:tried)
    for(size_t ci=0; ci<CST.size(); ci++){ i64 cst=CST[ci]; int j=CJ[ci];
      for(int g1=8; g1<=15; g1++) for(int r1=0;r1<2;r1++){
        i64 P1[NG]; for(int i=0;i<NG;i++){ i64 dx=(i64)GT[i]<<g1; P1[i]= r1? arr(dx,cst): arr(cst,dx); }
        for(int e=-9;e<=-3;e++){ int s1=g1+17+e+j; if(s1<2||s1>40) continue; i64 per=(i64)1<<s1;
          for(int rl1=0;rl1<5;rl1++){
            i64 v0[NG],v1[NG],bp[NG]; bool two[NG]; i64 cuts[NG+2]; int nc=0; cuts[nc++]=0;
            for(int i=0;i<NG;i++){ v0[i]=RULE(rl1,P1[i],0,s1); v1[i]=RULE(rl1,P1[i],per-1,s1); two[i]=v0[i]!=v1[i];
              if(two[i]){ i64 lo=0,hi=per-1; while(lo<hi){ i64 m=lo+((hi-lo)>>1);
                  if(RULE(rl1,P1[i],m,s1)!=v0[i]) hi=m; else lo=m+1; } bp[i]=lo; cuts[nc++]=bp[i]; } else bp[i]=per; }
            cuts[nc++]=per; std::sort(cuts,cuts+nc); nc=(int)(std::unique(cuts,cuts+nc)-cuts);
            for(int m1=-1;m1<=1;m1++) for(int mk=0;mk<3;mk++)
              for(int g2=8; g2<=15; g2++){ int s2=g1+g2+17-(s1-j); if(s2<2||s2>40) continue;
                for(int r2=0;r2<2;r2++){
                  i64 P2a[NG],P2b[NG];
                  for(int i=0;i<NG;i++){ i64 dx=(i64)GT[i]<<g2;
                    i64 A=MASKV(mk,v0[i]+m1), B=MASKV(mk,v1[i]+m1);
                    P2a[i]= r2? arr(dx,A): arr(A,dx);
                    P2b[i]= two[i]? (r2? arr(dx,B): arr(B,dx)) : P2a[i]; }
                  for(int rl2=0;rl2<5;rl2++) for(int sg=0;sg<2;sg++){ tried++;
                    for(int seg=0; seg+1<nc; seg++){ i64 k1=cuts[seg];
                      std::pair<i64,int> ev[2*NG]; int ne=0; i64 lim=(i64)1<<(s2+2);
                      for(int i=0;i<NG;i++){ i64 P2=(two[i]&&k1>=bp[i])?P2b[i]:P2a[i]; i64 L,H; int ti=GT[i]+512;
                        if(!k2range(rl2,sg,s2,P2,WL[ti],WH[ti],L,H)) continue;
                        if(H<-lim||L>lim) continue; if(L<-lim)L=-lim; if(H>lim)H=lim;
                        ev[ne++]={L,i}; ev[ne++]={H+1,i|16}; }
                      std::sort(ev,ev+ne); int cur=0;
                      for(int z=0;z<ne;z++){ if(ev[z].second&16) cur&=~(1<<(ev[z].second&15)); else cur|=1<<ev[z].second;
                        if(z+1==ne || ev[z+1].first!=ev[z].first) la[cur]=1; }
                    } }
                } }
          } } } }
    #pragma omp critical
    { for(int m=0;m<(1<<NG);m++) if(la[m]) ach[m]=1; } }
  // subset closure
  for(int b=0;b<NG;b++) for(int m=0;m<(1<<NG);m++) if(ach[m]&&(m>>b&1)) ach[m&~(1<<b)]=1;
  int mx=0; for(int m=0;m<(1<<NG);m++) if(ach[m]) mx=std::max(mx,__builtin_popcount(m));
  printf("structures tested: %lld\n",tried);
  printf("largest jointly achievable subset of the %d tightest: %d\n",NG,mx);
  printf("achievable maximal sets:\n");
  for(int m=0;m<(1<<NG);m++) if(ach[m]&&__builtin_popcount(m)==mx){ printf("   {");
    for(int i=0;i<NG;i++) if(m>>i&1) printf(" %d",GT[i]); printf(" }\n"); }
  // their published cores
  auto idx=[&](int t){ for(int i=0;i<NG;i++) if(GT[i]==t) return i; return -1; };
  int c1=(1<<idx(-342))|(1<<idx(376))|(1<<idx(-376))|(1<<idx(258));
  int c2=(1<<idx(376))|(1<<idx(-376))|(1<<idx(258))|(1<<idx(-303));
  printf("claimed core A {-342,376,-376,258}: %s\n", ach[c1]?"ACHIEVABLE (claim refuted)":"infeasible (claim upheld)");
  printf("claimed core B {376,-376,258,-303}: %s\n", ach[c2]?"ACHIEVABLE (claim refuted)":"infeasible (claim upheld)");
  // minimal infeasible subsets
  printf("minimal infeasible subsets (size<=4):\n"); int shown=0;
  for(int sz=2;sz<=4&&shown<12;sz++) for(int m=0;m<(1<<NG)&&shown<12;m++){ if(__builtin_popcount(m)!=sz||ach[m])continue;
    bool minimal=true; for(int b=0;b<NG;b++) if(m>>b&1) if(!ach[m&~(1<<b)]) minimal=false;
    if(!minimal) continue; printf("   {"); for(int i=0;i<NG;i++) if(m>>i&1) printf(" %d",GT[i]); printf(" }\n"); shown++; }
  return 0; }
