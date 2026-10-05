// v41_delta.cpp -- INDEPENDENT check of the "delta is not a function of the array's loss state" claim.
// delta(t) = required J - model J, as the FULL interval [JL-Jm, JH-Jm].
// For a key K(t): stabbing score = sum over classes of the max number of that class's intervals sharing
// a point; contradictory classes = classes whose intervals have empty common intersection.
#include "base2.h"
#include <cstdio>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <string>
static i64 JL[1024],JH[1024];
static void load_win(){ FILE* f=fopen("jwin18.txt","r"); char l[512];
  while(fgets(l,sizeof l,f)){ if(l[0]=='#')continue; long long t,a,b,w; if(sscanf(l,"%lld %lld %lld %lld",&t,&a,&b,&w)!=4)continue; JL[t+512]=a; JH[t+512]=b;} fclose(f);}
static i64 dlo[1024],dhi[1024], L1[1024],L2[1024],I1v[1024],DXv[1024],JM[1024];
static void report(const char* name, const std::vector<long long>& key){
    std::map<long long,std::vector<int>> cl;
    for(int i=0;i<1024;i++) cl[key[i]].push_back(i);
    long long score=0; int contra=0;
    for(auto& kv:cl){ std::vector<std::pair<i64,int>> ev;
        i64 ilo=-(1ll<<60), ihi=(1ll<<60);
        for(int i:kv.second){ ev.push_back({dlo[i],+1}); ev.push_back({dhi[i]+1,-1}); ilo=std::max(ilo,dlo[i]); ihi=std::min(ihi,dhi[i]); }
        if(ilo>ihi) contra++;
        std::sort(ev.begin(),ev.end()); int cur=0,bs=0;
        for(size_t j=0;j<ev.size();j++){ cur+=ev[j].second; if(j+1==ev.size()||ev[j+1].first!=ev[j].first) bs=std::max(bs,cur); }
        score+=bs; }
    printf("  %-28s classes %5zu   stabbing %4lld/1024   contradictory %d\n",name,cl.size(),score,contra);
}
int main(){ build_tables(); load_win();
    int nz=0;
    for(int t=-512;t<=511;t++){ int i=t+512; i64 dx=(i64)t<<13; DXv[i]=dx;
        i64 a1=arr(KC,dx); L1[i]=a1-(i64)KC*dx; i64 I1=(a1+K)>>23; I1v[i]=I1;
        i64 a2=arr(I1,dx); L2[i]=a2-I1*dx; i64 J=(a2+K)>>18; JM[i]=J;
        dlo[i]=JL[i]-J; dhi[i]=JH[i]-J; if(!(dlo[i]<=0&&0<=dhi[i])) nz++; }
    printf("offsets where delta=0 is NOT allowed: %d/1024 (=> baseline score %d)\n",nz,1024-nz);
    i64 mn=1ll<<60,mx=-(1ll<<60); for(int i=0;i<1024;i++){ mn=std::min(mn,dlo[i]); mx=std::max(mx,dhi[i]); }
    printf("delta range over all t: [%lld,%lld]\n",(long long)mn,(long long)mx);
    printf("landmarks: t=17 delta=[%lld,%lld]  t=342 [%lld,%lld]  t=-342 [%lld,%lld]\n\n",
        (long long)dlo[17+512],(long long)dhi[17+512],(long long)dlo[342+512],(long long)dhi[342+512],
        (long long)dlo[-342+512],(long long)dhi[-342+512]);
    { std::set<i64> s1,s2,s3; for(int i=0;i<1024;i++){ s1.insert(L1[i]); s2.insert(L2[i]); s3.insert(((I1v[i]&((1ll<<18)-1))<<16)|(DXv[i]&0xffff)); }
      printf("BIT REACH at w=13: distinct loss1 values %zu ; distinct loss2 values %zu ; distinct (I1 mod 2^18, dx mod 2^16) keys %zu of 1024\n",
        s1.size(),s2.size(),s3.size());
      std::set<i64> s4,s5; for(int i=0;i<1024;i++){ s4.insert(DXv[i]&0xffff); s5.insert(I1v[i]&((1ll<<12)-1)); }
      printf("  distinct dx mod 2^16: %zu (= t's low 3 bits)   distinct I1 mod 2^12: %zu\n\n",s4.size(),s5.size()); }
    std::vector<long long> k(1024);
    for(int i=0;i<1024;i++) k[i]=0; report("constant",k);
    for(int b=1;b<=10;b++){ for(int t=-512;t<=511;t++) k[t+512]=t&((1<<b)-1); char nm[64]; snprintf(nm,64,"t & %d",(1<<b)-1); report(nm,k); }
    for(int t=-512;t<=511;t++) k[t+512]=(t>0)-(t<0); report("sign(t)",k);
    for(int t=-512;t<=511;t++) k[t+512]=t<0?-t:t; report("|t|",k);
    for(int m:{3,7,13,31,63}){ for(int t=-512;t<=511;t++) k[t+512]=((t%m)+m)%m; char nm[64]; snprintf(nm,64,"t mod %d",m); report(nm,k); }
    for(int i=0;i<1024;i++) k[i]=L1[i]; report("loss1 (array, mul 1)",k);
    for(int i=0;i<1024;i++) k[i]=L2[i]; report("loss2 (array, mul 2)",k);
    for(int i=0;i<1024;i++) k[i]=L1[i]*1000003ll+L2[i]; report("(loss1, loss2) jointly",k);
    for(int b:{6,8,10,12,14}){ for(int i=0;i<1024;i++) k[i]=I1v[i]&((1ll<<b)-1); char nm[64]; snprintf(nm,64,"I1 mod 2^%d",b); report(nm,k); }
    for(int i=0;i<1024;i++) k[i]=JM[i]&0xff; report("J_model mod 2^8",k);
    return 0; }
