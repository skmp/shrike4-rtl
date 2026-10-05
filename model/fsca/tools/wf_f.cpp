// wf_f.cpp -- wide structural sweep of the square-latch chain, scored against wf_jwin.txt (1022 feasible t).
// Families:
//   A: I1 = lat(arr(Kq,dx)+K1, s1) ; J = lat(arr(I1,dx)+K2, s2)            (constant first, current family)
//   B: I1 = lat(arr(Kp,dx)+K1, s1) ; J = lat(arr(I1,I1)+K2, s2)            (square of a floored 2pi*t)
//   C: I1 = lat(arr(dx,dx)+K1, s1) ; J = lat(arr(Kq,I1)+K2, s2)            (exact-square first, symmetric)
// All operand-role choices, three latch rounding rules, four rounding constants, constant scanned +-8.
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cstring>
static i64 JL[1024],JH[1024]; static int OK[1024]; static int NFEAS=0;
static inline i64 lat(i64 P,int s,int rule){ switch(rule){ case 0: return P>>s; case 1: return P>=0?(P>>s):-((-P)>>s); default: return P>=0?(P>>s):-((~P)>>s);} }
struct Cfg{int fam,s1,s2,k1,k2,r1,r2,rule1,rule2;i64 Kq;};
int main(){ FILE*f=fopen("wf_jwin.txt","r"); int t;long long a,b;int o;
  while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){JL[t+512]=a;JH[t+512]=b;OK[t+512]=o;NFEAS+=o;} fclose(f);
  printf("feasible t = %d\n",NFEAS);
  std::vector<Cfg> C;
  for(int s1=12;s1<=32;s1++){
    // family A / C : s1+s2 in 43..47, Kq = KC<<(s1+s2-43)
    for(int sum=43;sum<=47;sum++){ int s2=sum-s1; if(s2<8||s2>34) continue; i64 base=KC<<(sum-43); if(base>=(1ll<<27))continue;
      for(int dk=-8;dk<=8;dk++) for(int k1=0;k1<4;k1++) for(int k2=0;k2<4;k2++)
        for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++) for(int rule1=0;rule1<3;rule1++){
          C.push_back({0,s1,s2,k1,k2,r1,r2,rule1,0,base+dk});
          C.push_back({2,s1,s2,k1,k2,r1,r2,rule1,0,base+dk}); } }
    // family B : 2*s1+s2 = 66, Kq near 2pi*2^20
    { int s2=66-2*s1; if(s2>=8&&s2<=34){ i64 base=6588397;
      for(int dk=-8;dk<=8;dk++) for(int k1=0;k1<4;k1++) for(int k2=0;k2<4;k2++)
        for(int r1=0;r1<2;r1++) for(int rule1=0;rule1<3;rule1++) C.push_back({1,s1,s2,k1,k2,r1,0,rule1,0,base+dk}); } }
  }
  printf("configs: %zu\n",C.size());
  std::vector<int> sc(C.size(),0);
  #pragma omp parallel for schedule(dynamic,256)
  for(size_t ci=0;ci<C.size();ci++){ Cfg c=C[ci];
    i64 K1 = c.k1==0?0 : c.k1==1?(1ll<<19) : c.k1==2?(c.s1>0?(1ll<<(c.s1-1)):0) : (1ll<<c.s1);
    i64 K2 = c.k2==0?0 : c.k2==1?(1ll<<19) : c.k2==2?(c.s2>0?(1ll<<(c.s2-1)):0) : (1ll<<c.s2);
    int s=0;
    for(int t=-512;t<=511;t++){ if(!OK[t+512])continue; i64 dx=(i64)t<<13; i64 J;
      if(c.fam==0){ i64 P1=c.r1?arr(dx,c.Kq):arr(c.Kq,dx); i64 I1=lat(P1+K1,c.s1,c.rule1);
        if(I1>=(1ll<<27)||I1<=-(1ll<<27)) { s=-1; break; }
        i64 P2=c.r2?arr(dx,I1):arr(I1,dx); J=lat(P2+K2,c.s2,0); }
      else if(c.fam==1){ i64 P1=c.r1?arr(dx,c.Kq):arr(c.Kq,dx); i64 I1=lat(P1+K1,c.s1,c.rule1);
        if(I1>=(1ll<<27)||I1<=-(1ll<<27)) { s=-1; break; }
        i64 P2=arr(I1,I1); J=lat(P2+K2,c.s2,0); }
      else { i64 P1=arr(dx,dx); i64 I1=lat(P1+K1,c.s1,c.rule1);
        if(I1>=(1ll<<27)) { s=-1; break; }
        i64 P2=c.r2?arr(I1,c.Kq):arr(c.Kq,I1); J=lat(P2+K2,c.s2,0); }
      if(J>=JL[t+512]&&J<=JH[t+512]) s++; }
    sc[ci]=s; }
  std::vector<size_t> id(C.size()); for(size_t i=0;i<id.size();i++)id[i]=i;
  std::sort(id.begin(),id.end(),[&](size_t x,size_t y){return sc[x]>sc[y];});
  printf("score fam s1 s2 k1 k2 r1 r2 rule1  Kq\n");
  int shown=0; std::vector<int> seenfam(3,0);
  for(size_t i=0;i<id.size()&&shown<30;i++){ size_t ci=id[i]; Cfg c=C[ci];
    printf("%5d  %d %3d %3d %2d %2d %2d %2d %5d  %lld\n",sc[ci],c.fam,c.s1,c.s2,c.k1,c.k2,c.r1,c.r2,c.rule1,(long long)c.Kq); shown++; }
  // best per family
  for(int fm=0;fm<3;fm++){ int best=-1; size_t bi=0; for(size_t i=0;i<C.size();i++) if(C[i].fam==fm&&sc[i]>best){best=sc[i];bi=i;}
    Cfg c=C[bi]; printf("best family %d: %d  (s1=%d s2=%d k1=%d k2=%d r1=%d r2=%d rule1=%d Kq=%lld)\n",fm,best,c.s1,c.s2,c.k1,c.k2,c.r1,c.r2,c.rule1,(long long)c.Kq); }
  return 0; }
