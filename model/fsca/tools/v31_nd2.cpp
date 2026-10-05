// v31_nd2.cpp -- second adversarial pass: regenerate the LATCH WINDOWS per ND (the other agent's
// harness does this; my first tool held the published windows fixed), check where the ND family has
// any free parameter at all, and whether ND<14 can move the three pinned landmarks.
#include "base2.h"
#include <cstring>
#include <cstdio>
typedef __int128 i128;
static i64 v31_arr(i64 rec, i64 mul, int ND) {
    i128 R[48]; i128 T = 0;
    for (int k = 0; k < ND; k++) {
        auto bit=[&](int b)->int{ return b<0?0:(int)((rec>>(b>62?62:b))&1); };
        int d=-2*bit(2*k+1)+bit(2*k)+bit(2*k-1);
        i128 mag=(i128)(d<0?-d:d)*mul; i128 row=d<0?~mag:mag;
        R[k]=row*((i128)1<<(2*k)); T+=(R[k]>>16)<<16;
        if(d<0 && 2*k>=18) T+=(i128)1<<(2*k);
    }
    if(ND>=3){ int m15=(int)((R[0]>>15)&1)+(int)((R[1]>>15)&1)+(int)((R[2]>>15)&1); T+=((i128)1<<16)*(m15>=2); }
    if(ND>=9){ int p16=(int)(((R[6]>>16)^(R[7]>>16)^(R[8]>>16))&1); T-=((i128)1<<16)*p16; }
    return (i64)T;
}
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi){ Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} }
static i64 PL[1024],PH[1024];                 // published jwin18.txt
static bool load_jwin(){ FILE*f=fopen("jwin18.txt","r"); if(!f)return false; char ln[256]; int n=0;
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){PL[t+512]=l;PH[t+512]=h;n++;} } fclose(f); return n==1024; }
// regenerate windows with ALL call sites at digit count nd
static void gen_win(int nd, i64* GL, i64* GH, bool* FE){
    const int DU=2; const i64 LIM=12000000ll<<DU;
    for(int t=-512;t<=511;t++){
        i64 dx=(i64)t<<13; i64 gl=0,gh=LIM;
        i64 A3=((v31_arr(((i64)t*t)<<3,dx,nd)+K)>>20)>>3;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
            const Tab&T=TB[n]; i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(v31_arr(T.C1f,dx,nd)+K,23-T.e1):0;
            i64 T3=T.c3? sm(v31_arr(T.c3<<5,A3,nd)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
            i64 t2lo=base-hi,t2hi=base-lo; int u=U2TAB[n]+DU;
            i64 a1=0,b1=LIM+1; while(a1<b1){i64 m=(a1+b1)/2; if(sm(v31_arr(m,T.S,nd)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=LIM;  while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(v31_arr(m,T.S,nd)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        GL[t+512]=gl; GH[t+512]=gh; FE[t+512]= gl<=gh; }
}
static inline i64 Jfine(int t,int nd){ i64 dx=(i64)t<<13; i64 I1=(v31_arr(KC,dx,nd)+K)>>23; return (v31_arr(I1,dx,nd)+K)>>18; }
int main(){
    if(!load_fsca(F)){puts("no dump");return 1;} build_tables(); if(!load_jwin()){puts("no jwin");return 1;}
    puts("A. Is J itself ND-invariant, and from which ND?  (bit-identical over all 1024 offsets vs ND=14)");
    for(int nd=8;nd<=24;nd++){ int d=0; for(int t=-512;t<=511;t++) if(Jfine(t,nd)!=Jfine(t,14)) d++;
        printf("   ND=%2d : %4d of 1024 offsets differ from ND=14%s\n", nd, d, d==0?"   <- identical":""); }
    puts("");
    puts("B. Regenerated latch windows per ND (the measurement that produces the '694' number)");
    static i64 GL[1024],GH[1024]; static bool FE[1024];
    i64 base17L=0,base17H=0;
    for(int nd=11;nd<=20;nd++){
        gen_win(nd,GL,GH,FE);
        int rowdiff=0,infeas=0,sc=0;
        for(int i=0;i<1024;i++){ if(GL[i]!=PL[i]||GH[i]!=PH[i]) rowdiff++; if(!FE[i]) infeas++; }
        for(int t=-512;t<=511;t++){ i64 j=Jfine(t,nd); if(j>=GL[t+512]&&j<=GH[t+512]) sc++; }
        printf("   ND=%2d : %4d/1024 window rows differ from jwin18.txt, %4d infeasible, chain scores %4d/1024"
               "  | window(17)=[%lld,%lld] window(342)=[%lld,%lld] window(-342)=[%lld,%lld]\n",
            nd,rowdiff,infeas,sc,(long long)GL[17+512],(long long)GH[17+512],
            (long long)GL[342+512],(long long)GH[342+512],(long long)GL[-342+512],(long long)GH[-342+512]);
        if(nd==14){base17L=GL[17+512];base17H=GH[17+512];}
    }
    (void)base17L;(void)base17H;
    puts("");
    puts("C. Does ANY per-site ND assignment change J or hit a landmark?  (sq1 x sq2 independent, 8..24)");
    { int best=-1,bs1=0,bs2=0; long long hits=0, cfgs=0;
      static i64 WL[1024],WH[1024]; static bool WF[1024]; gen_win(14,WL,WH,WF);
      for(int a=8;a<=24;a++) for(int b=8;b<=24;b++){
        cfgs++;
        i64 dx17=(i64)17<<13, dx342=(i64)342<<13, dxm342=-(i64)342<<13;
        auto Jab=[&](int t)->i64{ i64 dx=(i64)t<<13; i64 I1=(v31_arr(KC,dx,a)+K)>>23; return (v31_arr(I1,dx,b)+K)>>18; };
        (void)dx17;(void)dx342;(void)dxm342;
        int sc=0; for(int t=-512;t<=511;t++){ i64 j=Jab(t); if(j>=WL[t+512]&&j<=WH[t+512]) sc++; }
        int pins=(Jab(17)==45617)+(Jab(342)==18466836)+(Jab(-342)>=18466858&&Jab(-342)<=18466867);
        hits+=pins;
        if(sc>best){best=sc;bs1=a;bs2=b;}
      }
      printf("   %lld (sq1,sq2) configs, best jwin18 score %d/1024 at (%d,%d), total landmark hits across all configs: %lld\n",
             cfgs,best,bs1,bs2,hits); }
    puts("");
    puts("D. The decisive question: can a 13-digit array (misreading J's bit 25) explain the landmarks?");
    { gen_win(13,GL,GH,FE);
      printf("   at ND=13 the windows at the three landmarks are [%lld,%lld] [%lld,%lld] [%lld,%lld]\n",
        (long long)GL[17+512],(long long)GH[17+512],(long long)GL[342+512],(long long)GH[342+512],
        (long long)GL[-342+512],(long long)GH[-342+512]);
      printf("   J at ND=13: %lld %lld %lld  -> the landmarks are UNCHANGED because J(17),J(342) < 2^25\n",
        (long long)Jfine(17,13),(long long)Jfine(342,13),(long long)Jfine(-342,13));
      int lo=1<<30,hi=-1; for(int t=-512;t<=511;t++) if(GL[t+512]!=PL[t+512]||GH[t+512]!=PH[t+512]){ if(t<lo)lo=t; if(t>hi)hi=t; }
      printf("   window rows that ND=13 disturbs: t in [%d,%d]  (J>=2^25 region is |t|>=462)\n", lo, hi); }
    return 0; }
