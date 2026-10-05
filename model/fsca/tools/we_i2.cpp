// we_i2.cpp -- necessary-condition filter on the SECOND multiply of the square chain.
// For each candidate second stage  J = latch(arr(rec,mul) + K2, s2)  with {rec,mul} = {I1, t<<a},
// count the t for which NO integer I1 makes J land in the allowed window.  That count is a hard
// upper bound on the score: score <= 1022 - empty.  Then, for the best ones, fit I1.
#include "we_win.h"
#include <cstring>
#include <algorithm>
#include <cmath>
int main(){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    struct R{int a,b,s1,s2,role,rule,k2i;int empty;};
    std::vector<i64> K2c={0,1ll<<19,1ll<<18,1ll<<17,1ll<<16,1ll<<20};
    std::vector<R> out;
    for(int a=10;a<=16;a++) for(int b=10;b<=16;b++) for(int s1=14;s1<=32;s1++){ int s2=17+a+b-s1; if(s2<8||s2>34) continue;
        for(int role=0;role<2;role++) for(int rule=0;rule<3;rule++) for(int k2i=0;k2i<(int)K2c.size();k2i++){
            i64 K2=K2c[k2i]; if(K2>=(1ll<<s2)) continue;
            int empty=0;
            for(int t=-512;t<=511;t++){ if(!JOK[t+512]) continue; i64 dx=(i64)t<<a;
                long double ctr=(long double)KC*t*powl(2.0L,b)/powl(2.0L,s1); i64 c=(i64)llroundl(ctr);
                i64 lo2=std::max((i64)0,c-6000), hi2=c+6000; bool any=false;
                for(i64 v=lo2;v<=hi2&&!any;v++){ i64 P=(role? arr(dx,v):arr(v,dx))+K2; i64 J;
                    switch(rule){case 0: J=P>>s2;break; case 1: J=P>=0?(P>>s2):-((-P)>>s2);break; default: J=P>=0?(P>>s2):-((~P)>>s2);} 
                    if(J>=JLo[t+512]&&J<=JHi[t+512]) any=true; }
                if(!any) empty++; }
            out.push_back({a,b,s1,s2,role,rule,k2i,empty}); } }
    std::sort(out.begin(),out.end(),[](const R&x,const R&y){return x.empty<y.empty;});
    printf("configs %zu ; best (fewest t with NO feasible I1):\n",out.size());
    printf("empty | a  b  s1 s2 role rule K2\n");
    int shown=0; for(auto&r:out){ if(shown++>=40) break;
        printf("%5d | %2d %2d %2d %2d  %d    %d   %lld\n",r.empty,r.a,r.b,r.s1,r.s2,r.role,r.rule,(long long)K2c[r.k2i]); }
    return 0; }
