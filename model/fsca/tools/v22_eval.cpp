// v22_eval.cpp -- take every (w,s1,s2,KCw,K1,K2) that hits BOTH pinned landmarks exactly with K2
// unconstrained (i.e. a genuine rounding constant r in [0,2^s2) plus an integer bias m on J), then
// (a) check the third landmark J(-342), (b) score jwin18, (c) re-run the FULL model over nodes 0..31.
// This is the test the branch report never ran: it evaluated a loose-K2 config that MISSED the
// landmarks, and evaluated landmark hits only under a separate "additive bias" parameterisation.
#include "base2.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
static Fsca F;
static const i64 C0P[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,
                          51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2P[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static i64 JL[1024],JH[1024];
static void load_jwin(){ FILE*f=fopen("jwin18.txt","r"); if(!f)exit(1); char ln[256]; int n=0;
  while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
    if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){JL[t+512]=l;JH[t+512]=h;n++;} } fclose(f); if(n!=1024)exit(1); }
static inline void win_of(int a,i64&lo,i64&hi){ Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} }
struct Cfg{ int w,s1,s2; i64 kc,K1,K2; };
static inline i64 Jof(int t,const Cfg&c){ i64 dx=(i64)t<<c.w; i64 I1=(arr(c.kc,dx)+c.K1)>>c.s1; return (arr(I1,dx)+c.K2)>>c.s2; }
static int jscore(const Cfg&c){ int s=0; for(int i=0;i<1024;i++){ i64 j=Jof(i-512,c); if(j>=JL[i]&&j<=JH[i])s++; } return s; }
static int fullscore(const Cfg&c,int*guard){ int v=0,g=0;
  for(int tn=0;tn<=16;tn++){ int nodes[2]={tn,32-tn}, nn=(tn==0||tn==16)?1:2; int vv=0;
    for(int i=0;i<nn;i++){ int m=nodes[i], flip=(m>=17);
      for(int t=-512;t<=511;t++){ int a=(m*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
        const Tab&T=TB[tn]; i64 dx=(i64)t<<13;
        i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
        i64 T2=T.S? sm(arr(Jof(t,c),T.S)+K,U2P[tn]+2):0;
        i64 s=flip?-1:1; i64 V=C0P[tn]+s*T1-T2-s*T3; if(V<lo||V>hi)vv++; } }
    v+=vv; if(tn==0||tn==1||tn==2||tn==16) g+=vv; }
  *guard=g; return v; }
static const i64 TA=45617,TBB=18466836,TCL=18466858,TCH=18466867;
int main(){ if(!load_fsca(F))return 1; build_tables(); load_jwin();
    int nwin=0,nthird=0,bestj=-1,bestj3=-1; Cfg bc{},bc3{}; long long k1span_max=0;
    std::vector<Cfg> cands;
    for(int w=6;w<=18;w++){ i64 dxA=(i64)17<<w,dxB=(i64)342<<w;
      if(llabs((i64)512<<w)>=(1ll<<27))continue;
      for(int s2=w+8;s2<=w+24;s2++) for(int s1=8;s1<=2*w+26;s1++){
        i64 per1=1ll<<s1,per2=1ll<<s2;
        long double ctr=(long double)TBB*powl(2.0L,(long double)(s1+s2))/((long double)dxB*(long double)dxB);
        if(ctr<16384.0L||ctr>=134217728.0L)continue;
        i64 c0=(i64)llroundl(ctr),span=(i64)(ctr*1e-5L)+2048,lo=c0-span,hi=c0+span; if(lo<1)lo=1;
        if(llabs((arr(hi,dxB)+per1-1)>>s1)>=(1ll<<27))continue;
        for(i64 kc=lo;kc<=hi;kc++){ i64 PA=arr(kc,dxA),PB=arr(kc,dxB);
          for(i64 a=PA>>s1;a<=(PA+per1-1)>>s1;a++){
            i64 k1lo=std::max((i64)0,a*per1-PA),k1hi=std::min(per1-1,a*per1+per1-1-PA); if(k1lo>k1hi)continue;
            for(i64 b=(PB+k1lo)>>s1;b<=(PB+k1hi)>>s1;b++){
              i64 kl=std::max(k1lo,b*per1-PB),kh=std::min(k1hi,b*per1+per1-1-PB); if(kl>kh)continue;
              if(llabs(a)>=(1ll<<27)||llabs(b)>=(1ll<<27))continue;
              i64 QA=arr(a,dxA),QB=arr(b,dxB);
              i64 L=std::max(TA*per2-QA,TBB*per2-QB), H=std::min(TA*per2+per2-1-QA,TBB*per2+per2-1-QB);
              if(L>H)continue; nwin++;
              if(kh-kl>k1span_max)k1span_max=kh-kl;
              // sample K1 across its interval and K2 across its interval
              std::vector<i64> k1s={kl,kh,(kl+kh)/2}, k2s={L,H,(L+H)/2};
              for(int q=1;q<=16;q++) k1s.push_back(kl+(kh-kl)*q/17);
              std::sort(k1s.begin(),k1s.end()); k1s.erase(std::unique(k1s.begin(),k1s.end()),k1s.end());
              std::sort(k2s.begin(),k2s.end()); k2s.erase(std::unique(k2s.begin(),k2s.end()),k2s.end());
              for(i64 k1v:k1s) for(i64 k2v:k2s){ Cfg c{w,s1,s2,kc,k1v,k2v};
                if(Jof(17,c)!=TA||Jof(342,c)!=TBB) continue;
                int js=jscore(c);
                if(js>bestj){bestj=js;bc=c;}
                i64 j3=Jof(-342,c);
                if(j3>=TCL&&j3<=TCH){ nthird++; if(js>bestj3){bestj3=js;bc3=c;} }
                if(js>=900) cands.push_back(c); } } } } } }
    printf("joint-landmark solutions found (K2 unconstrained)     : %d\n",nwin);
    printf("of the sampled ones, also inside the J(-342) window   : %d\n",nthird);
    printf("widest K1 interval encountered                        : %lld\n",k1span_max);
    auto rep=[&](const char*tag,Cfg&c,int js){ if(js<0){printf("%s: none\n",tag);return;} int g; int v=fullscore(c,&g);
        i64 m=c.K2>>c.s2, r=c.K2-(m<<c.s2);
        printf("%s: w=%d s1=%d s2=%d KCw=%lld K1=%lld K2=%lld (= bias %lld on J + rounding const %lld)\n"
               "     jwin18 %d/1024   FULL MODEL %d viol (baseline 148)  guard %d\n"
               "     J(17)=%lld J(342)=%lld J(-342)=%lld  [targets 45617 / 18466836 / 18466858..18466867]\n",
               tag,c.w,c.s1,c.s2,(long long)c.kc,(long long)c.K1,(long long)c.K2,(long long)m,(long long)r,
               js,v,g,(long long)Jof(17,c),(long long)Jof(342,c),(long long)Jof(-342,c)); };
    printf("\n"); rep("BEST jwin18 among joint-landmark hits      ",bc,bestj);
    printf("\n"); rep("BEST jwin18 among ALL-THREE-landmark hits  ",bc3,bestj3);
    printf("\ncandidates with jwin18 >= 900: %zu\n",cands.size());
    std::sort(cands.begin(),cands.end(),[&](const Cfg&x,const Cfg&y){return jscore(x)>jscore(y);});
    for(size_t i=0;i<cands.size()&&i<6;i++){ int g;int v=fullscore(cands[i],&g);
        printf("   jwin18 %d full %d guard %d : w=%d s1=%d s2=%d KCw=%lld K1=%lld K2=%lld\n",
            jscore(cands[i]),v,g,cands[i].w,cands[i].s1,cands[i].s2,
            (long long)cands[i].kc,(long long)cands[i].K1,(long long)cands[i].K2); }
    return 0; }
