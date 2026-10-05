// wc_rep.cpp -- report stage.  (A) every guard-clean arrangement of the square path with its full-dump
// score, deduped; (B) the best arrangement regardless of the guard, with a per-node breakdown;
// (C) a wide scan of the global constant for the best structures; (D) the 2pi-squared form's required
// constant (evidence on whether the quadratic coefficient must be economised).
#include "wc_core.h"
#include <vector>
#include <algorithm>
#include <omp.h>
static inline i64 MUL(i64 a,i64 b,int role){ return role? arr(b,a) : arr(a,b); }
static const int PERM4[12][4] = {{0,1,2,3},{0,2,1,3},{0,2,3,1},{1,0,2,3},{2,0,1,3},{2,0,3,1},
                                 {1,2,0,3},{2,1,0,3},{2,3,0,1},{1,2,3,0},{2,1,3,0},{2,3,1,0}};
static const char* ITN[4]={"Kc","S","dx","dx"};
struct C4 { int order,s0,s1,du,ro0,ro1,ro2,c0,c1,c2,r0,r1; i64 kc; };
static i64 ev4(const C4&c,int node,int t){ int n=node>=17?32-node:node; if(!TB[n].S) return 0;
    i64 dx=(i64)t<<13; i64 it[4]={c.kc,TB[n].S,dx,dx},o[4];
    for(int i=0;i<4;i++) o[i]=it[PERM4[c.order][i]];
    i64 a=o[0];
    a=wlatch(MUL(a,o[1],c.ro0)+(c.c0?K:0),c.s0,c.r0);
    a=wlatch(MUL(a,o[2],c.ro1)+(c.c1?K:0),c.s1,c.r1);
    return sm(MUL(a,o[3],c.ro2)+(c.c2?K:0),TB[n].u2+c.du); }
static void pr(const C4&c,int tot,int g){
    printf("tot=%3d guard=%d | %s x %s x %s x %s | s=(%d,%d) du=%+d roles=(%d,%d,%d) K=(%d,%d,%d) rules=(%d,%d) kc=%06llx\n",
        tot,g,ITN[PERM4[c.order][0]],ITN[PERM4[c.order][1]],ITN[PERM4[c.order][2]],ITN[PERM4[c.order][3]],
        c.s0,c.s1,c.du,c.ro0,c.ro1,c.ro2,c.c0,c.c1,c.c2,c.r0,c.r1,(long long)c.kc); }
int main(){ wc_init();
    std::vector<C4> cs;
    for(int du=-1;du<=1;du++) for(int s0=0;s0<=43-du;s0++){ int s1=43-du-s0; if(s1<0)continue;
      for(int order=0;order<12;order++) for(int ro0=0;ro0<2;ro0++) for(int ro1=0;ro1<2;ro1++) for(int ro2=0;ro2<2;ro2++)
      for(int q0=0;q0<2;q0++) for(int q1=0;q1<2;q1++) for(int q2=0;q2<2;q2++)
      for(int r0=0;r0<3;r0++) for(int r1=0;r1<3;r1++)
        cs.push_back({order,s0,s1,du,ro0,ro1,ro2,q0,q1,q2,r0,r1,(i64)KC}); }
    printf("form-0 configs: %zu\n",cs.size());
    std::vector<int> tot(cs.size(),1<<30),gd(cs.size(),1<<30);
    #pragma omp parallel for schedule(dynamic,64)
    for(size_t i=0;i<cs.size();i++){ const C4&c=cs[i];
        // cheap reject on node 16 and node 13 at 6 offsets
        static const int PN[6]={16,16,13,13,9,9}; static const int PT[6]={511,-512,400,-400,-300,301};
        bool ok=true; for(int k=0;k<6&&ok;k++){ int nd=PN[k],t=PT[k]; int n=nd>=17?32-nd:nd;
            i64 V=C0B[n]+LT[nd][t+512]-ev4(c,nd,t); if(V<WLO[nd][t+512]-4||V>WHI[nd][t+512]+4) ok=false; }
        if(!ok) continue;
        int g=0; tot[i]=wc_score([&](int nd,int t){ return ev4(c,nd,t); },nullptr,&g); gd[i]=g; }
    // (A) all guard-clean
    std::vector<size_t> ix; for(size_t i=0;i<cs.size();i++) if(gd[i]==0) ix.push_back(i);
    std::sort(ix.begin(),ix.end(),[&](size_t a,size_t b){return tot[a]<tot[b];});
    printf("\n(A) guard-clean arrangements (nodes 0,1,2,16 exact): %zu, best %d.  Listing all with tot<=152:\n",
        ix.size(), ix.empty()?-1:tot[ix[0]]);
    int shown=0; for(size_t k=0;k<ix.size();k++){ if(tot[ix[k]]>152) break; pr(cs[ix[k]],tot[ix[k]],0); if(++shown>=60){puts("  ...");break;} }
    // (B) best overall
    size_t bi=0; for(size_t i=0;i<cs.size();i++) if(tot[i]<tot[bi]) bi=i;
    { int per[32]={0},g=0; int T=wc_score([&](int nd,int t){ return ev4(cs[bi],nd,t); },per,&g);
      printf("\n(B) best regardless of guard: "); pr(cs[bi],T,g);
      printf("    per node:"); for(int i=0;i<32;i++) printf(" %d:%d",i,per[i]); puts(""); }
    // (C) constant scan on the best guard-clean structures
    puts("\n(C) global constant scan on the best guard-clean structures (kc = 0x4ef145 + d):");
    for(size_t k=0;k<ix.size()&&k<6;k++){ C4 c=cs[ix[k]]; int bt=1<<30; i64 bk=c.kc; int bg=0;
        for(int d=-600;d<=600;d++){ C4 c2=c; c2.kc=(i64)KC+d; int g=0;
            int T=wc_score([&](int nd,int t){ return ev4(c2,nd,t); },nullptr,&g);
            if(T<bt||(T==bt&&g<bg)){bt=T;bk=c2.kc;bg=g;} }
        printf("  struct %zu: best kc=%06llx (d=%+lld) tot=%d guard=%d   [kc=0x4ef145 gives %d]\n",
            k,(long long)bk,(long long)(bk-(i64)KC),bt,bg,tot[ix[k]]); }
    // (D) J = (latch(B*dx))^2 : what B would be needed?
    puts("\n(D) J = latch(B*dx)^2 form: best score vs the constant B  (RN(2pi*2^20) = 6588397)");
    { int bestT=1<<30; i64 bestB=0; int bs0=0,bs1=0,bg=0;
      #pragma omp parallel
      { int lT=1<<30; i64 lB=0; int ls0=0,ls1=0,lg=0;
        #pragma omp for schedule(dynamic)
        for(int s0=18;s0<=28;s0++){ int s1=66-2*s0; if(s1<0||s1>40)continue;
          for(i64 B=6586000;B<=6590000;B+=1){ int g=0;
            // quick reject at node 16, t=-512 and 511
            bool ok=true; for(int kk=0;kk<2&&ok;kk++){ int t=kk?511:-512; i64 dx=(i64)t<<13;
                i64 w=wlatch(arr(B,dx)+K,s0,0), J=wlatch(arr(w,w)+K,s1,0);
                i64 V=C0B[16]+LT[16][t+512]-sm(arr(J,TB[16].S)+K,TB[16].u2);
                if(V<WLO[16][t+512]-6||V>WHI[16][t+512]+6) ok=false; }
            if(!ok) continue;
            int T=wc_score([&](int nd,int t)->i64{ int n=nd>=17?32-nd:nd; if(!TB[n].S)return 0;
                i64 dx=(i64)t<<13; i64 w=wlatch(arr(B,dx)+K,s0,0), J=wlatch(arr(w,w)+K,s1,0);
                return sm(arr(J,TB[n].S)+K,TB[n].u2); },nullptr,&g);
            if(T<lT){lT=T;lB=B;ls0=s0;ls1=s1;lg=g;} } }
        #pragma omp critical
        if(lT<bestT){bestT=lT;bestB=lB;bs0=bs0=ls0;bs1=ls1;bg=lg;} }
      if(bestT<(1<<30)) printf("   best B=%lld (s0=%d s1=%d) tot=%d guard=%d ; B/RN(2pi*2^20) = %.7f\n",
          (long long)bestB,bs0,bs1,bestT,bg,(double)bestB/6588397.0);
      else puts("   nothing passed the reject test"); }
    return 0; }
