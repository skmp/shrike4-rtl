// wv_feedback_j.cpp -- INDEPENDENT adversarial verification of the claimed FSCA square-latch J rules.
// Written from the claim text alone.  Uses only base2.h (the established shared model) + the dump.
// Everything integer.  No wb_* file is read or included.
#include "base2.h"
#include <cstring>
#include <cstdio>
#include <vector>
#include <algorithm>

static Fsca F;
static i64 C0T[17]; static int U2T[17];

static inline void win_of(int a, i64& lo, i64& hi, bool& ex){ Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} ex=w.exact; }

// ---------------- J rules (all global constants; no per-node or per-t freedom) ----------------
static i64 J_base(int t){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; return (arr(I1,dx)+K)>>20; }
static i64 J_base_nok2(int t){ i64 dx=(i64)t<<13; i64 I1=(arr(KC,dx)+K)>>23; return arr(I1,dx)>>20; }
// claimed chain, config A/B: L=(arr(A,dx)+K1)>>s1 (floor); J=sm(arr(L,dx)+K2,s2)
static i64 J_cA(int t){ i64 dx=(i64)t<<13; i64 L=(arr(20694294,dx) + (i64)-3276800) >> 25; return sm(arr(L,dx)+0,20); }
static i64 J_cB(int t){ i64 dx=(i64)t<<13; i64 L=(arr(41388588,dx) + (i64)-6488064) >> 26; return sm(arr(L,dx)+0,20); }
static i64 J_cC(int t){ i64 dx=(i64)t<<13; i64 L=(arr(dx,82777175) + (i64)-15859712) >> 27; return sm(arr(L,dx)+0,20); }
// claimed "best clean symmetric rule": J = arr(2t^2, 2586787) >> 17, 2t^2 = (arr(dx,dx)+2^19)>>25
static i64 J_sym(int t){ i64 dx=(i64)t<<13; i64 q=(arr(dx,dx)+K)>>25; return arr(q,2586787)>>17; }
static i64 J_sym_sw(int t){ i64 dx=(i64)t<<13; i64 q=(arr(dx,dx)+K)>>25; return arr(2586787,q)>>17; }
// exact rational floor(KC t^2 / 2^17) reference
static i64 J_rat(int t){ return (KC*(i64)t*(i64)t)>>17; }

struct Rule { const char* name; i64 (*f)(int); };
static Rule RULES[] = {
 {"baseline chain  (arr(KC,dx)+K)>>23 ; (arr(I1,dx)+K)>>20", J_base},
 {"baseline chain, K2 dropped",                              J_base_nok2},
 {"claim A  A=20694294 K1=-3276800 s1=25 s2=20 K2=0",        J_cA},
 {"claim B  A=41388588 K1=-6488064 s1=26 s2=20 K2=0",        J_cB},
 {"claim C  A=82777175 K1=-15859712 s1=27 s2=20 (swapped)",  J_cC},
 {"claim sym  arr(2t^2,2586787)>>17",                        J_sym},
 {"claim sym (roles swapped)",                               J_sym_sw},
 {"exact rational floor(KC t^2/2^17)",                       J_rat},
};
static const int NR = sizeof(RULES)/sizeof(RULES[0]);

// ---------------- step 1: my own C0/u2 fit (same criterion as the established baseline) ----------------
static inline i64 Vof(int n,int t,int flip,i64 C0,int u2,i64 J){ const Tab&T=TB[n]; i64 dx=(i64)t<<13;
    i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
    i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
    i64 T2=T.S? sm(arr(J,T.S)+K,u2):0; i64 s=flip?-1:1; return C0+s*T1-T2-s*T3; }

int main(){ if(!load_fsca(F)){ puts("dump load failed"); return 1; } build_tables();
    // ---- fit C0,u2 per table entry over its two nodes, using the BASELINE J (this is the published baseline fit)
    for(int n=0;n<=16;n++){ int nodes[2]={n,32-n}; int nn=(n==0||n==16)?1:2;
        i64 lo0,hi0; bool ex0; win_of(n*1024,lo0,hi0,ex0);
        int bv=1<<30; i64 bc=lo0; int bu=TB[n].u2;
        for(i64 c=lo0-3;c<=hi0+3;c++) for(int u=TB[n].u2-1;u<=TB[n].u2+1;u++){ int v=0;
            for(int i=0;i<nn;i++){ int m=nodes[i],flip=(m>=17),tn=flip?32-m:m;
                for(int t=-512;t<=511;t++){ int a=(m*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                    i64 V=Vof(tn,t,flip,c,u,J_base(t)); if(V<lo||V>hi)v++; } }
            if(v<bv){bv=v;bc=c;bu=u;} }
        C0T[n]=bc; U2T[n]=bu; }
    printf("my C0 fit : "); for(int n=0;n<=16;n++)printf("%lld%s",(long long)C0T[n],n<16?",":"\n");
    printf("my u2 fit : "); for(int n=0;n<=16;n++)printf("%d%s",U2T[n],n<16?",":"\n");
    { // compare against the values hard-coded in b12.cpp (which generated jwin.txt)
      static const i64 C0R[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
      static const int U2R[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
      int d=0; for(int n=0;n<=16;n++){ if(C0T[n]!=C0R[n]||U2T[n]!=U2R[n]) d++; }
      printf("tables identical to b12.cpp's hard-coded pair: %s (%d differing entries)\n", d?"NO":"YES", d); }

    // ---- monotonicity of T2 in J, checked (not assumed) over the whole J range, per table entry
    { int bad=0;
      #pragma omp parallel for reduction(+:bad) schedule(dynamic)
      for(int n=1;n<=16;n++){ i64 prev=-1<<30; i64 S=TB[n].S; int u=U2T[n];
        for(i64 m=0;m<=12000000;m++){ i64 v=sm(arr(m,S)+K,u); if(v<prev) bad++; prev=v; } }
      printf("T2(J) monotone non-decreasing over J in [0,12e6] for all 16 entries: %s (%d inversions)\n", bad?"NO":"YES", bad); }

    // ---- step 2: rebuild the J windows from the dump, my own code
    static i64 JL[1024],JH[1024]; static char OKT[1024];
    #pragma omp parallel for schedule(dynamic)
    for(int ti=0;ti<1024;ti++){ int t=ti-512; i64 dx=(i64)t<<13; i64 gl=0,gh=12000000;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
            const Tab&T=TB[n]; i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 base=C0T[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            i64 t2lo=base-hi,t2hi=base-lo; int u=U2T[n];
            i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        JL[ti]=gl;JH[ti]=gh;OKT[ti]=gl<=gh; }
    int feas=0; for(int i=0;i<1024;i++) feas+=OKT[i];
    printf("my regenerated window: feasible t = %d of 1024\n", feas);
    // compare against jwin.txt on disk
    { FILE*f=fopen("jwin.txt","r"); int diff=0,lines=0; if(f){ int t,ok; long long l,h;
        while(fscanf(f,"%d %lld %lld %d",&t,&l,&h,&ok)==4){ lines++; int i=t+512;
            if(i<0||i>1023) {diff++;continue;} if(JL[i]!=l||JH[i]!=h||OKT[i]!=ok) diff++; } fclose(f);
        printf("jwin.txt on disk: %d lines, %d disagree with my regeneration\n", lines, diff); }
      else puts("jwin.txt missing"); }
    // disjoint +/- pairs, my own count
    { int dis=0; for(int a=1;a<=511;a++){ int ip=a+512,im=512-a; if(!OKT[ip]||!OKT[im])continue;
        if(JH[im]<JL[ip]||JH[ip]<JL[im]) dis++; } printf("disjoint +/- pairs (my count): %d\n",dis); }

    // ---- step 3: score every rule against MY window
    puts("\nrule                                                        score/1022   (misses)");
    for(int r=0;r<NR;r++){ int sc=0; for(int ti=0;ti<1024;ti++){ if(!OKT[ti])continue; i64 J=RULES[r].f(ti-512);
            if(J>=JL[ti]&&J<=JH[ti]) sc++; }
        printf("%-58s %5d        %4d\n", RULES[r].name, sc, feas-sc); }

    // ---- step 4: full model end to end, nodes 0..31, per rule
    puts("\nrule                                                        total  n0 n1 n2 n16  guards-exact");
    for(int r=0;r<NR;r++){ int tot=0,g[4]={0,0,0,0};
        for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; int v=0;
            for(int t=-512;t<=511;t++){ int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                i64 V=Vof(n,t,flip,C0T[n],U2T[n],RULES[r].f(t)); if(V<lo||V>hi)v++; }
            tot+=v; if(node==0)g[0]=v; if(node==1)g[1]=v; if(node==2)g[2]=v; if(node==16)g[3]=v; }
        printf("%-58s %5d  %2d %2d %2d %3d   %s\n", RULES[r].name, tot,g[0],g[1],g[2],g[3],
            (g[0]|g[1]|g[2]|g[3])?"NO":"yes"); }
    return 0; }
