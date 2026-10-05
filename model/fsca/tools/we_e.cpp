// we_e.cpp -- exact characterisation of every array truncation loss in the square chain.
//   e1(t) = arr(KC,dx) - KC*dx        (dx = t<<13)
//   e2(t) = arr(I1,dx) - I1*dx
//   esq(t)= arr(dx,dx) - dx*dx
// Prints the value sets, their dependence on t mod 8 / sign, and the exact algebraic form of I1 and J.
#include "we_win.h"
#include <cstring>
#include <algorithm>
#include <map>
#include <set>
int main(){ if(!load_fsca(WF))return 1; build_tables();
    std::map<i64,int> s1,s2,ssq; std::map<int,std::set<i64>> bymod8p,bymod8m;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
        i64 e1=arr(KC,dx)-(i64)KC*dx; s1[e1]++;
        i64 I1=(arr(KC,dx)+K)>>23; i64 e2=arr(I1,dx)-I1*dx; s2[e2]++;
        i64 esq=arr(dx,dx)-dx*dx; ssq[esq]++;
        if(t>0) bymod8p[t&7].insert(e1); else if(t<0) bymod8m[((-t)&7)].insert(e1); }
    printf("e1 values (%zu distinct): ",s1.size()); for(auto&p:s1)printf(" %lld/2^13=%lld(x%d)",(long long)p.first,(long long)(p.first/8192),p.second); puts("");
    printf("e1 by |t| mod 8 -> {e1/2^13}: \n");
    for(int m=0;m<8;m++){ printf("  m=%d  t>0:",m); for(i64 v:bymod8p[m])printf(" %lld",(long long)(v/8192));
        printf("   t<0:"); for(i64 v:bymod8m[m])printf(" %lld",(long long)(v/8192)); puts(""); }
    printf("e2 values (%zu distinct), all /2^13? ",s2.size()); { bool all=true; for(auto&p:s2) if(p.first%8192)all=false; printf("%s\n",all?"yes":"NO"); }
    { i64 mn=1ll<<62,mx=-(1ll<<62); for(auto&p:s2){mn=std::min(mn,p.first);mx=std::max(mx,p.first);} printf("   e2 range [%lld,%lld] = [%.2f,%.2f]*2^13\n",(long long)mn,(long long)mx,mn/8192.0,mx/8192.0); }
    printf("esq values (%zu distinct):",ssq.size()); for(auto&p:ssq)printf(" %lld(x%d)",(long long)p.first,p.second); puts("");
    // verify the algebraic form of I1:  I1 = floor((KC*t + 48 - k)/1024)  with e1 = -(2^17 + k*2^13)
    int bad=0; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 e1=arr(KC,dx)-(i64)KC*dx;
        i64 num=(i64)KC*t*8192 + e1 + K; i64 I1a = num>>23;   // arithmetic shift = floor
        if(I1a != ((arr(KC,dx)+K)>>23)) bad++; }
    printf("I1 algebraic form check: %d mismatches\n",bad);
    // and J
    int bad2=0; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23;
        i64 e2=arr(I1,dx)-I1*dx; i64 J=(I1*dx+e2+K)>>20;
        if(J!=((arr(I1,dx)+K)>>20)) bad2++; }
    printf("J algebraic form check: %d mismatches\n",bad2);
    // e1 as a pure function of (t mod 8, sign)?
    { bool f=true; i64 tab[2][8]; bool set_[2][8]={};
      for(int t=-512;t<=511;t++){ if(!t)continue; i64 dx=(i64)t<<13; i64 e1=arr(KC,dx)-(i64)KC*dx; int sg=t>0?0:1,m=((t%8)+8)%8;
        if(!set_[sg][m]){tab[sg][m]=e1;set_[sg][m]=true;} else if(tab[sg][m]!=e1) f=false; }
      printf("e1 is a pure function of (sign, t mod 8): %s\n", f?"YES":"no");
      if(f){ printf("   t mod 8 :"); for(int m=0;m<8;m++)printf(" %4d",m); puts("");
             printf("   t>0  k  :"); for(int m=0;m<8;m++)printf(" %4lld",(long long)(-(tab[0][m])/8192-16)); puts("");
             printf("   t<0  k  :"); for(int m=0;m<8;m++)printf(" %4lld",(long long)(-(tab[1][m])/8192-16)); puts(""); } }
    return 0; }
