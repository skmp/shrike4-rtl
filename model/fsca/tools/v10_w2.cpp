// v10_w2.cpp -- the (w,s1) grid alone, with operand-width instrumentation and landmarks.
#include "base2.h"
#include <cstring>
#include <algorithm>
#include <cstdio>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static i64 WL[1024],WH[1024];
static void load_jwin(){ FILE*f=fopen("jwin18.txt","r"); char l[512];
    while(fgets(l,sizeof l,f)){ if(l[0]=='#')continue; long long t,a,b,w; if(sscanf(l,"%lld %lld %lld %lld",&t,&a,&b,&w)==4){WL[t+512]=a;WH[t+512]=b;} } fclose(f); }
static inline void win_of(int a,i64&lo,i64&hi){Win w=decode(F.srz[a],F.srn[a]); if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}}
static i64 T1T[32][1024],T3T[32][1024],LOv[32][1024],HIv[32][1024];
static void prep(){ for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
        T1T[node][t+512]=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; T3T[node][t+512]=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
        int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi); LOv[node][t+512]=lo; HIv[node][t+512]=hi; } } }
static int full_of(const i64*J){ int viol=0;
    for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; i64 s=flip?-1:1;
        for(int t=0;t<1024;t++){ i64 T2=TB[n].S? sm(arr(J[t],TB[n].S)+K,U2TAB[n]):0;
            i64 V=C0TAB[n]+s*T1T[node][t]-T2-s*T3T[node][t];
            if(V<LOv[node][t]||V>HIv[node][t])viol++; } }
    return viol; }
int main(){ if(!load_fsca(F))return 1; build_tables(); load_jwin(); prep();
  puts("w  s1 s2c s2f | jwin18 | full | bits(dx) bits(I1) | J17  J342  J-342     which");
  puts("targets:                                            45617 18466836 [18466858,18466867]");
  for(int w=8;w<=20;w++){
    int bestj=-1,bjs=0,bestf=1<<30,bfs=0;
    for(int s1=2;s1<=2*w+15;s1++){ int s2f=2*w+15-s1,s2c=2*w+17-s1; if(s2f<0||s2c<0)continue;
      i64 Jc[1024],Jf[1024];
      for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<w; i64 I1=(arr(KC,dx)+K)>>s1;
        Jc[t+512]=(arr(I1,dx)+K)>>s2c; Jf[t+512]=(arr(I1,dx)+K)>>s2f; }
      int sj=0; for(int t=0;t<1024;t++) if(Jf[t]>=WL[t]&&Jf[t]<=WH[t])sj++;
      int fv=full_of(Jc);
      if(sj>bestj){bestj=sj;bjs=s1;} if(fv<bestf){bestf=fv;bfs=s1;} }
    for(int which=0;which<2;which++){ int s1=which?bfs:bjs; int s2f=2*w+15-s1,s2c=2*w+17-s1;
      i64 Jc[1024],Jf[1024]; int mdx=0,mi1=0;
      for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<w; i64 I1=(arr(KC,dx)+K)>>s1;
        i64 ad=dx<0?-dx:dx,ai=I1<0?-I1:I1; while((1ll<<mdx)<=ad)mdx++; while((1ll<<mi1)<=ai)mi1++;
        Jc[t+512]=(arr(I1,dx)+K)>>s2c; Jf[t+512]=(arr(I1,dx)+K)>>s2f; }
      int sj=0; for(int t=0;t<1024;t++) if(Jf[t]>=WL[t]&&Jf[t]<=WH[t])sj++;
      printf("%2d %3d %3d %3d | %6d | %4d | %8d %8d | %lld %lld %lld  %s\n",
        w,s1,s2c,s2f,sj,full_of(Jc),mdx,mi1,(long long)Jf[17+512],(long long)Jf[342+512],(long long)Jf[-342+512],
        which?"best-full":"best-jwin18");
      if(which==0&&bjs==bfs){ puts("   (same split)"); break; } }
  }
  return 0; }
