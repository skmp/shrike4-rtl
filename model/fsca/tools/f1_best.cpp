// f1_best.cpp -- DIRECTION 1, part A: the honest end-to-end comparison.
// f1_scan picks each variant's chain by LATCH MISSES; that is not the full model, and letting the chain float
// is a 5-parameter fit.  Here every final-multiply representation is scored TWICE:
//  (i)  at the PUBLISHED chain  I1=(arr(dx,KC)+2^19)>>23, J=(arr(I1,dx)+2^19)>>20 -- NOTHING fitted; the only
//       change from the 147-violation baseline is the final square product's representation.  All 92400.
//  (ii) with (s1,s2,ro,K1,K2) also searched -- labelled a SEARCH, not a derivation.  Top candidates only,
//       ranked with per-node latch windows and then RE-VERIFIED with the exact full model.
// Guard nodes 0, 1/31, 2/30, 16 are reported separately throughout.  The baseline is the control.
#include "f1_final.h"
#include <omp.h>
struct Row { FVar v; int pub,pg[4]; int bst,bg[4],bs1,bs2,bro; i64 bK1,bK2; };
static std::vector<int> CS1,CS2,CRO; static std::vector<i64> CK1,CK2; static std::vector<i64> CJ;
static void build_chains(){ for(int s1=16;s1<=30;s1++)for(int s2=10;s2<=28;s2++)for(int ro=0;ro<2;ro++)
      for(int k1=0;k1<2;k1++)for(int k2=0;k2<2;k2++){ CS1.push_back(s1);CS2.push_back(s2);CRO.push_back(ro);
        CK1.push_back(k1?(1ll<<19):0);CK2.push_back(k2?(1ll<<19):0); }
    CJ.resize(CS1.size()*1024);
    for(size_t c=0;c<CS1.size();c++) for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
        i64 I1=((CRO[c]?arr(dx,KC):arr(KC,dx))+CK1[c])>>CS1[c]; CJ[c*1024+t+512]=(arr(I1,dx)+CK2[c])>>CS2[c]; } }
int main(){ if(!load_fsca(FF))return 1; build_tables(); f1_precompute(); build_chains();
    std::vector<FVar> V;
    for(int W=20;W<=26;W++) for(int rm=0;rm<4;rm++) for(int fm=0;fm<5;fm++) for(int b=0;b<3;b++)
      for(int d=2;d<=12;d++) for(int role=0;role<2;role++) for(int kc=0;kc<5;kc++) for(int sg=0;sg<2;sg++)
        V.push_back(FVar{W,rm,fm,b,W+d,role,kc,sg});
    std::vector<Row> R(V.size());
    #pragma omp parallel for schedule(dynamic,64)
    for(size_t i=0;i<V.size();i++){ Row&r=R[i]; r.v=V[i]; Ctx C; f1_field(V[i],C.S,C.u,C.kk);
        Chain ch{23,20,1,1ll<<19,1ll<<19,0}; r.pub=f1_full(V[i],C,ch,r.pg); r.bst=-1; }
    std::vector<size_t> idx(R.size()); for(size_t i=0;i<R.size();i++)idx[i]=i;
    std::sort(idx.begin(),idx.end(),[&](size_t a,size_t b){return R[a].pub<R[b].pub;});
    // phase (ii) on the strongest 600, plus every guard-clean variant
    std::vector<size_t> cand(idx.begin(),idx.begin()+600);
    for(size_t i:idx) if(!(R[i].pg[0]||R[i].pg[1]||R[i].pg[2]||R[i].pg[3])) cand.push_back(i);
    std::sort(cand.begin(),cand.end()); cand.erase(std::unique(cand.begin(),cand.end()),cand.end());
    fprintf(stderr,"phase (ii) candidates: %zu\n",cand.size());
    const i64 LIM=(1ll<<27)-1;
    #pragma omp parallel for schedule(dynamic,4)
    for(size_t q=0;q<cand.size();q++){ size_t i=cand[q]; Row&r=R[i]; Ctx C; f1_field(r.v,C.S,C.u,C.kk);
        std::vector<i64> lo(1024*32,0), hi(1024*32,-1);
        for(int t=-512;t<=511;t++){ int it=t+512;
            for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node;
                if(node==0){ lo[it*32]=0; hi[it*32]= (BASE[it][0]>=VLO[it][0]&&BASE[it][0]<=VHI[it][0])?LIM:-1; continue; }
                i64 Sv=C.S[n]; int uu=C.u[n]; i64 t2lo=T2LO[it][node],t2hi=T2HI[it][node];
                i64 a1=0,b1=LIM+1; while(a1<b1){i64 m=a1+((b1-a1)>>1); if(f1_T2(r.v,m,Sv,uu)>=t2lo)b1=m;else a1=m+1;}
                i64 a2=-1,b2=LIM;  while(a2<b2){i64 m=a2+((b2-a2+1)>>1); if(f1_T2(r.v,m,Sv,uu)<=t2hi)a2=m;else b2=m-1;}
                lo[it*32+node]=a1; hi[it*32+node]=a2; } }
        int best=1<<30; size_t bc=0;
        for(size_t c=0;c<CS1.size();c++){ int v=0; const i64*J=&CJ[c*1024];
            for(int it=0;it<1024;it++){ i64 j=J[it]; const i64*L=&lo[it*32],*H=&hi[it*32];
                for(int node=0;node<32;node++) if(j<L[node]||j>H[node]) v++; }
            if(v<best){best=v;bc=c;} }
        Chain ch{CS1[bc],CS2[bc],CRO[bc],CK1[bc],CK2[bc],0}; int g[4];
        r.bst=f1_full(r.v,C,ch,g);          // exact re-verification of the window-based ranking
        memcpy(r.bg,g,sizeof g); r.bs1=CS1[bc];r.bs2=CS2[bc];r.bro=CRO[bc];r.bK1=CK1[bc];r.bK2=CK2[bc]; }
    auto pr=[&](const Row&r,const char*tag){
        printf("%-9s W=%2d %-8s %-18s b=%d a=%2d %s K3=%-10s %-7s | pub %4d (g %d/%d/%d/%d)",
            tag,r.v.W,RMNAME[r.v.rm],FMNAME[r.v.fm],r.v.b,r.v.a,r.v.role?"arr(S,J)":"arr(J,S)",
            KCNAME[r.v.kc],SGNAME[r.v.sg],r.pub,r.pg[0],r.pg[1],r.pg[2],r.pg[3]);
        if(r.bst>=0) printf(" | best %4d @ (%d,%d,%s,%lld,%lld) (g %d/%d/%d/%d)",r.bst,r.bs1,r.bs2,
            r.bro?"arr(dx,KC)":"arr(KC,dx)",(long long)r.bK1,(long long)r.bK2,r.bg[0],r.bg[1],r.bg[2],r.bg[3]);
        printf("\n"); };
    for(size_t i=0;i<R.size();i++){ FVar&v=R[i].v;
        if(v.W==23&&v.rm==0&&v.fm==0&&v.b==1&&v.a==30&&v.role==0&&v.kc==1&&v.sg==0) pr(R[i],"BASELINE"); }
    puts("\n-- best 20 at the PUBLISHED chain (only the final square product changed; nothing fitted):");
    for(int i=0;i<20;i++) pr(R[idx[i]],"");
    puts("\n-- best 20 at the PUBLISHED chain that are ALSO guard-clean (nodes 0,1/31,2/30,16 all exact):");
    { int s=0; for(size_t i:idx){ if(R[i].pg[0]||R[i].pg[1]||R[i].pg[2]||R[i].pg[3])continue;
        pr(R[i],"GUARD-OK"); if(++s>=20)break; } if(!s)puts("   (none)"); }
    std::vector<size_t> c2=cand; std::sort(c2.begin(),c2.end(),[&](size_t a,size_t b){return R[a].bst<R[b].bst;});
    puts("\n-- best 20 with the chain ALSO SEARCHED (a search over 2280 chains, not a derivation):");
    for(int i=0;i<20&&i<(int)c2.size();i++) pr(R[c2[i]],"");
    puts("\n-- best 20 of those that are guard-clean:");
    { int s=0; for(size_t i:c2){ if(R[i].bg[0]||R[i].bg[1]||R[i].bg[2]||R[i].bg[3])continue;
        pr(R[i],"GUARD-OK"); if(++s>=20)break; } if(!s)puts("   (none)"); }
    return 0; }
