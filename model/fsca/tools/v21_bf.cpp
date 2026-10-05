// v21_bf.cpp -- ADVERSARIAL re-derivation of w2_bf's cross-check numbers.
//   geometry: dx = t<<w ; I1 = (arr(KCw,dx)+K1)>>s1 ; J = (arr(I1,dx)+K2)>>s2
//   K1 in [0,2^s1), K2 in [0,2^s2).
// TWO independent algorithms, both written here from scratch:
//   A : literal enumeration of (KCw,K1), K2 solved by interval  (same semantics as w2_bf, my code)
//   B : closed form -- as K1 sweeps a full period I1 takes exactly two values with multiplicities
//       (2^s1 - r) and r, r = P mod 2^s1 ; the joint case is a 3-way partition of the K1 line.
// If A and B disagree the harness is broken and nothing below is usable.
// Bracket: ARG1 = "bf" reproduces w2_bf's bracket exactly (the claimed count is bracket-dependent);
//          ARG1 = "wide" uses an 8x loss bound + 1000-unit pad, to test bracket robustness.
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
static i64 JL[1024],JH[1024];
static void load(){ FILE*f=fopen("jwin18.txt","r"); if(!f){printf("NO jwin18.txt\n");exit(1);} char ln[256];
  while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
    if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;} } fclose(f); }
static int MODE=0;                      // 0 = bf bracket, 1 = wide bracket
static void brk(int t,int w,int s1,int s2,i64&lo,i64&hi){
    long double LM = MODE? (long double)(1ll<<24) : (long double)(1ll<<21);
    long double ul = MODE? 4.0L : 2.0L; long double pad = MODE? 1000.0L : 8.0L;
    long double KCM = MODE? (long double)(1ll<<28) : (long double)(1ll<<27);
    long double d=fabsl((long double)t)*powl(2.0L,w); int i=t+512;
    long double sc=powl(2.0L,(long double)(s1+s2))/(d*d);
    long double slack=(LM/powl(2.0L,s2)+ul)+(LM/powl(2.0L,s1)+ul)*d/powl(2.0L,s2);
    long double l=((long double)JL[i]-slack)*sc,h=((long double)JH[i]+slack)*sc;
    lo=(i64)floorl(l)-(i64)pad; hi=(i64)ceill(h)+(i64)pad; if(lo<1)lo=1; if(hi>(i64)KCM)hi=(i64)KCM; }
// exists K2 in [0,2^s2) with floor((Q+K2)/2^s2) in [lo,hi] ?   -> K2 in [A,B] intersect [0,per-1]
static inline bool k2iv(i64 Q,i64 lo,i64 hi,int s2,i64&A,i64&B){ i64 per=1ll<<s2;
    A=lo*per-Q; B=(hi+1)*per-1-Q; if(A<0)A=0; if(B>per-1)B=per-1; return A<=B; }
int main(int argc,char**argv){ load(); if(argc>1&&!strcmp(argv[1],"wide")) MODE=1;
  const i64 J17L=JL[529],J17H=JH[529],JpL=JL[854],JpH=JH[854],JmL=JL[170],JmH=JH[170];
  long long A_pairs=0,A_342=0,A_both=0,A_all=0;
  long long B_pairs=0,B_342=0,B_both=0,B_all=0; long long kcs=0; int cfg=0;
  printf("mode=%s  landmarks J17=[%lld,%lld] J342=[%lld,%lld] Jm342=[%lld,%lld]\n",
      MODE?"wide":"bf",(long long)J17L,(long long)J17H,(long long)JpL,(long long)JpH,(long long)JmL,(long long)JmH);
  printf(" w  s1  s2 |   kc lo..hi (n)   |   A:h342   A:both |   B:h342   B:both | B:all3\n");
  for(int w=6;w<=8;w++)for(int s1=12;s1<=14;s1++)for(int s2=15;s2<=16;s2++){
    long double nom=157.88L*powl(2.0L,(long double)(s1+s2-2*w));
    if(nom<0.5L||nom>=(long double)(1ll<<27)) { printf("%2d %3d %3d  SKIPPED by scale test\n",w,s1,s2); continue; }
    i64 lo,hi; brk(342,w,s1,s2,lo,hi); if(hi<lo){printf("%2d %3d %3d  EMPTY bracket\n",w,s1,s2);continue;}
    if(!MODE && hi-lo>60000){printf("%2d %3d %3d  SKIPPED by 60000 width filter\n",w,s1,s2);continue;}
    cfg++; kcs+=hi-lo+1;
    const i64 d17=(i64)17<<w, dp=(i64)342<<w, dm=-(i64)342<<w;
    const i64 per1=1ll<<s1;
    long long a342=0,aboth=0,aall=0,b342=0,bboth=0,ball=0,apairs=0,bpairs=0;
    #pragma omp parallel for schedule(dynamic,64) reduction(+:a342,aboth,aall,b342,bboth,ball,apairs,bpairs)
    for(i64 kc=lo;kc<=hi;kc++){
      i64 P17=arr(kc,d17), Pp=arr(kc,dp), Pm=arr(kc,dm);
      // ---------- A : literal enumeration ----------
      for(i64 k1=0;k1<per1;k1++){ apairs++;
        i64 i17=(P17+k1)>>s1, ip=(Pp+k1)>>s1, im=(Pm+k1)>>s1;
        i64 Qp=arr(ip,dp), A1,B1; if(!k2iv(Qp,JpL,JpH,s2,A1,B1)) continue; a342++;
        i64 Q17=arr(i17,d17), A2,B2; if(!k2iv(Q17,J17L,J17H,s2,A2,B2)) continue;
        i64 L=std::max(A1,A2), H=std::min(B1,B2); if(L>H) continue; aboth++;
        i64 Qm=arr(im,dm), A3,B3; if(!k2iv(Qm,JmL,JmH,s2,A3,B3)) continue;
        if(std::max(L,A3)<=std::min(H,B3)) aall++; }
      // ---------- B : closed form, no K1 loop ----------
      i64 v17=P17>>s1, r17=P17-(v17<<s1); i64 vp=Pp>>s1, rp=Pp-(vp<<s1); i64 vm=Pm>>s1, rm=Pm-(vm<<s1);
      // k1 >= per1-r  =>  +1 .   threshold per1-r (== per1 when r==0, i.e. never)
      i64 T17=per1-r17, Tp=per1-rp, Tm=per1-rm;      // r in [0,per1) so T in (0,per1]
      // partition the K1 line by the sorted distinct thresholds, count each cell, evaluate once per cell
      i64 cuts[5]={0,T17,Tp,Tm,per1}; std::sort(cuts,cuts+5);
      for(int c=0;c<4;c++){ i64 a=cuts[c],b=cuts[c+1]; if(b<=a) continue; i64 cnt=b-a; bpairs+=cnt;
        i64 i17=v17+(a>=T17), ip=vp+(a>=Tp), im=vm+(a>=Tm);
        i64 Qp=arr(ip,dp), A1,B1; if(!k2iv(Qp,JpL,JpH,s2,A1,B1)) continue; b342+=cnt;
        i64 Q17=arr(i17,d17), A2,B2; if(!k2iv(Q17,J17L,J17H,s2,A2,B2)) continue;
        i64 L=std::max(A1,A2), H=std::min(B1,B2); if(L>H) continue; bboth+=cnt;
        i64 Qm=arr(im,dm), A3,B3; if(!k2iv(Qm,JmL,JmH,s2,A3,B3)) continue;
        if(std::max(L,A3)<=std::min(H,B3)) ball+=cnt; }
    }
    printf("%2d %3d %3d | %9lld..%-9lld (%lld) | %8lld %8lld | %8lld %8lld | %lld%s\n",w,s1,s2,
        (long long)lo,(long long)hi,(long long)(hi-lo+1),a342,aboth,b342,bboth,ball,
        (a342==b342&&aboth==bboth&&aall==ball&&apairs==bpairs)?"":"   <<< A/B MISMATCH");
    A_pairs+=apairs;A_342+=a342;A_both+=aboth;A_all+=aall;
    B_pairs+=bpairs;B_342+=b342;B_both+=bboth;B_all+=ball;
  }
  printf("\ngeometries: %d   kc values: %lld\n",cfg,kcs);
  printf("A (enumeration) : pairs %lld  h342 %lld  hboth %lld  hall3 %lld\n",A_pairs,A_342,A_both,A_all);
  printf("B (closed form) : pairs %lld  h342 %lld  hboth %lld  hall3 %lld\n",B_pairs,B_342,B_both,B_all);
  printf("AGREEMENT: %s\n",(A_pairs==B_pairs&&A_342==B_342&&A_both==B_both&&A_all==B_all)?"identical":"*** DISAGREE ***");
  return 0; }
