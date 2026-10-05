// w1_comp2.cpp -- the complement hypothesis at EVERY granularity where it can physically occur,
// with the sensitivity of each knob measured so the reader can see how far off each one is.
// Also: the operand-width (w) sweep with the derived compensating shift law s1+s2 = 2w+15, which is
// the "is t 10 bits / is the alignment right" question -- w changes WHICH BITS OF t reach the array's
// truncated columns (dx mod 2^16 = (t & (2^(16-w)-1)) << w).
#include "base2.h"
#include <cstring>
#include <algorithm>
#include <string>
static Fsca F; static i64 VL[65536],VH[65536]; static i64 JL[1024],JH[1024];
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
// knobs: e_t  = added to t when t<0 (t-level complement)
//        e_dx = added to dx when t<0 (dx-level complement)
//        e_i1 = added to I1 when I1<0 (the ONE negative Booth-recoded operand in the whole model)
struct Kn { i64 et, edx, ei1; int s1, s2, w; };
static inline i64 Jf(int t,const Kn&k){ i64 tt = t + (t<0? k.et:0);
    i64 dx = (tt<<k.w) + (t<0? k.edx:0);
    i64 I1 = (arr(KC,dx)+K)>>k.s1; if(I1<0) I1 += k.ei1;
    return (arr(I1,dx)+K)>>k.s2; }
static int sj(const Kn&k){ int s=0; for(int t=-512;t<=511;t++){ i64 j=Jf(t,k);
    if(j>=JL[t+512]&&j<=JH[t+512]) s++; } return s; }
static int sfull(const Kn&k){ int v=0;
  for(int node=0;node<32;node++){ int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n];
    for(int t=-512;t<=511;t++){ int a=(node*1024+t)&0xffff; i64 dx=(i64)t<<13;
      i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
      i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
      i64 I2=Jf(t,k)>>2; i64 T2=T.S? sm(arr(I2,T.S)+K,U2TAB[n]):0;
      i64 s=flip?-1:1; i64 V=C0TAB[n]+s*T1-T2-s*T3; if(V<VL[a]||V>VH[a]) v++; } }
  return v; }
static void rep(const char*nm,const Kn&k,int withfull=1){
  printf("  %-44s jwin18 %4d/1024  %s%-6s  J(17)=%-9lld J(342)=%-10lld J(-342)=%-10lld D=%+lld\n",
    nm,sj(k), withfull?"full ":"", withfull?std::to_string(sfull(k)).c_str():"",
    (long long)Jf(17,k),(long long)Jf(342,k),(long long)Jf(-342,k),(long long)(Jf(-342,k)-Jf(342,k))); }
int main(){ if(!load_fsca(F))return 1; build_tables();
  for(int a=0;a<65536;a++){ Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){VL[a]=w.lo;VH[a]=w.hi;}else{VL[a]=-w.hi;VH[a]=-w.lo;} }
  { FILE*f=fopen("jwin18.txt","r"); char ln[256]; while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue;
      long long t,l,h,wd; if(sscanf(ln,"%lld %lld %lld %lld",&t,&l,&h,&wd)==4){JL[t+512]=l;JH[t+512]=h;} } fclose(f); }
  Kn B{0,0,0,23,18,13};
  printf("TARGET  J(17)=45617 (exactly)   J(342)=18466836 (exactly)   J(-342) in [18466858,18466867]\n");
  printf("        so: J(17) must DROP 13, J(342) must DROP 10, J(-342) must RISE 1..10, D must reach 22..31\n\n");
  rep("BASELINE",B);

  printf("\n[1] SENSITIVITY of every sign-dependent knob, measured at t = -342 (fine J units per unit knob)\n");
  { Kn a=B; a.et=1;  printf("      d J(-342) / d(t)   when t<0 : %+lld\n",(long long)(Jf(-342,a)-Jf(-342,B)));
    Kn b=B; b.edx=1; printf("      d J(-342) / d(dx)  when t<0 : %+lld\n",(long long)(Jf(-342,b)-Jf(-342,B)));
    Kn c=B; c.ei1=1; printf("      d J(-342) / d(I1)  when I1<0: %+lld\n",(long long)(Jf(-342,c)-Jf(-342,B))); }

  printf("\n[2] t computed 2's complement, consumed as 1's complement  (t<0 -> t+1, and the reverse)\n");
  for(int e : {+1,-1}){ Kn k=B; k.et=e; char nm[80]; snprintf(nm,sizeof nm,"t<0 -> t%+d  (a full LSB of t)",e); rep(nm,k); }

  printf("\n[3] the same mis-read one level down: dx negated with ~ instead of -   (dx<0 -> dx+-1)\n");
  for(int e : {+1,-1}){ Kn k=B; k.edx=e; char nm[80]; snprintf(nm,sizeof nm,"dx<0 -> dx%+d",e); rep(nm,k); }

  printf("\n[4] the ONE negative Booth-recoded operand in the model: I1 mis-read (2's vs 1's complement)\n");
  for(int e : {+1,-1,+2,-2,+3,-3,+4,-4}){ Kn k=B; k.ei1=e; char nm[80];
    snprintf(nm,sizeof nm,"I1<0 -> I1%+d",e); rep(nm,k); }

  printf("\n[5] every combination of the three, e_t in {-1,0,1}, e_dx in [-8,8], e_i1 in [-8,8]\n");
  { int best=-1; Kn bk=B; int nhit17=0,nhit342=0;
    for(i64 et=-1;et<=1;et++) for(i64 ed=-8;ed<=8;ed++) for(i64 ei=-8;ei<=8;ei++){
      Kn k=B; k.et=et; k.edx=ed; k.ei1=ei; int s=sj(k);
      if(Jf(17,k)==45617) nhit17++; if(Jf(342,k)==18466836) nhit342++;
      if(s>best){best=s;bk=k;} }
    printf("      configs scanned: %d   best jwin18 = %d\n",3*17*17,best);
    printf("      configs hitting J(17)=45617 : %d      configs hitting J(342)=18466836 : %d\n",nhit17,nhit342);
    char nm[80]; snprintf(nm,sizeof nm,"best: et=%+lld edx=%+lld ei1=%+lld",(long long)bk.et,(long long)bk.edx,(long long)bk.ei1);
    rep(nm,bk); }

  printf("\n[6] WHY the positive branch is rigid: every intermediate in the chain at t = +17 and t = +342\n");
  for(int t : {17,342}){ i64 dx=(i64)t<<13; i64 p1=arr(KC,dx); i64 I1=(p1+K)>>23; i64 p2=arr(I1,dx);
    printf("      t=%4d : dx=%-9lld arr(KC,dx)=%-16lld I1=%-10lld arr(I1,dx)=%-18lld  all >= 0: %s\n",
      t,(long long)dx,(long long)p1,(long long)I1,(long long)p2,
      (dx>=0&&p1>=0&&I1>=0&&p2>=0)?"YES":"no"); }
  puts("      -> no sign-dependent rule of any kind can move J at a positive offset.");

  printf("\n[7] OPERAND WIDTH w with the derived compensating shift law s1 + s2 = 2w + 15 (KC fixed = 0x4EF145)\n");
  printf("      w changes which bits of t reach the array's truncated columns: dx mod 2^16 keeps %s bits of t\n","(16-w)");
  printf("      w   bits of t in loss   best split (s1,s2)   jwin18   J(17)      J(342)      J(-342)    D\n");
  for(int w=8;w<=20;w++){ int bs=-1,bs1=0; Kn bk=B;
    for(int s1=w-6;s1<=2*w+21;s1++){ int s2=2*w+15-s1; if(s2<0||s2>40||s1<0) continue;
      Kn k{0,0,0,s1,s2,w}; int s=sj(k); if(s>bs){bs=s;bs1=s1;bk=k;} }
    printf("     %3d        %2d          (%2d,%2d)          %4d   %-10lld %-11lld %-11lld %+lld%s\n",
      w, w>=16?0:16-w, bk.s1,bk.s2,bs,(long long)Jf(17,bk),(long long)Jf(342,bk),(long long)Jf(-342,bk),
      (long long)(Jf(-342,bk)-Jf(342,bk)), w==13?"  <== the published alignment":""); }
  return 0; }
