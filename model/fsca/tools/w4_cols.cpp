// w4_cols.cpp -- BRANCH 4 (c): let the TRUNCATION ALIGNMENT vary, in the square path only.
//  exp 1: the array's own column boundaries -- rows truncated below column C (default 16), hot ones from
//         column H (default 18), and the two group rules' columns shifted or switched off -- chosen
//         independently for the two square-path multiplies.  (FIPR's own use keeps the validated array.)
//  exp 2: the OPERAND shifted relative to fixed columns: dx presented as t<<(13+j) with the latch shift
//         compensated, independently for each multiply.  KC held fixed, J's scale unchanged.
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
#include <omp.h>
static i64 JL[1024],JH[1024]; static bool have[1024];
static void load(){ FILE*f=fopen("jwin18.txt","r"); char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){int i=(int)t+512;JL[i]=l;JH[i]=h;have[i]=true;} } fclose(f); }
struct AV{int C,H,gA,gB;};
static inline i64 arrv(i64 rec,i64 mul,const AV&v){ i64 R[14]; i64 T=0;
    for(int k=0;k<14;k++){ auto bit=[&](int b)->int{ return b<0?0:(int)((rec>>(b>62?62:b))&1); };
        int d=-2*bit(2*k+1)+bit(2*k)+bit(2*k-1); i64 mag=(i64)(d<0?-d:d)*mul; i64 row=d<0?~mag:mag;
        R[k]=row*(1ll<<(2*k)); T += (R[k]>>v.C)<<v.C; if(d<0 && 2*k>=v.H) T += 1ll<<(2*k); }
    if(v.gA>-3){ int c=v.C-1+v.gA; int m=(int)(((R[0]>>c)&1)+((R[1]>>c)&1)+((R[2]>>c)&1)); if(m>=2) T += 1ll<<v.C; }
    if(v.gB>-3){ int c=v.C+v.gB; int p=(int)(((R[6]>>c)^(R[7]>>c)^(R[8]>>c))&1); if(p) T -= 1ll<<v.C; }
    return T; }
static inline i64 Jv(int t,const AV&a,const AV&b){ i64 dx=(i64)t<<13;
    i64 I1=(arrv(KC,dx,a)+K)>>23; return (arrv(I1,dx,b)+K)>>18; }
int main(){ load();
    { AV d{16,18,0,0}; int s=0; for(int i=0;i<1024;i++) if(have[i]){ i64 j=Jv(i-512,d,d); if(j>=JL[i]&&j<=JH[i])s++; }
      printf("sanity: default array (C=16,H=18,both group rules) scores %d/1024 and J(17)=%lld (baseline 918 / 45630)\n\n",
        s,(long long)Jv(17,d,d)); }
    std::vector<AV> AVS; for(int C=13;C<=19;C++) for(int H=15;H<=21;H++) for(int gA=-3;gA<=2;gA++) for(int gB=-3;gB<=2;gB++) AVS.push_back({C,H,gA,gB});
    printf("exp 1: %zu array variants per multiply, %zu combinations, full 1024 scoring\n",AVS.size(),AVS.size()*AVS.size());
    int best=0; AV ba{},bb{}; long long n17=0,n3=0; int bestland=0; AV la{},lb{};
    #pragma omp parallel
    { int lbest=0; AV lba{},lbb{}; long long l17=0,l3=0;
      #pragma omp for schedule(dynamic)
      for(size_t ia=0;ia<AVS.size();ia++) for(size_t ib=0;ib<AVS.size();ib++){
        const AV&a=AVS[ia]; const AV&b=AVS[ib];
        i64 j17=Jv(17,a,b); if(j17==45617) l17++;
        i64 ja=Jv(342,a,b), jb=Jv(-342,a,b);
        bool land = (j17==45617)&&(ja>=JL[854]&&ja<=JH[854])&&(jb>=JL[170]&&jb<=JH[170]);
        if(land) l3++;
        int sc=0; for(int i=0;i<1024;i++) if(have[i]){ i64 v=Jv(i-512,a,b); if(v>=JL[i]&&v<=JH[i])sc++; }
        if(sc>lbest){lbest=sc;lba=a;lbb=b;} }
      #pragma omp critical
      { if(lbest>best){best=lbest;ba=lba;bb=lbb;} n17+=l17; n3+=l3; } }
    printf("  variants hitting J(17)=45617 : %lld   hitting ALL THREE landmarks : %lld\n",n17,n3);
    printf("  best full score %d/1024 at  mul1(C=%d,H=%d,gA=%d,gB=%d)  mul2(C=%d,H=%d,gA=%d,gB=%d)\n",
        best,ba.C,ba.H,ba.gA,ba.gB,bb.C,bb.H,bb.gA,bb.gB);
    printf("  its landmarks: J(17)=%lld  J(342)=%lld  J(-342)=%lld\n",(long long)Jv(17,ba,bb),(long long)Jv(342,ba,bb),(long long)Jv(-342,ba,bb));
    (void)bestland;(void)la;(void)lb;
    printf("\nexp 2: operand shifted relative to fixed columns, dx = t<<(13+j), latch shift compensated\n");
    int b2=0,bj1=0,bj2=0;
    for(int j1=-6;j1<=6;j1++) for(int j2=-6;j2<=6;j2++){
        int s1=23+j1, s2=18+j2; if(s1<1||s2<1) continue;
        auto J=[&](int t)->i64{ i64 d1=(i64)t<<(13+j1), d2=(i64)t<<(13+j2);
            i64 I1=(arr(KC,d1)+K)>>s1; return (arr(I1,d2)+K)>>s2; };
        int sc=0; for(int i=0;i<1024;i++) if(have[i]){ i64 v=J(i-512); if(v>=JL[i]&&v<=JH[i])sc++; }
        if(sc>b2){b2=sc;bj1=j1;bj2=j2;}
        if(J(17)==45617) printf("   j1=%+d j2=%+d HITS J(17)=45617  score %d  J(342)=%lld J(-342)=%lld\n",j1,j2,sc,(long long)J(342),(long long)J(-342));
        if(j1>=-2&&j1<=2&&j2>=-2&&j2<=2) printf("   j1=%+d j2=%+d : %4d/1024   J(17)=%lld  J(342)=%lld\n",j1,j2,sc,(long long)J(17),(long long)J(342)); }
    printf("  best %d/1024 at j1=%+d j2=%+d\n",b2,bj1,bj2);
    return 0; }
