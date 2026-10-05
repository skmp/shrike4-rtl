// wf_r.cpp -- the EVEN part on its own (wf_jsym.txt, 466 consistent |t|).
// (a) local certificates: pairs of nearby |t| whose windows force the latch to jump away from any smooth
//     curve, with the exact size of the forced jump (integer arithmetic on t^2).
// (b) a broad structural sweep of even rules, and the misses of the best one.
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
static i64 SL[513],SH[513]; static int SOK[513]; static int NS=0;
int main(){ FILE*f=fopen("wf_jsym.txt","r"); int t;long long a,b;int o;
  while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){SL[t]=a;SH[t]=b;SOK[t]=o;NS+=o;} fclose(f);
  printf("even dataset: %d consistent |t|\n",NS);
  // (a) certificates.  A rule J = floor(F(t^2)) with F increasing and F(t2)-F(t1) <= G forces
  //     SL[b] - SH[a] - 1 < G  (else impossible).  Report the pairs with the largest forced slope.
  puts("\nforced jumps (a<b, both tight):  need F(b^2)-F(a^2) >= Jlo  while KC/2^17*(b^2-a^2) = ideal");
  struct C{ double need; int a,b; i64 jlo; double ideal; };
  std::vector<C> cs;
  for(int x=1;x<=511;x++){ if(!SOK[x])continue; for(int y=x+1;y<=std::min(511,x+24);y++){ if(!SOK[y])continue;
      i64 need = SL[y]-SH[x];                                  // F must rise by at least this many units
      long double ideal = (long double)KC*((long double)y*y-(long double)x*x)/131072.0L;
      long double excess = need - ideal;                        // how much more than the true quadratic rise
      i64 need2 = SL[x]-SH[y]; long double excess2 = need2 + ideal;  // the other direction
      if(excess>1.2L) cs.push_back({(double)excess,x,y,need,(double)ideal});
      if(excess2>1.2L) cs.push_back({(double)excess2,y,x,need2,(double)-ideal}); } }
  std::sort(cs.begin(),cs.end(),[](const C&p,const C&q){return p.need>q.need;});
  for(size_t i=0;i<cs.size()&&i<25;i++) printf("  |t|=%3d -> %3d : window forces a rise of %+5lld while the quadratic gives %+9.3f  => excess %+6.3f\n",
    cs[i].a,cs[i].b,(long long)cs[i].jlo,cs[i].ideal,cs[i].need);
  printf("  (%zu such pairs with excess > 1.2 units)\n",cs.size());
  // (b) structural sweep of even rules
  int best=-1; char bd[256]={0};
  auto score=[&](auto rule){ int s=0; for(int x=0;x<=511;x++){ if(!SOK[x])continue; i64 J=rule(x); if(J>=SL[x]&&J<=SH[x])s++; } return s; };
  // b1: single multiply, multiplicand t^2<<g, both roles
  for(int g=0;g<=10;g++) for(int role=0;role<2;role++) for(i64 dk=-64;dk<=64;dk++) for(int kr=0;kr<14;kr++){
    i64 Kq=KC+dk; int sh=17+g; i64 Kr=kr==0?0:(1ll<<(kr+7)); if(kr&&Kr>=(1ll<<sh))continue;
    int s=score([&](int x){ i64 sq=((i64)x*x)<<g; return ((role?arr(sq,Kq):arr(Kq,sq))+Kr)>>sh; });
    if(s>best){best=s;snprintf(bd,256,"single: J=(arr%s(Kq=%lld, t^2<<%d)+%lld)>>%d",role?"(t^2,Kq)":"(Kq,t^2)",(long long)Kq,g,(long long)Kr,sh);} }
  // b2: single multiply on the exact dx^2 product without the intermediate latch
  for(int sh=40;sh<=48;sh++) for(i64 dk=-64;dk<=64;dk++) for(int kr=0;kr<20;kr++){
    i64 Kq=KC+dk; i64 Kr=kr==0?0:(1ll<<(kr+13)); if(kr&&Kr>=(1ll<<sh))continue;
    int s=score([&](int x){ i64 dx=(i64)x<<13; i64 D=arr(dx,dx); return (arr(Kq,D>>20)+Kr)>>(sh-20); });
    if(s>best){best=s;snprintf(bd,256,"single on the raw dx^2 product, shift %d, Kq=%lld Kr=%lld",sh,(long long)Kq,(long long)Kr);} }
  // b3: two-multiply even chain  Y=(arr(Kq,dx)+K1)>>s1 ; J=(arr(Y,dx)+K2)>>s2   on |t|
  for(int s1=16;s1<=30;s1++){ int s2=43-s1; if(s2<4||s2>34)continue;
    for(i64 dk=-16;dk<=16;dk++) for(int k1=0;k1<5;k1++) for(int k2=0;k2<5;k2++) for(int r2=0;r2<2;r2++){
      i64 Kq=KC+dk; i64 K1=k1==0?0:k1==1?(1ll<<19):k1==2?(1ll<<(s1-1)):k1==3?(1ll<<s1):(1ll<<16);
      i64 K2=k2==0?0:k2==1?(1ll<<19):k2==2?(1ll<<(s2-1)):k2==3?(1ll<<s2):(1ll<<16);
      int s=score([&](int x){ i64 dx=(i64)x<<13; i64 Y=(arr(Kq,dx)+K1)>>s1; if(Y>=(1ll<<27))return (i64)-1;
        return (i64)(((r2?arr(dx,Y):arr(Y,dx))+K2)>>s2); });
      if(s>best){best=s;snprintf(bd,256,"chain |t|: s1=%d s2=%d Kq=%lld K1=%lld K2=%lld r2=%d",s1,s2,(long long)Kq,(long long)K1,(long long)K2,r2);} } }
  // b4: square of a floored 2pi*|t|
  for(int s1=8;s1<=28;s1++){ int s2=66-2*s1; if(s2<4||s2>40)continue;
    for(i64 dk=-64;dk<=64;dk++) for(int k1=0;k1<4;k1++) for(int k2=0;k2<4;k2++){
      i64 Kp=6588397+dk; i64 K1=k1==0?0:k1==1?(1ll<<19):k1==2?(1ll<<(s1-1)):(1ll<<s1);
      i64 K2=k2==0?0:k2==1?(1ll<<19):k2==2?(1ll<<(s2-1)):(1ll<<s2);
      int s=score([&](int x){ i64 dx=(i64)x<<13; i64 P=(arr(Kp,dx)+K1)>>s1; if(P>=(1ll<<27))return (i64)-1;
        return (i64)((arr(P,P)+K2)>>s2); });
      if(s>best){best=s;snprintf(bd,256,"square of a floored 2pi t: s1=%d s2=%d Kp=%lld K1=%lld K2=%lld",s1,s2,(long long)Kp,(long long)K1,(long long)K2);} } }
  printf("\nbest even rule: %d / %d   %s\n",best,NS,bd);
  return 0; }
