// v51_align.cpp -- ADVERSARIAL VERIFY of branch-5's B(iv) alignment scan, plus the SHIFT-SPLIT
// extension they did not run (NOTES' earlier width scan held KC fixed; branch 5 held the split fixed).
#include "base2.h"
#include <cstdio>
#include <algorithm>
#include <vector>
static i64 JL[1024],JH[1024];
static void load(){ FILE*f=fopen("jwin18.txt","r"); char ln[512]; int n=0;
  while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h,w;
    if(sscanf(ln,"%lld %lld %lld %lld",&t,&l,&h,&w)!=4)continue; JL[t+512]=l;JH[t+512]=h;n++; }
  fclose(f); if(n!=1024){fprintf(stderr,"bad jwin18\n");exit(1);} }
static const i64 DOM=1ll<<24, HARD=1ll<<28;
struct Out{int score; int outdom; int hard; };
static inline Out run(int w,i64 kc,int s1,int s2,int order){ Out o{0,0,0};
  for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<w;
    i64 rec = order? dx : kc, mul = order? kc : dx;
    if(std::abs((long long)rec)>=DOM||std::abs((long long)mul)>=DOM)o.outdom++;
    if(std::abs((long long)rec)>=HARD)o.hard++;
    i64 I1=(arr(rec,mul)+K)>>s1;
    if(std::abs((long long)I1)>=DOM)o.outdom++; if(std::abs((long long)I1)>=HARD)o.hard++;
    i64 j=(arr(I1,dx)+K)>>s2;
    if(j>=JL[t+512]&&j<=JH[t+512])o.score++; }
  return o; }
int main(int argc,char**argv){ build_tables(); load();
  printf("=== reproduce branch-5 B(iv): shifts FIXED at (23,18), KC rescaled by 2^(26-2w), KC swept +-4096 ===\n");
  printf("  w  nominalKC      best  atKC        J(17) J(342)   in-domain?\n");
  long long ncfg=0;
  for(int w=10;w<=16;w++){
    long long nom = (w<=13)? ((long long)0x4ef145<<(26-2*w)) : ((long long)0x4ef145>>(2*w-26));
    int best=-1; long long bk=0; Out bo{};
    for(long long kc=std::max(1LL,nom-4096); kc<=nom+4096; kc++){ ncfg++;
      Out o=run(w,kc,23,18,1); if(o.score>best){best=o.score;bk=kc;bo=o;} }
    i64 dx17=(i64)17<<w, dxa=(i64)342<<w;
    i64 I1a=(arr(dx17,bk)+K)>>23, j17=(arr(I1a,dx17)+K)>>18;
    i64 I1b=(arr(dxa,bk)+K)>>23, j342=(arr(I1b,dxa)+K)>>18;
    printf("  %2d  %11lld  %4d  %10lld  %6lld %9lld   %s%s\n",w,nom,best,bk,(long long)j17,(long long)j342,
      bo.outdom? "OUT OF 24-BIT DOMAIN":"ok", bo.hard?" (ARRAY WIDTH EXCEEDED)":"");
  }
  printf("  configs scanned: %lld   (targets 45617 / 18466836)\n\n",ncfg);

  printf("=== EXTENSION branch 5 did NOT run: joint (w, shift split, KC).  s1+s2 = 15+2w. ===\n");
  int gbest=-1; int gw=0,gs1=0; long long gkc=0;
  for(int w=12;w<=14;w++){ int tot=15+2*w;
    for(int s1=16;s1<=28;s1++){ int s2=tot-s1; if(s2<2||s2>40)continue;
      int best=-1; long long bk=0; Out bo{};
      #pragma omp parallel for schedule(dynamic)
      for(long long kc=(long long)0x4ef145-2048; kc<=(long long)0x4ef145+2048; kc++){
        Out o=run(w,kc,s1,s2,1);
        #pragma omp critical
        { if(o.score>best){best=o.score;bk=kc;bo=o;} } }
      if(best>=900||(w==13&&s1==23)) printf("  w=%d (s1,s2)=(%2d,%2d) best %4d at KC=%lld %s%s\n",
        w,s1,s2,best,bk,bo.outdom?"[OUT OF DOMAIN]":"",bo.hard?"[ARRAY WIDTH EXCEEDED]":"");
      if(best>gbest){gbest=best;gw=w;gs1=s1;gkc=bk;} } }
  printf("  GLOBAL BEST over the joint scan: %d/1024 at w=%d s1=%d KC=%lld (baseline 918)\n",gbest,gw,gs1,gkc);
  return 0; }
