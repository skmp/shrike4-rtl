// wv_asym_symq.cpp -- adversarial verification, part 8: how good IS the best pure symmetric quadratic?
// Claim item 5(g) says 917-923.  Wide sweep + direct witness evaluation + end-to-end violations.
#include "wv_asym_win.h"
#include <cstdio>
#include <vector>
#include <algorithm>
static i64 TLO[1024][32],THI[1024][32]; static int NODES[32],NCNT;
static void prep(){ NCNT=0; for(int node=1;node<=31;node++){int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue; NODES[NCNT++]=node;}
    for(int t=-512;t<=511;t++) for(int i=0;i<NCNT;i++){ int node=NODES[i]; int a=(node*1024+t)&0xffff;
        DWin w=wv_dumpwin(a); i64 b=wv_base(node,t); TLO[t+512][i]=b-w.hi; THI[t+512][i]=b-w.lo; } }
static int endviol(const i64*J,int*guard){ int v=0,g=0;
    for(int t=-512;t<=511;t++) for(int i=0;i<NCNT;i++){ int node=NODES[i]; int flip=node>=17,n=flip?32-node:node;
        i64 T2=sm(arr(J[t+512],TB[n].S)+K,WV_U2[n]);
        if(T2<TLO[t+512][i]||T2>THI[t+512][i]){v++; if(node==1||node==2||node==16)g++;} }
    *guard=g; return v; }
static inline i64 fdiv(i64 a,i64 b){ i64 q=a/b; if((a%b)&&((a<0)!=(b<0)))q--; return q; }
int main(){ if(!load_fsca(WVF))return 1; build_tables(); wv_build(0); prep();
    for(int m : {17,20,26,30}){
      i64 Qc=(i64)KC<<(m-17); i64 span = m<=20? 20000 : 4000000;
      int bestsc=0; i64 bq=0,br=0;
      #pragma omp parallel
      { int lb=0; i64 lq=0,lr=0; std::vector<std::pair<i64,int>> ev; ev.reserve(2100);
        #pragma omp for schedule(dynamic,256)
        for(i64 dq=-span;dq<=span;dq++){ i64 Q=Qc+dq; ev.clear();
          for(int t=-512;t<=511;t++){ if(!WV_OK[t+512])continue; i64 q=Q*(i64)t*(i64)t;
            ev.push_back({(WV_LO[t+512]<<m)-q,+1}); ev.push_back({((WV_HI[t+512]+1)<<m)-q,-1}); }
          std::sort(ev.begin(),ev.end()); int cur=0,mx=0; i64 at=0;
          for(auto&e:ev){ cur+=e.second; if(cur>mx){mx=cur;at=e.first;} }
          if(mx>lb){lb=mx;lq=Q;lr=at;} }
        #pragma omp critical
        { if(lb>bestsc){bestsc=lb;bq=lq;br=lr;} } }
      // direct witness evaluation, no interval algebra
      i64 J[1024]; int sc=0; for(int t=-512;t<=511;t++){ J[t+512]=fdiv(bq*(i64)t*(i64)t+br,1ll<<m);
        if(WV_OK[t+512]&&J[t+512]>=WV_LO[t+512]&&J[t+512]<=WV_HI[t+512])sc++; }
      int g; int v=endviol(J,&g);
      printf("m=%2d span=%lld : sweep says %d, direct check %d   Q=%lld R=%lld  Q/2^m=%.9Lf  end-to-end viol=%d guard=%d\n",
          m,(long long)span,bestsc,sc,(long long)bq,(long long)br,(long double)bq/(long double)(1ll<<m),v,g);
    }
    // for reference: the claimed best rule and the baseline, end-to-end
    { i64 J[1024]; for(int t=-512;t<=511;t++){ i64 I1=(arr(KC,(i64)t<<14)+(1ll<<20))>>24; J[t+512]=(arr(I1,(i64)t<<15)+(1ll<<20))>>22; }
      int g,v=endviol(J,&g); printf("CLAIM rule end-to-end viol=%d guard=%d\n",v,g); }
    { i64 J[1024]; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; J[t+512]=(arr(I1,dx)+K)>>20; }
      int g,v=endviol(J,&g); printf("baseline   end-to-end viol=%d guard=%d\n",v,g); }
    return 0; }
