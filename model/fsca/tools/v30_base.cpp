// v30_base.cpp -- ADVERSARIAL VERIFIER (branch 30), independent of w3_common.h.
// Rebuilds the fine-scale latch windows from the raw dump, checks them against the published
// jwin18.txt, and scores the baseline square chain in BOTH operand orders, at several s2.
#include "base2.h"
#include <cstring>
#include <cstdio>
#include <vector>
#include <algorithm>

static Fsca F;
// pinned tables, copied from b39.cpp (checked independently below against b2.cpp's fit)
static const i64 C0T[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2T[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};

static void winof(int a, i64&lo, i64&hi){ Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} }

// ---- my own window generator.  DU = extra latch bits (0 = coarse shift20, 2 = fine shift18) ----
static i64 GL[1024], GH[1024];
static void gen(int DU){
    const i64 LIM = 12000000ll<<DU;
    for(int t=-512;t<=511;t++){
        i64 dx=(i64)t<<13; i64 gl=0, gh=LIM;
        i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
        for(int node=1;node<=31;node++){
            int flip=node>=17, n=flip?32-node:node; if(!TB[n].S) continue;
            const Tab&T=TB[n]; i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 T3=T.c3?  sm(arr(T.c3<<5,A3)+K,26):0;
            i64 base=C0T[n]+s*T1-s*T3;
            int a=(node*1024+t)&0xffff; i64 lo,hi; winof(a,lo,hi);
            i64 t2lo=base-hi, t2hi=base-lo; int u=U2T[n]+DU;
            // linear scan replaced by binary search; sm(arr(m,S)+K,u) is nondecreasing in m>=0 -- VERIFIED below
            i64 a1=0,b1=LIM+1; while(a1<b1){ i64 m=(a1+b1)/2; if(sm(arr(m,T.S)+K,u)>=t2lo) b1=m; else a1=m+1; }
            i64 a2=-1,b2=LIM;  while(a2<b2){ i64 m=(a2+b2+1)/2; if(sm(arr(m,T.S)+K,u)<=t2hi) a2=m; else b2=m-1; }
            if(a1>gl) gl=a1; if(a2<gh) gh=a2;
        }
        GL[t+512]=gl; GH[t+512]=gh;
    }
}
// ---- BRUTE-FORCE cross-check of one row: no binary search, no monotonicity assumption ----
static void brute_row(int t,int DU,i64 lo_hint,i64 hi_hint,i64&bl,i64&bh){
    i64 dx=(i64)t<<13; i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
    i64 L = lo_hint>2000 ? lo_hint-2000 : 0, H = hi_hint+2000;
    bl=-1; bh=-1;
    for(i64 J=L;J<=H;J++){
        bool ok=true;
        for(int node=1;node<=31 && ok;node++){
            int flip=node>=17,n=flip?32-node:node; if(!TB[n].S) continue;
            const Tab&T=TB[n]; i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 T3=T.c3?  sm(arr(T.c3<<5,A3)+K,26):0;
            i64 T2=sm(arr(J,T.S)+K,U2T[n]+DU);
            i64 V=C0T[n]+s*T1-T2-s*T3;
            int a=(node*1024+t)&0xffff; i64 lo,hi; winof(a,lo,hi);
            if(V<lo||V>hi) ok=false;
        }
        if(ok){ if(bl<0) bl=J; bh=J; }
    }
}

static i64 FL[1024], FH[1024];
static bool load_jwin(){ FILE*f=fopen("jwin18.txt","r"); if(!f) return false; char ln[256]; int got=0;
    for(int i=0;i<1024;i++){FL[i]=-1;FH[i]=-2;}
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h;
        if(sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3){ if(t<-512||t>511){printf("BAD t %lld\n",t);} FL[t+512]=l; FH[t+512]=h; got++; } }
    fclose(f); printf("jwin18.txt rows parsed: %d\n",got); return got==1024; }

// baseline chain, both operand orders
static inline i64 Jchain(int t,int order,int s1,int s2){
    i64 dx=(i64)t<<13;
    i64 I1 = order? (arr(dx,KC)+K)>>s1 : (arr(KC,dx)+K)>>s1;
    i64 J  = order? (arr(dx,I1)+K)>>s2 : (arr(I1,dx)+K)>>s2;
    return J;
}
static int score(int order,int s1,int s2,const i64*L,const i64*H){
    int s=0; for(int t=-512;t<=511;t++){ i64 j=Jchain(t,order,s1,s2); if(j>=L[t+512]&&j<=H[t+512]) s++; } return s; }

// full model with pluggable J and DU
template<class FN> static int fullv(FN Jf,int DU,int&guard){
    int v=0; guard=0;
    for(int node=0;node<=31;node++){
        int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n]; i64 s=flip?-1:1;
        for(int t=-512;t<=511;t++){
            i64 dx=(i64)t<<13;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
            i64 T3=T.c3?  sm(arr(T.c3<<5,A3)+K,26):0;
            i64 J=Jf(t);
            i64 T2=T.S? sm(arr(J,T.S)+K,U2T[n]+DU):0;
            i64 V=C0T[n]+s*T1-T2-s*T3;
            int a=(node*1024+t)&0xffff; i64 lo,hi; winof(a,lo,hi);
            if(V<lo||V>hi){ v++; if(node==0||node==1||node==2||node==16) guard++; }
        }
    }
    return v; }

int main(int argc,char**argv){
    if(!load_fsca(F)){ printf("no dump\n"); return 1; }
    build_tables();
    if(!load_jwin()){ printf("jwin18 load FAILED\n"); return 2; }

    // (0) Soundness of the binary search: is sm(arr(m,S)+K,u) nondecreasing in m over the whole range?
    //     arr(m,S) = m*S + loss(m mod 2^18, S mod 2^16) (measured law), so the first difference has
    //     period 2^18 in m.  Verify BOTH: (a) the difference really is 2^18-periodic over [0,2^20),
    //     (b) no decrease anywhere in one full period plus the wrap.
    {
        int nonper=0, dec=0;
        for(int n=1;n<=16;n++){ if(!TB[n].S) continue; i64 S=TB[n].S; int u=U2T[n]+2;
            std::vector<i64> d(1ll<<18);
            for(i64 m=0;m<(1ll<<18);m++) d[m]=arr(m+1,S)-arr(m,S);
            for(i64 m=(1ll<<18); m<(1ll<<20); m++){ if(arr(m+1,S)-arr(m,S)!=d[m&((1ll<<18)-1)]) nonper++; }
            i64 prev=sm(arr(0,S)+K,u);
            for(i64 m=1;m<=(1ll<<19);m++){ i64 c=sm(arr(m,S)+K,u); if(c<prev) dec++; prev=c; }
        }
        printf("(0) first-difference 2^18-periodicity breaks: %d ; sm(arr(m,S)+K,u) decreases over m in [0,2^19]: %d\n",nonper,dec);
    }

    // (1) regenerate fine windows and diff against the published file
    gen(2);
    int diff=0, firstdiff=-1;
    for(int i=0;i<1024;i++) if(GL[i]!=FL[i]||GH[i]!=FH[i]){ diff++; if(firstdiff<0) firstdiff=i-512; }
    printf("(1) my gen(DU=2) vs jwin18.txt: %d of 1024 rows differ", diff);
    if(diff) printf("  first at t=%d: mine[%lld,%lld] file[%lld,%lld]",firstdiff,(long long)GL[firstdiff+512],(long long)GH[firstdiff+512],(long long)FL[firstdiff+512],(long long)FH[firstdiff+512]);
    printf("\n");
    int feas=0; i64 sw=0,tight=0; for(int i=0;i<1024;i++){ if(FL[i]<=FH[i]){feas++;sw+=FH[i]-FL[i]+1; if(FH[i]-FL[i]<=3)tight++;} }
    printf("    published file: %d/1024 feasible, %lld pinned width<=4, mean width %.1f\n",feas,(long long)tight,(double)sw/feas);

    // (2) brute-force cross-check of the three pinned rows (no binary search)
    for(int k=0;k<3;k++){ int t=(int[]){17,342,-342}[k]; i64 bl,bh; brute_row(t,2,FL[t+512],FH[t+512],bl,bh);
        printf("(2) brute t=%4d: [%lld,%lld]   file [%lld,%lld]  %s\n",t,(long long)bl,(long long)bh,
            (long long)FL[t+512],(long long)FH[t+512], (bl==FL[t+512]&&bh==FH[t+512])?"MATCH":"*** DIFFER ***"); }

    // (3) baseline chain scores and pins, both operand orders, s2 in {17,18,19,20}
    printf("(3) baseline chain scores against jwin18.txt (s1=23):\n");
    for(int order=0;order<2;order++) for(int s2=17;s2<=20;s2++){
        int sc=score(order,23,s2,FL,FH);
        printf("    order=%s s2=%2d  score %4d/1024   J(17)=%lld  J(342)=%lld  J(-342)=%lld\n",
            order? "arr(dx,KC)/arr(dx,I1)":"arr(KC,dx)/arr(I1,dx)", s2, sc,
            (long long)Jchain(17,order,23,s2),(long long)Jchain(342,order,23,s2),(long long)Jchain(-342,order,23,s2)); }

    // (4) full-model counts: coarse (DU=0, s2=20) and fine (DU=2, s2=18), both orders
    printf("(4) full model over nodes 0..31:\n");
    for(int order=0;order<2;order++){
        { int g; int v=fullv([&](int t){return Jchain(t,order,23,20);},0,g);
          printf("    coarse DU=0 s2=20 order=%d : violations %d , guard(0,1,2,16) %d\n",order,v,g); }
        { int g; int v=fullv([&](int t){return Jchain(t,order,23,18);},2,g);
          printf("    fine   DU=2 s2=18 order=%d : violations %d , guard(0,1,2,16) %d\n",order,v,g); }
    }
    return 0; }
