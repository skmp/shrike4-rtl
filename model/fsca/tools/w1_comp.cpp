// w1_comp.cpp -- the user's two hypotheses, run at full scale with real numbers:
//   (A) t is COMPUTED as a 2's complement but CONSUMED as a 1's complement (or vice versa).
//       A 2's-complement bit pattern B read as 1's complement is worth t+1 when t<0, t when t>=0.
//       The reverse mis-read is worth t-1 when t<0.  Also the sign-magnitude mis-read.
//   (B) t carries ONE EXTRA BIT of precision -- i.e. the offset actually used is t +- 1/2 LSB,
//       equivalently dx = (t<<w) +- 2^(w-1); and the chain-internal version, where the first latch
//       keeps one more bit (s1 -> s1-1, s2 -> s2+1, KC held fixed).
// Scored two ways: (1) against tools/jwin18.txt (the square-path latch window, baseline 918/1024),
// (2) the full model's window violations over nodes 0..31 (baseline 147/148).
#include "base2.h"
#include <cstring>
#include <algorithm>
#include <vector>
static Fsca F; static i64 VL[65536],VH[65536];
static i64 JL[1024],JH[1024];
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};

// --- operand transform: dx' = (t<<w) + a + b*(t<0)
struct Tf { i64 a, b; };
static inline i64 mkdx(int t,int w,Tf f){ return ((i64)t<<w) + f.a + (t<0? f.b:0); }

// placements: 0=linear, 1=cubic, 2=square first multiply, 3=square feedback multiply
struct Cfg { Tf f[4]; int s1, s2; };
static const Cfg BASE = {{{0,0},{0,0},{0,0},{0,0}},23,18};

static inline i64 Jfine(int t,const Cfg&c){ i64 dxA=mkdx(t,13,c.f[2]), dxB=mkdx(t,13,c.f[3]);
    i64 I1=(arr(KC,dxA)+K)>>c.s1; return (arr(I1,dxB)+K)>>c.s2; }

static int score_j(const Cfg&c){ int s=0; for(int t=-512;t<=511;t++){ i64 j=Jfine(t,c);
    if(j>=JL[t+512]&&j<=JH[t+512]) s++; } return s; }

static inline i64 Vmod(int n,int t,int flip,const Cfg&c){ const Tab&T=TB[n];
    i64 dxL=mkdx(t,13,c.f[0]), dxC=mkdx(t,13,c.f[1]);
    i64 T1=T.C1f? sm(arr(T.C1f,dxL)+K,23-T.e1):0;
    i64 A3=((arr(((i64)t*t)<<3,dxC)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
    i64 J=Jfine(t,c);                       // fine scale
    i64 I2=J>>2;                            // back to the shift-20 latch the published model uses
    i64 T2=T.S? sm(arr(I2,T.S)+K,U2TAB[n]):0;
    i64 s=flip?-1:1; return C0TAB[n]+s*T1-T2-s*T3; }

static int score_full(const Cfg&c){ int v=0;
    for(int node=0;node<32;node++){ int flip=node>=17,n=flip?32-node:node;
        for(int t=-512;t<=511;t++){ int a=(node*1024+t)&0xffff; i64 V=Vmod(n,t,flip,c);
            if(V<VL[a]||V>VH[a]) v++; } } return v; }

static void report(const char*name,const Cfg&c){
    int sj=score_j(c), sf=score_full(c);
    printf("  %-52s jwin18 %4d/1024   full %6d   J(17)=%-8lld J(342)=%-10lld J(-342)=%-10lld D=%lld\n",
        name,sj,sf,(long long)Jfine(17,c),(long long)Jfine(342,c),(long long)Jfine(-342,c),
        (long long)(Jfine(-342,c)-Jfine(342,c))); }

int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int a=0;a<65536;a++){ Win w=decode(F.srz[a],F.srn[a]);
        if(w.sign>0){VL[a]=w.lo;VH[a]=w.hi;}else{VL[a]=-w.hi;VH[a]=-w.lo;} }
    { FILE*f=fopen("jwin18.txt","r"); if(!f){puts("no jwin18.txt");return 1;} char ln[256];
      while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h,wd;
        if(sscanf(ln,"%lld %lld %lld %lld",&t,&l,&h,&wd)==4){ JL[t+512]=l; JH[t+512]=h; } } fclose(f); }
    printf("TARGETS: J(17)=45617 exactly, J(342)=18466836 exactly, J(-342) in [18466858,18466867]\n\n");
    printf("BASELINE\n"); report("published model",BASE);

    printf("\n(A) t computed 2's complement, consumed as 1's complement (t<0 -> t+1) and the reverse\n");
    const char* pn[4]={"linear","cubic","square-1st","square-fb"};
    for(int q : {+8192,-8192}){
        printf("  -- t<0 shifted by %+d in t units (dx offset %+d) --\n", q/8192, q);
        for(int mask=1;mask<16;mask++){ Cfg c=BASE; char nm[128]; nm[0]=0; int first=1;
            for(int p=0;p<4;p++) if(mask>>p&1){ c.f[p].b=q; strcat(nm,first?"":"+"); strcat(nm,pn[p]); first=0; }
            report(nm,c); } }

    printf("\n(A2) sign-magnitude mis-read: for t<0 the 10-bit 2's pattern read as 1's-complement magnitude\n");
    { // t<0: pattern B = 1024+t ; 1's-complement magnitude read = -(1023-B) = -(1023-1024-t) = t+1  (same as A)
      // the other sign-magnitude read: -(B & 511) = -(t+512) for t in [-512,-1]
      Cfg c=BASE; int s=0; for(int t=-512;t<=511;t++){ int tt = t<0 ? -( (1024+t) & 511 ) : t;
        i64 dx=(i64)tt<<13; i64 I1=(arr(KC,dx)+K)>>23; i64 j=(arr(I1,dx)+K)>>18;
        if(j>=JL[t+512]&&j<=JH[t+512]) s++; }
      printf("  %-52s jwin18 %4d/1024\n","sign-magnitude read -(B&511) at all placements",s); }

    printf("\n(B) one extra bit of precision in t: dx = (t<<13) + h, h = half an LSB of t\n");
    for(int h : {+4096,-4096}){
        for(int mask=1;mask<16;mask++){ Cfg c=BASE; char nm[160]; snprintf(nm,sizeof nm,"h=%+5d at ",h); int first=1;
            for(int p=0;p<4;p++) if(mask>>p&1){ c.f[p].a=h; strcat(nm,first?"":"+"); strcat(nm,pn[p]); first=0; }
            report(nm,c); } }
    printf("  (sign-dependent half-LSB: toward zero / away from zero)\n");
    for(int sgn : {+1,-1}){ Cfg c=BASE; for(int p=0;p<4;p++){ c.f[p].a=sgn*4096; c.f[p].b=-2*sgn*4096; }
        char nm[80]; snprintf(nm,sizeof nm,"half-LSB %s zero, all placements", sgn>0?"away from":"toward");
        report(nm,c); }

    printf("\n(B2) one extra bit kept by the FIRST chain latch (s1+s2 = 41 held, KC held fixed)\n");
    for(int s1=26;s1>=16;s1--){ Cfg c=BASE; c.s1=s1; c.s2=41-s1; char nm[80];
        snprintf(nm,sizeof nm,"s1=%d s2=%d",s1,41-s1); report(nm,c); }

    printf("\n(B3) extra bit + complement together: best of a in [-8192,8192] step 256, b likewise, all placements\n");
    { int best=-1; i64 ba=0,bb=0;
      for(i64 a=-8192;a<=8192;a+=256) for(i64 b=-8192;b<=8192;b+=256){
          Cfg c=BASE; for(int p=0;p<4;p++){ c.f[p].a=a; c.f[p].b=b; }
          int s=score_j(c); if(s>best){best=s;ba=a;bb=b;} }
      Cfg c=BASE; for(int p=0;p<4;p++){ c.f[p].a=ba; c.f[p].b=bb; }
      char nm[80]; snprintf(nm,sizeof nm,"best a=%+lld b=%+lld of 4225",(long long)ba,(long long)bb);
      report(nm,c); }
    return 0; }
