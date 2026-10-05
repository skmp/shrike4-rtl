// wb_sqr.cpp -- SQUARE-THE-LATCH form:  L = R1((arr(A,dx) or arr(dx,A)) + K1) >> s1 ;  J = R2(arr(L,L)+K2)>>s2
// Predicted asymmetry rate 2|L|/2^s2 = 12.5654/2^(s2/2) per |t|; only s2=19 falls inside the measured
// allowed band (0.01462, 0.02311).  Stage 2 is inverted exactly; K1 is solved by an interval sweep.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024]; static int OKT[1024];
static inline i64 latch(i64 P,int s,int rule){ return rule==0 ? (P>>s) : sm(P,s); }
int main(int argc,char**argv){ build_tables();
    FILE*f=fopen(argc>1?argv[1]:"jwin.txt","r"); int t;long long a,b;int o;
    while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){JL[t+512]=a;JH[t+512]=b;OKT[t+512]=o;} fclose(f);
    struct Best{int sc; int s1,s2,r1,r2,role1; i64 A,K1,K2;}; std::vector<Best> res;
    for(int s2=17;s2<=21;s2++){
        long double c=sqrtl(39.47120L*powl(2.0L,(long double)s2));      // |L| ~ c*|t|
        for(int r2=0;r2<2;r2++) for(int k2u=0;k2u<(1<<((s2-16)>0?s2-16:0));k2u++){
            i64 K2=(i64)k2u<<16;
            std::vector<i64> Llo(1024),Lhi(1024); std::vector<char> ok(1024,0); int reach=0;
            for(int tt=-512;tt<=511;tt++){ if(!OKT[tt+512])continue;
                i64 c0=(i64)llroundl(c*(long double)tt); i64 fi=0,gi=0; bool any=false;
                for(i64 v=c0-3000;v<=c0+3000;v++){ i64 J=latch(arr(v,v)+K2,s2,r2);
                    if(J>=JL[tt+512]&&J<=JH[tt+512]){ if(!any){fi=v;any=true;} gi=v; } }
                if(any){Llo[tt+512]=fi;Lhi[tt+512]=gi;ok[tt+512]=1;reach++;} }
            fprintf(stderr,"s2=%d r2=%d K2=%lld reach=%d\n",s2,r2,(long long)K2,reach);
            for(int s1=18;s1<=27;s1++){
                long double A0l=c*powl(2.0L,(long double)s1)/8192.0L;
                if(A0l<(1<<17)||A0l>(1<<26)) continue; i64 A0=(i64)llroundl(A0l);
                for(int role1=0;role1<2;role1++) for(int r1=0;r1<2;r1++){
                    int gsc=-1; i64 gA=0,gK=0;
                    #pragma omp parallel for schedule(dynamic)
                    for(i64 A=A0-150;A<=A0+150;A++){
                        std::vector<std::pair<i64,i64>> iv; iv.reserve(1024);
                        for(int tt=-512;tt<=511;tt++){ if(!ok[tt+512])continue; i64 dx=(i64)tt<<13;
                            i64 P=role1?arr(dx,A):arr(A,dx);
                            i64 lo=-(1ll<<34),hi=(1ll<<34);
                            i64 a1=lo,b1=hi; while(a1<b1){i64 m=a1+((b1-a1)>>1); if(latch(P+m,s1,r1)>=Llo[tt+512])b1=m; else a1=m+1;}
                            i64 a2=lo-1,b2=hi; while(a2<b2){i64 m=a2+((b2-a2+1)>>1); if(latch(P+m,s1,r1)<=Lhi[tt+512])a2=m; else b2=m-1;}
                            if(a1<=a2) iv.push_back({a1,a2}); }
                        std::vector<std::pair<i64,int>> ev; ev.reserve(iv.size()*2);
                        for(auto&p:iv){ev.push_back({p.first,1});ev.push_back({p.second+1,-1});}
                        std::sort(ev.begin(),ev.end()); int cur=0,best=0; i64 bk=0;
                        for(size_t i=0;i<ev.size();i++){cur+=ev[i].second; if(cur>best){best=cur;bk=ev[i].first;}}
                        #pragma omp critical
                        { if(best>gsc){gsc=best;gA=A;gK=bk;} } }
                    res.push_back({gsc,s1,s2,r1,r2,role1,gA,gK,K2});
                    if(gsc>=935){ printf("HIT %d | s1=%d s2=%d r1=%d r2=%d ro1=%d A=%lld K1=%lld K2=%lld\n",
                        gsc,s1,s2,r1,r2,role1,(long long)gA,(long long)gK,(long long)K2); fflush(stdout); } } } } }
    std::sort(res.begin(),res.end(),[](const Best&x,const Best&y){return x.sc>y.sc;});
    puts("score | s1 s2 r1 r2 ro1 |      A          K1         K2");
    for(int i=0;i<15&&i<(int)res.size();i++){ Best&r=res[i];
        printf("%5d | %2d %2d  %d  %d  %d  | %10lld %11lld %9lld\n",r.sc,r.s1,r.s2,r.r1,r.r2,r.role1,
            (long long)r.A,(long long)r.K1,(long long)r.K2); }
    return 0; }
