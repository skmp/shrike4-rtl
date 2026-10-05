// v20_base.cpp -- ADVERSARIAL VERIFIER (branch v20).  Independent re-implementation of the
// baseline square-path chain, the jwin18 scorer, the three pinned landmarks, the array loss law
// and the array's signed-operand range.  Nothing here reuses w2_*.cpp.
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>

static i64 JL[1024], JH[1024]; static bool have[1024]; static int nrows = 0;
static void load_win(const char* p){
    FILE* f = fopen(p,"r"); if(!f){ printf("FATAL: cannot open %s\n",p); exit(1);} char ln[512];
    memset(have,0,sizeof have);
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#') continue; long long t,l,h,wdt;
        int n = sscanf(ln,"%lld %lld %lld %lld",&t,&l,&h,&wdt);
        if(n>=3){ if(t<-512||t>511){printf("FATAL: t out of range %lld\n",t);exit(1);} int i=(int)t+512;
                  if(have[i]){printf("FATAL: duplicate t %lld\n",t);exit(1);}
                  JL[i]=l; JH[i]=h; have[i]=true; nrows++;
                  if(n==4 && wdt != h-l+1){ printf("FATAL: width column mismatch at t=%lld: %lld vs %lld\n",t,wdt,h-l+1); exit(1);} } }
    fclose(f);
}

// the baseline chain, generalised: dx = t<<w ; I1=(arr(kc,dx)+K1)>>s1 ; J=(arr(I1,dx)+K2)>>s2
static inline i64 Jchain(int t,int w,int s1,int s2,i64 kc,i64 K1,i64 K2){
    i64 dx = (i64)t << w;
    i64 I1 = (arr(kc,dx)+K1) >> s1;
    return (arr(I1,dx)+K2) >> s2;
}

int main(int argc,char**argv){
    load_win("jwin18.txt");
    printf("jwin18 rows loaded: %d  (all 1024 present: %s)\n", nrows, nrows==1024?"YES":"NO");
    for(int i=0;i<1024;i++) if(!have[i]){ printf("FATAL missing t=%d\n",i-512); return 1; }
    printf("landmark windows: J(17)=[%lld,%lld]  J(342)=[%lld,%lld]  J(-342)=[%lld,%lld]\n",
        (long long)JL[529],(long long)JH[529],(long long)JL[854],(long long)JH[854],
        (long long)JL[170],(long long)JH[170]);

    // ---- 1. baseline score on jwin18 at (w,s1,s2,KC,K,K) = (13,23,18,0x4EF145,2^19,2^19)
    int sc=0; for(int t=-512;t<=511;t++){ i64 J=Jchain(t,13,23,18,KC,K,K); if(J>=JL[t+512]&&J<=JH[t+512]) sc++; }
    printf("\nBASELINE (w=13,s1=23,s2=18,KC=0x4EF145,K1=K2=2^19) jwin18 score = %d / 1024\n", sc);
    printf("  J(17)   = %lld  (need 45617)\n",   (long long)Jchain(17,13,23,18,KC,K,K));
    printf("  J(342)  = %lld  (need 18466836)\n",(long long)Jchain(342,13,23,18,KC,K,K));
    printf("  J(-342) = %lld  (need [18466858,18466867])\n",(long long)Jchain(-342,13,23,18,KC,K,K));

    // ---- 2. loss law over the ACTUAL branch operand domain, and its range
    //   arr(rec,mul) = rec*mul + loss ; loss claimed in [-513024, 0] over branch-2 domain
    {
        i64 lmin=0,lmax=0; long long n=0, viol=0;
        // domain: rec = KCw-scale constants and I1 values, mul = dx = t<<w
        for(int w=2;w<=24;w++) for(int t=-512;t<=511;t+=7){
            i64 dx=(i64)t<<w;
            for(int q=0;q<40;q++){
                i64 rec = ((i64)1<<(q%27)) + (q*1234577ll)%100003;
                if(q&1) rec=-rec;
                if(rec>= (1ll<<27) || rec <= -(1ll<<27)) continue;
                i64 a=arr(rec,dx), e=a-rec*dx; n++;
                if(e<lmin)lmin=e; if(e>lmax)lmax=e;
                if(e>0||e<-(1ll<<21)) viol++;
            }
        }
        printf("\nloss law spot-check: %lld samples, loss in [%lld,%lld], outside [-2^21,0]: %lld\n",
               n,(long long)lmin,(long long)lmax,viol);
        // exact loss-law form: loss depends only on (rec mod 2^18, mul mod 2^16)?
        long long bad=0, tested=0;
        for(int i=0;i<200000;i++){
            i64 rec = ((i64)rand()<<15 ^ rand()) % (1ll<<27); if(i&1) rec=-rec;
            i64 mul = ((i64)rand()<<10 ^ rand()) % (1ll<<23); if(i&3) ; else mul=-mul;
            i64 rec2 = rec + ((i64)1<<18)*((rand()%9)-4);
            if(rec2>=(1ll<<27)||rec2<=-(1ll<<27)) continue;
            i64 mul2 = mul + ((i64)1<<16)*((rand()%9)-4);
            i64 l1 = arr(rec,mul)-rec*mul, l2 = arr(rec2,mul2)-rec2*mul2;
            tested++; if(l1!=l2) bad++;
        }
        printf("loss law (rec mod 2^18, mul mod 2^16) invariance: %lld/%lld violations\n",bad,tested);
    }

    // ---- 3. the array's recoded-operand range: signed 28-bit?
    {
        i64 mul = 12345;
        printf("\narr(2^27-1,%lld)=%lld   arr(2^27,%lld)=%lld   arr(2^28,%lld)=%lld  arr(0,..)=%lld\n",
            (long long)mul,(long long)arr((1ll<<27)-1,mul),(long long)mul,(long long)arr(1ll<<27,mul),
            (long long)mul,(long long)arr(1ll<<28,mul),(long long)arr(0,mul));
        long long bad=0; for(int i=0;i<20000;i++){ i64 r=rand()%(1ll<<28), m=rand()%(1ll<<20);
            i64 rs = r>=(1ll<<27) ? r-(1ll<<28) : r;
            if(arr(r,m)!=arr(rs,m)) bad++; }
        printf("arr(rec) == arr(rec - 2^28) for rec in [2^27,2^28): %lld/20000 mismatches\n",bad);
    }

    // ---- 4. cross-check the jwin18 dataset itself against the raw dump (independent regeneration
    //        of a few pinned entries) is done in v20_win.cpp; here just report windows widths
    { int w1=0,w4=0; long long tot=0; for(int i=0;i<1024;i++){ i64 wd=JH[i]-JL[i]+1; tot+=wd; if(wd==1)w1++; if(wd<=4)w4++; }
      printf("\njwin18: mean width %.1f, width==1: %d, width<=4: %d\n",(double)tot/1024.0,w1,w4); }
    return 0;
}
