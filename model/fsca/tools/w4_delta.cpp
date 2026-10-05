// w4_delta.cpp -- BRANCH 4 (b): the exact required correction delta(t), as an INTERVAL, and whether it is
// a FUNCTION of any small key.  Beyond m8_delta.cpp: every key also gets its STABBING SCORE, i.e. the
// number of offsets satisfiable if delta were an arbitrary function of that key (max over one value per
// key class).  A key is meaningful only if the score is high AND the class count is small.
//   window convention: delta_lo = JL - Jmodel, delta_hi = JH - Jmodel, the FULL allowed interval.
//   (midpoints would invent precision the dataset does not have; every test below uses the interval.)
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <map>
#include <algorithm>
#include <climits>
static i64 JL[1024],JH[1024]; static bool have[1024];
static i64 dl[1024],dh[1024];
static i64 Jm[1024], I1v[1024], L1v[1024], L2v[1024];
static void load(){ FILE*f=fopen("jwin18.txt","r"); char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} } fclose(f); }
// max number of intervals stabbed by one point, within a class
static int stab(std::vector<std::pair<i64,i64>>& v){ std::vector<std::pair<i64,int>> ev;
    for(auto&p:v){ ev.push_back({p.first,+1}); ev.push_back({p.second+1,-1}); }
    std::sort(ev.begin(),ev.end()); int c=0,b=0; for(auto&e:ev){ c+=e.second; if(c>b)b=c; } return b; }
template<class F> static void key(const char* name, F f){
    std::map<i64,std::vector<std::pair<i64,i64>>> g;
    for(int i=0;i<1024;i++) if(have[i]) g[f(i-512,i)].push_back({dl[i],dh[i]});
    int bad=0, sc=0; for(auto&kv:g){ i64 lo=LLONG_MIN,hi=LLONG_MAX; for(auto&p:kv.second){lo=std::max(lo,p.first);hi=std::min(hi,p.second);} 
        if(lo>hi) bad++; sc += stab(kv.second); }
    printf("  %-34s classes=%5zu  contradictory=%4d  STABBING SCORE %4d/1024%s\n",
        name,g.size(),bad,sc, sc==1024?"  <<< COMPLETE":"");
}
int main(){ build_tables(); load();
    for(int i=0;i<1024;i++) if(have[i]){ int t=i-512; i64 dx=(i64)t<<13;
        i64 A1=arr(KC,dx); L1v[i]=A1-(i64)KC*dx; i64 I1=(A1+K)>>23; I1v[i]=I1;
        i64 A2=arr(I1,dx); L2v[i]=A2-I1*dx; Jm[i]=(A2+K)>>18;
        dl[i]=JL[i]-Jm[i]; dh[i]=JH[i]-Jm[i]; }
    printf("delta(t) = required J - model J, as the FULL interval [JL-Jm, JH-Jm]\n");
    { int need=0; i64 mn=1e9,mx=-1e9; for(int i=0;i<1024;i++) if(have[i]){ if(!(dl[i]<=0&&0<=dh[i]))need++;
        mn=std::min(mn,dl[i]); mx=std::max(mx,dh[i]); }
      printf("  offsets needing delta != 0: %d/1024   delta range [%lld,%lld]\n",need,(long long)mn,(long long)mx); }
    printf("  landmarks: t=17 [%lld,%lld]  t=342 [%lld,%lld]  t=-342 [%lld,%lld]\n\n",
        (long long)dl[529],(long long)dh[529],(long long)dl[854],(long long)dh[854],(long long)dl[170],(long long)dh[170]);
    printf("KEY TESTS (a key names the mechanism only if the score is 1024 with FEW classes)\n");
    key("constant (no key at all)", [](int,int){return (i64)0;});
    for(int b=0;b<=10;b++){ char nm[80]; snprintf(nm,80,"t & %d (low %d bits)",(1<<b)-1,b); int bb=b;
        key(nm,[bb](int t,int){return (i64)(t&((1<<bb)-1));}); }
    for(int b=0;b<=6;b++){ char nm[80]; snprintf(nm,80,"(t & %d) , sign(t)",(1<<b)-1); int bb=b;
        key(nm,[bb](int t,int){return (i64)((t&((1<<bb)-1))*2 + (t<0));}); }
    for(int s=1;s<=6;s++){ char nm[80]; snprintf(nm,80,"t >> %d (high bits)",s); int ss=s;
        key(nm,[ss](int t,int){return (i64)(t>>ss);}); }
    key("sign(t)",[](int t,int){return (i64)(t<0);});
    key("|t|",[](int t,int){return (i64)(t<0?-t:t);});
    for(int m : {3,5,6,7,9,10,11,12,13,15,17,24,31,33,63,65}){ char nm[80]; snprintf(nm,80,"t mod %d",m); int mm=m;
        key(nm,[mm](int t,int){return (i64)(((t%mm)+mm)%mm);}); }
    printf("\n  -- keys built from the ARRAY's own state (this is the branch-4 hypothesis) --\n");
    key("loss1 (first multiply's loss)", [](int,int i){return L1v[i];});
    key("loss2 (second multiply's loss)",[](int,int i){return L2v[i];});
    key("(loss1, loss2)",              [](int,int i){return L1v[i]*1000003+L2v[i];});
    key("dx mod 2^16",                 [](int t,int){ return (i64)((((i64)t<<13)%65536+65536)%65536); });
    for(int b=2;b<=18;b+=2){ char nm[80]; snprintf(nm,80,"I1 mod 2^%d",b); int bb=b;
        key(nm,[bb](int,int i){ i64 m=1ll<<bb; return ((I1v[i]%m)+m)%m; }); }
    for(int b=2;b<=12;b+=2){ char nm[80]; snprintf(nm,80,"(I1 mod 2^%d , t&7)",b); int bb=b;
        key(nm,[bb](int t,int i){ i64 m=1ll<<bb; return (((I1v[i]%m)+m)%m)*8 + (t&7); }); }
    key("I1 mod 2^18 (the full loss key)",[](int,int i){ i64 m=1ll<<18; return ((I1v[i]%m)+m)%m; });
    printf("\n  -- for contrast: keys that trivially separate everything --\n");
    key("t itself",[](int t,int){return (i64)t;});
    return 0; }
