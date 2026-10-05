// fv_final-fields_1_base.cpp -- INDEPENDENT verification harness, written from the description only.
// Does NOT include f1_*.  Uses base2.h (pre-existing model + derived tables) and fsca_common.h (dump).
// Purpose: (a) reproduce the published baselines, (b) test the claimed 147 -> 146 "one's-complement
// square term", (c) localise it, (d) score both over the whole 65536-angle dump.
#include "base2.h"
#include <cstring>
#include <vector>
static Fsca F;
// the pinned hardware C0 table (26-bit), frozen -- no per-node refitting anywhere in this file
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,
                            51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static inline void win_of(int a,i64&lo,i64&hi){ Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} }
// sgn: 0 = published  T2 = sm(P+K,u)          (P>=0 so = (P+K)>>u)
//      1 = one's-complement negative product   T2 = (P+K-1)>>u
static inline i64 T2of(int sgn,i64 J,i64 S,int u){ if(!S) return 0; i64 P=arr(J,S)+K;
    if(sgn==0) return sm(P,u); i64 Q=-P; return -(-((~Q)>>u)); }
// full model value; ro=1 -> arr(dx,KC) (the published tie-break), ro=0 -> arr(KC,dx)
static inline i64 Vof(int n,int t,int flip,int ro,int sgn){ const Tab&T=TB[n]; i64 dx=(i64)t<<13;
    i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
    i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
    i64 I1=((ro?arr(dx,KC):arr(KC,dx))+K)>>23, J=(arr(I1,dx)+K)>>20;
    i64 T2=T2of(sgn,J,T.S,T.u2); i64 s=flip?-1:1; return C0TAB[n]+s*T1-T2-s*T3; }
static void score(int ro,int sgn,const char*tag){ int tot=0,g[6]={0,0,0,0,0,0};
    const int gn[6]={0,1,2,16,30,31};
    for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; int v=0;
        for(int t=-512;t<=511;t++){ int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
            i64 V=Vof(n,t,flip,ro,sgn); if(V<lo||V>hi)v++; }
        tot+=v; for(int i=0;i<6;i++) if(gn[i]==node) g[i]=v; }
    printf("%-46s total=%4d   guards n0=%d n1=%d n2=%d n16=%d n30=%d n31=%d\n",tag,tot,g[0],g[1],g[2],g[3],g[4],g[5]); }
static void dumpscore(int ro,int sgn,const char*tag){ int rn=0,rz=0,wv=0;
    for(int a=0;a<65536;a++){ int q=(a+512)>>10,m=q&63,t=a-1024*q; if(t<-512)t+=65536;
        int neg=0,flip=0; int mm=m; if(mm>=33){mm-=32;neg=1;} if(mm>=17){mm=32-mm;flip=1;}
        i64 V=Vof(mm,t,flip,ro,sgn); if(neg)V=-V;
        if(to_float(V,0)!=F.srn[a])rn++; if(to_float(V,1)!=F.srz[a])rz++;
        i64 lo,hi; Win w=decode(F.srz[a],F.srn[a]); if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}
        if(V<lo||V>hi)wv++; }
    printf("%-46s whole dump: RN %d / RZ %d float mismatches, %d window violations (of 65536)\n",tag,rn,rz,wv); }
int main(){ if(!load_fsca(F)){puts("dump load failed");return 1;} build_tables();
    // frozen-table audit: the S/u2 fields used below are the derived ones, printed so they can be eyeballed
    printf("derived S/u2 (one rule: S=RN(sin*2^(23+k)) clamped, u2=30+k):\n  ");
    for(int n=0;n<=16;n++) printf("%lld/%d ",(long long)TB[n].S,TB[n].u2); printf("\n\n");
    puts("== A. baselines, nodes 0..31, published chain, coefficient tables FROZEN ==");
    score(0,0,"arr(KC,dx), published square term");
    score(1,0,"arr(dx,KC), published square term  [BASELINE]");
    puts("\n== B. the claimed consistency fix: T2 = (arr(J,S)+2^19-1)>>u2 ==");
    score(1,1,"arr(dx,KC), one's-complement square term");
    score(0,1,"arr(KC,dx), one's-complement square term");
    puts("\n== C. where do the two square rules differ at all? (node,t) over 0..31 x [-512,511] ==");
    { int ndiff=0,fix=0,brk=0;
      for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 I1=(arr(dx,KC)+K)>>23, J=(arr(I1,dx)+K)>>20;
            i64 A=T2of(0,J,TB[n].S,TB[n].u2), B=T2of(1,J,TB[n].S,TB[n].u2);
            if(A==B) continue; ndiff++;
            int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
            i64 Va=Vof(n,t,flip,1,0), Vb=Vof(n,t,flip,1,1);
            int ka=(Va<lo||Va>hi), kb=(Vb<lo||Vb>hi);
            printf("  node %2d t=%+4d  J=%lld  T2 %lld -> %lld   window [%lld,%lld]  V %lld -> %lld  %s\n",
                node,t,(long long)J,(long long)A,(long long)B,(long long)lo,(long long)hi,
                (long long)Va,(long long)Vb, ka&&!kb?"FIXES a violation":(!ka&&kb?"BREAKS a hit":"no change"));
            if(ka&&!kb)fix++; if(!ka&&kb)brk++; }
      }
      printf("  differing (node,t) pairs: %d of %d   fixes %d   breaks %d\n",ndiff,32*1024,fix,brk); }
    puts("\n== D. whole-dump scores ==");
    dumpscore(1,0,"published square term");
    dumpscore(1,1,"one's-complement square term");
    return 0; }
