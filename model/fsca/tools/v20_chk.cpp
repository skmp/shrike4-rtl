// v20_chk.cpp -- sanity checks on the v20 bracket machinery, against brute force.
#include "base2.h"
#include <cstdio>
#include <algorithm>
typedef __int128 i128;
static const i64 L=1ll<<21, KCHI=(1ll<<27)-1;
static inline i64 fsh(i128 v,int s){ return (i64)(v>>s); }
struct Bnd{ i64 lo,hi; };
static inline Bnd Jbound(int t,int w,int s1,int s2,i64 kc,bool fk){
    i128 dx=(i128)t<<w;
    i128 k1lo=fk?0:(i128)K,k1hi=fk?(((i128)1<<s1)-1):(i128)K;
    i128 k2lo=fk?0:(i128)K,k2hi=fk?(((i128)1<<s2)-1):(i128)K;
    i128 base=(i128)kc*dx;
    i64 I1lo=fsh(base-L+k1lo,s1), I1hi=fsh(base+k1hi,s1);
    i128 a=(i128)I1lo*dx,b=(i128)I1hi*dx;
    return Bnd{ fsh(std::min(a,b)-L+k2lo,s2), fsh(std::max(a,b)+k2hi,s2) };
}
static inline i64 Jfix(int t,int w,int s1,int s2,i64 kc){
    i64 dx=(i64)t<<w; i64 I1=(arr(kc,dx)+K)>>s1; return (arr(I1,dx)+K)>>s2; }
int main(){
    // A. monotonicity of the bounds in kc, sampled
    long long bad=0,n=0;
    for(int w=8;w<=18;w++) for(int s1=16;s1<=30;s1++) for(int s2=10;s2<=26;s2++){
        int e=s1+s2-2*w-15; i128 nom = e>=0?((i128)KC<<e):((i128)KC>>(-e)); if(nom<1||nom>KCHI) continue;
        for(int t : {17,342,-342}){
            i64 prevlo=-1,prevhi=-1; bool first=true;
            for(i64 kc=(i64)nom-400;kc<=(i64)nom+400;kc+=7){ if(kc<1) continue;
                Bnd b=Jbound(t,w,s1,s2,kc,false); n++;
                if(!first && (b.lo<prevlo||b.hi<prevhi)) bad++;
                prevlo=b.lo; prevhi=b.hi; first=false; }
        }
    }
    printf("A. bound monotonicity: %lld violations over %lld samples\n",bad,n);

    // B. the bounds really bracket the exact chain
    long long out=0,m=0;
    for(int w=6;w<=20;w++) for(int s1=8;s1<=34;s1++) for(int s2=6;s2<=30;s2++){
        int e=s1+s2-2*w-15; i128 nom=e>=0?((i128)KC<<e):((i128)KC>>(-e)); if(nom<1||nom>KCHI) continue;
        for(int t : {17,342,-342,511,-512,1,-1})
        for(i64 d=-50;d<=50;d+=11){ i64 kc=(i64)nom+d; if(kc<1||kc>KCHI) continue;
            i64 j=Jfix(t,w,s1,s2,kc); Bnd b=Jbound(t,w,s1,s2,kc,false); m++;
            if(j<b.lo||j>b.hi){ out++; if(out<5) printf("   OUT t=%d w=%d s1=%d s2=%d kc=%lld J=%lld bnd=[%lld,%lld]\n",t,w,s1,s2,(long long)kc,(long long)j,(long long)b.lo,(long long)b.hi); } }
    }
    printf("B. exact J outside its bound: %lld / %lld\n",out,m);

    // C. at the baseline triple, brute-force which kc hit each landmark
    int w=13,s1=23,s2=18;
    printf("C. triple (13,23,18), brute force kc in [KC-4000,KC+4000] (KC=%lld):\n",(long long)KC);
    long long c17=0,c342=0,cb=0; i64 f17=-1,f342=-1;
    for(i64 kc=KC-4000;kc<=KC+4000;kc++){
        i64 j17=Jfix(17,w,s1,s2,kc), jp=Jfix(342,w,s1,s2,kc);
        bool a=(j17==45617), b=(jp==18466836);
        if(a){c17++; if(f17<0)f17=kc;} if(b){c342++; if(f342<0)f342=kc;} if(a&&b)cb++;
    }
    printf("   kc hitting J(17)=45617   : %lld  (first %lld)\n",c17,(long long)f17);
    printf("   kc hitting J(342)=18466836: %lld  (first %lld)\n",c342,(long long)f342);
    printf("   kc hitting BOTH          : %lld\n",cb);
    { Bnd lo1=Jbound(17,w,s1,s2,KC-4000,false), hi1=Jbound(17,w,s1,s2,KC+4000,false);
      printf("   bound J(17) at kc=KC-4000: [%lld,%lld] ; at KC+4000: [%lld,%lld]\n",
        (long long)lo1.lo,(long long)lo1.hi,(long long)hi1.lo,(long long)hi1.hi);
      Bnd lo2=Jbound(342,w,s1,s2,KC-4000,false), hi2=Jbound(342,w,s1,s2,KC+4000,false);
      printf("   bound J(342) at kc=KC-4000: [%lld,%lld] ; at KC+4000: [%lld,%lld]\n",
        (long long)lo2.lo,(long long)lo2.hi,(long long)hi2.lo,(long long)hi2.hi); }
    return 0;
}
