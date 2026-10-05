// wv_orders_check.cpp -- INDEPENDENT adversarial verification of the claimed FSCA square-path latch rule.
// Written from the textual claim only; uses only base2.h (shared model header) + fsca_common.h (dump loader).
// Everything integer.  No fitted constants introduced here beyond the b2 baseline's own C0/u2 refit,
// which is measured separately with and without.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdio>

static Fsca F;
static inline void win_of(int a, i64& lo, i64& hi, bool& ex){ Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} ex=w.exact; }

// ---------------- the candidate J rules -----------------------------------------------------------
// latch rules for the SECOND latch
static inline i64 lat(i64 P,int sh,int rule){
    if(rule==0) return P>>sh;                                   // floor (arithmetic)
    if(rule==1) return P>=0? (P>>sh) : -((-P)>>sh);              // toward zero
    return sm(P,sh);                                             // one's-complement magnitude truncation
}
// rid: 0 = BASELINE           I1=(arr(KC,dx)+K)>>23 ; J=lat((arr(I1,dx)+K),20)
//      1 = CLAIM A            I1=(arr(dx,KC)+K)>>23 ; J=lat((arr(I1,dx)+K),20)
//      2 = CLAIM B (jwin 934) I1=(arr(KC,dx)+K)>>23 ; J=lat( arr(I1,dx)    ,20)
//      3 =                    I1=(arr(dx,KC)+K)>>23 ; J=lat( arr(I1,dx)    ,20)
static inline i64 Jrule(int t,int rid,int rule2){
    i64 dx=(i64)t<<13;
    i64 I1,P;
    if(rid==0||rid==2) I1=(arr(KC,dx)+K)>>23; else I1=(arr(dx,KC)+K)>>23;
    P=arr(I1,dx); if(rid==0||rid==1) P+=K;
    return lat(P,20,rule2);
}

// ---------------- jwin scoring --------------------------------------------------------------------
static i64 fJL[1024],fJH[1024]; static int fOK[1024];
static bool load_jwin(const char* path){ FILE*f=fopen(path,"r"); if(!f)return false;
    for(int i=0;i<1024;i++){ int t,ok; long long a,b; if(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&ok)!=4){fclose(f);return false;}
        if(t!=i-512){fclose(f);return false;} fJL[i]=a;fJH[i]=b;fOK[i]=ok; } fclose(f); return true; }

// ---------------- independent regeneration of the J windows ---------------------------------------
static i64 gJL[1024],gJH[1024]; static int gOK[1024];
static void regen_jwin(const i64* C0T,const int* U2T){
    for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=12000000;
        for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n]; if(!T.S)continue;
            i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 base=C0T[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            i64 t2lo=base-hi,t2hi=base-lo; int u=U2T[n];
            i64 a1=0,b1=12000001; while(a1<b1){i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo)b1=m;else a1=m+1;}
            i64 a2=-1,b2=12000000; while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi)a2=m;else b2=m-1;}
            if(a1>gl)gl=a1; if(a2<gh)gh=a2; }
        gJL[t+512]=gl;gJH[t+512]=gh;gOK[t+512]=gl<=gh; } }

// ---------------- full model (b2 objective) -------------------------------------------------------
static inline i64 Vof(int n,int t,int flip,i64 C0,int u2,int rid,int rule2){ const Tab&T=TB[n]; i64 dx=(i64)t<<13;
    i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
    i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
    i64 J=Jrule(t,rid,rule2); i64 T2=T.S? sm(arr(J,T.S)+K,u2):0;
    i64 s=flip?-1:1; return C0+s*T1-T2-s*T3; }

// mode 0 : b2 objective (C0 swept lo0-3..hi0+3, u2 swept derived-1..+1, minimise per table entry)
// mode 1 : C0 and u2 FIXED to the given tables (no fit at all)
// mode 2 : C0 swept over the t=0 window ONLY (forced by data), u2 fixed to derived
static int model_score(int rid,int rule2,int mode,const i64* C0F,const int* U2F,int* perTab,i64* C0out,int* U2out){
    int grand=0;
    for(int n=0;n<=16;n++){
        int nodes[2]={n,32-n}; int nn=(n==0||n==16)?1:2;
        i64 lo0,hi0; bool ex0; win_of(n*1024,lo0,hi0,ex0);
        i64 cLo,cHi; int uLo,uHi;
        if(mode==0){ cLo=lo0-3; cHi=hi0+3; uLo=TB[n].u2-1; uHi=TB[n].u2+1; }
        else if(mode==1){ cLo=cHi=C0F[n]; uLo=uHi=U2F[n]; }
        else { cLo=lo0; cHi=hi0; uLo=uHi=TB[n].u2; }
        int bv=1<<30; i64 bc=cLo; int bu=uLo;
        for(i64 c=cLo;c<=cHi;c++) for(int u=uLo;u<=uHi;u++){
            int v=0;
            for(int i=0;i<nn;i++){ int m=nodes[i]; int flip=(m>=17); int tn=flip?32-m:m;
                for(int t=-512;t<=511;t++){ int a=(m*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                    i64 V=Vof(tn,t,flip,c,u,rid,rule2); if(V<lo||V>hi) v++; } }
            if(v<bv){bv=v;bc=c;bu=u;} }
        grand+=bv; if(perTab)perTab[n]=bv; if(C0out)C0out[n]=bc; if(U2out)U2out[n]=bu;
    }
    return grand; }

static const i64 C0TAB_B12[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB_B12[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};

int main(){
    if(!load_fsca(F)){puts("dump load failed");return 1;} build_tables();

    // ---- 0. table sanity: is u2 the DERIVED rule, is C0TAB inside the t=0 window? ----
    puts("== table check (derived vs b12's stored tables) ==");
    printf("n  S_n      u2der u2b12  C0b12     RN26sin   t0win[lo,hi] exact  C0inWin\n");
    for(int n=0;n<=16;n++){ i64 lo,hi;bool ex; win_of(n*1024,lo,hi,ex);
        printf("%2d %8lld   %2d   %2d   %9lld %9lld  [%lld,%lld] %d  %d\n",n,(long long)TB[n].S,TB[n].u2,U2TAB_B12[n],
            (long long)C0TAB_B12[n],(long long)TB[n].sin26,(long long)lo,(long long)hi,(int)ex,
            (int)(C0TAB_B12[n]>=lo&&C0TAB_B12[n]<=hi)); }

    // ---- 1. regenerate the J windows independently and diff against jwin.txt ----
    if(!load_jwin("jwin.txt")){puts("jwin.txt missing");return 1;}
    regen_jwin(C0TAB_B12,U2TAB_B12);
    int nd=0,ndok=0; for(int i=0;i<1024;i++){ if(gJL[i]!=fJL[i]||gJH[i]!=fJH[i])nd++; if(gOK[i]!=fOK[i])ndok++; }
    int nfeas=0; for(int i=0;i<1024;i++) nfeas+=fOK[i];
    printf("\n== jwin.txt integrity ==\nrows differing from my regeneration: %d (feasible flag diffs %d); feasible rows: %d\n",nd,ndok,nfeas);
    printf("infeasible t:"); for(int i=0;i<1024;i++) if(!fOK[i]) printf(" %d",i-512); printf("\n");

    // ---- 2. how often do the two operand orders actually differ? ----
    { int d1=0,d2=0; for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
        if(arr(KC,dx)!=arr(dx,KC)) d1++;
        if(((arr(KC,dx)+K)>>23)!=((arr(dx,KC)+K)>>23)) d2++; }
      printf("\n== operand-order effect ==\narr(KC,dx) != arr(dx,KC) for %d of 1024 t ; the I1 latch differs for %d of 1024 t\n",d1,d2); }

    // ---- 3. jwin scores for every rid x latch rule ----
    const char* rn_[4]={"BASE   arr(KC,dx),+K on 2nd","CLAIM A arr(dx,KC),+K on 2nd","CLAIM B arr(KC,dx),NO K 2nd","       arr(dx,KC),NO K 2nd"};
    const char* ln_[3]={"floor","towardzero","onescompl"};
    puts("\n== jwin score (out of 1022 feasible) ==");
    for(int rid=0;rid<4;rid++) for(int r2=0;r2<3;r2++){
        int sc=0; for(int i=0;i<1024;i++){ if(!fOK[i])continue; i64 J=Jrule(i-512,rid,r2); if(J>=fJL[i]&&J<=fJH[i])sc++; }
        printf("  %-28s  latch=%-10s  %4d\n",rn_[rid],ln_[r2],sc); }

    // ---- 4. full model, b2 objective ----
    puts("\n== full model, b2 objective (C0 swept t0win+-3, u2 swept derived+-1, per table entry) ==");
    for(int rid=0;rid<4;rid++) for(int r2=0;r2<3;r2++){
        int per[17]; i64 c0[17]; int u2[17];
        int g=model_score(rid,r2,0,nullptr,nullptr,per,c0,u2);
        int guard=per[0]+per[1]+per[2]+per[16];
        int umoved=0; for(int n=1;n<=16;n++) if(u2[n]!=TB[n].u2) umoved++;
        printf("  %-28s latch=%-10s total=%4d guard(0,1,2,16)=%d  u2moved=%d\n",rn_[rid],ln_[r2],g,guard,umoved); }

    // ---- 5. full model with NO fitting at all (C0/u2 fixed to b12's published tables) ----
    puts("\n== full model, NO fit (C0 = b12 table, u2 = b12 table) ==");
    for(int rid=0;rid<4;rid++) for(int r2=0;r2<3;r2++){
        int per[17]; int g=model_score(rid,r2,1,C0TAB_B12,U2TAB_B12,per,nullptr,nullptr);
        printf("  %-28s latch=%-10s total=%4d guard=%d\n",rn_[rid],ln_[r2],g,per[0]+per[1]+per[2]+per[16]); }

    // ---- 6. full model with C0 forced to lie inside the t=0 window, u2 = derived ----
    puts("\n== full model, C0 restricted to the t=0 window, u2 = derived rule ==");
    for(int rid=0;rid<4;rid++) for(int r2=0;r2<3;r2++){
        int per[17]; i64 c0[17]; int g=model_score(rid,r2,2,nullptr,nullptr,per,c0,nullptr);
        printf("  %-28s latch=%-10s total=%4d guard=%d\n",rn_[rid],ln_[r2],g,per[0]+per[1]+per[2]+per[16]); }
    return 0; }
