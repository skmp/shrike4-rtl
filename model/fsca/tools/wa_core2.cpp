// wa_core2.cpp -- wa_core widened with SIGN-MAGNITUDE operand modes (the datapath may take |dx|
// and/or |I1| into the array and apply the sign separately).  amode bit0: first multiply uses |dx|,
// bit1: second multiply uses |I1|, bit2: second multiply uses |dx|.  Does the 13/16 ceiling move?
#include "wa_common.h"
#include <omp.h>
static Windows W;
static int TS[16]={342,-342,376,-376,40,-40,258,-258,348,-348,303,-303,408,-408,144,-144};
int main(){ build_tables(); if(!load_windows(W)){puts("no wa_jwin.txt");return 1;}
    std::vector<std::pair<i64,int>> csts;
    for(int j=0;j<=2;j++) for(int d=-8;d<=8;d++){ csts.push_back({((i64)(0x4ef145+d))<<j,j}); csts.push_back({-(((i64)(0x4ef145+d))<<j),j}); }
    struct Outer { i64 cst; int g1,s1,r1,rule1,m1,am; };
    std::vector<Outer> outs;
    for(auto&cc:csts) for(int g1=8;g1<=15;g1++) for(int e=-9;e<=-3;e++) for(int r1=0;r1<2;r1++)
        for(int rule1=0;rule1<7;rule1++) for(int m1=-1;m1<=1;m1++) for(int am=0;am<2;am++){
            int s1=g1+17+e+cc.second; if(s1<4||s1>44)continue; outs.push_back({cc.first,g1,s1,r1,rule1,m1,am}); }
    printf("outer combos %zu\n",outs.size()); fflush(stdout);
    int MM[9]={0,1,2,3,4,9,10,11,12};
    int nth=omp_get_max_threads();
    std::vector<std::vector<unsigned char>> seenT(nth, std::vector<unsigned char>(65536,0));
    double t0=omp_get_wtime(); long long tested=0;
    #pragma omp parallel for schedule(dynamic,128) reduction(+:tested)
    for(size_t oi=0;oi<outs.size();oi++){ Outer O=outs[oi]; int tid=omp_get_thread_num();
        auto&seen=seenT[tid]; i64 S1=1ll<<O.s1;
        i64 vv[16][2], bp[16]; int sg[16];
        for(int k=0;k<16;k++){ int t=TS[k]; i64 d1=(i64)t<<O.g1; int s=(t<0)?-1:1; sg[k]=s;
            if(O.am&1) d1=(i64)(t<0?-t:t)<<O.g1;               // magnitude into multiply 1
            i64 P1=arrole(O.cst,d1,O.r1);
            i64 a0=latchv(P1,0,O.s1,O.rule1), a1=latchv(P1,S1-1,O.s1,O.rule1);
            vv[k][0]=a0+O.m1; vv[k][1]=a1+O.m1;
            if(a0==a1) bp[k]=S1;
            else { i64 a=0,b=S1-1; while(a<b){ i64 m=a+((b-a)>>1); if(latchv(P1,m,O.s1,O.rule1)!=a0)b=m; else a=m+1; } bp[k]=a; } }
        int e=O.s1-O.g1-17;
        std::vector<size_t> ord(16); for(int i=0;i<16;i++)ord[i]=i;
        std::sort(ord.begin(),ord.end(),[&](size_t a,size_t b){return bp[a]<bp[b];});
        for(int am2=0;am2<4;am2++)
        for(int mi=0;mi<9;mi++){ int mm=MM[mi]; i64 mv[16][2];
            for(int k=0;k<16;k++)for(int c=0;c<2;c++){ i64 v=applymask(vv[k][c],mm);
                if(O.am&1) v = v*sg[k];                        // restore the sign after multiply 1
                if(am2&1) v = v<0?-v:v;                        // |I1| into multiply 2
                mv[k][c]=v; }
            for(int g2=8;g2<=15;g2++){ int s2=g2-e; if(s2<4||s2>44)continue;
              for(int r2=0;r2<2;r2++){
                i64 P2[16][2]; int jsg[16];
                for(int k=0;k<16;k++){ int t=TS[k]; i64 d2=(am2&2)? ((i64)(t<0?-t:t)<<g2) : ((i64)t<<g2);
                    for(int c=0;c<2;c++){ P2[k][c]=arrole(mv[k][c],d2,r2); }
                    jsg[k] = 1; }
                for(int rule2=0;rule2<7;rule2++){ tested++;
                  i64 lo[16][2],hi[16][2]; char ok[16][2];
                  for(int k=0;k<16;k++)for(int c=0;c<2;c++){ i64 a,b;
                      int js = P2[k][c]<0 ? -1 : 1;            // sign-magnitude: score the magnitude
                      ok[k][c]=k2_interval(P2[k][c],s2,rule2,js,W.JL[TS[k]+512],W.JH[TS[k]+512],1ll<<(s2+1),a,b);
                      lo[k][c]=a; hi[k][c]=b; }
                  int sel=0;
                  for(int stage=0;stage<=16;stage++){
                      if(stage>0){ if(bp[ord[stage-1]]>=S1) break; sel|=1<<ord[stage-1];
                          if(stage<16&&bp[ord[stage]]==bp[ord[stage-1]]) continue; }
                      std::pair<i64,int> ev[32]; int ne=0;
                      for(int k=0;k<16;k++){ int c=(sel>>k)&1; if(!ok[k][c])continue;
                          ev[ne++]={lo[k][c],k+1}; ev[ne++]={hi[k][c]+1,-(k+1)}; }
                      std::sort(ev,ev+ne); int cur=0,i=0;
                      while(i<ne){ i64 x=ev[i].first; while(i<ne&&ev[i].first==x){ int v=ev[i].second;
                              if(v>0)cur|=1<<(v-1); else cur&=~(1<<(-v-1)); i++; }
                          seen[cur]=1; } }
                } } } } }
    std::vector<unsigned char> seen(65536,0);
    for(int i=0;i<nth;i++) for(int m=0;m<65536;m++) if(seenT[i][m]) seen[m]=1;
    printf("tested %lld, %.1fs\n",tested,omp_get_wtime()-t0);
    std::vector<unsigned char> reach=seen;
    for(int b=0;b<16;b++) for(int m=0;m<65536;m++) if(!(m>>b&1)) if(reach[m|(1<<b)]) reach[m]=1;
    int bestpc=0; for(int m=0;m<65536;m++) if(reach[m]&&__builtin_popcount(m)>bestpc)bestpc=__builtin_popcount(m);
    printf("largest achievable subset of the 16 tightest constraints: %d of 16\n",bestpc);
    int nc=0; puts("minimal infeasible cores of size <= 4:");
    for(int sz=2;sz<=4&&nc<15;sz++) for(int m=0;m<65536&&nc<15;m++){ if(__builtin_popcount(m)!=sz||reach[m])continue;
        bool mini=true; for(int b=0;b<16&&mini;b++) if(m>>b&1) if(!reach[m^(1<<b)]) mini=false;
        if(!mini)continue; printf("  {"); for(int b=0;b<16;b++) if(m>>b&1) printf(" t=%d",TS[b]); printf(" }\n"); nc++; }
    return 0; }
