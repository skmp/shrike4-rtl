// m2_tform.cpp -- part A: does a 1s-complement negate of the square operand give the RIGHT ASYMMETRY?
//                 part B: generalise the operand SHIFT width w (dx = t<<w) with the compensating
//                 scale law s1+s2 = 41 + 2(w-13), since that changes the granularity of a dx bias.
#include "base2.h"
#include <cstdio>
#include <vector>
static i64 JL[1024],JH[1024]; static bool have[1024];
static inline i64 shf(i64 v,int u,int m){ return m? sm(v,u) : (v>>u); }
struct Cfg{ int w,s1,s2,p,q,r,m1,m2; };
static inline i64 chainJ(int t,const Cfg&c){
    i64 dx=((i64)t<<c.w)+c.p+(t<0?c.q:0);
    i64 I1=shf(arr(KC,dx)+K,c.s1,c.m1); if(t<0) I1+=c.r;
    return shf(arr(I1,dx)+K,c.s2,c.m2);
}
int main(){
    FILE*f=fopen("jwin18.txt","r"); char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){ int i=(int)t+512; JL[i]=l;JH[i]=h;have[i]=true; } }
    fclose(f);
    printf("=== A. the 1s-complement negate, on its own (w=13, s1=23, s2=18) ===\n");
    printf("need: J(17)=45617   J(342)=18466836   J(-342) in [%lld,%lld]   so D in [22,31]\n",
        (long long)JL[512-342],(long long)JH[512-342]);
    for(int q=-3;q<=1;q++){ Cfg c{13,23,18,0,q,0,0,0};
        i64 jp=chainJ(342,c), jm=chainJ(-342,c);
        printf("  q=%+d (dx for t<0 shifted by %+d): J(342)=%lld  J(-342)=%lld  D=%+lld %s\n",
            q,q,(long long)jp,(long long)jm,(long long)(jm-jp),
            (jm>=JL[512-342]&&jm<=JH[512-342])?"  <-- J(-342) IN WINDOW":"");
    }
    printf("\n=== B. generalised operand width + shift split ===\n");
    int tot13=41; long long prim=0; std::vector<Cfg> hits;
    for(int w=8;w<=22;w++){ int tot=tot13+2*(w-13);
      for(int s1=10;s1<=34;s1++){ int s2=tot-s1; if(s2<6||s2>34) continue;
        for(int m1=0;m1<2;m1++) for(int m2=0;m2<2;m2++){
          for(int p=-8192;p<=8192;p++){ Cfg c{w,s1,s2,p,0,0,m1,m2}; prim++;
            i64 j17=chainJ(17,c); if(j17!=JL[512+17]||j17!=JH[512+17]) continue;
            i64 jp=chainJ(342,c); if(jp<JL[512+342]||jp>JH[512+342]) continue;
            for(int q=-256;q<=256;q++) for(int r=-256;r<=256;r++){
                c.q=q;c.r=r; i64 jm=chainJ(-342,c);
                if(jm>=JL[512-342]&&jm<=JH[512-342]) hits.push_back(c);
            }
          } } } }
    printf("primary configs (w,s1,s2,mode,p) tried: %lld ; passing t=17 AND t=342 AND t=-342: %zu\n",prim,hits.size());
    int best=-1; Cfg bc{};
    for(auto&c:hits){ int sc=0; for(int i=0;i<1024;i++) if(have[i]){ i64 j=chainJ(i-512,c); if(j>=JL[i]&&j<=JH[i]) sc++; }
        if(sc>best){best=sc;bc=c;} }
    if(best>=0) printf("BEST full score %d / 1024  at w=%d s1=%d s2=%d p=%d q=%d r=%d shifts=(%s,%s)\n",
        best,bc.w,bc.s1,bc.s2,bc.p,bc.q,bc.r,bc.m1?"sm":">>",bc.m2?"sm":">>");
    else printf("none.\n");
    return 0; }
