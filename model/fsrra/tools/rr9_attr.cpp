// rr9_attr.cpp -- rr3's error CDF redone on the REFIT table (rr3 used the published table, so the three bad nodes --
// 15.8K of its 24.9K misses -- polluted it), and per PRODUCT: for each of the three aligned products, the raw distance
// its register value would have to move to change W by +1 / -1; P(hw = model +- 1 | that distance in a bin).  The
// product that really carries the error shows a CDF; the others show the flat background.
// Also writes misses.txt (one line per remaining miss) for rr10.
#include "rr_common.h"
#include <omp.h>
static const int NB=16;   // bins of log2(raw gap): [2^(10+b), 2^(11+b))
struct Acc { long A[3][2][NB], B[3][2][NB]; long miss,hi,lo,ns; };
int main(int argc,char**argv){ int STEP=argc>1?atoi(argv[1]):1; if(!load_fsrra())return 1; Word REF[34];
    if(!load_words(argc>2?argv[2]:"refit.txt",REF))return 1;
    std::vector<Acc> AC(34); std::vector<std::string> LINES(34);
    #pragma omp parallel for schedule(dynamic,1)
    for(int id=0;id<34;id++){ int par=id/17,n=id%17; Word w=REF[id]; auto S=node_samples(par,n,STEP); Acc a; memset(&a,0,sizeof a); std::string out; char buf[512];
        for(auto&s:S){ i64 P[3]={arr(w.rec1,s.dx)+K,arr(w.rec2,s.sq21)+K,0}; i64 I=(arr(w.rec3,s.sq19)+K)>>23; P[2]=arr(s.dx,I)+K; int U[3]={26-w.e1,25+w.k2,27}; int sg[3]={-1,+1,-1};
            i64 W=w.C0-sm(P[0],U[0])+sm(P[1],U[1])-sm(P[2],U[2]); i64 d=W-s.q4; bool in0=okd(s,d),inP=okd(s,d+1),inM=okd(s,d-1); a.ns++; if(!in0){a.miss++; if(inP)a.hi++; else if(inM)a.lo++;}
            i64 gup[3],gdn[3];
            for(int x=0;x<3;x++){ int u=U[x]; i64 p=P[x]; i64 m=p>=0?p:~p; i64 fr=m&((1ll<<u)-1); i64 gi=(1ll<<u)-fr, gd=fr+1;   // raw move of the MAGNITUDE that increments / decrements sm()
                // sm value = sign(p) * (m >> u).  W gets sg * sm.  W up  <=>  sg*sign(p) > 0 ? magnitude up : magnitude down
                bool pos=(p>=0)==(sg[x]>0); gup[x]=pos?gi:gd; gdn[x]=pos?gd:gi;
                auto bin=[&](i64 v){ int b=(v<=1)?0:(int)floor(log2((double)v))-10; if(b<0)b=0; if(b>=NB)b=NB-1; return b; };
                if(!in0&&inP)a.A[x][0][bin(gup[x])]++; else if(in0&&!inP)a.B[x][0][bin(gup[x])]++;
                if(!in0&&inM)a.A[x][1][bin(gdn[x])]++; else if(in0&&!inM)a.B[x][1][bin(gdn[x])]++; }
            if(!in0){ int dir=inP?+1:inM?-1:0; const i64*g=dir>0?gup:gdn; snprintf(buf,sizeof buf,"%d %d %lld %d %lld %lld %lld %d %d %d %lld %lld %lld %lld %lld\n",par,n,(long long)s.t,dir,(long long)g[0],(long long)g[1],(long long)g[2],U[0],U[1],U[2],(long long)s.sq21,(long long)s.sq19,(long long)I,(long long)P[1],(long long)P[2]); out+=buf; } }
        AC[id]=a; LINES[id]=out; }
    FILE*o=fopen(argc>3?argv[3]:"misses.txt","w"); fputs("# par n t dir gap1 gap2 gap3 u1 u2 u3 sq21 sq19 I P2 P3\n",o); for(auto&l:LINES)fputs(l.c_str(),o); fclose(o);
    Acc T; memset(&T,0,sizeof T); for(auto&a:AC){ T.miss+=a.miss;T.hi+=a.hi;T.lo+=a.lo;T.ns+=a.ns; for(int x=0;x<3;x++)for(int dd=0;dd<2;dd++)for(int b=0;b<NB;b++){T.A[x][dd][b]+=a.A[x][dd][b];T.B[x][dd][b]+=a.B[x][dd][b];} }
    printf("refit table, stride %d: %ld misses of %ld (%.4f%%): hw = model+1 %ld, hw = model-1 %ld\n",STEP,T.miss,T.ns,100.0*T.miss/T.ns,T.hi,T.lo);
    const char*NM[3]={"LINEAR  rec1 x dx","SQUARE  rec2 x sq21","CUBIC   dx x I"};
    for(int x=0;x<3;x++){ printf("\n%s\n   raw move needed       |  W+1: flipped  pinned-not   P        |  W-1: flipped  pinned-not   P\n",NM[x]);
        for(int b=0;b<NB;b++){ long a0=T.A[x][0][b],b0=T.B[x][0][b],a1=T.A[x][1][b],b1=T.B[x][1][b]; printf("   [2^%2d, 2^%2d)          |   %8ld  %10ld  %7.3f%%   |   %8ld  %10ld  %7.3f%%\n",10+b,11+b,a0,b0,a0+b0?100.0*a0/(a0+b0):0.0,a1,b1,a1+b1?100.0*a1/(a1+b1):0.0); } }
    return 0; }
