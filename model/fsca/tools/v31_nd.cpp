// v31_nd.cpp -- ADVERSARIAL VERIFIER for the claim:
//   "ND in 14..18 produces ZERO differences in I1 or J over all 1024 offsets, so Booth digit
//    count above 14 is unobservable in the square chain; only arr(J,S_n) ever exceeds 13 digits,
//    and only at 101 of 1024 offsets."
// Written from scratch.  Own Booth array (128-bit accumulator), own jwin18 loader, own scorers.
#include "base2.h"
#include <cstring>
#include <cstdio>
#include <random>

typedef __int128 i128;

// ---------------------------------------------------------------- my own parameterised array
// Semantics copied from base2.h::arr, but with an explicit digit count and a 128-bit row value
// so that a large ND cannot silently overflow (base2's i64 would).
static i64 v31_arr(i64 rec, i64 mul, int ND) {
    i128 R[48]; i128 T = 0;
    for (int k = 0; k < ND; k++) {
        auto bit = [&](int b) -> int { return b < 0 ? 0 : (int)((rec >> (b > 62 ? 62 : b)) & 1); };
        int d = -2*bit(2*k+1) + bit(2*k) + bit(2*k-1);
        i128 mag = (i128)(d < 0 ? -d : d) * mul;
        i128 row = d < 0 ? ~mag : mag;
        R[k] = row * ((i128)1 << (2*k));
        T += (R[k] >> 16) << 16;
        if (d < 0 && 2*k >= 18) T += (i128)1 << (2*k);
    }
    if (ND >= 3) { int m15 = (int)((R[0]>>15)&1) + (int)((R[1]>>15)&1) + (int)((R[2]>>15)&1);
                   T += ((i128)1 << 16) * (m15 >= 2); }
    if (ND >= 9) { int p16 = (int)(((R[6]>>16) ^ (R[7]>>16) ^ (R[8]>>16)) & 1);
                   T -= ((i128)1 << 16) * p16; }
    return (i64)T;
}
// minimal digit count that already equals the wide-array answer (measured, not from a bit formula)
static int v31_min_nd(i64 rec, i64 mul, int NDmax = 30) {
    i64 ref = v31_arr(rec, mul, NDmax);
    for (int nd = 1; nd <= NDmax; nd++) if (v31_arr(rec, mul, nd) == ref) return nd;
    return -1;
}

// ---------------------------------------------------------------- dump + windows + tables
static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={30,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a, i64& lo, i64& hi) { Win w = decode(F.srz[a], F.srn[a]);
    if (w.sign > 0) { lo = w.lo; hi = w.hi; } else { lo = -w.hi; hi = -w.lo; } }

static i64 JL[1024], JH[1024];
static bool load_jwin() {
    FILE* f = fopen("jwin18.txt","r"); if (!f) return false; char ln[256]; int n = 0;
    for (int i=0;i<1024;i++) JL[i]=-1;
    while (fgets(ln,sizeof ln,f)) { if (ln[0]=='#') continue; long long t,l,h;
        if (sscanf(ln,"%lld %lld %lld",&t,&l,&h)==3) { if(t<-512||t>511){fclose(f);return false;} JL[t+512]=l; JH[t+512]=h; n++; } }
    fclose(f); if (n != 1024) { printf("jwin18.txt: %d rows, expected 1024\n", n); return false; }
    for (int i=0;i<1024;i++) if (JL[i] < 0) return false;
    return true;
}

// ---------------------------------------------------------------- baseline square chain
// order A: I1 = (arr(KC,dx)+K)>>23 ; order B: I1 = (arr(dx,KC)+K)>>23
static inline i64 chain_J(int t, int nd_sq1, int nd_sq2, int order, int s2) {
    i64 dx = (i64)t << 13;
    i64 I1 = order==0 ? (v31_arr(KC,dx,nd_sq1)+K)>>23 : (v31_arr(dx,KC,nd_sq1)+K)>>23;
    return (v31_arr(I1,dx,nd_sq2)+K) >> s2;
}
static inline i64 chain_I1(int t, int nd_sq1, int order) {
    i64 dx = (i64)t << 13;
    return order==0 ? (v31_arr(KC,dx,nd_sq1)+K)>>23 : (v31_arr(dx,KC,nd_sq1)+K)>>23;
}

// jwin18 score at the FINE scale (s2 = 18)
static int score_jwin(int nd1,int nd2,int order) {
    int s=0; for (int t=-512;t<=511;t++){ i64 j=chain_J(t,nd1,nd2,order,18); if (j>=JL[t+512]&&j<=JH[t+512]) s++; }
    return s;
}

// ---------------------------------------------------------------- full model, coarse published scale
// ND_* let every call site's digit count be set independently.
struct NDs { int t1=14,t3a=14,t3b=14,sq1=14,sq2=14,t2=14; };
static int full_viol(const NDs& nd, int order, int* guard_out) {
    int v=0, guard=0;
    for (int node=0;node<=31;node++){
        int flip = node>=17, n = flip?32-node:node; const Tab& T = TB[n]; i64 s = flip?-1:1;
        for (int t=-512;t<=511;t++){
            i64 dx = (i64)t<<13;
            i64 T1 = T.C1f ? sm(v31_arr(T.C1f,dx,nd.t1)+K, 23-T.e1) : 0;
            i64 A3 = ((v31_arr(((i64)t*t)<<3,dx,nd.t3a)+K)>>20)>>3;
            i64 T3 = T.c3 ? sm(v31_arr(T.c3<<5,A3,nd.t3b)+K, 26) : 0;
            i64 I1 = order==0 ? (v31_arr(KC,dx,nd.sq1)+K)>>23 : (v31_arr(dx,KC,nd.sq1)+K)>>23;
            i64 J  = (v31_arr(I1,dx,nd.sq2)+K)>>20;
            i64 T2 = T.S ? sm(v31_arr(J,T.S,nd.t2)+K, U2TAB[n]) : 0;
            i64 V = C0TAB[n] + s*T1 - T2 - s*T3;
            int a = (node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
            if (V<lo||V>hi){ v++; if(node==0||node==1||node==2||node==16) guard++; }
        }
    }
    *guard_out = guard; return v;   // sequenced: guard read after the call returns
}

int main(){
    if(!load_fsca(F)){ puts("no dump"); return 1; }
    build_tables();
    if(!load_jwin()){ puts("jwin18.txt load failed"); return 1; }

    puts("=========== 0. HARNESS VALIDATION (my own code) ===========");
    // 0a. my array at ND=14 must equal base2.h's arr on every operand pair the model ever forms,
    //     plus a large random sample inside the plausible operand box.
    {
        long long checked=0, bad=0;
        for (int t=-512;t<=511;t++){
            i64 dx=(i64)t<<13;
            i64 I1a=(arr(KC,dx)+K)>>23, I1b=(arr(dx,KC)+K)>>23;
            i64 J18=(arr(I1a,dx)+K)>>18, J20=(arr(I1a,dx)+K)>>20;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
            auto chk=[&](i64 r,i64 m){ checked++; if (v31_arr(r,m,14)!=arr(r,m)) bad++; };
            chk(KC,dx); chk(dx,KC); chk(I1a,dx); chk(I1b,dx);
            chk(((i64)t*t)<<3,dx);
            for(int n=0;n<=16;n++){ if(TB[n].C1f) chk(TB[n].C1f,dx); if(TB[n].c3) chk(TB[n].c3<<5,A3);
                if(TB[n].S){ chk(J18,TB[n].S); chk(J20,TB[n].S); } }
        }
        std::mt19937_64 rng(12345);
        for (long long i=0;i<400000;i++){
            i64 r=(i64)(rng()%(1ull<<27))-(1ll<<26), m=(i64)(rng()%(1ull<<25))-(1ll<<24);
            checked++; if (v31_arr(r,m,14)!=arr(r,m)) bad++;
        }
        printf("0a. v31_arr(.,.,14) vs base2 arr(): %lld pairs checked, %lld mismatches\n", checked, bad);
        if (bad) { puts("    HARNESS BROKEN -- stop"); return 1; }
    }
    // 0b. regenerate jwin18 windows with my own code and diff against the published file
    {
        int rows_diff=0; const int DU=2; const i64 LIM=12000000ll<<DU;
        for (int t=-512;t<=511;t++){
            i64 dx=(i64)t<<13; i64 gl=0, gh=LIM;
            i64 A3=((v31_arr(((i64)t*t)<<3,dx,14)+K)>>20)>>3;
            for (int node=1;node<=31;node++){
                int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
                const Tab&T=TB[n]; i64 s=flip?-1:1;
                i64 T1=T.C1f? sm(v31_arr(T.C1f,dx,14)+K,23-T.e1):0;
                i64 T3=T.c3? sm(v31_arr(T.c3<<5,A3,14)+K,26):0;
                i64 base=C0TAB[n]+s*T1-s*T3; int a=(node*1024+t)&0xffff; i64 lo,hi; win_of(a,lo,hi);
                i64 t2lo=base-hi,t2hi=base-lo; int u=U2TAB[n]+DU;
                i64 a1=0,b1=LIM+1; while(a1<b1){i64 m=(a1+b1)/2; if(sm(v31_arr(m,T.S,14)+K,u)>=t2lo)b1=m;else a1=m+1;}
                i64 a2=-1,b2=LIM;   while(a2<b2){i64 m=(a2+b2+1)/2; if(sm(v31_arr(m,T.S,14)+K,u)<=t2hi)a2=m;else b2=m-1;}
                if(a1>gl)gl=a1; if(a2<gh)gh=a2;
            }
            if (gl!=JL[t+512] || gh!=JH[t+512]) rows_diff++;
        }
        printf("0b. my regenerated windows vs jwin18.txt: %d of 1024 rows differ\n", rows_diff);
    }
    // 0c. pinned landmarks and the baseline score
    printf("0c. landmarks from the FILE: J(17) in [%lld,%lld]  J(342) in [%lld,%lld]  J(-342) in [%lld,%lld]\n",
        (long long)JL[17+512],(long long)JH[17+512],(long long)JL[342+512],(long long)JH[342+512],
        (long long)JL[-342+512],(long long)JH[-342+512]);
    printf("    baseline chain J: J(17)=%lld  J(342)=%lld  J(-342)=%lld\n",
        (long long)chain_J(17,14,14,0,18),(long long)chain_J(342,14,14,0,18),(long long)chain_J(-342,14,14,0,18));
    printf("    baseline jwin18 score: order arr(KC,dx) %d/1024   order arr(dx,KC) %d/1024\n",
        score_jwin(14,14,0), score_jwin(14,14,1));
    { NDs nd; int g0=0,g1=0; int vA=full_viol(nd,0,&g0); int vB=full_viol(nd,1,&g1);
      printf("    baseline full model (coarse, shift 20): arr(KC,dx) %d viol guard %d | arr(dx,KC) %d viol guard %d\n", vA,g0,vB,g1); }

    puts("");
    puts("=========== 1. THE CLAIM: ND 14..18 changes nothing in I1 or J ===========");
    // 1a. exactly the claimed test, my code, BOTH operand orders, BOTH latch scales
    for (int order=0;order<2;order++){
        for (int s2=18;s2<=20;s2+=2){
            int diffI=0,diffJ=0;
            for (int ndv=14; ndv<=18; ndv++)
                for (int t=-512;t<=511;t++){
                    if (chain_I1(t,ndv,order)!=chain_I1(t,14,order)) diffI++;
                    if (chain_J(t,ndv,ndv,order,s2)!=chain_J(t,14,14,order,s2)) diffJ++;
                }
            printf("1a. order %s  latch>>%d : ND 14..18 vs ND=14 -> I1 diffs %d, J diffs %d (of %d comparisons each)\n",
                order?"arr(dx,KC)":"arr(KC,dx)", s2, diffI, diffJ, 5*1024);
        }
    }
    // 1b. widen: ND up to 24, and also mixed (sq1 != sq2)
    {
        int worst=0;
        for (int a=13;a<=24;a++) for (int b=13;b<=24;b++) {
            int d=0; for (int t=-512;t<=511;t++) if (chain_J(t,a,b,0,18)!=chain_J(t,14,14,0,18)) d++;
            if (a>=14&&b>=14&&d>worst) worst=d;
        }
        printf("1b. widest sweep sq1,sq2 in 14..24 (independent): max J diffs vs (14,14) over 1024 offsets = %d\n", worst);
    }
    // 1c. per-ND jwin18 score and full-model violations -- does a bigger array EVER move any number?
    puts("1c. ND applied to EVERY call site:");
    printf("    %3s  %-12s  %-28s %s\n","ND","jwin18/1024","full viol (KC,dx) / guard","landmarks J(17),J(342),J(-342)");
    for (int ndv=10; ndv<=20; ndv++){
        NDs nd; nd.t1=nd.t3a=nd.t3b=nd.sq1=nd.sq2=nd.t2=ndv;
        int g=0; int v=full_viol(nd,0,&g);
        printf("    %3d  %11d  %20d / %-5d %lld, %lld, %lld\n", ndv, score_jwin(ndv,ndv,0), v, g,
            (long long)chain_J(17,ndv,ndv,0,18),(long long)chain_J(342,ndv,ndv,0,18),(long long)chain_J(-342,ndv,ndv,0,18));
    }

    puts("");
    puts("=========== 2. HOW MANY DIGITS DOES EACH CALL SITE ACTUALLY NEED? ===========");
    // measured minimal ND (result equal to a 30-digit array), per call site, over all offsets/nodes
    {
        struct Acc { int mx=0; long long over13=0, over12=0; };
        Acc a_t1, a_t3a, a_t3b, a_sq1A, a_sq1B, a_sq2, a_t2_18, a_t2_20;
        auto upd=[&](Acc& A, i64 rec, i64 mul){ int m=v31_min_nd(rec,mul); if(m>A.mx)A.mx=m; if(m>13)A.over13++; if(m>12)A.over12++; };
        int j18_ge25=0, j20_ge25=0;
        for (int t=-512;t<=511;t++){
            i64 dx=(i64)t<<13;
            i64 I1=(v31_arr(KC,dx,14)+K)>>23;
            i64 J18=(v31_arr(I1,dx,14)+K)>>18, J20=(v31_arr(I1,dx,14)+K)>>20;
            if (J18 >= (1ll<<25)) j18_ge25++;
            if (J20 >= (1ll<<25)) j20_ge25++;
            i64 A3=((v31_arr(((i64)t*t)<<3,dx,14)+K)>>20)>>3;
            upd(a_sq1A,KC,dx); upd(a_sq1B,dx,KC); upd(a_sq2,I1,dx); upd(a_t3a,((i64)t*t)<<3,dx);
            for(int n=0;n<=16;n++){ if(TB[n].C1f) upd(a_t1,TB[n].C1f,dx); if(TB[n].c3) upd(a_t3b,TB[n].c3<<5,A3);
                if(TB[n].S){ upd(a_t2_18,J18,TB[n].S); upd(a_t2_20,J20,TB[n].S); } }
        }
        auto pr=[&](const char* nm, Acc&A, long long tot){ printf("    %-22s max min-ND %2d   needing >13: %lld/%lld   >12: %lld/%lld\n",
            nm, A.mx, A.over13, tot, A.over12, tot); };
        pr("arr(C1f,dx)        ", a_t1, 1024LL*16);
        pr("arr(t*t<<3,dx)     ", a_t3a, 1024);
        pr("arr(c3<<5,A3)      ", a_t3b, 1024LL*16);
        pr("arr(KC,dx)         ", a_sq1A, 1024);
        pr("arr(dx,KC)         ", a_sq1B, 1024);
        pr("arr(I1,dx)         ", a_sq2, 1024);
        pr("arr(J,S) fine s2=18", a_t2_18, 1024LL*16);
        pr("arr(J,S) coarse s20", a_t2_20, 1024LL*16);
        printf("    offsets with J>=2^25 : fine(>>18) %d/1024   coarse(>>20) %d/1024\n", j18_ge25, j20_ge25);
        // distinct offsets where arr(J,S) needs >13 digits (the claim counts OFFSETS, not (offset,node) pairs)
        int off=0; for (int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 I1=(v31_arr(KC,dx,14)+K)>>23;
            i64 J18=(v31_arr(I1,dx,14)+K)>>18; bool any=false;
            for(int n=1;n<=16;n++) if(TB[n].S && v31_min_nd(J18,TB[n].S)>13) any=true;
            if(any) off++; }
        printf("    OFFSETS where arr(J,S) needs >13 digits at the fine scale: %d/1024\n", off);
    }

    puts("");
    puts("=========== 3. STRUCTURAL PROOF CHECK (not bounded-constant) ===========");
    // The extra rows of a wider array vanish iff every added Booth digit is 0.  Verify the digit
    // predicate directly on every operand the chain forms, for all k >= 13, and separately verify
    // that a zero digit contributes exactly nothing to T (row and hot-one both 0).
    {
        long long zero_digit_checks=0, nonzero=0;
        auto digits_zero_above=[&](i64 rec,int kmin,int kmax){
            for(int k=kmin;k<=kmax;k++){ auto bit=[&](int b)->int{ return b<0?0:(int)((rec>>(b>62?62:b))&1); };
                int d=-2*bit(2*k+1)+bit(2*k)+bit(2*k-1); zero_digit_checks++; if(d) nonzero++; } };
        for (int t=-512;t<=511;t++){
            i64 dx=(i64)t<<13; i64 I1=(v31_arr(KC,dx,14)+K)>>23;
            i64 J18=(v31_arr(I1,dx,14)+K)>>18;
            digits_zero_above(KC,13,29); digits_zero_above(dx,13,29); digits_zero_above(I1,13,29);
            digits_zero_above(J18,14,29);           // J needs digit 13, so check from 14
        }
        printf("3a. Booth digits above the needed count, over every chain operand: %lld checked, %lld nonzero\n",
            zero_digit_checks, nonzero);
        printf("3b. a zero digit gives mag=0 -> row=0 -> R[k]=0 -> (R[k]>>16)<<16 = 0, and d<0 is false so no hot one.\n");
        printf("    m15 reads R[0..2] and p16 reads R[6..8], both fixed indices, so ND>=9 leaves them untouched.\n");
        printf("    => adding digits is EXACTLY a no-op whenever the added digits are 0, for ALL operands (unbounded).\n");
    }

    puts("");
    puts("=========== 4. WHERE THE FREEDOM IS NOT EMPTY ===========");
    // The claim says digit count above 14 is unobservable.  Test the converse edge: does ND=13
    // actually break the FINE chain, and is the published coarse model really ND-13-clean?
    {
        NDs nd13; nd13.t1=nd13.t3a=nd13.t3b=nd13.sq1=nd13.sq2=nd13.t2=13;
        int g=0; int v=full_viol(nd13,0,&g);
        printf("4a. ND=13 everywhere: coarse full model %d viol, guard %d ; jwin18 %d/1024\n", v, g, score_jwin(13,13,0));
        // ND=13 only at the T2 site (the only site with a >13-digit operand at the fine scale)
        int s=0; for(int t=-512;t<=511;t++){ i64 j=chain_J(t,14,14,0,18); if(j>=JL[t+512]&&j<=JH[t+512])s++; }
        printf("4b. (chain itself is ND-independent, so jwin18 at ND=13 differs only via the WINDOWS, not J: %d)\n", s);
    }
    return 0;
}
