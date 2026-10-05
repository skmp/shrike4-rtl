// b32.cpp -- the asymmetry is binary.  In the two-factor picture the sign asymmetry of the square term is
// delta_A(t) * |B_n| / 2^u3, where delta_A(t) = |A(-t)| - |A(t)| for the node-INDEPENDENT latch A.
// The data forces delta_A = 1 at 45 specific |t| and delta_A = 0 at 4 others (where the ladder step t/64 is
// too large to hide).  That is a 49-constraint signature.  Solve for (Kq, sA, K1) exactly: as K1 sweeps a full
// period each of A(t), A(-t) steps once, so the set of K1 giving the required delta is a union of intervals.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static const int ONES[]={86,148,179,189,194,197,201,209,233,249,251,255,258,277,285,299,308,324,325,332,342,344,
    359,383,385,396,401,408,411,412,415,417,431,444,447,455,456,458,459,469,476,481,487,494,499};
static const int ZEROS[]={301,348,376,420};
int main(){
    int nO=sizeof(ONES)/sizeof(int), nZ=sizeof(ZEROS)/sizeof(int);
    printf("signature: %d ones, %d zeros\n",nO,nZ);
    int found=0;
    for(int sA=10;sA<=30;sA++){ i64 per=1ll<<sA;
      for(i64 Kq=KC-3000;Kq<=KC+3000;Kq++){
        // for each constrained t, the set of K1 in [0,per) giving the required delta, as a union of intervals
        // A(t)  = (arr(Kq, t<<13) + K1) >> sA   (floor, signed)
        // delta = -A(-t) - A(t)
        std::vector<std::pair<i64,i64>> cur; cur.push_back({0,per-1}); bool dead=false;
        auto restrict_to=[&](int a,int want){
            i64 P=arr(Kq,(i64)a<<13), M=arr(Kq,-((i64)a<<13));
            // delta(K1) = -((M+K1)>>sA) - ((P+K1)>>sA) ; both floors step up once as K1 sweeps
            // so delta steps DOWN at bM = per - (M mod per) and at bP likewise -> compute breakpoints
            auto md=[&](i64 v){ i64 r=v-((v>>sA)<<sA); return r; };
            i64 bP = per - md(P), bM = per - md(M);          // in (0,per]
            std::vector<std::pair<i64,i64>> out;
            i64 pts[4]={0,bP<per?bP:per,bM<per?bM:per,per}; std::sort(pts,pts+4);
            for(int i=0;i<3;i++){ i64 lo=pts[i],hi=pts[i+1]-1; if(lo>hi)continue;
                i64 d = -((M+lo)>>sA) - ((P+lo)>>sA);
                if(d==want) out.push_back({lo,hi}); }
            std::vector<std::pair<i64,i64>> nc;
            for(auto&x:cur) for(auto&y:out){ i64 lo=std::max(x.first,y.first), hi=std::min(x.second,y.second); if(lo<=hi) nc.push_back({lo,hi}); }
            cur.swap(nc); if(cur.empty()) dead=true; };
        for(int i=0;i<nZ&&!dead;i++) restrict_to(ZEROS[i],0);
        for(int i=0;i<nO&&!dead;i++) restrict_to(ONES[i],1);
        if(!dead){ printf("MATCH sA=%d Kq=%lld (KC%+lld)  K1 in",sA,(long long)Kq,(long long)(Kq-KC));
            for(auto&x:cur) printf(" [%lld,%lld]",(long long)x.first,(long long)x.second); printf("\n");
            if(++found>30){ printf("...\n"); return 0; } } } }
    printf("matches found: %d\n",found);
    return 0; }
