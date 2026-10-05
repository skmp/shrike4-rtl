// wv_orders_detail.cpp -- where does the 148 -> 147 come from, and are the auxiliary structural claims true?
#include "base2.h"
#include <cstring>
#include <cstdio>
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static const i64 C0T[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2T[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static i64 fJL[1024],fJH[1024]; static int fOK[1024];
static bool load_jwin(){FILE*f=fopen("jwin.txt","r");if(!f)return false;
  for(int i=0;i<1024;i++){int t,ok;long long a,b;if(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&ok)!=4)return false;fJL[i]=a;fJH[i]=b;fOK[i]=ok;}fclose(f);return true;}
// generic chain with configurable operand roles, shifts, rounding constants, KC
static inline i64 Jgen(int t,i64 kc,int roleA,int s0,int s1,int k0,int k1){
    i64 dx=(i64)t<<13; i64 I1 = ((roleA? arr(dx,kc):arr(kc,dx)) + (k0?K:0)) >> s0;
    return (arr(I1,dx) + (k1?K:0)) >> s1; }
static inline i64 Vof(int n,int t,int flip,i64 C0,int u2,i64 J){const Tab&T=TB[n];i64 dx=(i64)t<<13;
    i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
    i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
    i64 T2=T.S? sm(arr(J,T.S)+K,u2):0; i64 s=flip?-1:1; return C0+s*T1-T2-s*T3;}
// score with FIXED C0/u2 tables, return per-table counts
static int score_fixed(i64 kc,int roleA,int s0,int s1,int k0,int k1,int* per){
    int g=0; for(int n=0;n<=16;n++) if(per) per[n]=0;
    for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node;
        for(int t=-512;t<=511;t++){ int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            i64 V=Vof(n,t,flip,C0T[n],U2T[n],Jgen(t,kc,roleA,s0,s1,k0,k1));
            if(V<lo||V>hi){g++; if(per)per[n]++;} } }
    return g; }
int main(){ if(!load_fsca(F))return 1; build_tables(); if(!load_jwin())return 1;
    // 1. which t have a different I1 latch, and does J differ there
    puts("== t where the I1 latch differs between arr(KC,dx) and arr(dx,KC) ==");
    printf("   t   I1_base   I1_claim   J_base     J_claim   JL        JH        base_in claim_in\n");
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 a=(arr(KC,dx)+K)>>23, b=(arr(dx,KC)+K)>>23; if(a==b)continue;
        i64 jb=(arr(a,dx)+K)>>20, jc=(arr(b,dx)+K)>>20; int i=t+512;
        printf("%5d %9lld %9lld %10lld %10lld %9lld %9lld   %d      %d%s\n",t,(long long)a,(long long)b,(long long)jb,(long long)jc,
          (long long)fJL[i],(long long)fJH[i],(int)(jb>=fJL[i]&&jb<=fJH[i]),(int)(jc>=fJL[i]&&jc<=fJH[i]),fOK[i]?"":"  (infeasible)"); }
    // 2. per-table violation breakdown, fixed tables
    int pb[17],pc[17]; int gb=score_fixed(KC,0,23,20,1,1,pb), gc=score_fixed(KC,1,23,20,1,1,pc);
    printf("\n== per-table violations, C0/u2 FIXED ==\ntab:"); for(int n=0;n<=16;n++)printf("%5d",n);
    printf("\nbase"); for(int n=0;n<=16;n++)printf("%5d",pb[n]); printf("   total %d\n",gb);
    printf("clm "); for(int n=0;n<=16;n++)printf("%5d",pc[n]); printf("   total %d\n",gc);
    // 3. exactly which (node,t) samples change verdict
    puts("\n== samples whose verdict changes (base ok -> claim bad, or base bad -> claim ok) ==");
    for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node;
        for(int t=-512;t<=511;t++){ int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            i64 Vb=Vof(n,t,flip,C0T[n],U2T[n],Jgen(t,KC,0,23,20,1,1));
            i64 Vc=Vof(n,t,flip,C0T[n],U2T[n],Jgen(t,KC,1,23,20,1,1));
            int ob=!(Vb<lo||Vb>hi), oc=!(Vc<lo||Vc>hi); if(ob==oc)continue;
            printf("node %2d t %5d  win[%lld,%lld] ex=%d  Vbase %lld (%s)  Vclaim %lld (%s)\n",node,t,(long long)lo,(long long)hi,(int)ex,
                (long long)Vb,ob?"ok":"BAD",(long long)Vc,oc?"ok":"BAD"); } }
    // 4. KC scan +-600 on the claimed structure (fixed tables)
    puts("\n== KC scan on the claimed structure (fixed C0/u2), any d with total < 147? ==");
    { int best=1<<30; i64 bkc=KC; int nbetter=0;
      for(i64 d=-600;d<=600;d++){ int g=score_fixed(KC+d,1,23,20,1,1,nullptr); if(g<best){best=g;bkc=KC+d;} if(g<147)nbetter++; }
      printf("best total over KC in [0x%llx-600, +600] = %d at KC=0x%llx (d=%lld); count of KC with total<147: %d\n",
        (unsigned long long)KC,best,(unsigned long long)bkc,(long long)(bkc-KC),nbetter); }
    // 5. shift-split grid for the CLAIM B variant (no K on 2nd multiply), jwin + model
    puts("\n== (s0, 43-s0) grid, arr(KC,dx) first, no K on 2nd multiply ==");
    for(int s0=21;s0<=30;s0++){ int s1=43-s0; int sc=0;
        for(int i=0;i<1024;i++){ if(!fOK[i])continue; i64 J=Jgen(i-512,KC,0,s0,s1,1,0); if(J>=fJL[i]&&J<=fJH[i])sc++; }
        int g=score_fixed(KC,0,s0,s1,1,0,nullptr); printf("  s0=%2d s1=%2d  jwin %4d  model %6d\n",s0,s1,sc,g); }
    // 6. rounding-constant combinations on (23,20), both roles
    puts("\n== rounding-constant combinations, shifts (23,20), fixed tables ==");
    for(int roleA=0;roleA<2;roleA++) for(int k0=0;k0<2;k0++) for(int k1=0;k1<2;k1++){
        int sc=0; for(int i=0;i<1024;i++){ if(!fOK[i])continue; i64 J=Jgen(i-512,KC,roleA,23,20,k0,k1); if(J>=fJL[i]&&J<=fJH[i])sc++; }
        int per[17]; int g=score_fixed(KC,roleA,23,20,k0,k1,per);
        printf("  rec=%-3s K0=%d K1=%d  jwin %4d  model %5d  guard %d\n",roleA?"dx":"KC",k0,k1,sc,g,per[0]+per[1]+per[2]+per[16]); }
    return 0; }
