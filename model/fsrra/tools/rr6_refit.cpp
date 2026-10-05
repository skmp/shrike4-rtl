// rr6_refit.cpp -- full-resolution refit of every FSRRA node word at stride 1 (DIAGNOSTIC: fitted fields, used only to
// find what the three bad nodes (0,0), (1,2), (1,16) want and to give rr7+ a table whose misses are not table errors).
// Each field is scanned JOINTLY with C0 (they trade against each other), by coordinate descent until nothing moves.
// Writes refit.txt:  par n C0 rec1 e1 rec2 k2 rec3 misses samples
#include "rr_common.h"
#include <omp.h>
struct Fit { Word w; int miss0, miss; size_t ns; int hi, lo; };
static int count(const std::vector<Smp>&S,const std::vector<i64>&base,i64 c0,int lim=1<<30){ int m=0; for(size_t i=0;i<S.size();i++){ if(!okd(S[i],c0+base[i]-S[i].q4)){ if(++m>=lim)break; } } return m; }
int main(int argc,char**argv){ int STEP=argc>1?atoi(argv[1]):1; if(!load_fsrra())return 1; std::vector<Fit> R(34);
    #pragma omp parallel for schedule(dynamic,1)
    for(int id=0;id<34;id++){ int par=id/17,n=id%17; Word w=word_from_table(par,n); auto S=node_samples(par,n,STEP); size_t N=S.size();
        std::vector<i64> T1(N),T2(N),T3(N),base(N); for(size_t i=0;i<N;i++){ T1[i]=termT1(w,S[i]); T2[i]=termT2(w,S[i]); T3[i]=termT3(w,S[i]); base[i]=-T1[i]+T2[i]-T3[i]; }
        int m0=count(S,base,w.C0), best=m0;
        for(int round=0;round<4;round++){ bool any=false;
            for(int fld=1;fld<=3;fld++){ i64 cur=fld==1?w.rec1:fld==2?w.rec2:w.rec3; int R_=fld==1?40:fld==2?64:(round==0?4096:96); int st=(fld==3&&round==0)?64:1; i64 bv=cur,bc=w.C0; std::vector<i64> tb(N);
                for(i64 v=cur-R_;v<=cur+R_;v+=st){ if(v<=0)continue; Word x=w; if(fld==1)x.rec1=v; else if(fld==2)x.rec2=v; else x.rec3=v;
                    for(size_t i=0;i<N;i++){ i64 a=fld==1?termT1(x,S[i]):T1[i], b=fld==2?termT2(x,S[i]):T2[i], c=fld==3?termT3(x,S[i]):T3[i]; tb[i]=-a+b-c; }
                    for(i64 c0=w.C0-3;c0<=w.C0+3;c0++){ int m=count(S,tb,c0,best); if(m<best){best=m;bv=v;bc=c0;} } }
                if(bv!=cur||bc!=w.C0){ any=true; if(fld==1)w.rec1=bv; else if(fld==2)w.rec2=bv; else w.rec3=bv; w.C0=bc; for(size_t i=0;i<N;i++){ T1[i]=termT1(w,S[i]); T2[i]=termT2(w,S[i]); T3[i]=termT3(w,S[i]); base[i]=-T1[i]+T2[i]-T3[i]; } } }
            if(!any)break; }
        int hi=0,lo=0; for(size_t i=0;i<N;i++){ i64 d=w.C0+base[i]-S[i].q4; if(okd(S[i],d))continue; if(okd(S[i],d+1))hi++; else if(okd(S[i],d-1))lo++; }
        R[id]={w,m0,best,N,hi,lo}; }
    FILE*o=fopen("refit.txt","w"); long t0=0,t1=0,tn=0;
    puts("par  n |  samples | misses table -> refit | hw=model+1 / -1 |      C0 (d)        rec1 (d)  e1      rec2 (d)  k2     rec3 (d)");
    for(int id=0;id<34;id++){ int par=id/17,n=id%17; Word w0=word_from_table(par,n); const Fit&f=R[id]; t0+=f.miss0; t1+=f.miss; tn+=f.ns;
        printf(" %d  %2d | %8zu |   %6d -> %5d    |  %5d / %5d  | %9lld (%+lld)  %8lld (%+3lld)  %d  %8lld (%+4lld)  %d  %7lld (%+5lld)\n",par,n,f.ns,f.miss0,f.miss,f.hi,f.lo,(long long)f.w.C0,(long long)(f.w.C0-w0.C0),(long long)f.w.rec1,(long long)(f.w.rec1-w0.rec1),f.w.e1,(long long)f.w.rec2,(long long)(f.w.rec2-w0.rec2),f.w.k2,(long long)f.w.rec3,(long long)(f.w.rec3-w0.rec3));
        fprintf(o,"%d %d %lld %lld %d %lld %d %lld %d %zu\n",par,n,(long long)f.w.C0,(long long)f.w.rec1,f.w.e1,(long long)f.w.rec2,f.w.k2,(long long)f.w.rec3,f.miss,f.ns); }
    fclose(o); printf("TOTAL  table %ld  ->  refit %ld  of %ld  (%.4f%% -> %.4f%%)\n",t0,t1,tn,100.0*t0/tn,100.0*t1/tn); return 0; }
