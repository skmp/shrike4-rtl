// wb_k2.cpp -- same inversion as wb_k1 but the K1 sweep is done SEPARATELY for t>0 and t<0.
// If the two sides want different rounding constants, the first latch is sign-magnitude, and the
// relation between the two constants names the convention.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024]; static int OKT[1024];
static void sweep(std::vector<std::pair<i64,i64>>&kv,i64&bl,i64&bh,int&best){
    std::vector<std::pair<i64,int>> ev; for(auto&p:kv){ ev.push_back({p.first,1}); ev.push_back({p.second+1,-1}); }
    std::sort(ev.begin(),ev.end()); int cur=0; best=0; bl=bh=0;
    for(size_t i=0;i<ev.size();i++){ cur+=ev[i].second; if(cur>best&&i+1<ev.size()){best=cur;bl=ev[i].first;bh=ev[i+1].first-1;} } }
int main(int argc,char**argv){ build_tables();
    FILE*f=fopen("jwin.txt","r"); int t;long long a,b;int o;
    while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){JL[t+512]=a;JH[t+512]=b;OKT[t+512]=o;} fclose(f);
    printf("s1 s2 K2      role | reach+ reach- | bestK1+ (cov)          bestK1- (cov)        total\n");
    for(int s1=21;s1<=25;s1++){ int s2=43-s1; for(int role=0;role<2;role++) for(int k2i=0;k2i<3;k2i++){
        i64 K2=k2i==0?0:(k2i==1?(1ll<<19):(1ll<<(s2-1)));
        std::vector<std::pair<i64,i64>> kp,kn; int rp=0,rn=0,np=0,nn=0;
        for(int tt=-512;tt<=511;tt++){ if(!OKT[tt+512])continue; if(tt>0)np++; else if(tt<0)nn++;
            i64 dx=(i64)tt<<13;
            long double c=(long double)KC*(long double)tt*8192.0L/powl(2.0L,(long double)s1);
            i64 lo0=(i64)floorl(c)-4000, hi0=(i64)ceill(c)+4000; i64 fi=0,gi=0; bool any=false;
            for(i64 v=lo0;v<=hi0;v++){ i64 P=role?arr(dx,v):arr(v,dx); i64 J=(P+K2)>>s2;
                if(J>=JL[tt+512]&&J<=JH[tt+512]){ if(!any){fi=v;any=true;} gi=v; } }
            if(!any) continue; if(tt>0)rp++; else if(tt<0)rn++;
            i64 P1=arr(KC,dx);
            auto iv=std::make_pair(fi*(1ll<<s1)-P1,(gi+1)*(1ll<<s1)-1-P1);
            if(tt>0)kp.push_back(iv); else if(tt<0)kn.push_back(iv); }
        i64 bl1,bh1,bl2,bh2; int c1,c2; sweep(kp,bl1,bh1,c1); sweep(kn,bl2,bh2,c2);
        printf("%2d %2d %-8lld %d | %4d %4d | [%9lld,%9lld] %3d/%3d  [%10lld,%10lld] %3d/%3d  %4d\n",
            s1,s2,(long long)K2,role,rp,rn,(long long)bl1,(long long)bh1,c1,np,(long long)bl2,(long long)bh2,c2,nn,c1+c2);
      } }
    return 0; }
