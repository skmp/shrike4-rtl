// v41_wit.cpp -- at the MODEL'S OWN alignment w=13,(s1,s2)=(23,18), is t=17 really incapable?
// Scan KC with K1,K2 constant (exact breakpoint treatment) and print real witnesses of J(17)=45617,
// then show what those witnesses do at t=342 / t=-342 and over the full 1024 offsets.
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
static inline i64 fdv(i64 a,i64 b){ i64 q=a/b,r=a%b; if(r&&((r<0)!=(b<0)))q--; return q; }
static i64 JL[1024],JH[1024];
static void load_win(){ FILE* f=fopen("jwin18.txt","r"); char l[512];
  while(fgets(l,sizeof l,f)){ if(l[0]=='#')continue; long long t,a,b,w; if(sscanf(l,"%lld %lld %lld %lld",&t,&a,&b,&w)!=4)continue; JL[t+512]=a; JH[t+512]=b;} fclose(f);}
static const int w=13,s1=23,s2=18;
static i64 Jof(i64 kc,i64 k1,i64 k2,int t){ i64 dx=(i64)t<<w; i64 I1=fdv(arr(kc,dx)+k1,(i64)1<<s1); return fdv(arr(I1,dx)+k2,(i64)1<<s2); }
int main(){
    build_tables(); load_win();
    i64 P1=(i64)1<<s1,P2=(i64)1<<s2;
    long long n17=0,n17_342=0; int shown=0;
    std::vector<std::pair<i64,std::pair<i64,i64>>> wit;
    for(i64 kc=5000000; kc<=5350000; kc++){
        i64 dx=(i64)17<<w; i64 A1=arr(kc,dx); i64 q1=fdv(A1,P1); i64 m1=A1-q1*P1; i64 thr1=(m1==0)?P1:(P1-m1);
        for(int up=0;up<2;up++){ if(up && thr1>=P1) continue;
            i64 I1=q1+up; i64 klo = up?thr1:0, khi = up?P1-1:thr1-1; if(klo>khi) continue;
            i64 A2=arr(I1,dx); i64 q2=fdv(A2,P2); i64 m2=A2-q2*P2; i64 thr2=(m2==0)?P2:(P2-m2);
            i64 k2=-1;
            if(q2==45617) k2=0; else if(q2+1==45617 && thr2<P2) k2=thr2;
            if(k2<0) continue;
            n17++;
            if(shown<6){ printf("WITNESS J(17)=45617 : KC=%lld K1=%lld K2=%lld  (canonical KC=%lld, ratio %.9f)\n",
                (long long)kc,(long long)klo,(long long)k2,(long long)KC,(double)kc/(double)KC); shown++; }
            wit.push_back({kc,{klo,k2}});
        }
    }
    printf("\nKC values in [5000000,5350000] for which J(17)=45617 is reachable at w=13,(23,18) with a constant (K1,K2): %lld\n",n17);
    // what do they do elsewhere?
    int best=0; i64 bkc=0,bk1=0,bk2=0;
    for(auto&W:wit){ i64 kc=W.first,k1=W.second.first,k2=W.second.second;
        i64 j342=Jof(kc,k1,k2,342), jm=Jof(kc,k1,k2,-342);
        if(j342==18466836) n17_342++;
        int sc=0; for(int t=-512;t<=511;t++){ i64 J=Jof(kc,k1,k2,t); if(J>=JL[t+512]&&J<=JH[t+512]) sc++; }
        if(sc>best){best=sc;bkc=kc;bk1=k1;bk2=k2;} }
    printf("of those, also hitting J(342)=18466836 : %lld\n",n17_342);
    printf("best FULL jwin18 score among them: %d/1024 at KC=%lld K1=%lld K2=%lld\n",best,(long long)bkc,(long long)bk1,(long long)bk2);
    { i64 a=Jof(bkc,bk1,bk2,17),b=Jof(bkc,bk1,bk2,342),c=Jof(bkc,bk1,bk2,-342);
      printf("   its landmarks: J(17)=%lld[45617] J(342)=%lld[18466836] J(-342)=%lld[18466858..18466867]\n",(long long)a,(long long)b,(long long)c); }
    // and the canonical KC for comparison
    { i64 a=Jof(KC,K,K,17),b=Jof(KC,K,K,342),c=Jof(KC,K,K,-342); int sc=0;
      for(int t=-512;t<=511;t++){ i64 J=Jof(KC,K,K,t); if(J>=JL[t+512]&&J<=JH[t+512]) sc++; }
      printf("   canonical KC=0x%llx K=2^19: %d/1024, J(17)=%lld J(342)=%lld J(-342)=%lld\n",(long long)KC,sc,(long long)a,(long long)b,(long long)c); }
    return 0; }
