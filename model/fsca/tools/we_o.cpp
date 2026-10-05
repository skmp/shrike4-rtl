// we_o.cpp -- the OTHER square-chain orders, scored over all nodes 0..31 against the V windows.
// Guard: nodes 0,1,2,16 must stay exact (they are under the current model).
// Orders (left-associated, operands {KC,S,dx,dx}):
//  0: ((KC*dx)*dx)*S   (current)      1: ((KC*dx)*S)*dx     2: ((KC*S)*dx)*dx
//  3: ((dx*dx)*KC)*S                  4: ((dx*dx)*S)*KC     5: ((S*dx)*KC)*dx   6: ((S*dx)*dx)*KC
//  7: (KC*dx)*(S*dx)  (balanced)
// The scale constraint is  s1+s2+s3 = 73  for every order (T2 = KC t^2 S / 2^47, dx = t<<13).
#include "we_win.h"
#include <cstring>
#include <algorithm>
static inline i64 lat(i64 P,int s,int rule){ switch(rule){case 0:return P>>s; case 1:return P>=0?(P>>s):-((-P)>>s); default:return P>=0?(P>>s):-((~P)>>s);} }
struct Inst{int n,flip;i64 lo,hi;}; static std::vector<Inst> ins[1024];
int main(int argc,char**argv){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    for(int t=-512;t<=511;t++) for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node;
        int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex); ins[t+512].push_back({n,flip,lo,hi}); }
    i64 Kc[4]={1ll<<19,0,1ll<<18,1ll<<20};
    struct Cfg{int ord,s1,s2,k1,k2,k3,r1,r2,r3,u1,u2;};
    std::vector<Cfg> cfgs;
    for(int ord=0;ord<8;ord++) for(int s1=8;s1<=40;s1++) for(int s2=1;s2<=40;s2++){ int s3=73-s1-s2; if(s3<8||s3>40) continue;
        for(int k1=0;k1<4;k1++) for(int k2=0;k2<4;k2++) for(int k3=0;k3<1;k3++)
          for(int r1=0;r1<2;r1++) for(int r2=0;r2<2;r2++) for(int r3=0;r3<2;r3++) for(int u1=0;u1<2;u1++) for(int u2=0;u2<2;u2++){
            if(Kc[k1]>=(1ll<<s1)||Kc[k2]>=(1ll<<s2)) continue; cfgs.push_back({ord,s1,s2,k1,k2,k3,r1,r2,r3,u1,u2}); } }
    printf("configs %zu\n",cfgs.size());
    std::vector<int> guard(cfgs.size(),-1), full(cfgs.size(),-1);
    // T2 for node table n, offset t
    auto T2of=[&](const Cfg&c,int n,int t)->i64{ const Tab&T=TB[n]; if(!T.S) return 0; i64 dx=(i64)t<<13;
        int kn=U2TAB[n]-30;                       // the sine field's octave: it rides on the latch that consumes S
        int s1=c.s1,s2=c.s2,s3=73-c.s1-c.s2; i64 S=T.S; i64 A,B,C2;
        switch(c.ord){ case 0: case 1: break; default: break; }
        switch(c.ord){ case 0: s3+=kn; break; case 1: s2+=kn; break; case 2: s1+=kn; break;
                       case 3: s3+=kn; break; case 4: s2+=kn; break; case 5: s1+=kn; break;
                       case 6: s1+=kn; break; default: s2+=kn; break; }
        switch(c.ord){
          case 0: A=lat((c.r1?arr(dx,KC):arr(KC,dx))+Kc[c.k1],s1,c.u1); B=lat((c.r2?arr(dx,A):arr(A,dx))+Kc[c.k2],s2,c.u2); return sm((c.r3?arr(S,B):arr(B,S))+K,s3);
          case 1: A=lat((c.r1?arr(dx,KC):arr(KC,dx))+Kc[c.k1],s1,c.u1); B=lat((c.r2?arr(S,A):arr(A,S))+Kc[c.k2],s2,c.u2); return sm((c.r3?arr(dx,B):arr(B,dx))+K,s3);
          case 2: A=lat((c.r1?arr(S,KC):arr(KC,S))+Kc[c.k1],s1,c.u1); B=lat((c.r2?arr(dx,A):arr(A,dx))+Kc[c.k2],s2,c.u2); return sm((c.r3?arr(dx,B):arr(B,dx))+K,s3);
          case 3: A=lat(arr(dx,dx)+Kc[c.k1],s1,c.u1); B=lat((c.r2?arr(KC,A):arr(A,KC))+Kc[c.k2],s2,c.u2); return sm((c.r3?arr(S,B):arr(B,S))+K,s3);
          case 4: A=lat(arr(dx,dx)+Kc[c.k1],s1,c.u1); B=lat((c.r2?arr(S,A):arr(A,S))+Kc[c.k2],s2,c.u2); return sm((c.r3?arr(KC,B):arr(B,KC))+K,s3);
          case 5: A=lat((c.r1?arr(dx,S):arr(S,dx))+Kc[c.k1],s1,c.u1); B=lat((c.r2?arr(KC,A):arr(A,KC))+Kc[c.k2],s2,c.u2); return sm((c.r3?arr(dx,B):arr(B,dx))+K,s3);
          case 6: A=lat((c.r1?arr(dx,S):arr(S,dx))+Kc[c.k1],s1,c.u1); B=lat((c.r2?arr(dx,A):arr(A,dx))+Kc[c.k2],s2,c.u2); return sm((c.r3?arr(KC,B):arr(B,KC))+K,s3);
          default: A=lat((c.r1?arr(dx,KC):arr(KC,dx))+Kc[c.k1],s1,c.u1); C2=lat((c.r2?arr(dx,S):arr(S,dx))+Kc[c.k2],s2,c.u2); return sm((c.r3?arr(C2,A):arr(A,C2))+K,s3); } };
    auto Vof=[&](const Cfg&c,int n,int t,int flip)->i64{ const Tab&T=TB[n]; i64 dx=(i64)t<<13;
        i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
        i64 s=flip?-1:1; return C0TAB[n]+s*T1-T2of(c,n,t)-s*T3; };
    #pragma omp parallel for schedule(dynamic,64)
    for(size_t ci=0;ci<cfgs.size();ci++){ const Cfg&c=cfgs[ci]; int g=0;
        for(int node : {1,2,16}){ int flip=0,n=node; for(int t=-512;t<=511;t++){ i64 V=Vof(c,n,t,flip);
            auto&L=ins[t+512]; for(auto&I:L) if(I.n==n&&I.flip==flip){ if(V<I.lo||V>I.hi)g++; break; } } if(g>3) break; }
        guard[ci]=g;
        if(g) continue;
        int tot=0; for(int t=-512;t<=511;t++) for(auto&I:ins[t+512]){ i64 V=Vof(c,I.n,t,I.flip); if(V<I.lo||V>I.hi)tot++; }
        full[ci]=tot; }
    std::vector<size_t> idx; for(size_t i=0;i<cfgs.size();i++) if(full[i]>=0) idx.push_back(i);
    printf("configs exact on nodes 1,2,16: %zu\n",idx.size());
    std::sort(idx.begin(),idx.end(),[&](size_t a,size_t b){return full[a]<full[b];});
    printf("viol | ord s1 s2 s3 K1     K2    r1 r2 r3 u1 u2\n");
    for(int i=0;i<25&&i<(int)idx.size();i++){ const Cfg&c=cfgs[idx[i]];
        printf("%4d |  %d  %2d %2d %2d %6lld %6lld  %d  %d  %d  %d  %d\n",full[idx[i]],c.ord,c.s1,c.s2,73-c.s1-c.s2,
            (long long)Kc[c.k1],(long long)Kc[c.k2],c.r1,c.r2,c.r3,c.u1,c.u2); }
    return 0; }
