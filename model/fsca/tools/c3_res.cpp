// c3_res.cpp -- the black-box search in the domain where the answer must live.
// Any chain of truncating multiplies gives  J = floor((KC t^2 + c(t)) / 2^P)  with
//     c(t) = (E - r(t)) * t + W * (64 - h(t)),   E in [41,48], h in [0,56], r(t) in [0,W)
// where r is the first latch's truncation residue.  For the KC-first chain r(t) = (KC t + E) mod W, which is
// why that family is pinned.  RELAX it: let r(t) = (A t + B) mod W for ANY A, B -- one extra physical degree of
// freedom (a residue set by a constant other than the quadratic coefficient, i.e. one more multiply).
// Then score EVERY (A, B) for every modulus W: a W x W table, exhaustively, by difference array over B.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdio>
static i64 JL[1024],JH[1024];
static bool load(const char* p){ FILE*f=fopen(p,"r"); if(!f)return false; char ln[256];
    while(fgets(ln,sizeof ln,f)){ if(ln[0]=='#')continue; int t; long long a,b,c;
        if(sscanf(ln,"%d %lld %lld %lld",&t,&a,&b,&c)>=3){ JL[t+512]=a; JH[t+512]=b; } } fclose(f); return true; }
int main(int argc,char**argv){ build_tables();
    int DU = argc>1? atoi(argv[1]) : 2; int P = 17 - DU;
    const char* ds = DU? "jwin18.txt":"jwin.txt";
    if(!load(ds)){ printf("no %s\n",ds); return 1; }
    printf("dataset %s, J = floor((KC*t^2 + c)/2^%d); scanning r(t) = (A*t + B) mod W over ALL A,B\n",ds,P);
    puts("  w   W      best score/1024   best A        (A == KC mod W ?)   A/W as a fraction");
    for(int w=8;w<=16;w++){ i64 W=1ll<<w;
        // per t, the allowed residue interval(s) -> for each A, an allowed B-interval mod W
        std::vector<i64> rlo(1024),rhi(1024); std::vector<char> ok(1024,0);
        for(int t=-512;t<=511;t++){ int i=t+512; if(JL[i]>JH[i]) continue;
            i64 clo = JL[i]*(1ll<<P) - (i64)KC*t*t, chi = (JH[i]+1)*(1ll<<P) - 1 - (i64)KC*t*t;
            if(t==0){ ok[i]=1; rlo[i]=0; rhi[i]=W-1; continue; }
            i64 Nlo = (t>0? 41ll*t : 48ll*t) + 8*W - chi;
            i64 Nhi = (t>0? 48ll*t : 41ll*t) + 64*W - clo;
            i64 a,b;
            if(t>0){ a=(Nlo>=0? (Nlo+t-1)/t : -((-Nlo)/t)); b=(Nhi>=0? Nhi/t : -((-Nhi+t-1)/t)); }
            else   { i64 tt=-t; a=(Nhi<=0? (-Nhi)/tt : -(( Nhi+tt-1)/tt)); b=(Nlo<=0? ((-Nlo)+tt-1)/tt : -(( Nlo)/tt)); }
            if(a<0)a=0; if(b>W-1)b=W-1; if(a>b) continue; ok[i]=1; rlo[i]=a; rhi[i]=b; }
        int nconstr=0, vac=0; for(int i=0;i<1024;i++){ if(!ok[i])continue; nconstr++; if(rhi[i]-rlo[i]>=W-1) vac++; }
        int best=-1; i64 bA=0,bB=0;
        std::vector<int> diff(W+2);
        for(i64 A=0;A<W;A++){ std::fill(diff.begin(),diff.end(),0);
            for(int t=-512;t<=511;t++){ int i=t+512; if(!ok[i]) continue;
                // (A t + B) mod W in [rlo,rhi]  ->  B in [rlo - A t, rhi - A t] (mod W)
                i64 s=((rlo[i]-A*t)%W+W)%W, e=((rhi[i]-A*t)%W+W)%W; i64 len=rhi[i]-rlo[i];
                if(len>=W-1){ diff[0]++; diff[W]--; continue; }
                if(s<=e){ diff[s]++; diff[e+1]--; } else { diff[s]++; diff[W]--; diff[0]++; diff[e+1]--; } }
            int cur=0; for(i64 B=0;B<W;B++){ cur+=diff[B]; if(cur>best){ best=cur; bA=A; bB=B; } } }
        i64 kcmod=((i64)KC)%W;
        printf(" %2d %6lld    %4d / %d       %-8lld  %s        %.6f\n",w,(long long)W,best,nconstr,(long long)bA,
            bA==kcmod? "YES (= KC mod W)":"no", (double)bA/(double)W);
        printf("        constrained t: %d, of which vacuous (full range): %d\n",nconstr,vac);
    }
    return 0; }
