// fv_wiring_5.cpp -- (E) score the PUBLISHED chain against the REGENERATED coarse windows;
//                    (F) certificate offsets re-checked with a WIDE exhaustive I1 scan (no bracket);
//                    (G) the t=0 windows and C0.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdio>
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,
                            47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static inline void win_of(int a,i64&lo,i64&hi){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}}
static int kof(int n){ if(n==0)return 0; i128 s=sin_(PI_()*n/32); int k=0; while(s*((i128)1<<(k+1))<QONE)k++; return k; }
static i64 S21[17],S23[17]; static int U21[17],U23[17];
static const i64 MM=1ll<<24;
static inline i64 fvv(int form,i64 J,i64 S,int u){ if(!S)return 0;
    if(form==0) return sm(arr(J,S)+K,u);
    return (arr(S,MM)-arr(S,MM-J)+K)>>u; }
static int build(int form,int sv,int du,i64*JL,i64*JH){
    i64*S=sv?S21:S23; int*U=sv?U21:U23; i64 LIM=12000000ll<<du; int feas=0;
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=LIM;
      i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
      for(int node=1;node<=31;node++){int flip=node>=17,n=flip?32-node:node; if(!S[n])continue;
        i64 s=flip?-1:1; i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
        i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
        i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 wl,wh; win_of(a,wl,wh);
        i64 t2lo=base-wh,t2hi=base-wl; int uu=U[n]+du;
        i64 a1=0,b1=LIM+1; while(a1<b1){i64 m=(a1+b1)/2; if(fvv(form,m,S[n],uu)>=t2lo)b1=m;else a1=m+1;}
        i64 a2=-1,b2=LIM;  while(a2<b2){i64 m=(a2+b2+1)/2; if(fvv(form,m,S[n],uu)<=t2hi)a2=m;else b2=m-1;}
        if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
      JL[t+512]=gl; JH[t+512]=gh; if(gl<=gh)feas++; }
    return feas; }
int main(){ if(!load_fsca(F))return 1; build_tables();
    for(int n=0;n<=16;n++){ int k=kof(n); i128 s=sin_(PI_()*n/32);
        S23[n]=n?std::min(rn_scale(s,23+k),(i64)((1ll<<23)-1)):0; U23[n]=30+k;
        S21[n]=n?std::min(rn_scale(s,21+k),(i64)((1ll<<21)-1)):0; U21[n]=28+k; if(!n){U23[0]=30;U21[0]=30;} }
    i64 NL[1024],NH[1024],PL[1024],PH[1024],FL[1024],FH[1024];
    build(1,1,0,NL,NH);   // new coarse (claim)
    build(0,0,0,PL,PH);   // published coarse
    build(0,0,2,FL,FH);   // published fine (== jwin18)
    // cross-check FL/FH against the published jwin18.txt file
    { FILE*g=fopen("jwin18.txt","r"); char ln[256]; int diff=0,tot=0; long long t,a,b,w2;
      while(fgets(ln,sizeof ln,g)){ if(ln[0]=='#')continue; if(sscanf(ln,"%lld %lld %lld %lld",&t,&a,&b,&w2)!=4)continue;
        tot++; if(a!=FL[t+512]||b!=FH[t+512])diff++; } fclose(g);
      printf("(0) my regenerated fine dataset vs the published jwin18.txt: %d of %d rows differ\n\n",diff,tot); }
    puts("(E) the PUBLISHED chain  I1=(arr(dx,KC)+2^19)>>23, J=(arr(I1,dx)+2^19)>>20  scored against:");
    { int sN=0,sP=0,sF=0;
      for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
        i64 I1=(arr(dx,KC)+K)>>23; i64 Jc=(arr(I1,dx)+K)>>20; i64 Jf=(arr(I1,dx)+K)>>18;
        if(NL[t+512]<=NH[t+512]&&Jc>=NL[t+512]&&Jc<=NH[t+512])sN++;
        if(PL[t+512]<=PH[t+512]&&Jc>=PL[t+512]&&Jc<=PH[t+512])sP++;
        if(FL[t+512]<=FH[t+512]&&Jf>=FL[t+512]&&Jf<=FH[t+512])sF++; }
      printf("    the NEW coarse windows (claim's final multiply) : %d/1024\n",sN);
      printf("    the published coarse windows                    : %d/1024\n",sP);
      printf("    the published fine windows (jwin18, J at >>18)  : %d/1024\n\n",sF); }
    puts("(F) certificate offsets, WIDE exhaustive I1 scan (m = |I1|, 0..6e6, no bracketing)");
    { const int PT[6]={-476,-376,258,342,376,-459};
      const char*dn[2]={"published coarse","NEW coarse"};
      for(int ds=0;ds<2;ds++){ i64*JL=ds?NL:PL,*JH=ds?NH:PH;
        for(int s2=20;s2<=21;s2++) for(int k2=0;k2<2;k2++){ i64 K2=k2?K:0;
          printf("    %-17s s2=%d K2=%-4s :",dn[ds],s2,K2?"2^19":"0");
          for(int i=0;i<6;i++){ int t=PT[i]; if(JL[t+512]>JH[t+512]){printf("  t=%d:win-infeas",t);continue;}
            i64 dx=(i64)t<<13; int sg=t>=0?1:-1; long cnt=0; i64 first=-1;
            for(i64 m=0;m<=6000000;m++){ i64 J=(arr(sg*m,dx)+K2)>>s2;
              if(J>=JL[t+512]&&J<=JH[t+512]){ if(first<0)first=sg*m; cnt++; } }
            printf("  t=%d:%s",t,cnt?"OK":"NONE"); if(cnt)printf("(I1=%lld,n=%ld)",(long long)first,cnt); }
          printf("\n"); } }
      puts(""); }
    puts("(G) C0: the t=0 window of every node instance, and whether C0TAB is the unique integer in it");
    { int uniq=0,inw=0;
      for(int node=1;node<=16;node++){ int n=node; int a=(node*1024)&0xffff; i64 lo,hi; win_of(a,lo,hi);
        // at t=0 the model gives V = C0 + T1(0) - T2(0) - T3(0); print the residual window on C0
        i64 dx=0; i64 T1=TB[n].C1f? sm(arr(TB[n].C1f,dx)+K,23-TB[n].e1):0;
        i64 A3=((arr(0,0)+K)>>20)>>3; i64 T3=TB[n].c3? sm(arr(TB[n].c3<<5,A3)+K,26):0;
        i64 T2=S23[n]? sm(arr(0,S23[n])+K,U23[n]):0;
        i64 cl=lo-T1+T2+T3, ch=hi-T1+T2+T3;
        bool ok=C0TAB[n]>=cl&&C0TAB[n]<=ch; if(ok)inw++; if(cl==ch)uniq++;
        printf("    node %2d: C0 window [%lld,%lld] width %lld   C0TAB=%lld %s\n",n,(long long)cl,(long long)ch,
               (long long)(ch-cl+1),(long long)C0TAB[n], ok?"inside":"*** OUTSIDE ***"); }
      printf("    -> %d/16 inside, %d/16 pinned to a single integer by t=0 alone\n",inw,uniq); }
    return 0; }
