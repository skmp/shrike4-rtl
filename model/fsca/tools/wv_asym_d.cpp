// wv_asym_d.cpp -- adversarial verification, part 3: the required-asymmetry table, the even-J ceiling,
// the free-J floor, the miss profile of the baseline chain, the tight-window pins, the esq asymmetry.
#include "wv_asym_win.h"
#include <cstdio>
#include <map>
#include <set>
int main(){ if(!load_fsca(WVF)){puts("no dump");return 1;} build_tables(); wv_build(0);
    // ---- D table ----------------------------------------------------------
    int forced1=0,forced2=0,forced3=0,zeroD=0; std::vector<int> f3,f2,zl;
    std::map<int,std::pair<i64,i64>> Dmap;
    for(int a=1;a<=511;a++){ int ip=a+512, im=-a+512; if(!WV_OK[ip]||!WV_OK[im])continue;
        i64 dlo=WV_LO[im]-WV_HI[ip], dhi=WV_HI[im]-WV_LO[ip]; Dmap[a]={dlo,dhi};
        if(dlo>=1) forced1++; if(dlo>=2) forced2++; if(dlo>2){forced3++; f3.push_back(a);} if(dlo>=2&&dlo<=2) f2.push_back(a);
        if(dlo<=0&&dhi>=0){ /* D=0 allowed */ if(dlo==0&&dhi==0){zeroD++; zl.push_back(a);} } }
    printf("|t| pairs with both sides feasible: %zu\n",Dmap.size());
    printf("forced D>=1: %d ;  forced D>=2: %d ;  forced D>2: %d  ->",forced1,forced2,forced3);
    for(int a:f3) printf(" %d[%lld,%lld]",a,(long long)Dmap[a].first,(long long)Dmap[a].second); puts("");
    printf("forced D exactly>=2 (not >2):"); for(int a:f2) printf(" %d[%lld,%lld]",a,(long long)Dmap[a].first,(long long)Dmap[a].second); puts("");
    printf("D pinned to 0 (dlo==dhi==0):"); for(int a:zl) printf(" %d",a); puts("");
    printf("forced-D list (dlo>=1):"); { int n=0; for(auto&kv:Dmap) if(kv.second.first>=1){ printf(" %d",kv.first); n++; } printf("   (%d)\n",n); }
    { int nneg=0; for(auto&kv:Dmap) if(kv.second.second<0) nneg++; printf("forced D<0 (dhi<0): %d\n",nneg); }
    // ---- even-J ceiling and free-J floor ----------------------------------
    int evenmax=0; for(int t=-512;t<=511;t++){} 
    { int s=0; // t=0 and t=-512 have no partner
      if(WV_OK[0+512]) s++; if(WV_OK[-512+512]) s++;
      for(int a=1;a<=511;a++){ int ip=a+512,im=-a+512; bool op=WV_OK[ip],om=WV_OK[im];
        if(op&&om){ // can one J serve both?
            bool inter=false; size_t i=0,j=0; auto&A=WV_FEAS[ip];auto&B=WV_FEAS[im];
            while(i<A.size()&&j<B.size()){ if(A[i]==B[j]){inter=true;break;} if(A[i]<B[j])i++;else j++; }
            s += inter?2:1; }
        else if(op||om) s+=1; }
      evenmax=s; }
    printf("best score achievable by any rule EVEN in t: %d / 1022\n",evenmax);
    // free-J floor: min node violations at the infeasible t
    { int floorv=0; for(int t=-512;t<=511;t++){ if(WV_OK[t+512])continue;
        i64 Jc=(KC*(i64)t*(i64)t)>>17; int best=1<<30;
        for(i64 J=Jc-4000;J<=Jc+4000;J++){ int v=0;
            for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
                int a=(node*1024+t)&0xffff; DWin w=wv_dumpwin(a); i64 V=wv_base(node,t)-wv_T2(node,J,0);
                if(V<w.lo||V>w.hi)v++; }
            if(v<best)best=v; }
        printf("  t=%d: min node violations over all J = %d\n",t,best); floorv+=best; }
      printf("free-per-t-J floor (shift 20) = %d violations\n",floorv); }
    // ---- miss profile of the baseline chain --------------------------------
    { int above=0,below=0,tpos=0,tneg=0,miss=0; i64 wmin=0,wmax=0;
      for(int t=-512;t<=511;t++){ if(!WV_OK[t+512])continue;
        i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23, J=(arr(I1,dx)+K)>>20;
        if(J>=WV_LO[t+512]&&J<=WV_HI[t+512])continue; miss++;
        if(J>WV_HI[t+512]){above++; i64 d=J-WV_HI[t+512]; if(d>wmax)wmax=d;} else {below++; i64 d=J-WV_LO[t+512]; if(d<wmin)wmin=d;}
        if(t>0)tpos++; else tneg++; }
      printf("baseline chain misses: %d  (above JH %d, below JL %d ; t>0 %d, t<0 %d ; worst deviations %lld..+%lld)\n",
        miss,above,below,tpos,tneg,(long long)wmin,(long long)wmax); }
    // ---- tight windows -----------------------------------------------------
    { printf("t with window width <= 2 (J - floor(KC t^2/2^17)):\n");
      int n=0; for(int t=-512;t<=511;t++){ if(!WV_OK[t+512])continue; if(WV_HI[t+512]-WV_LO[t+512]>2-1)continue;
        i64 fl=(KC*(i64)t*(i64)t)>>17; printf("  t=%+4d  width %lld  J-fl in [%lld,%lld]\n",t,
            (long long)(WV_HI[t+512]-WV_LO[t+512]+1),(long long)(WV_LO[t+512]-fl),(long long)(WV_HI[t+512]-fl)); n++; }
      printf("  count = %d\n",n); }
    // ---- esq asymmetry -----------------------------------------------------
    { int diff=0; std::map<int,std::set<int>> byres;
      for(int a=1;a<=511;a++){ i64 dp=(i64)a<<13, dm=-dp;
        i64 ep=arr(dp,dp)-dp*dp, em=arr(dm,dm)-dm*dm;
        if(ep!=em){ diff++; byres[(int)((em-ep)/(1ll<<17))].insert(a%32); } }
      printf("esq(+t)!=esq(-t) for %d of 511 |t|\n",diff);
      for(auto&kv:byres){ printf("  delta=%+d * 2^17 : |t| mod 32 in {",kv.first); for(int r:kv.second)printf(" %d",r); printf(" }\n"); } }
    // ---- e1 correlation test ------------------------------------------------
    { int n0=0,f0=0,n1=0,f1=0; for(auto&kv:Dmap){ int a=kv.first; bool forced=kv.second.first>=1;
        if(a%4==0){n0++; if(forced)f0++;} else {n1++; if(forced)f1++;} }
      printf("both-feasible |t| with t mod 4 == 0: %d, forced D>=1: %d (%.1f%%);  others: %d, forced: %d (%.1f%%)\n",
        n0,f0,100.0*f0/n0,n1,f1,100.0*f1/n1); }
    return 0; }
