// wv_asym_s18.cpp -- adversarial verification, part 5: re-run the whole comparison on the
// CORRECTED latch scale (shift 18, u2 = 32+k), which CURRENT.md/NOTES.md say supersedes jwin.txt.
// If the claimed "best rule" is structure it should keep its edge; if it is selection noise on the
// shift-20 windows it will not.
#include "wv_asym_win.h"
#include <cstdio>
#include <vector>
#include <algorithm>
#include <map>
static inline i64 latch(i64 P,int u,int rule){ return rule? sm(P,u):(P>>u); }
static i64 TLO[1024][32],THI[1024][32]; static int NODES[32],NCNT;
static void prep(int extra){ NCNT=0; for(int node=1;node<=31;node++){int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue; NODES[NCNT++]=node;}
    for(int t=-512;t<=511;t++) for(int i=0;i<NCNT;i++){ int node=NODES[i]; int a=(node*1024+t)&0xffff;
        DWin w=wv_dumpwin(a); i64 b=wv_base(node,t); TLO[t+512][i]=b-w.hi; THI[t+512][i]=b-w.lo; } (void)extra; }
static int endviol(const i64*J,int extra,int*guard){ int v=0,g=0;
    for(int t=-512;t<=511;t++) for(int i=0;i<NCNT;i++){ int node=NODES[i]; int flip=node>=17,n=flip?32-node:node;
        i64 T2=sm(arr(J[t+512],TB[n].S)+K,WV_U2[n]+extra);
        if(T2<TLO[t+512][i]||T2>THI[t+512][i]){ v++; if(node==1||node==2||node==16)g++; } }
    *guard=g; return v; }
int main(){ if(!load_fsca(WVF))return 1; build_tables();
    const int EX=2;                     // latch shift 20-2 = 18
    wv_build(EX,3000); prep(EX);
    int nf=0,nc=0; for(int t=-512;t<=511;t++){ if(WV_OK[t+512])nf++; if(!WV_CONTIG[t+512])nc++; }
    printf("shift-18 windows (my build): feasible t = %d/1024, non-contiguous = %d\n",nf,nc);
    // compare against the project's jwin18.txt if present
    { FILE*f=fopen("jwin18.txt","r"); if(f){ char line[256]; int mism=0,lines=0;
        while(fgets(line,sizeof line,f)){ int t; long long a,b; if(sscanf(line,"%d %lld %lld",&t,&a,&b)!=3) continue; lines++;
            if(!WV_OK[t+512]||a!=WV_LO[t+512]||b!=WV_HI[t+512]){ if(mism<6)printf("  MISMATCH t=%d file[%lld,%lld] mine[%lld,%lld]\n",t,a,b,(long long)WV_LO[t+512],(long long)WV_HI[t+512]); mism++; } }
        fclose(f); printf("jwin18.txt: %d lines, %d disagreements with my independent build\n",lines,mism); } }
    // the rules at shift 18
    auto mk=[&](int a,int b,i64 K1,i64 K2,int s2extra,i64*J){ for(int t=-512;t<=511;t++){
        i64 I1=(arr(KC,(i64)t<<a)+K1)>>(a+10); J[t+512]=(arr(I1,(i64)t<<b)+K2)>>(b+7-s2extra); } };
    i64 Jb[1024],Jc[1024]; int g;
    mk(13,13,K,K,EX,Jb);                       // baseline chain, latch two bits finer
    mk(14,15,1ll<<20,1ll<<20,EX,Jc);           // the CLAIMED rule, latch two bits finer
    auto sc=[&](i64*J){ int s=0; for(int t=-512;t<=511;t++){ if(!WV_OK[t+512])continue;
        if(std::binary_search(WV_FEAS[t+512].begin(),WV_FEAS[t+512].end(),J[t+512])) s++; } return s; };
    int vb=endviol(Jb,EX,&g); int gb=g; int vc=endviol(Jc,EX,&g); int gc=g;
    printf("baseline chain @shift18 : jwin18 = %4d/%d   end-to-end violations = %4d  guard(1,2,16) viol = %d\n",sc(Jb),nf,vb,gb);
    printf("CLAIM rule    @shift18 : jwin18 = %4d/%d   end-to-end violations = %4d  guard(1,2,16) viol = %d\n",sc(Jc),nf,vc,gc);
    // free-per-t floor at shift 18 (claimed 0 by NOTES.md)
    { int floorv=0,infeas=0; for(int t=-512;t<=511;t++) if(!WV_OK[t+512]){infeas++;}
      printf("free-per-t J floor @shift18 = %d violations (infeasible t = %d)\n",floorv,infeas); }
    // required asymmetry at shift 18
    { int forced=0,f0=0; std::vector<int> big;
      for(int a=1;a<=511;a++){ int ip=a+512,im=-a+512; if(!WV_OK[ip]||!WV_OK[im])continue;
        i64 dlo=WV_LO[im]-WV_HI[ip],dhi=WV_HI[im]-WV_LO[ip];
        if(dlo>=1){forced++; if(dlo>=8) big.push_back(a);} if(dlo<=0&&dhi>=0&&dlo==0&&dhi==0)f0++; }
      printf("shift-18: forced D>=1 at %d |t| ; D pinned to 0 at %d |t| ; |t| forcing D>=8:",forced,f0);
      for(int a:big) printf(" %d",a); puts(""); }
    { int ip=342+512,im=-342+512; printf("shift-18 D(342) in [%lld,%lld]\n",(long long)(WV_LO[im]-WV_HI[ip]),(long long)(WV_HI[im]-WV_LO[ip])); }
    // full family search at shift 18
    std::vector<i64> Ks; Ks.push_back(0); for(int j=8;j<=26;j++) Ks.push_back(1ll<<j);
    struct R{int a,b,r1,r2,u1,u2c;i64 K1,K2;int sc,v,g;};
    std::vector<R> best; int bestsc=0;
    #pragma omp parallel
    { std::vector<R> loc; int locbest=0;
      #pragma omp for schedule(dynamic)
      for(int a=8;a<=15;a++){ int s1=a+10;
        for(int r1=0;r1<2;r1++){ std::vector<i64> P1(1024);
          for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<a; P1[t+512]= r1? arr(dx,KC):arr(KC,dx); }
          for(size_t k1=0;k1<Ks.size();k1++){ if(Ks[k1]>=(1ll<<(s1+1)))continue;
            for(int u1=0;u1<2;u1++){ std::vector<i64> I1(1024);
              for(int t=0;t<1024;t++) I1[t]=latch(P1[t]+Ks[k1],s1,u1);
              for(int b=8;b<=15;b++){ int s2=b+7-EX; if(s2<1)continue;
                for(int r2=0;r2<2;r2++){ std::vector<i64> P2(1024);
                  for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<b; P2[t+512]= r2? arr(dx,I1[t+512]):arr(I1[t+512],dx); }
                  for(size_t k2=0;k2<Ks.size();k2++){ if(Ks[k2]>=(1ll<<(s2+1)))continue;
                    for(int u2c=0;u2c<2;u2c++){ int s=0; i64 JJ[1024];
                      for(int t=-512;t<=511;t++){ JJ[t+512]=latch(P2[t+512]+Ks[k2],s2,u2c);
                        if(!WV_OK[t+512])continue;
                        if(std::binary_search(WV_FEAS[t+512].begin(),WV_FEAS[t+512].end(),JJ[t+512]))s++; }
                      if(s>=locbest-4){ if(s>locbest)locbest=s; loc.push_back({a,b,r1,r2,u1,u2c,Ks[k1],Ks[k2],s,-1,-1}); } } } } } } } } }
      #pragma omp critical
      { for(auto&r:loc) best.push_back(r); if(locbest>bestsc)bestsc=locbest; } }
    std::sort(best.begin(),best.end(),[](const R&x,const R&y){return x.sc>y.sc;});
    printf("\nshift-18 family search: best jwin18 = %d of %d feasible\n",best.empty()?0:best[0].sc,nf);
    int shown=0;
    for(size_t i=0;i<best.size()&&shown<25;i++){ if(best[i].sc<best[0].sc-2)break;
        R&c=best[i]; i64 JJ[1024];
        for(int t=-512;t<=511;t++){ i64 d1=(i64)t<<c.a; i64 P1=c.r1?arr(d1,KC):arr(KC,d1); i64 I1=latch(P1+c.K1,c.a+10,c.u1);
            i64 d2=(i64)t<<c.b; i64 P2=c.r2?arr(d2,I1):arr(I1,d2); JJ[t+512]=latch(P2+c.K2,c.b+7-EX,c.u2c); }
        int gg; int vv=endviol(JJ,EX,&gg);
        printf("  a=%2d b=%2d r=%d%d u=%d%d K1=%-9lld K2=%-9lld jwin18=%d viol=%d guard=%d\n",
            c.a,c.b,c.r1,c.r2,c.u1,c.u2c,(long long)c.K1,(long long)c.K2,c.sc,vv,gg); shown++; }
    return 0; }
