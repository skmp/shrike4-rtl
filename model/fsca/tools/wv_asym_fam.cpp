// wv_asym_fam.cpp -- adversarial verification, part 4: independent search of the two-multiply
// family  I1 = latch(arr(KC,t<<a) + K1, a+10) ;  J = latch(arr(I1,t<<b) + K2, b+7)
// over both operand roles, both latch rules and a superset of the rounding constants.
// Purpose: (i) confirm the claimed optimum, (ii) measure how many configs tie it
//          (i.e. how much of the 931/139 is selection on the scoring data).
#include "wv_asym_win.h"
#include <cstdio>
#include <vector>
#include <algorithm>
#include <map>
struct Cfg{int a,b,r1,r2,u1,u2c;i64 K1,K2;int sc;};
static inline i64 latch(i64 P,int u,int rule){ return rule? sm(P,u) : (P>>u); }
// per-(t,node) allowed T2 interval, precomputed
static i64 TLO[1024][32],THI[1024][32]; static int NODES[32],NCNT;
static void prep(){ NCNT=0; for(int node=1;node<=31;node++){int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue; NODES[NCNT++]=node;}
    for(int t=-512;t<=511;t++) for(int i=0;i<NCNT;i++){ int node=NODES[i]; int a=(node*1024+t)&0xffff;
        DWin w=wv_dumpwin(a); i64 b=wv_base(node,t); TLO[t+512][i]=b-w.hi; THI[t+512][i]=b-w.lo; } }
static int endviol(const i64*J,int*guard){ int v=0; int g=0;
    for(int t=-512;t<=511;t++) for(int i=0;i<NCNT;i++){ int node=NODES[i]; int flip=node>=17,n=flip?32-node:node;
        i64 T2=sm(arr(J[t+512],TB[n].S)+K,WV_U2[n]);
        if(T2<TLO[t+512][i]||T2>THI[t+512][i]){ v++; if(node==1||node==2||node==16) g++; } }
    *guard=g; return v; }
int main(){ if(!load_fsca(WVF))return 1; build_tables(); wv_build(0); prep();
    std::vector<Cfg> all;
    std::vector<i64> Ks; Ks.push_back(0); for(int j=8;j<=26;j++) Ks.push_back(1ll<<j);
    #pragma omp parallel
    { std::vector<Cfg> loc;
      #pragma omp for schedule(dynamic)
      for(int a=8;a<=15;a++){ int s1=a+10;
        for(int r1=0;r1<2;r1++){ std::vector<i64> P1(1024);
          for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<a; P1[t+512]= r1? arr(dx,KC) : arr(KC,dx); }
          for(size_t k1=0;k1<Ks.size();k1++){ if(Ks[k1]>=(1ll<<(s1+1))) continue;
            for(int u1=0;u1<2;u1++){ std::vector<i64> I1(1024);
              for(int t=0;t<1024;t++) I1[t]=latch(P1[t]+Ks[k1],s1,u1);
              for(int b=8;b<=15;b++){ int s2=b+7;
                for(int r2=0;r2<2;r2++){ std::vector<i64> P2(1024);
                  for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<b; P2[t+512]= r2? arr(dx,I1[t+512]) : arr(I1[t+512],dx); }
                  for(size_t k2=0;k2<Ks.size();k2++){ if(Ks[k2]>=(1ll<<(s2+1))) continue;
                    for(int u2c=0;u2c<2;u2c++){ int sc=0;
                      for(int t=-512;t<=511;t++){ if(!WV_OK[t+512])continue; i64 J=latch(P2[t+512]+Ks[k2],s2,u2c);
                        if(J>=WV_LO[t+512]&&J<=WV_HI[t+512]) sc++; }
                      loc.push_back({a,b,r1,r2,u1,u2c,Ks[k1],Ks[k2],sc}); } } } } } } } }
      #pragma omp critical
      all.insert(all.end(),loc.begin(),loc.end()); }
    printf("configs scored: %zu\n",all.size());
    std::sort(all.begin(),all.end(),[](const Cfg&x,const Cfg&y){return x.sc>y.sc;});
    printf("best jwin in family: %d\n",all[0].sc);
    std::map<int,int> hist; for(auto&c:all) hist[c.sc]++;
    printf("score histogram (top): "); { int n=0; for(auto it=hist.rbegin();it!=hist.rend()&&n<12;++it,++n) printf("%d:%d ",it->first,it->second); puts(""); }
    // end-to-end for every config with jwin >= 920
    struct Res{Cfg c;int v,g;}; std::vector<Res> R;
    std::vector<Cfg> top; for(auto&c:all) if(c.sc>=920) top.push_back(c);
    printf("configs with jwin >= 920: %zu  -- computing end-to-end violations\n",top.size());
    R.resize(top.size());
    #pragma omp parallel for schedule(dynamic)
    for(size_t i=0;i<top.size();i++){ Cfg&c=top[i]; i64 J[1024];
      for(int t=-512;t<=511;t++){ i64 d1=(i64)t<<c.a; i64 P1= c.r1? arr(d1,KC):arr(KC,d1);
        i64 I1=latch(P1+c.K1,c.a+10,c.u1); i64 d2=(i64)t<<c.b; i64 P2= c.r2? arr(d2,I1):arr(I1,d2);
        J[t+512]=latch(P2+c.K2,c.b+7,c.u2c); }
      int g; int v=endviol(J,&g); R[i]={c,v,g}; }
    std::sort(R.begin(),R.end(),[](const Res&x,const Res&y){ if(x.v!=y.v)return x.v<y.v; return x.c.sc>y.c.sc;});
    puts("\nbest end-to-end (any guard):    a  b r1 r2 u1 u2   K1        K2        jwin  viol  guardviol");
    for(int i=0;i<10&&i<(int)R.size();i++) printf("  %20s %2d %2d %2d %2d %2d %2d  %-9lld %-9lld %4d  %4d  %d\n","",
        R[i].c.a,R[i].c.b,R[i].c.r1,R[i].c.r2,R[i].c.u1,R[i].c.u2c,(long long)R[i].c.K1,(long long)R[i].c.K2,R[i].c.sc,R[i].v,R[i].g);
    puts("best end-to-end with guard-exact (nodes 1,2,16 clean):");
    int shown=0; for(size_t i=0;i<R.size()&&shown<12;i++){ if(R[i].g)continue;
        printf("  a=%d b=%d r1=%d r2=%d u1=%d u2=%d K1=%lld K2=%lld  jwin=%d viol=%d\n",
            R[i].c.a,R[i].c.b,R[i].c.r1,R[i].c.r2,R[i].c.u1,R[i].c.u2c,(long long)R[i].c.K1,(long long)R[i].c.K2,R[i].c.sc,R[i].v); shown++; }
    // how many distinct configs reach viol<=139 with guard clean, and <=148
    { int n139=0,n148=0,n139g=0; for(auto&r:R){ if(r.v<=139){n139++; if(!r.g)n139g++;} if(r.v<=148)n148++; }
      printf("\nconfigs (jwin>=920) with end-to-end viol <= 139: %d (guard-clean %d);  <= 148: %d\n",n139,n139g,n148); }
    // where does the claimed config sit?
    for(auto&r:R) if(r.c.a==14&&r.c.b==15&&r.c.K1==(1ll<<20)&&r.c.K2==(1ll<<20)&&r.c.r1==0&&r.c.r2==0&&r.c.u1==0&&r.c.u2c==0)
        printf("CLAIMED config (a=14,b=15,K1=K2=2^20,roles 0,floor): jwin=%d viol=%d guardviol=%d\n",r.c.sc,r.v,r.g);
    return 0; }
