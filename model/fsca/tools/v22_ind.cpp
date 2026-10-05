// v22_ind.cpp -- INDEPENDENT adversarial re-implementation of the jwin18 score and the full-model
// score. Written from base2.h + the jwin18.txt contract only; does not reuse w2_*.cpp code.
// Extra: optionally REFIT C0 and u2 per table entry (as b2.cpp does) so guard-node claims can be
// checked against a rule-consistent table rather than the baseline-fitted one.
#include "base2.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>
static Fsca F;
// The pinned t=0 table values (CURRENT.md 3; identical in b39.cpp which BUILT jwin18.txt).
static const i64 C0P[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,
                          51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2P[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};

struct Cfg { int w,s1,s2; i64 kc,K1,K2; int du; };
static inline i64 Jrule(int t,const Cfg&c){ i64 dx=(i64)t<<c.w;
    i64 I1=(arr(c.kc,dx)+c.K1)>>c.s1; return (arr(I1,dx)+c.K2)>>c.s2; }

// window of the internal signed value at a raw 16-bit angle
static inline void win_of(int a,i64&lo,i64&hi){ Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} }

// full model value; C0/u2 passed in so they can be refit
static inline i64 Vof(int tn,int t,int flip,i64 C0,int u2,const Cfg&c){ const Tab&T=TB[tn];
    i64 dx=(i64)t<<13;
    i64 T1 = T.C1f ? sm(arr(T.C1f,dx)+K, 23-T.e1) : 0;
    i64 A3 = ((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
    i64 T3 = T.c3 ? sm(arr(T.c3<<5,A3)+K, 26) : 0;
    i64 T2 = T.S ? sm(arr(Jrule(t,c),T.S)+K, u2+c.du) : 0;
    i64 s = flip?-1:1; return C0 + s*T1 - T2 - s*T3; }

// violations for table entry tn at the two physical nodes that use it, with given C0/u2
static int viol_tab(int tn,i64 C0,int u2,const Cfg&c){
    int nodes[2]={tn,32-tn}; int nn=(tn==0||tn==16)?1:2; int v=0;
    for(int i=0;i<nn;i++){ int m=nodes[i]; int flip=(m>=17);
        for(int t=-512;t<=511;t++){ int a=(m*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
            i64 V=Vof(tn,t,flip,C0,u2,c); if(V<lo||V>hi) v++; } }
    return v; }

static i64 JL[1024],JH[1024]; static bool have[1024];
static int load_jwin(){ FILE*f=fopen("jwin18.txt","r"); if(!f){fprintf(stderr,"no jwin18.txt\n");exit(1);} 
    char ln[256]; int nl=0,badw=0,infeas=0;
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; long long t,l,h,wd;
        int k=sscanf(ln,"%lld %lld %lld %lld",&t,&l,&h,&wd);
        if(k<3) continue; if(t<-512||t>511){fprintf(stderr,"bad t %lld\n",t);exit(1);}
        int i=(int)t+512; JL[i]=l; JH[i]=h; have[i]=true; nl++;
        if(k==4 && wd!=h-l+1) badw++;
        if(h<l) infeas++; }
    fclose(f); printf("[jwin18.txt] %d lines, width-column mismatches %d, infeasible t %d\n",nl,badw,infeas);
    if(nl!=1024){fprintf(stderr,"expected 1024 lines\n");exit(1);} return nl; }

static int jscore(const Cfg&c){ int s=0; for(int i=0;i<1024;i++) if(have[i]){ i64 j=Jrule(i-512,c); if(j>=JL[i]&&j<=JH[i])s++; } return s; }

int main(int argc,char**argv){ if(!load_fsca(F)){fprintf(stderr,"no dump\n");return 1;} build_tables();
    load_jwin();
    // sanity: the free-latch bound -- a J chosen per t inside its window must give 0 violations over nodes 1..31
    {
        int v=0; Cfg dummy{13,23,18,KC,K,K,2};
        for(int tn=1;tn<=16;tn++){ int nodes[2]={tn,32-tn}; int nn=(tn==16)?1:2;
            for(int i=0;i<nn;i++){ int m=nodes[i]; int flip=(m>=17);
                for(int t=-512;t<=511;t++){ int a=(m*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
                    i64 J=JL[t+512];                       // any J in the window; take JL
                    const Tab&T=TB[tn]; i64 dx=(i64)t<<13;
                    i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
                    i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
                    i64 T2=T.S? sm(arr(J,T.S)+K,U2P[tn]+2):0; i64 s=flip?-1:1;
                    i64 V=C0P[tn]+s*T1-T2-s*T3; if(V<lo||V>hi)v++; } } }
        printf("[contract check] free per-t latch = JL(t) at shift 18, nodes 1..31: %d violations (expect 0)\n",v);
    }
    struct Named{const char*n;Cfg c;};
    std::vector<Named> R = {
      {"BASELINE coarse w13 s1=23 s2=20 KC K=2^19 du=0", {13,23,20,KC,K,K,0}},
      {"BASELINE fine   w13 s1=23 s2=18 KC K=2^19 du=2", {13,23,18,KC,K,K,2}},
      {"claim fixedK    w14 s1=27 s2=19 KCw=41388583  ", {14,27,19,41388583,K,K,2}},
      {"claim fixedK    w14 s1=28 s2=19 KCw=82777166  ", {14,28,19,82777166,K,K,2}},
      {"CLAIM BEST      w12 s1=26 s2=17 KCw=82777165 K1=5963776 K2=65536", {12,26,17,82777165,5963776,65536,2}},
      {"claim loose     w16 s1=30 s2=21 KCw=82777165 K1=85590016 K2=-2293760", {16,30,21,82777165,85590016,-2293760,2}},
    };
    if(argc>6){ Cfg c; c.w=atoi(argv[1]); c.s1=atoi(argv[2]); c.s2=atoi(argv[3]); c.kc=atoll(argv[4]);
        c.K1=atoll(argv[5]); c.K2=atoll(argv[6]); c.du=argc>7?atoi(argv[7]):2;
        R.push_back({"CLI", c}); }
    printf("\n%-68s | jwin18 | full(fixed tab) guard | full(refit tab) guard | J(17) J(342) J(-342)\n","rule");
    for(auto&nm:R){ const Cfg&c=nm.c;
        int js=jscore(c);
        int vf=0,gf=0, vr=0,gr=0;
        for(int tn=0;tn<=16;tn++){
            int v=viol_tab(tn,C0P[tn],U2P[tn],c); vf+=v; if(tn==0||tn==1||tn==2||tn==16) gf+=v;
            // refit: C0 over the t=0 window +-3, u2 +-1, exactly b2.cpp's search
            i64 l0,h0; win_of(tn*1024,l0,h0); int bv=1<<30;
            for(i64 C0=l0-3;C0<=h0+3;C0++) for(int u=U2P[tn]-1;u<=U2P[tn]+1;u++){
                int vv=viol_tab(tn,C0,u,c); if(vv<bv)bv=vv; }
            vr+=bv; if(tn==0||tn==1||tn==2||tn==16) gr+=bv; }
        printf("%-68s | %4d   | %5d %10d | %5d %10d | %lld %lld %lld\n",nm.n,js,vf,gf,vr,gr,
            (long long)Jrule(17,c),(long long)Jrule(342,c),(long long)Jrule(-342,c));
        fflush(stdout); }
    printf("\nlandmark targets: J(17)=45617 exactly, J(342)=18466836 exactly, J(-342) in [18466858,18466867]\n");
    printf("jwin18 windows: t=17 [%lld,%lld]  t=342 [%lld,%lld]  t=-342 [%lld,%lld]\n",
        (long long)JL[17+512],(long long)JH[17+512],(long long)JL[342+512],(long long)JH[342+512],
        (long long)JL[-342+512],(long long)JH[-342+512]);
    return 0; }
