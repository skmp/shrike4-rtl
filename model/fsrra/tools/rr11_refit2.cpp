// rr11_refit2.cpp -- refit under the rr10 structure: the SQUARE coefficient is a FIXED-POINT field, rec2 = C2f << 9 with
// the aligner shift 29 for every node (Word.k2 = 4 everywhere).  Coordinate descent jointly with C0 (as rr6), on the
// natural grids: rec1 step 1, rec2 step 512 (one C2f LSB), rec3 step 1024 (one C3f LSB).  Then a GRANULARITY check: does
// any finer value of rec2 / rec3 (offsets +-2^i below the field LSB) beat the on-grid optimum?   DIAGNOSTIC (fitted).
// Writes refit2.txt (same format as refit.txt).
#include "rr_common.h"
#include <omp.h>
struct Fit { Word w; int miss0, miss; size_t ns; int hi, lo; int fine2, fine3; i64 f2off, f3off; };
int main(int argc,char**argv){ int STEP=argc>1?atoi(argv[1]):1; if(!load_fsrra())return 1; Word REF[34]; if(!load_words("refit.txt",REF))return 1; std::vector<Fit> R(34);
    #pragma omp parallel for schedule(dynamic,1)
    for(int id=0;id<34;id++){ int par=id/17,n=id%17; Word tw=word_from_table(par,n); Word w=REF[id]; w.rec2=tw.rec2>>(tw.k2-4); w.k2=4; auto S=node_samples(par,n,STEP); size_t N=S.size();
        std::vector<i64> T1(N),T2(N),T3(N),tb(N); auto recompute=[&](){ for(size_t i=0;i<N;i++){ T1[i]=termT1(w,S[i]); T2[i]=termT2(w,S[i]); T3[i]=termT3(w,S[i]); } }; recompute();
        auto cntc=[&](i64 c0,int lim){ int m=0; for(size_t i=0;i<N;i++) if(!okd(S[i],c0+tb[i]-S[i].q4)){ if(++m>=lim)break; } return m; };
        for(size_t i=0;i<N;i++)tb[i]=-T1[i]+T2[i]-T3[i]; int m0=cntc(w.C0,1<<30), best=m0;
        auto tryv=[&](int fld,i64 v,i64&bc){ Word x=w; if(fld==1)x.rec1=v; else if(fld==2)x.rec2=v; else x.rec3=v; for(size_t i=0;i<N;i++){ i64 a=fld==1?termT1(x,S[i]):T1[i], b=fld==2?termT2(x,S[i]):T2[i], c=fld==3?termT3(x,S[i]):T3[i]; tb[i]=-a+b-c; }
            int bm=1<<30; for(i64 c0=w.C0-2;c0<=w.C0+2;c0++){ int m=cntc(c0,bm); if(m<bm){bm=m;bc=c0;} } return bm; };
        for(int round=0;round<5;round++){ bool any=false;
            for(int fld=1;fld<=3;fld++){ i64 cur=fld==1?w.rec1:fld==2?w.rec2:w.rec3; i64 st=fld==1?1:fld==2?512:1024; int R_=fld==1?6:4; i64 bv=cur,bc=w.C0;
                for(i64 v=cur-R_*st;v<=cur+R_*st;v+=st){ if(v<=0)continue; i64 c0; int m=tryv(fld,v,c0); if(m<best){best=m;bv=v;bc=c0;} }
                if(bv!=cur||bc!=w.C0){ any=true; if(fld==1)w.rec1=bv; else if(fld==2)w.rec2=bv; else w.rec3=bv; w.C0=bc; recompute(); } }
            if(!any)break; }
        int f2=best,f3=best; i64 o2=0,o3=0; for(int i=0;i<9;i++)for(int sg=-1;sg<=1;sg+=2){ i64 c0; int m=tryv(2,w.rec2+sg*(1ll<<i),c0); if(m<f2){f2=m;o2=sg*(1ll<<i);} } for(int i=0;i<10;i++)for(int sg=-1;sg<=1;sg+=2){ i64 c0; int m=tryv(3,w.rec3+sg*(1ll<<i),c0); if(m<f3){f3=m;o3=sg*(1ll<<i);} }
        for(size_t i=0;i<N;i++)tb[i]=-T1[i]+T2[i]-T3[i]; int hi=0,lo=0; for(size_t i=0;i<N;i++){ i64 d=w.C0+tb[i]-S[i].q4; if(okd(S[i],d))continue; if(okd(S[i],d+1))hi++; else if(okd(S[i],d-1))lo++; }
        R[id]={w,m0,best,N,hi,lo,f2,f3,o2,o3}; }
    FILE*o=fopen("refit2.txt","w"); long t0=0,t1=0,tn=0;
    puts("par  n |  samples | misses start -> refit | hw=model+1 / -1 |      C0 (d vs rr6)   rec1 (d)  e1    C2f = rec2>>9 (d vs table)   C3f = rec3>>10 (d) | finer rec2: misses (offset)   finer rec3");
    for(int id=0;id<34;id++){ int par=id/17,n=id%17; Word tw=word_from_table(par,n); const Fit&f=R[id]; t0+=f.miss0; t1+=f.miss; tn+=f.ns;
        printf(" %d  %2d | %8zu |   %6d -> %5d    |  %5d / %5d  | %9lld (%+lld)  %8lld (%+3lld)  %d     %6lld (%+lld)                 %5lld (%+lld)         |   %4d (%+lld)             %4d (%+lld)\n",par,n,f.ns,f.miss0,f.miss,f.hi,f.lo,(long long)f.w.C0,(long long)(f.w.C0-REF[id].C0),(long long)f.w.rec1,(long long)(f.w.rec1-REF[id].rec1),f.w.e1,(long long)(f.w.rec2>>9),(long long)((f.w.rec2>>9)-((tw.rec2>>(tw.k2-4))>>9)),(long long)(f.w.rec3>>10),(long long)((f.w.rec3-tw.rec3)>>10),f.fine2,(long long)f.f2off,f.fine3,(long long)f.f3off);
        fprintf(o,"%d %d %lld %lld %d %lld %d %lld %d %zu\n",par,n,(long long)f.w.C0,(long long)f.w.rec1,f.w.e1,(long long)f.w.rec2,f.w.k2,(long long)f.w.rec3,f.miss,f.ns); }
    fclose(o); printf("TOTAL  start %ld  ->  refit %ld  of %ld  (%.5f%% -> %.5f%%)\n",t0,t1,tn,100.0*t0/tn,100.0*t1/tn); return 0; }
