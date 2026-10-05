// we_i1.cpp -- invert the SECOND multiply exactly: per t, the set of first-latch values I1 for which
// J = (arr(I1,dx)+K)>>20 lands in the allowed window.  I1 ~ KC*t/1024 is near-linear in t, so the
// required intervals give a directly solvable linear-fit problem.
#include "we_win.h"
#include <cstring>
#include <algorithm>
static inline i64 Jof(i64 v,i64 dx){ return (arr(v,dx)+K)>>20; }
int main(int argc,char**argv){ if(!load_fsca(WF))return 1; build_tables(); build_windows();
    printf("# t  IL  IH  Imodel  (Imodel-IL)  ideal=KC*t/1024  IL-ideal  IH-ideal  g  feas\n");
    int nempty=0, nin=0;
    // also collect for the linear fit:  I1 must be in [IL,IH];  model I1 = floor((KC*t+48-g)/1024)
    std::vector<i64> IL(1024,1),IH(1024,0);
    for(int t=-512;t<=511;t++){ if(!JOK[t+512]) continue; i64 dx=(i64)t<<13;
        i64 I1m=(arr(KC,dx)+K)>>23;
        // search around I1m; J is monotone in v for fixed dx sign (verify by scan)
        i64 lo=0,hi=-1; bool any=false;
        for(i64 v=I1m-4000;v<=I1m+4000;v++){ i64 j=Jof(v,dx); if(j>=JLo[t+512]&&j<=JHi[t+512]){ if(!any){lo=v;any=true;} hi=v; } }
        if(!any){ nempty++; IL[t+512]=1;IH[t+512]=0; continue; }
        IL[t+512]=lo;IH[t+512]=hi;
        if(I1m>=lo&&I1m<=hi) nin++;
    }
    printf("# t with empty I1 interval: %d ; model I1 inside: %d\n",nempty,nin);
    // linear fit: I1(t) = floor((Q*t + R)/1024)  exact interval intersection over Q for each R
    // more general: I1(t) in [IL,IH]. Try I1 = floor((Q*t + c(t mod 8))/2^10) with free Q and per-residue c.
    // Step 1: pure  I1 = floor((Q*t + R)/2^10):  Q*t + R in [IL*2^10, (IH+1)*2^10-1]
    printf("\n# pure linear rule I1 = floor((Q*t+R)/1024): best (Q,R) by sweep\n");
    int bestsc=-1; i64 bQ=0,bR=0;
    for(i64 R=0;R<1024;R++){
        // for each t: Q in [ceil((IL*1024-R)/t), floor(((IH+1)*1024-1-R)/t)] careful with sign
        std::vector<std::pair<i64,int>> ev;
        for(int t=-512;t<=511;t++){ if(IL[t+512]>IH[t+512]) continue; if(t==0) continue;
            i64 A=IL[t+512]*1024-R, B=(IH[t+512]+1)*1024-1-R;   // Q*t in [A,B]
            i64 qlo,qhi;
            if(t>0){ qlo=(A>=0)?( (A+t-1)/t ):-((-A)/t); qhi=(B>=0)?(B/t):-(((-B)+t-1)/t); }
            else { i64 u=-t; /* -Q*u in [A,B] -> Q in [-B/u, -A/u] */ i64 lo2=-B, hi2=-A;
                   qlo=(lo2>=0)?((lo2+u-1)/u):-((-lo2)/u); qhi=(hi2>=0)?(hi2/u):-(((-hi2)+u-1)/u); }
            if(qlo>qhi) continue; ev.push_back({qlo,1}); ev.push_back({qhi+1,-1}); }
        std::sort(ev.begin(),ev.end()); int cur=0,best=0; i64 bq=0;
        for(size_t i=0;i<ev.size();i++){ cur+=ev[i].second; if(cur>best){best=cur;bq=ev[i].first;} }
        if(best>bestsc){bestsc=best;bQ=bq;bR=R;} }
    printf("   best %d/%d  Q=%lld R=%lld   (KC=%lld)\n",bestsc,1022-nempty-1,(long long)bQ,(long long)bR,(long long)KC);
    // Step 2: per-residue constant c(t mod 8) (8 independent problems, same Q)
    printf("\n# rule I1 = floor((Q*t + c[t mod 8])/1024): for each Q near KC, maximise sum over residues\n");
    i64 gbest=-1,gQ=0; i64 gc[8]={0};
    for(i64 Q=KC-40;Q<=KC+40;Q++){ i64 tot=0; i64 cc[8];
        for(int m=0;m<8;m++){ int bs=-1,bc=0;
            for(i64 c=0;c<1024;c++){ int s=0;
                for(int t=-512;t<=511;t++){ if(((t%8)+8)%8!=m) continue; if(IL[t+512]>IH[t+512])continue;
                    i64 v=Q*t+c; i64 I=v>>10; if(I>=IL[t+512]&&I<=IH[t+512])s++; }
                if(s>bs){bs=s;bc=(int)c;} }
            tot+=bs; cc[m]=bc; }
        if(tot>gbest){gbest=tot;gQ=Q;for(int m=0;m<8;m++)gc[m]=cc[m];} }
    printf("   best %lld  Q=%lld (KC%+lld)  c=",(long long)gbest,(long long)gQ,(long long)(gQ-KC));
    for(int m=0;m<8;m++)printf(" %lld",(long long)gc[m]); puts("");
    return 0; }
