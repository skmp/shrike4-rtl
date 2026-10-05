// rr10_place2.cpp -- rr9: the square product's excess eps scales with 2^k2 (k2 = 5: < 2^19, 6: < 2^21, 7: < 2^22 raw) and
// is ABSENT on the k2 = 4 nodes.  The table's C2 are all multiples of 8 (B2 multiples of 32): a ~14-bit value, which the
// model NORMALISES to 23 bits (rec2 = B2 << k2).  If the silicon feeds the fixed-point field at a FIXED placement instead,
// the array's absolute truncation loss (and the +2^19) weigh 2^(k2-4) times more  ->  exactly such an excess.
// Test: rec2' = rec2 >> j, aligner shift 25 + k2 - j, per node for j = -2..k2 (0 = the model), with the published C2
// (no refit nudges), C0 +-1 joint.  Then the totals for every FIXED absolute placement p = k2 - j.
#include "rr_common.h"
#include <omp.h>
int main(int argc,char**argv){ int STEP=argc>1?atoi(argv[1]):1; if(!load_fsrra())return 1; Word REF[34];
    { FILE*f=fopen("refit.txt","r"); if(!f){puts("run rr6 first");return 1;} int par,n,e1,k2,ms; long long c0,r1,r2,r3; size_t ns; while(fscanf(f,"%d %d %lld %lld %d %lld %d %lld %d %zu",&par,&n,&c0,&r1,&e1,&r2,&k2,&r3,&ms,&ns)==10) REF[par*17+n]={c0,r1,r2,r3,e1,k2}; fclose(f); }
    const int JN=11; static int M[34][JN]; static int REFM[34];
    #pragma omp parallel for schedule(dynamic,1)
    for(int id=0;id<34;id++){ int par=id/17,n=id%17; Word w=REF[id]; Word tw=word_from_table(par,n); auto S=node_samples(par,n,STEP); size_t N=S.size(); std::vector<i64> b(N),tb(N);
        for(size_t i=0;i<N;i++) b[i]=-termT1(w,S[i])-termT3(w,S[i]);
        auto cnt=[&](){ int best=1<<30; for(i64 c0=w.C0-1;c0<=w.C0+1;c0++){ int m=0; for(size_t i=0;i<N;i++) if(!okd(S[i],c0+tb[i]-S[i].q4)){ if(++m>=best)break; } if(m<best)best=m; } return best; };
        for(size_t i=0;i<N;i++) tb[i]=b[i]+termT2(w,S[i]); REFM[id]=cnt();
        for(int ji=0;ji<JN;ji++){ int j=ji-2; i64 r=j>=0?(tw.rec2>>j):(tw.rec2<<(-j)); if(j>0&&(r<<j)!=tw.rec2){M[id][ji]=-1;continue;} int u=25+tw.k2-j; for(size_t i=0;i<N;i++) tb[i]=b[i]+sm(arr(r,S[i].sq21)+K,u); M[id][ji]=cnt(); } }
    printf("par  n k2 | refit(rr6) |"); for(int ji=0;ji<JN;ji++)printf("  j=%+d ",ji-2); puts("   (j = 0: the model's normalised field, published C2)");
    for(int id=0;id<34;id++){ Word tw=word_from_table(id/17,id%17); printf(" %d  %2d  %d |   %5d    |",id/17,id%17,tw.k2,REFM[id]); for(int ji=0;ji<JN;ji++){ if(M[id][ji]<0)printf("     - "); else printf(" %6d",M[id][ji]); } puts(""); }
    puts("\nFIXED absolute placement p (field LSB = 2^p x (C2/8), aligner shift 20 + p):  total misses over the 34 nodes");
    for(int p=6;p<=13;p++){ long tot=0; bool ok=true; for(int id=0;id<34;id++){ Word tw=word_from_table(id/17,id%17); int j=(5+tw.k2)-p; int ji=j+2; if(ji<0||ji>=JN||M[id][ji]<0){ok=false;break;} tot+=M[id][ji]; } if(ok)printf("   p = %2d : %ld\n",p,tot); else printf("   p = %2d : (not representable)\n",p); }
    long t=0; for(int id=0;id<34;id++)t+=REFM[id]; printf("   refit table, normalised field (rr6): %ld\n",t); return 0; }
