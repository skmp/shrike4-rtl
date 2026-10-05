// v51_align2.cpp -- follow-up: the joint (w, split, KC) family, landmarks, domain check, FULL model.
#include "base2.h"
#include <cstdio>
#include <algorithm>
#include <cstring>
static Fsca F;
static i64 JL[1024],JH[1024];
static const i64 C0T[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2T[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static void load(){ FILE*f=fopen("jwin18.txt","r"); char ln[512];
  while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h,w;
    if(sscanf(ln,"%lld %lld %lld %lld",&t,&l,&h,&w)==4){JL[t+512]=l;JH[t+512]=h;} } fclose(f); }
static inline i64 Jf(int t,int w,i64 kc,int s1,int s2){ i64 dx=(i64)t<<w;
  i64 I1=(arr(dx,kc)+K)>>s1; return (arr(I1,dx)+K)>>s2; }
static int jscore(int w,i64 kc,int s1,int s2,int*mx=nullptr){ int s=0; i64 m=0;
  for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<w; i64 I1=(arr(dx,kc)+K)>>s1;
    { long long ad=std::abs((long long)dx), ai=std::abs((long long)I1); if(ad>(long long)m)m=ad; if(ai>(long long)m)m=ai; }
    i64 j=(arr(I1,dx)+K)>>s2; if(j>=JL[t+512]&&j<=JH[t+512])s++; }
  if(mx)*mx=(int)(63-__builtin_clzll((u64)m)); return s; }
// FULL model with the square path re-aligned; second latch at s2+2 so u2 stays published
static int fullscore(int w,i64 kc,int s1,int s2f){ int v=0;
  for(int a=0;a<65536;a++){ int nr=(a+512)>>10,node=nr&63,t=a-1024*nr;
    int so=1,m=node; if(m>=32){so=-1;m-=32;} int flip=m>=17,n=flip?32-m:m; const Tab&T=TB[n];
    i64 dx13=(i64)t<<13; i64 T1=T.C1f? sm(arr(T.C1f,dx13)+K,23-T.e1):0;
    i64 A3=((arr(((i64)t*t)<<3,dx13)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
    i64 dx=(i64)t<<w; i64 I1=(arr(dx,kc)+K)>>s1; i64 J=(arr(I1,dx)+K)>>s2f;
    i64 T2=T.S? sm(arr(J,T.S)+K,U2T[n]):0; i64 s=flip?-1:1;
    i64 V=so*(C0T[n]+s*T1-T2-s*T3);
    Win wd=decode(F.srz[a],F.srn[a]); i64 lo,hi;
    if(wd.sign>0){lo=wd.lo;hi=wd.hi;}else{lo=-wd.hi;hi=-wd.lo;}
    if(V<lo||V>hi)v++; }
  return v; }
int main(){ if(!load_fsca(F))return 1; build_tables(); load();
  printf("=== wide joint scan: w in 10..16, s1 in 12..32 (s2 = 15+2w-s1), KC in 0x4EF145 +- 4096 ===\n");
  printf("  (rows printed only when best >= 918; maxop = log2 of the largest operand seen)\n");
  int gb=-1,gw=0,gs=0; long long gk=0;
  for(int w=10;w<=16;w++){ int tot=15+2*w;
    for(int s1=12;s1<=32;s1++){ int s2=tot-s1; if(s2<4||s2>36)continue;
      int best=-1; long long bk=0;
      #pragma omp parallel for schedule(static)
      for(long long kc=(long long)0x4ef145-4096; kc<=(long long)0x4ef145+4096; kc++){
        int s=jscore(w,kc,s1,s2);
        #pragma omp critical
        { if(s>best){best=s;bk=kc;} } }
      if(best>=918){ int mx; jscore(w,bk,s1,s2,&mx);
        printf("  w=%2d (s1,s2)=(%2d,%2d) KC=%8lld  jwin18 %4d  maxop 2^%d %s  J(17)=%lld J(342)=%lld J(-342)=%lld\n",
          w,s1,s2,bk,best,mx,mx>=24?"OUT-OF-DOMAIN":"in-domain",
          (long long)Jf(17,w,bk,s1,s2),(long long)Jf(342,w,bk,s1,s2),(long long)Jf(-342,w,bk,s1,s2)); }
      if(best>gb){gb=best;gw=w;gs=s1;gk=bk;} } }
  printf("  GLOBAL BEST jwin18 = %d at w=%d s1=%d KC=%lld  (published 918; known family ceilings 938 / 944)\n\n",gb,gw,gs,gk);
  printf("=== FULL MODEL (65536 angles) for the interesting configs (second latch at s2+2, u2 published) ===\n");
  printf("  published w=13 (23,18->20) KC=0x4EF145 : %d violations\n",fullscore(13,0x4ef145,23,20));
  struct C{int w,s1,s2; long long kc;} cs[]={{14,24,19,5173573},{14,22,21,5173573},{14,23,20,5173573},
      {13,23,18,5173573},{12,20,19,5173572},{14,25,18,5173573},{15,26,19,5173573}};
  for(auto&c:cs){ int js=jscore(c.w,c.kc,c.s1,c.s2); int fs=fullscore(c.w,c.kc,c.s1,c.s2+2);
    printf("  w=%2d (s1,s2fine)=(%2d,%2d) KC=%lld : jwin18 %4d   FULL %5d violations\n",c.w,c.s1,c.s2,c.kc,js,fs); }
  return 0; }
