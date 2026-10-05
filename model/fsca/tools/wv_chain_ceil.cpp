// wv_chain_ceil.cpp -- independent test of the claimed CEILING of the two-multiply chain family.
// Stage 1: gate on the 8 tightest windows with K1 and K2 solved EXACTLY (closed-form inversion).
// Stage 2: survivors get an exact full solve over all 1022 feasible t.
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cstring>

static i64 WL[1024], WH[1024]; static int WF[1024];
static void loadw(){ FILE*f=fopen("jwin.txt","r"); for(int i=0;i<1024;i++){int t,fe;long long a,b;
  if(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&fe)!=4){exit(1);} WL[i]=a;WH[i]=b;WF[i]=fe;} fclose(f); }

static inline i64 RULE(int r,i64 P,i64 Kc,int s){ i64 x=P+Kc; switch(r){
  case 0: return x>>s; case 1: return -((-x)>>s);
  case 2: return x>=0? (x>>s):-((-x)>>s);
  case 3: return x>=0? (x>>s):-((~x)>>s);
  default: return P>=0? ((P+Kc)>>s) : -(((~P)+Kc)>>s); } }
static inline i64 MASK(int m,i64 v){ if(m==0)return v; if(m==1)return v&~(i64)1;
  i64 a=v<0?-v:v; a&=~(i64)1; return v<0?-a:a; }

// closed-form: set of K2 with  (sign? -RULE : RULE)(P2,K2,s) in [A,B],  A>0.  returns false if empty.
static inline bool k2range(int rule,int sgn,int s,i64 P2,i64 A,i64 B,i64&L,i64&H){
  i64 one=1; i64 xl,xh; bool useY=false; i64 base=P2;
  if(!sgn){
    switch(rule){
      case 0: case 2: case 3: xl=A<<s; xh=((B+1)<<s)-1; break;
      case 1: xl=((A-1)<<s)+1; xh=B<<s; break;
      default: if(P2>=0){ xl=A<<s; xh=((B+1)<<s)-1; }
               else { useY=true; base=~P2; xl=(-B)<<s; xh=((-A+1)<<s)-1; } break; }
  } else {
    switch(rule){
      case 0: xl=(-B)<<s; xh=((-A+1)<<s)-1; break;
      case 1: xl=((-B-1)<<s)+1; xh=(-A)<<s; break;
      case 2: xl=-(((B+1)<<s)-1); xh=-(A<<s); break;
      case 3: xl=-((B+1)<<s); xh=-(A<<s)-1; break;
      default: if(P2>=0){ xl=(-B)<<s; xh=((-A+1)<<s)-1; }
               else { useY=true; base=~P2; xl=A<<s; xh=((B+1)<<s)-1; } break; }
  }
  (void)one; L=xl-base; H=xh-base; return L<=H; }

static const int NG=8;
static int GT[NG]={-376,-40,258,342,376,40,-303,-342};

struct Best { int cnt; i64 cst; int j,g1,r1,s1,rl1,g2,r2,s2,rl2,m1,mk,sg; i64 K1,K2; };

int main(int argc,char**argv){
  build_tables(); loadw();
  // --- validate the closed forms against the rule definitions
  { unsigned long long st=12345678901ull; int bad=0;
    for(int i=0;i<200000;i++){ st^=st<<13; st^=st>>7; st^=st<<17;
      int rule=st%5, sgn=(st>>3)&1, s=8+(int)((st>>5)%14);
      i64 P2=(i64)(st%(1ull<<34))-(1ll<<33); i64 A=1+(i64)((st>>7)%1000000), B=A+(i64)((st>>11)%3);
      i64 L,H; bool ok=k2range(rule,sgn,s,P2,A,B,L,H);
      if(ok){ for(i64 d=-1; d<=1; d++){ // endpoints in, just outside out
          i64 k=L+d; i64 v=RULE(rule,P2,k,s); if(sgn)v=-v; bool inw=(v>=A&&v<=B);
          if(d>=0 && !inw) bad++; if(d<0 && inw) bad++; }
        for(i64 d=-1; d<=1; d++){ i64 k=H+d; i64 v=RULE(rule,P2,k,s); if(sgn)v=-v; bool inw=(v>=A&&v<=B);
          if(d<=0 && !inw) bad++; if(d>0 && inw) bad++; } }
      else { for(i64 k=-(1ll<<(s+3)); k<=(1ll<<(s+3)); k+= (1ll<<(s-2))){ i64 v=RULE(rule,P2,k,s); if(sgn)v=-v;
               if(v>=A&&v<=B) bad++; } } }
    printf("[v] closed-form K2 inversion: %d inconsistencies in 200000 random cases\n",bad); if(bad) return 1; }

  std::vector<i64> CST; std::vector<int> CJ;
  for(int d=-8;d<=8;d++) for(int j=0;j<3;j++){ i64 c=((i64)0x4EF145+d)<<j;
    CST.push_back(c); CJ.push_back(j); CST.push_back(-c); CJ.push_back(j); }

  long long tried=0, pass=0;
  std::vector<Best> keep; 
  #pragma omp parallel for schedule(dynamic) reduction(+:tried,pass)
  for(size_t ci=0; ci<CST.size(); ci++){
    i64 cst=CST[ci]; int j=CJ[ci];
    std::vector<Best> loc;
    for(int g1=8; g1<=15; g1++) for(int r1=0;r1<2;r1++){
      i64 P1[NG]; for(int i=0;i<NG;i++){ i64 dx=(i64)GT[i]<<g1; P1[i]= r1? arr(dx,cst): arr(cst,dx); }
      for(int e=-9;e<=-3;e++){ int s1=g1+17+e+j; if(s1<2||s1>40) continue; i64 per=(i64)1<<s1;
        for(int rl1=0;rl1<5;rl1++){
          i64 v0[NG],v1[NG],bp[NG]; bool two[NG]; i64 cuts[NG+2]; int nc=0; cuts[nc++]=0;
          for(int i=0;i<NG;i++){ v0[i]=RULE(rl1,P1[i],0,s1); v1[i]=RULE(rl1,P1[i],per-1,s1); two[i]=v0[i]!=v1[i];
            if(two[i]){ i64 lo=0,hi=per-1; while(lo<hi){ i64 m=lo+((hi-lo)>>1);
                if(RULE(rl1,P1[i],m,s1)!=v0[i]) hi=m; else lo=m+1; } bp[i]=lo; cuts[nc++]=bp[i]; } else bp[i]=per; }
          cuts[nc++]=per; std::sort(cuts,cuts+nc); nc=(int)(std::unique(cuts,cuts+nc)-cuts);
          for(int m1=-1;m1<=1;m1++) for(int mk=0;mk<3;mk++){
            // I1 per t per segment
            for(int g2=8; g2<=15; g2++){ int s2=g1+g2+17-(s1-j); if(s2<2||s2>40) continue;
              for(int r2=0;r2<2;r2++){
                i64 P2a[NG],P2b[NG];
                for(int i=0;i<NG;i++){ i64 dx=(i64)GT[i]<<g2;
                  i64 I1a=MASK(mk,v0[i]+m1), I1b=MASK(mk,v1[i]+m1);
                  P2a[i]= r2? arr(dx,I1a): arr(I1a,dx);
                  P2b[i]= two[i] ? (r2? arr(dx,I1b): arr(I1b,dx)) : P2a[i]; }
                for(int rl2=0;rl2<5;rl2++) for(int sg=0;sg<2;sg++){
                  tried++;
                  for(int seg=0; seg+1<nc; seg++){ i64 k1=cuts[seg];
                    i64 lo2=-((i64)1<<(s2+2)), hi2=((i64)1<<(s2+2)); bool ok=true;
                    for(int i=0;i<NG && ok;i++){ i64 P2 = (two[i] && k1>=bp[i]) ? P2b[i] : P2a[i];
                      i64 L,H; int ti=GT[i]+512;
                      if(!k2range(rl2,sg,s2,P2,WL[ti],WH[ti],L,H)) { ok=false; break; }
                      if(L>lo2) lo2=L; if(H<hi2) hi2=H; if(lo2>hi2) ok=false; }
                    if(ok){ pass++; Best b; b.cnt=NG;b.cst=cst;b.j=j;b.g1=g1;b.r1=r1;b.s1=s1;b.rl1=rl1;
                      b.g2=g2;b.r2=r2;b.s2=s2;b.rl2=rl2;b.m1=m1;b.mk=mk;b.sg=sg;b.K1=k1;b.K2=lo2;
                      loc.push_back(b); break; } }
                }
              } }
          } } } }
    if(!loc.empty()){
      #pragma omp critical
      keep.insert(keep.end(),loc.begin(),loc.end()); }
  }
  printf("[1] structures tested: %lld   structures satisfying ALL %d tightest windows: %lld\n",tried,NG,pass);
  // --- stage 2: exact full solve for survivors (K1 sub-segmented by every t)
  double bestscore=0; Best bb{}; int bs=-1;
  int nsurv=(int)keep.size(); printf("[2] full exact solve for %d survivors...\n",nsurv);
  std::vector<int> sc(nsurv,0);
  #pragma omp parallel for schedule(dynamic)
  for(int q=0;q<nsurv;q++){
    Best b=keep[q]; i64 per=(i64)1<<b.s1;
    // P1 and breakpoints for all t
    std::vector<i64> P1(1024), V0(1024), V1(1024), BP(1024); std::vector<char> TWO(1024);
    std::vector<i64> cuts; cuts.push_back(0);
    for(int t=-512;t<=511;t++){ int i=t+512; if(!WF[i]) continue; i64 dx=(i64)t<<b.g1;
      P1[i]= b.r1? arr(dx,b.cst): arr(b.cst,dx);
      V0[i]=RULE(b.rl1,P1[i],0,b.s1); V1[i]=RULE(b.rl1,P1[i],per-1,b.s1); TWO[i]=V0[i]!=V1[i];
      if(TWO[i]){ i64 lo=0,hi=per-1; while(lo<hi){ i64 m=lo+((hi-lo)>>1);
          if(RULE(b.rl1,P1[i],m,b.s1)!=V0[i]) hi=m; else lo=m+1; } BP[i]=lo; cuts.push_back(BP[i]); } else BP[i]=per; }
    cuts.push_back(per); std::sort(cuts.begin(),cuts.end()); cuts.erase(std::unique(cuts.begin(),cuts.end()),cuts.end());
    int best=0; i64 bK1=0,bK2=0;
    std::vector<std::pair<i64,int>> ev; ev.reserve(2048);
    for(size_t s=0;s+1<cuts.size();s++){ i64 k1=cuts[s]; ev.clear();
      for(int t=-512;t<=511;t++){ int i=t+512; if(!WF[i]) continue;
        i64 I1=MASK(b.mk, ((TWO[i]&&k1>=BP[i])?V1[i]:V0[i]) + b.m1);
        i64 dx=(i64)t<<b.g2; i64 P2= b.r2? arr(dx,I1): arr(I1,dx);
        i64 L,H; if(!k2range(b.rl2,b.sg,b.s2,P2,WL[i],WH[i],L,H)) continue;
        i64 lim=(i64)1<<(b.s2+2); if(H<-lim||L>lim) continue; if(L<-lim)L=-lim; if(H>lim)H=lim;
        ev.push_back({L,+1}); ev.push_back({H+1,-1}); }
      std::sort(ev.begin(),ev.end()); int cur=0;
      for(size_t z=0;z<ev.size();z++){ cur+=ev[z].second;
        if(cur>best && (z+1==ev.size() || ev[z+1].first!=ev[z].first)){ best=cur; bK1=k1; bK2=ev[z].first; } }
    }
    sc[q]=best;
    #pragma omp critical
    { if(best>bs){ bs=best; bb=b; bb.K1=bK1; bb.K2=bK2; } }
  }
  (void)bestscore;
  printf("[3] best full score over survivors: %d / 1022\n", bs);
  if(bs>0) printf("    cst=%lld j=%d g1=%d role1=%d s1=%d rule1=%d K1=%lld | g2=%d role2=%d s2=%d rule2=%d m1=%d mask=%d sign=%d K2=%lld\n",
     (long long)bb.cst,bb.j,bb.g1,bb.r1,bb.s1,bb.rl1,(long long)bb.K1,bb.g2,bb.r2,bb.s2,bb.rl2,bb.m1,bb.mk,bb.sg,(long long)bb.K2);
  std::sort(sc.begin(),sc.end()); if(nsurv) printf("    survivor score distribution: min %d median %d max %d\n",sc[0],sc[nsurv/2],sc[nsurv-1]);
  return 0; }
