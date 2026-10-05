// rr7_sublsb.cpp -- rr6 shows the refit wanting REC2 + 1 at k2 = 6 and + 2..3 at k2 = 7 and the remaining misses being
// hw = model + 1: the signature of a ROM field that is WIDER than the model's 23 bits (the recoded port is 28 bits wide).
// Sub-LSB scan, per node, of each field with f extra bits:  field' = (field << f) + j,  aligner shift + f,
// jointly with C0 +-1.  Starts from refit.txt (rr6).  DIAGNOSTIC (fitted), the rule is to be read off the result.
#include "rr_common.h"
#include <omp.h>
struct Out { int base; int m[4][9]; i64 j[4][9]; };
int main(int argc,char**argv){ int STEP=argc>1?atoi(argv[1]):1; int FMAX=argc>2?atoi(argv[2]):4; if(!load_fsrra())return 1;
    Word W[34]; { FILE*f=fopen("refit.txt","r"); if(!f){puts("run rr6 first");return 1;} int par,n,e1,k2,ms; long long c0,r1,r2,r3; size_t ns; while(fscanf(f,"%d %d %lld %lld %d %lld %d %lld %d %zu",&par,&n,&c0,&r1,&e1,&r2,&k2,&r3,&ms,&ns)==10) W[par*17+n]={c0,r1,r2,r3,e1,k2}; fclose(f); }
    std::vector<Out> O(34);
    #pragma omp parallel for schedule(dynamic,1)
    for(int id=0;id<34;id++){ int par=id/17,n=id%17; Word w=W[id]; auto S=node_samples(par,n,STEP); size_t N=S.size(); std::vector<i64> T1(N),T2(N),T3(N),tb(N);
        for(size_t i=0;i<N;i++){ T1[i]=termT1(w,S[i]); T2[i]=termT2(w,S[i]); T3[i]=termT3(w,S[i]); }
        auto cnt=[&](int lim){ int best=lim; for(i64 c0=w.C0-1;c0<=w.C0+1;c0++){ int m=0; for(size_t i=0;i<N;i++) if(!okd(S[i],c0+tb[i]-S[i].q4)){ if(++m>=best)break; } if(m<best)best=m; } return best; };
        for(size_t i=0;i<N;i++)tb[i]=-T1[i]+T2[i]-T3[i]; O[id].base=cnt(1<<30);
        for(int fld=1;fld<=3;fld++)for(int f=1;f<=FMAX+(fld==3?4:0);f++){ int best=1<<30; i64 bj=0; i64 fv=fld==1?w.rec1:fld==2?w.rec2:w.rec3; if(((fv+2)<<f)>=(1ll<<27)){O[id].m[fld][f]=-1;continue;}
            for(i64 j=-(1ll<<f);j<=(2ll<<f);j++){ i64 v=(fv<<f)+j;
                for(size_t i=0;i<N;i++){ i64 x; if(fld==1)x=-sm(arr(v,S[i].dx)+K,26-w.e1+f)+T2[i]-T3[i]; else if(fld==2)x=-T1[i]+sm(arr(v,S[i].sq21)+K,25+w.k2+f)-T3[i]; else x=-T1[i]+T2[i]-sm(arr(S[i].dx,(arr(v,S[i].sq19)+K)>>(23+f))+K,27); tb[i]=x; }
                int m=cnt(best); if(m<best){best=m;bj=j;} }
            O[id].m[fld][f]=best; O[id].j[fld][f]=bj; } }
    for(int fld=1;fld<=3;fld++){ printf("\nFIELD rec%d: misses (best sub-LSB value j, in units of 2^-f LSB) for f extra bits\npar  n | base |",fld); int fm=FMAX+(fld==3?4:0); for(int f=1;f<=fm;f++)printf("     f=%d      |",f); puts("");
        long tot[9]={0},tb=0; for(int id=0;id<34;id++){ printf(" %d  %2d | %4d |",id/17,id%17,O[id].base); tb+=O[id].base; for(int f=1;f<=fm;f++){ if(O[id].m[fld][f]<0)printf("      -       |"); else printf(" %4d (%+4lld)  |",O[id].m[fld][f],(long long)O[id].j[fld][f]); tot[f]+=O[id].m[fld][f]<0?O[id].base:O[id].m[fld][f]; } puts(""); }
        printf(" TOTAL  | %4ld |",tb); for(int f=1;f<=fm;f++)printf(" %6ld        |",tot[f]); puts(""); }
    return 0; }
