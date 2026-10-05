// wb_place.cpp -- the asymmetry quantum |t|/64 fixes the LSB WEIGHT of the first latch, not the array
// placement.  J = (arr(L<<g, dx<<h) + K2) >> (19+g+h) has the same quantum for every (g,h) but a different
// row truncation.  Which placement makes every t reachable?
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024]; static int OKT[1024];
int main(){ build_tables();
    FILE*f=fopen("jwin.txt","r"); int t;long long a,b;int o;
    while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){JL[t+512]=a;JH[t+512]=b;OKT[t+512]=o;} fclose(f);
    puts(" g  h  s2 role | best reach (over K2)   K2/2^16");
    int gbest=0; int bestreach=0;
    for(int g=0;g<=5;g++) for(int h=-5;h<=3;h++){ int s2=19+g+h; if(s2<16||s2>30)continue;
      int nk=1<<((s2-16)>0?(s2-16>12?12:s2-16):0);
      for(int role=0;role<2;role++){
        std::vector<int> cnt(nk,0);
        for(int tt=-512;tt<=511;tt++){ if(!OKT[tt+512])continue; i64 dx=(i64)tt<<(13+h);
            long double c=39.47120L*(long double)tt*powl(2.0L,(long double)(s2-13-h-g));
            i64 c0=(i64)llroundl(c); std::vector<char> hit(nk,0);
            for(i64 v=c0-2500;v<=c0+2500;v++){ i64 P=role?arr(dx,v<<g):arr(v<<g,dx);
                for(int k=0;k<nk;k++){ if(hit[k])continue; i64 J=(P+((i64)k<<16))>>s2;
                    if(J>=JL[tt+512]&&J<=JH[tt+512]) hit[k]=1; } }
            for(int k=0;k<nk;k++) cnt[k]+=hit[k]; }
        int bk=0; for(int k=1;k<nk;k++) if(cnt[k]>cnt[bk])bk=k;
        printf("%2d %2d %3d  %d   | %5d  %s   (K2=%d*2^16)\n",g,h,s2,role,cnt[bk],cnt[bk]>=1022?"<== ALL REACHABLE":"",bk);
        if(cnt[bk]>bestreach){bestreach=cnt[bk];} fflush(stdout); } }
    printf("best reach over all placements: %d\n",bestreach);
    return 0; }
