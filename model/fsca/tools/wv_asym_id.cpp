// wv_asym_id.cpp -- adversarial verification, part 2: the claimed ALGEBRAIC IDENTITIES.
// Everything integer.  Each identity is checked exhaustively over all 1024 t.
#include "base2.h"
#include <cstdio>
#include <set>
#include <map>
static inline i64 fdiv(i64 a,i64 b){ i64 q=a/b; if((a%b)!=0 && ((a<0)!=(b<0))) q--; return q; }
static inline i64 fmod_(i64 a,i64 b){ i64 r=a%b; if(r<0)r+=b; return r; }
int main(){ build_tables();
    const int gtab[8]={0,5,2,7,4,1,6,3};
    // closed form g(m) = m XOR (4*(m&1))
    for(int m=0;m<8;m++) if(gtab[m]!=(m^(4*(m&1)))) { printf("g closed form FAILS at m=%d\n",m); }
    printf("g(m) = m XOR 4*(m&1) reproduces [0,5,2,7,4,1,6,3]: yes\n");
    int bad1=0,bad2=0,bad3=0,badI=0,badJ=0; std::set<i64> e1set,e2set,sqset,hset;
    for(int t=-512;t<=511;t++){
        i64 dx=(i64)t<<13;
        i64 e1=arr(KC,dx)-KC*dx; e1set.insert(e1);
        int m=((t%8)+8)%8; i64 pred1=-(1ll<<17)-(1ll<<13)*gtab[m];
        if(e1!=pred1) bad1++;
        i64 I1=(arr(KC,dx)+K)>>23;
        i64 e2=arr(I1,dx)-I1*dx; e2set.insert(e2);
        if(e2%(1ll<<13)!=0) bad2++; else { i64 h=-e2/(1ll<<13); hset.insert(h); if(h<0||h>56) bad2++; }
        i64 esq=arr(dx,dx)-dx*dx; sqset.insert(esq);
        if(!(esq==0||esq==-(1ll<<17)||esq==-(1ll<<18))) bad3++;
        // closed forms
        i64 E=48-gtab[m];
        if(I1!=fdiv(KC*(i64)t+E,1024)) badI++;
        i64 J=(arr(I1,dx)+K)>>20;
        i64 h=-e2/(1ll<<13);
        i64 r=fmod_(KC*(i64)t+E,1024);
        i64 c=(E-r)*(i64)t+1024*(64-h);
        if(J!=fdiv(KC*(i64)t*(i64)t+c,131072)) badJ++;
    }
    printf("e1(t) = -2^17 - 2^13*g(t mod 8)            : mismatches %d / 1024   (distinct e1 values %zu)\n",bad1,e1set.size());
    printf("e2(t) = -2^13*h(t), h in [0,56]            : violations %d / 1024   (distinct h %zu, distinct e2 %zu)\n",bad2,hset.size(),e2set.size());
    printf("esq(t) in {0,-2^17,-2^18}                  : violations %d / 1024   (distinct %zu)\n",bad3,sqset.size());
    printf("I1(t) == floor((KC*t + 48 - g)/1024)       : mismatches %d / 1024\n",badI);
    printf("J(t)  == floor((KC*t^2 + c(t))/2^17)       : mismatches %d / 1024\n",badJ);
    printf("  distinct h values: %zu (min %lld max %lld)\n",hset.size(),(long long)*hset.begin(),(long long)*hset.rbegin());
    // e1 symmetry: e1(t)==e1(-t) iff t==0 mod 4 ?
    int agree=0,disagree=0,predfail=0;
    for(int a=1;a<=511;a++){ i64 e1p=arr(KC,(i64)a<<13)-KC*((i64)a<<13), e1m=arr(KC,-((i64)a<<13))-KC*(-((i64)a<<13));
        bool eq=(e1p==e1m); if(eq)agree++;else disagree++; if(eq!=((a%4)==0)) predfail++; }
    printf("e1(t)==e1(-t) for %d of 511 |t|; 'iff t mod 4 == 0' fails %d times\n",agree,predfail);
    // delta generator
    int dbad=0,dn=0; std::map<int,int> dhist;
    for(int a=1;a<=511;a++){ i64 I1p=(arr(KC,(i64)a<<13)+K)>>23, I1m=(arr(KC,-((i64)a<<13))+K)>>23;
        i64 delta=(-I1m)-I1p; dn++; dhist[(int)delta]++;
        int mp=a%8, mm=(8-mp)%8; i64 Ep=48-gtab[mp], Em=48-gtab[mm];
        i64 rho=fmod_(KC*(i64)a,1024);
        i64 pred=(rho>Em?1:0)-(rho>=1024-Ep?1:0);
        if(delta!=pred) dbad++; }
    printf("delta(a) = [rho>E_m] - [rho>=W-E_p]        : mismatches %d / %d   values:",dbad,dn);
    for(auto&kv:dhist) printf(" %d:%d",kv.first,kv.second); puts("");
    return 0; }
