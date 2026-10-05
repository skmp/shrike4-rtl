// rr8_ring.cpp -- is the FSRRA ROM a RING OF 32 (power of two) rather than 2 x 17 words?  The half-nodes at x = 2
// ((0,16) and (1,0)) and at x = 4 == 1 ((1,16) and (0,0)) would then read the SAME word.  The secondary half-node sees
// the word through a scale change (its t is half a unit of the primary's; at x = 4 the result is halved as well):
//   V1  aligner-side : dx = t<<4 as everywhere, aligner shifts + (a1,a2,a3), C0 >> c
//   V1b as V1 but the cubic scale taken on the stored field (rec3 >> a3) instead of the aligner
//   V2  operand-side : dx = t<<3 (the offset enters half a unit lower), shifts + c only
// For each variant: misses of the secondary with the primary's refit word, then a JOINT refit of one common word on
// both half-nodes, against the sum of the two separate refits (rr6).   DIAGNOSTIC (fitted).
#include "rr_common.h"
#include <omp.h>
struct View { int par,n,dxsh,a1,a2,a3,c,r3sh; };
struct Half { View v; std::vector<Smp> S; };
static int missW(const Half&h,const Word&w,int lim=1<<30){ int m=0; const View&v=h.v; i64 r3=w.rec3>>v.r3sh;
    for(auto&s:h.S){ i64 W=(w.C0>>v.c)-sm(arr(w.rec1,s.dx)+K,26-w.e1+v.a1)+sm(arr(w.rec2,s.sq21)+K,25+w.k2+v.a2)-sm(arr(s.dx,(arr(r3,s.sq19)+K)>>23)+K,27+v.a3); if(!okd(s,W-s.q4)){ if(++m>=lim)break; } } return m; }
static Word REF[34];
static void joint(const char*name,View pv,View sv){ Half P{pv,node_samples(pv.par,pv.n,1,pv.dxsh)},S{sv,node_samples(sv.par,sv.n,1,sv.dxsh)}; Word wp=REF[pv.par*17+pv.n];
    int mp=missW(P,wp), ms=missW(S,wp); printf("  %-4s primary's word as is: primary %d + secondary %d = %d",name,mp,ms,mp+ms); fflush(stdout);
    Word w=wp; int best=mp+ms;
    for(int round=0;round<4;round++){ bool any=false;
        for(int fld=1;fld<=3;fld++){ i64 cur=fld==1?w.rec1:fld==2?w.rec2:w.rec3; int R_=fld==1?40:fld==2?64:(round==0?8192:128); int st=(fld==3&&round==0)?128:1; i64 unit=1ll<<(fld==3?sv.r3sh:0);
            std::vector<i64> cand; for(i64 v=cur-R_*unit;v<=cur+R_*unit;v+=st*unit) if(v>0)cand.push_back(v); std::vector<int> res(cand.size()); std::vector<i64> rc0(cand.size());
            #pragma omp parallel for schedule(dynamic,1)
            for(size_t ci=0;ci<cand.size();ci++){ Word x=w; if(fld==1)x.rec1=cand[ci]; else if(fld==2)x.rec2=cand[ci]; else x.rec3=cand[ci]; int b=1<<30; i64 bc=w.C0;
                for(i64 c0=w.C0-(3ll<<sv.c);c0<=w.C0+(3ll<<sv.c);c0+=(1ll<<sv.c)){ x.C0=c0; int m=missW(P,x,b); if(m<b){ m+=missW(S,x,b-m); if(m<b){b=m;bc=c0;} } } res[ci]=b; rc0[ci]=bc; }
            for(size_t ci=0;ci<cand.size();ci++) if(res[ci]<best){ best=res[ci]; if(fld==1)w.rec1=cand[ci]; else if(fld==2)w.rec2=cand[ci]; else w.rec3=cand[ci]; w.C0=rc0[ci]; any=true; } }
        if(!any)break; }
    printf("   | JOINT refit: %d  (primary %d, secondary %d)   word C0 %lld rec1 %lld rec2 %lld rec3 %lld\n",best,missW(P,w),missW(S,w),(long long)w.C0,(long long)w.rec1,(long long)w.rec2,(long long)w.rec3); fflush(stdout); }
int main(){ if(!load_fsrra())return 1; { FILE*f=fopen("refit.txt","r"); if(!f){puts("run rr6 first");return 1;} int par,n,e1,k2,ms; long long c0,r1,r2,r3; size_t ns; while(fscanf(f,"%d %d %lld %lld %d %lld %d %lld %d %zu",&par,&n,&c0,&r1,&e1,&r2,&k2,&r3,&ms,&ns)==10) REF[par*17+n]={c0,r1,r2,r3,e1,k2}; fclose(f); }
    puts("x = 2 :  primary (1,0) [t >= 0], secondary (0,16) [t < 0].   separate refits (rr6): 35 + 76 = 111");
    joint("V1", {1,0,4,0,0,0,0,0},{0,16,4,1,2,3,0,0});
    joint("V1b",{1,0,4,0,0,0,0,0},{0,16,4,1,2,0,0,3});
    joint("V2", {1,0,4,0,0,0,0,0},{0,16,3,0,0,0,0,0});
    puts("x = 4 == 1 :  primary (0,0) [t >= 0], secondary (1,16) [t < 0].   separate refits (rr6): 28 + 70 = 98");
    joint("V1", {0,0,4,0,0,0,0,0},{1,16,4,2,3,4,1,0});
    joint("V1b",{0,0,4,0,0,0,0,0},{1,16,4,2,3,0,1,4});
    joint("V2", {0,0,4,0,0,0,0,0},{1,16,3,1,1,1,1,0});
    return 0; }
