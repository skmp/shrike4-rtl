// fv_certificate_g.cpp -- (1) the per-offset certificate-persistence table by s2-g, over the same
// form space the report describes: 7 g x 2 roles x 6 K2 x 3 latch rules = 252 cells per s2-g;
// (2) an honest measurement of genuine non-monotonicity of arr in the recoded operand (mul > 0 only);
// (3) g=13, s2=18: is there a K2 making the three pinned offsets simultaneously admissible?
#include "base2.h"
#include <cstdio>
#include <vector>
#include <algorithm>
static Fsca F;
static const i64 C0PUB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2PUB[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}}
static i64 JL[1024],JH[1024];
static const i64 EB=1ll<<21,SDEV=1ll<<21;
static void build_ds(int DU){ i64 LIM=12000000ll<<DU;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=LIM;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue; i64 s=flip?-1:1;
            i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
            i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
            i64 base=C0PUB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
            i64 t2lo=base-hi,t2hi=base-lo; int u=U2PUB[n]+DU; i64 S=TB[n].S;
            i64 a1=0,b1=LIM+1; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=LIM;  while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        JL[t+512]=gl; JH[t+512]=gh; } }
static inline i64 latch(i64 P,int s,int rule){ if(rule==0) return P>>s;
    if(rule==1) return P>=0?(P>>s):-((-P)>>s); return P>=0?(P>>s):-((~P)>>s); }
static int certify(int t,int g,int s2,i64 K2,int role,int rule,i64 jl,i64 jh,int*clamp){
    i64 dx=(i64)t<<g; if(jl>jh) return 0;
    if(dx==0){ for(i64 v=-4;v<=4;v++){ i64 P=role?arr(dx,v):arr(v,dx); if(latch(P+K2,s2,rule)>=jl&&latch(P+K2,s2,rule)<=jh) return 1; } return 0; }
    i64 Plo=jl*(1ll<<s2)-K2, Phi=(jh+1)*(1ll<<s2)-1-K2, ad=dx<0?-dx:dx;
    if(Phi-Plo+1>=ad+SDEV+1) return 1;
    i64 lo=Plo-EB,hi=Phi+EB,a,b;
    if(dx>0){ a=(lo>=0?lo/dx:-((-lo+dx-1)/dx)); b=(hi>=0?hi/dx:-((-hi+dx-1)/dx)); }
    else    { a=(hi>=0?-(hi/ad):(-hi+ad-1)/ad); b=(lo>=0?-(lo/ad):(-lo+ad-1)/ad); }
    a-=2;b+=2; if(a<-(1ll<<27)){a=-(1ll<<27);if(clamp)(*clamp)++;} if(b>(1ll<<27)){b=(1ll<<27);if(clamp)(*clamp)++;}
    for(i64 v=a;v<=b;v++){ i64 P=role?arr(dx,v):arr(v,dx); i64 J=latch(P+K2,s2,rule); if(J>=jl&&J<=jh) return 1; }
    return 0; }
int main(){ if(!load_fsca(F)){puts("cannot load dump");return 1;} build_tables(); build_ds(2);
    const int TS[8]={342,17,-17,376,-376,258,40,-40};
    printf("[G1] cells (of 252 = 7 g x 2 roles x 6 K2 x 3 latch rules) in which the offset admits NO integer I1,\n");
    printf("     by s2-g (0..12), fine scale.  A cell with s1 = 2g+15-s2 is not required: I1 is free.\n");
    printf("       t   :"); for(int d=0;d<=12;d++) printf("%5d",d); puts("");
    int clamp=0;
    for(int ti=0;ti<8;ti++){ int t=TS[ti]; printf("     %5d :",t);
        for(int d=0;d<=12;d++){ int bad=0;
            for(int g=8;g<=14;g++){ int s2=g+d; if(s2<1||s2>40) continue;
              i64 K2s[6]={0,1ll<<19,1ll<<(s2-1),1ll<<18,1ll<<20,(1ll<<s2)-1};
              for(int role=0;role<2;role++) for(int ki=0;ki<6;ki++) for(int rule=0;rule<3;rule++)
                if(!certify(t,g,s2,K2s[ki],role,rule,JL[t+512],JH[t+512],&clamp)) bad++; }
            printf("%5d",bad); } puts(""); }
    printf("     (bracket clamped %d times -- a nonzero count would invalidate a 'no I1 exists' cell)\n",clamp);

    // ---- G2: genuine non-monotonicity, mul > 0 only
    { long long back=0,tot=0; i64 worst=0,dev=0;
      for(int g=8;g<=14;g++) for(int t=1;t<=512;t++){ i64 dx=(i64)t<<g; i64 p=arr(-2200,dx);
        for(i64 v=-2199;v<=2200;v++){ i64 a=arr(v,dx); i64 s=a-p; tot++; if(s<0){back++; if(s<worst)worst=s;}
            i64 x=s-dx; if(x<0)x=-x; if(x>dev)dev=x; p=a; } }
      printf("\n[G2] arr(v+1,mul)-arr(v,mul) with mul>0, %lld steps: %lld genuinely backward (worst %lld); max |step-mul| %lld\n",
             tot,back,(long long)worst,(long long)dev);
      puts("     -> binary search on arr in the recoded operand is unsound, so e6's METHOD was indeed invalid"); }

    // ---- G3: g=13, s2=18 (the published fine-scale shift): can any K2 rescue anything?
    { int best=0,bestpin=0; i64 bk=0,bkp=0;
      for(int q=-4;q<=8;q++) for(int j=0;j<4;j++){ i64 K2=(i64)q*(1ll<<18)+(i64)j*65536; int f=0;
        for(int t=-512;t<=511;t++) f+=certify(t,13,18,K2,0,0,JL[t+512],JH[t+512],0);
        if(f>best){best=f;bk=K2;}
        int p3=certify(17,13,18,K2,0,0,JL[17+512],JH[17+512],0)+certify(342,13,18,K2,0,0,JL[342+512],JH[342+512],0)
              +certify(-342,13,18,K2,0,0,JL[-342+512],JH[-342+512],0);
        if(p3>bestpin){bestpin=p3;bkp=K2;} }
      printf("\n[G3] g=13, s2=18 (published fine-scale shift), K2 exhausted: best %d/1024 offsets (K2=%lld);\n",best,(long long)bk);
      printf("     the most pinned offsets any K2 gets simultaneously: %d of 3 (K2=%lld)\n",bestpin,(long long)bkp); }
    return 0; }
