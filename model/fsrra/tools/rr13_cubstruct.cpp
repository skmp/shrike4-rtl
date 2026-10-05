// rr13_cubstruct.cpp -- rr12: all remaining misses are the chain intermediate I off by one, the intermediate sitting
// within 1/64..2/64 of its truncation step -> an array-truncation-scale (2^17..2^18 raw) detail of the FIRST cubic product.
// Rescan that product's structure now that linear and square are clean (the old scans ran against a polluted square):
//   sqX = (arr(dx,dx) + K) >> (20 + g)               g = 3: the square term's own latch (sq21), 5: the model (sq19)
//   X   = arr(C3f << p, sqX)  or  arr(sqX, C3f << p)  (+ K1)
//   I   = X >> (20 + gi),  gi = p - g - 2 - e         e = extra bits carried in I (T3 aligner shift 27 + e)
//   T3  = sm(arr(dx, I) + K, 27 + e)  or  arr(I, dx)
// Score = total misses over the 34 nodes (C0 +-1 per node), linear/square/C0 from refit2.txt.
#include "rr_common.h"
#include <omp.h>
struct Cfg { int g,p,e,r1,r2,k1; long miss; };
int main(int argc,char**argv){ int STEP=argc>1?atoi(argv[1]):3; if(!load_fsrra())return 1; Word REF[34]; if(!load_words("refit2.txt",REF))return 1;
    struct Nd { std::vector<Smp> S; std::vector<i64> base, sqr; Word w; }; std::vector<Nd> ND(34);
    #pragma omp parallel for schedule(dynamic,1)
    for(int id=0;id<34;id++){ Nd&nd=ND[id]; nd.w=REF[id]; nd.S=node_samples(id/17,id%17,STEP); nd.base.resize(nd.S.size()); nd.sqr.resize(nd.S.size()); for(size_t i=0;i<nd.S.size();i++){ nd.base[i]=-termT1(nd.w,nd.S[i])+termT2(nd.w,nd.S[i]); nd.sqr[i]=arr(nd.S[i].dx,nd.S[i].dx); } }
    std::vector<Cfg> C; for(int g=0;g<=7;g++)for(int p=4;p<=17;p++)for(int e=-2;e<=3;e++){ int gi=p-g-2-e; if(gi<0||gi>12)continue; for(int r1=0;r1<2;r1++)for(int r2=0;r2<2;r2++)for(int k1=0;k1<2;k1++)C.push_back({g,p,e,r1,r2,k1,0}); }
    printf("%zu configurations, stride %d\n",C.size(),STEP);
    #pragma omp parallel for schedule(dynamic,4)
    for(size_t ci=0;ci<C.size();ci++){ Cfg&c=C[ci]; int gi=c.p-c.g-2-c.e; long tot=0; bool bad=false;
        for(int id=0;id<34&&!bad;id++){ Nd&nd=ND[id]; i64 c3=(nd.w.rec3>>10)<<c.p; if(c3>=(1ll<<27)){bad=true;break;} size_t N=nd.S.size(); int m[3]={0,0,0};
            for(size_t i=0;i<N;i++){ const Smp&s=nd.S[i]; i64 sq=(nd.sqr[i]+K)>>(20+c.g); if(sq>=(1ll<<24)&&!c.r1){ } i64 X=(c.r1?arr(sq,c3):arr(c3,sq))+(c.k1?K:0); i64 I=X>>(20+gi); if(I>=(1ll<<24)){bad=true;break;} i64 P=(c.r2?arr(I,s.dx):arr(s.dx,I))+K; i64 W=nd.w.C0+nd.base[i]-sm(P,27+c.e); i64 d=W-s.q4; for(int k=0;k<3;k++) if(!okd(s,d+k-1))m[k]++; }
            tot+=std::min(m[0],std::min(m[1],m[2])); }
        c.miss=bad?(1l<<40):tot; }
    std::sort(C.begin(),C.end(),[](const Cfg&a,const Cfg&b){return a.miss<b.miss;});
    puts("best 40:   misses |  g  p  e  gi | X roles (1 = square recoded)  T3 roles (1 = I recoded)  K1");
    for(int i=0;i<40&&i<(int)C.size();i++){ const Cfg&c=C[i]; printf("   %9ld | %2d %2d %+d  %2d |        %d                          %d                   %s\n",c.miss,c.g,c.p,c.e,c.p-c.g-2-c.e,c.r1,c.r2,c.k1?"2^19":"0"); }
    for(auto&c:C) if(c.g==5&&c.p==10&&c.e==0&&c.r1==0&&c.r2==0&&c.k1==1){ printf("the model (g 5, p 10, e 0): %ld\n",c.miss); break; }
    return 0; }
