// wb_i1.cpp -- exact inversion of the second chain multiply at a chosen (s1,s2), giving the allowed
// interval for the first latch I1 per t; then test first-stage rules against it.
// Also measures the array's sign asymmetry e(t) = -arr(KC,-dx) - arr(KC,dx).
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
static i64 JL[1024],JH[1024]; static int OKT[1024];
static void load_jwin(const char*p){ FILE*f=fopen(p,"r"); if(!f){puts("no jwin");exit(1);} int t;long long a,b;int o;
    while(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&o)==4){JL[t+512]=a;JH[t+512]=b;OKT[t+512]=o;} fclose(f); }
int main(int argc,char**argv){ build_tables(); load_jwin("jwin.txt");
    // (1) array sign asymmetry of the first product
    i64 emin=1ll<<62,emax=-(1ll<<62); double esum=0; int ecnt=0;
    for(int t=1;t<=512;t++){ i64 dx=(i64)t<<13; i64 e = -arr(KC,-dx) - arr(KC,dx);
        if(e<emin)emin=e; if(e>emax)emax=e; esum+=e; ecnt++; }
    printf("(1) e(t) = -arr(KC,-dx)-arr(KC,dx):  min %lld  max %lld  mean %.1f   (2^19=%d, 2^20=%d)\n",
        (long long)emin,(long long)emax,esum/ecnt,1<<19,1<<20);
    for(int t=1;t<=8;t++){ i64 dx=(i64)t<<13; printf("    t=%d  arr(+)=%lld arr(-)=%lld e=%lld\n",t,
        (long long)arr(KC,dx),(long long)arr(KC,-dx),(long long)(-arr(KC,-dx)-arr(KC,dx))); }
    // (2) invert stage 2 for a grid of (s1,s2,K2,role)
    printf("\n(2) allowed I1 intervals; rules tested: A=floor((arr(KC,dx)+K1)>>s1)  B=sm(arr(KC,dx)+K1,s1)\n");
    printf("s1 s2 role K2      nonempty  bestA(K1)   bestB(K1)\n");
    for(int s1=21;s1<=27;s1++){ int s2=43-s1;
      for(int role=0;role<2;role++) for(int k2i=0;k2i<4;k2i++){
        i64 K2 = k2i==0?0 : k2i==1?(1ll<<19) : k2i==2?(1ll<<(s2-1)) : (1ll<<(s2>1?s2-2:0));
        if(s2<1) continue;
        std::vector<i64> lo(1024),hi(1024); std::vector<char> ok(1024,0); int nonempty=0;
        for(int t=-512;t<=511;t++){ if(!OKT[t+512])continue; i64 dx=(i64)t<<13;
            long double c=(long double)KC*(long double)t*8192.0L/powl(2.0L,(long double)s1);
            i64 lo0=(i64)floorl(c)-3000, hi0=(i64)ceill(c)+3000; i64 f=0,g=0; bool any=false;
            for(i64 v=lo0;v<=hi0;v++){ i64 P=role?arr(dx,v):arr(v,dx); i64 J=(P+K2)>>s2;
                if(J>=JL[t+512]&&J<=JH[t+512]){ if(!any){f=v;any=true;} g=v; } }
            if(any){nonempty++; lo[t+512]=f; hi[t+512]=g; ok[t+512]=1;} }
        int bA=-1,bAk=0,bB=-1,bBk=0;
        for(int k1e=-1;k1e<=24;k1e++){ i64 K1 = k1e<0?0:(1ll<<k1e);
            int hA=0,hB=0; for(int t=-512;t<=511;t++){ if(!ok[t+512])continue; i64 dx=(i64)t<<13; i64 P=arr(KC,dx)+K1;
                i64 IA=P>>s1, IB=sm(P,s1);
                if(IA>=lo[t+512]&&IA<=hi[t+512])hA++;
                if(IB>=lo[t+512]&&IB<=hi[t+512])hB++; }
            if(hA>bA){bA=hA;bAk=k1e;} if(hB>bB){bB=hB;bBk=k1e;} }
        printf("%2d %2d  %d  %-10lld %5d     %4d (2^%d)  %4d (2^%d)\n",s1,s2,role,(long long)K2,nonempty,bA,bAk,bB,bBk);
      } }
    return 0; }
