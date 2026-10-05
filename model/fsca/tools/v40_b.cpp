// v40_b.cpp -- (b) delta intervals + key stabbing, and a PROPER test of the loss key-dependence.
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <map>
#include <algorithm>
#include <random>
static i64 JLv[1024], JHv[1024];
static bool loadwin(){ FILE*f=fopen("jwin18.txt","r"); if(!f) return false; char ln[256]; int n=0;
  while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#') continue; long long t,a,b,w; if(sscanf(ln,"%lld %lld %lld %lld",&t,&a,&b,&w)!=4) continue;
    JLv[t+512]=a; JHv[t+512]=b; n++; } fclose(f); return n==1024; }
// max-overlap stabbing within a class of closed integer intervals
static int stab(const std::vector<std::pair<i64,i64>>& v){
    std::vector<std::pair<i64,int>> e; for(auto&p:v){ e.push_back({p.first,+1}); e.push_back({p.second+1,-1}); }
    std::sort(e.begin(),e.end()); int cur=0,best=0; for(auto&x:e){ cur+=x.second; best=std::max(best,cur);} return best; }
struct Res{int score,ncls,contra;};
static Res scorekey(const std::vector<i64>& lo,const std::vector<i64>& hi,const std::vector<long long>& key){
    std::map<long long,std::vector<std::pair<i64,i64>>> M;
    for(int i=0;i<1024;i++) M[key[i]].push_back({lo[i],hi[i]});
    int s=0,c=0; for(auto&kv:M){ int b=stab(kv.second); s+=b; if(b<(int)kv.second.size()) c++; }
    return {s,(int)M.size(),c}; }
int main(){ if(!loadwin()){printf("FATAL\n");return 1;}
    std::vector<i64> lo(1024),hi(1024),l1(1024),l2(1024),I1v(1024);
    int nz=0; i64 dmin=0,dmax=0; bool f=true;
    for(int t=-512;t<512;t++){ int i=t+512; i64 dx=(i64)t<<13;
        i64 I1=(arr(KC,dx)+K)>>23; i64 J=(arr(I1,dx)+K)>>18;
        lo[i]=JLv[i]-J; hi[i]=JHv[i]-J; I1v[i]=I1;
        l1[i]=(i64)(arr(KC,dx)-(__int128)KC*dx); l2[i]=(i64)(arr(I1,dx)-(__int128)I1*dx);
        if(!(lo[i]<=0&&0<=hi[i])) nz++;
        if(f){dmin=lo[i];dmax=hi[i];f=false;} dmin=std::min(dmin,lo[i]); dmax=std::max(dmax,hi[i]); }
    printf("[b0] offsets needing delta!=0: %d/1024 ; delta interval extremes [%lld,%lld] ; landmarks t=17 [%lld,%lld] t=342 [%lld,%lld] t=-342 [%lld,%lld]\n",
        nz,(long long)dmin,(long long)dmax,(long long)lo[17+512],(long long)hi[17+512],(long long)lo[342+512],(long long)hi[342+512],(long long)lo[-342+512],(long long)hi[-342+512]);
    auto run=[&](const char*name,std::vector<long long> k){ Res r=scorekey(lo,hi,k);
        printf("[b1] key %-18s classes %5d  score %4d/1024  contradictory classes %d\n",name,r.ncls,r.score,r.contra); };
    std::vector<long long> k(1024);
    for(auto&x:k)x=0; run("constant",k);
    for(int M:{7,15,63,255,511,1023}){ for(int t=-512;t<512;t++) k[t+512]=(long long)((unsigned)t&(unsigned)M);
        char b[32]; snprintf(b,32,"t&%d",M); run(b,k); }
    for(int t=-512;t<512;t++) k[t+512]=(t<0); run("sign(t)",k);
    for(int t=-512;t<512;t++) k[t+512]=(t<0?-t:t); run("|t|",k);
    for(int m:{3,7,13,31,63}){ for(int t=-512;t<512;t++) k[t+512]=((t%m)+m)%m; char b[32]; snprintf(b,32,"t mod %d",m); run(b,k); }
    for(int i=0;i<1024;i++) k[i]=l1[i]; run("loss1",k);
    for(int i=0;i<1024;i++) k[i]=l2[i]; run("loss2",k);
    for(int i=0;i<1024;i++) k[i]=l1[i]*1000000ll+l2[i]; run("(loss1,loss2)",k);
    for(int b=6;b<=13;b++){ for(int i=0;i<1024;i++) k[i]=I1v[i]&((1ll<<b)-1); char s[32]; snprintf(s,32,"I1 mod 2^%d",b); run(s,k); }
    // ---- PROPER key-dependence test: hold (rec mod 2^18, mul mod 2^16) fixed, vary the high bits
    { std::mt19937_64 rng(7); long long bad=0,n=0;
      for(int trial=0;trial<4000;trial++){
        i64 rlo=(i64)(rng()%(1ull<<18)), mlo=(i64)(rng()%(1ull<<16)); i64 ref=0; bool have=false;
        for(int rep=0;rep<20;rep++){
            i64 rhi=(i64)(rng()%(1ull<<9))<<18, mhi=(i64)(rng()%(1ull<<8))<<16;
            i64 rec=rhi+rlo, mul=mhi+mlo; if(rng()&1) rec=-rec-(1ll<<28); // also probe negative rec with same low bits
            if(rec<=-(1ll<<27)||rec>=(1ll<<27)) rec=rhi+rlo;
            i64 loss=(i64)(arr(rec,mul)-(__int128)rec*mul);
            if(!have){ref=loss;have=true;} else if(loss!=ref) bad++;
            n++; } }
      printf("[b2] key-dependence, HIGH bits varied with (rec mod 2^18, mul mod 2^16) held: %lld probes, %lld disagreements\n",n,bad); }
    return 0; }
