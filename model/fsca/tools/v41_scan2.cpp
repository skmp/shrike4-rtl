// v41_scan2.cpp -- joint (w,s1,s2,KC) scan with K1,K2 CONSTANT across offsets (as physics requires).
// Exact integer.  Stage 1: the three pinned landmarks, with K1 in [0,2^s1), K2 in [0,2^s2) treated
// exactly (each offset splits K's range at one breakpoint, so the feasible set is an intersection of
// intervals -- no sampling).  Stage 2: full 1024-offset scoring of every stage-1 survivor, maximised
// over K2 by interval stabbing, for every K1 breakpoint.
#include "base2.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cmath>
static inline i64 fdv(i64 a,i64 b){ i64 q=a/b,r=a%b; if(r&&((r<0)!=(b<0)))q--; return q; }
static i64 JLw[1024], JHw[1024];
static void load_win(){ FILE* f=fopen("jwin18.txt","r"); char l[512];
  while(fgets(l,sizeof l,f)){ if(l[0]=='#')continue; long long t,a,b,w; if(sscanf(l,"%lld %lld %lld %lld",&t,&a,&b,&w)!=4)continue; JLw[t+512]=a; JHw[t+512]=b;} fclose(f);}

struct Cfg{int w,s1,s2; i64 kc; i64 k1,k2;};
static std::vector<Cfg> hits;

// landmark test with constant K1,K2. returns true and one witness (k1,k2) if feasible.
static bool land_ok(int w,int s1,int s2,i64 kc,i64& wk1,i64& wk2){
    const int T[3]={17,342,-342};
    const i64 RL[3]={45617,18466836,18466858}, RH[3]={45617,18466836,18466867};
    i64 P1=(i64)1<<s1, P2=(i64)1<<s2;
    i64 A1[3],q1[3],r1[3];
    for(int i=0;i<3;i++){ i64 dx=(i64)T[i]<<w; if(dx>(1ll<<23)||dx<-(1ll<<23))return false;
        A1[i]=arr(kc,dx); q1[i]=fdv(A1[i],P1); i64 m=A1[i]-q1[i]*P1; r1[i]= (m==0)?P1:(P1-m); }
    for(int b=0;b<8;b++){
        i64 klo=0, khi=P1-1; i64 I1[3];
        for(int i=0;i<3;i++){ int up=(b>>i)&1; I1[i]=q1[i]+up;
            if(up){ klo=std::max(klo,r1[i]); } else { khi=std::min(khi,r1[i]-1); } }
        if(klo>khi) continue;
        i64 k2lo=0,k2hi=P2-1; bool ok=true;
        for(int i=0;i<3&&ok;i++){ i64 dx=(i64)T[i]<<w;
            if(I1[i]>=(1ll<<24)||I1[i]<=-(1ll<<24)){ok=false;break;}
            i64 A2=arr(I1[i],dx); i64 q2=fdv(A2,P2); i64 m2=A2-q2*P2;
            // J(K2) = q2 + (K2 >= P2-m2), except m2==0 -> always q2
            i64 thr = (m2==0)?P2:(P2-m2);
            bool lowok  = (q2>=RL[i] && q2<=RH[i]);
            bool highok = (q2+1>=RL[i] && q2+1<=RH[i]) && (thr<P2);
            if(lowok&&highok) continue;                      // no constraint
            else if(lowok){ k2hi=std::min(k2hi,thr-1); }
            else if(highok){ k2lo=std::max(k2lo,thr); }
            else ok=false;
            if(k2lo>k2hi) ok=false; }
        if(ok){ wk1=klo; wk2=k2lo; return true; }
    }
    return false;
}
int main(){
    build_tables(); load_win();
    long long tot=0,pass=0; int warn=0;
    #pragma omp parallel for schedule(dynamic) reduction(+:tot,pass,warn)
    for(int w=2;w<=17;w++){
        for(int s1=6;s1<=36;s1++) for(int s2=6;s2<=36;s2++){
            int e=s1+s2-2*w; if(e<0||e>42) continue;
            long double kcs=45617.0L*powl(2.0L,(long double)e)/289.0L;
            if(kcs<512.0L||kcs>1.6e7L) continue;
            i64 kc0=(i64)llroundl(kcs);
            i64 Wd=std::max((i64)262144, kc0>>4);
            i64 klo=std::max((i64)1,kc0-Wd), khi=std::min((i64)(1ll<<24)-1,kc0+Wd);
            for(i64 kc=klo;kc<=khi;kc++){ tot++; i64 k1,k2;
                if(land_ok(w,s1,s2,kc,k1,k2)){ pass++;
                    #pragma omp critical
                    { hits.push_back({w,s1,s2,kc,k1,k2}); } } }
        }
    }
    printf("[stage1] KC values evaluated: %lld ; configs hitting ALL THREE landmarks with constant K1,K2: %lld\n",tot,pass);
    // stage 2: full scoring
    std::sort(hits.begin(),hits.end(),[](const Cfg&a,const Cfg&b){return a.w!=b.w?a.w<b.w:(a.s1!=b.s1?a.s1<b.s1:(a.s2!=b.s2?a.s2<b.s2:a.kc<b.kc));});
    printf("[stage1] distinct (w,s1,s2) cells with hits:\n");
    int pw=-1,p1=-1,p2=-1; long long cnt=0; i64 kmin=0,kmax=0;
    for(size_t i=0;i<hits.size();i++){ const Cfg&c=hits[i];
        if(c.w!=pw||c.s1!=p1||c.s2!=p2){ if(pw>=0) printf("   w=%d s1=%d s2=%d : %lld KC in [%lld,%lld]\n",pw,p1,p2,cnt,(long long)kmin,(long long)kmax);
            pw=c.w;p1=c.s1;p2=c.s2;cnt=0;kmin=c.kc;kmax=c.kc; }
        cnt++; kmax=c.kc; }
    if(pw>=0) printf("   w=%d s1=%d s2=%d : %lld KC in [%lld,%lld]\n",pw,p1,p2,cnt,(long long)kmin,(long long)kmax);

    // full 1024 score of each hit, maximised over K1 breakpoints and K2 by stabbing
    int best=0; Cfg bc{}; 
    long long nh = (long long)hits.size();
    printf("[stage2] full-scoring %lld hits ...\n", nh);
    #pragma omp parallel
    {
        int lbest=0; Cfg lbc{};
        #pragma omp for schedule(dynamic)
        for(long long h=0; h<nh; h++){
            const Cfg&c=hits[h]; i64 P1=(i64)1<<c.s1,P2=(i64)1<<c.s2;
            std::vector<i64> A1(1024),q1(1024),r1(1024);
            bool bad=false;
            for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<c.w; if(dx>(1ll<<23)||dx<-(1ll<<23)){bad=true;break;}
                i64 a=arr(c.kc,dx); A1[t+512]=a; q1[t+512]=fdv(a,P1); i64 m=a-q1[t+512]*P1; r1[t+512]=(m==0)?P1:(P1-m); }
            if(bad) continue;
            std::vector<i64> brk; brk.push_back(0); for(int i=0;i<1024;i++) if(r1[i]<P1) brk.push_back(r1[i]);
            std::sort(brk.begin(),brk.end()); brk.erase(std::unique(brk.begin(),brk.end()),brk.end());
            for(i64 k1: brk){
                // K2 intervals per t
                std::vector<std::pair<i64,i64>> iv; iv.reserve(1024); bool bb=false;
                for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<c.w; i64 I1=q1[t+512]+(k1>=r1[t+512]?1:0);
                    if(I1>=(1ll<<24)||I1<=-(1ll<<24)){bb=true;break;}
                    i64 A2=arr(I1,dx); i64 q2=fdv(A2,P2); i64 m2=A2-q2*P2; i64 thr=(m2==0)?P2:(P2-m2);
                    i64 L=JLw[t+512],H=JHw[t+512];
                    bool lowok=(q2>=L&&q2<=H), highok=(q2+1>=L&&q2+1<=H)&&(thr<P2);
                    if(lowok&&highok) iv.push_back({0,P2-1});
                    else if(lowok) iv.push_back({0,thr-1});
                    else if(highok) iv.push_back({thr,P2-1});
                }
                if(bb) continue;
                std::vector<std::pair<i64,int>> ev; ev.reserve(iv.size()*2);
                for(auto&p:iv){ if(p.first>p.second)continue; ev.push_back({p.first,+1}); ev.push_back({p.second+1,-1}); }
                std::sort(ev.begin(),ev.end());
                int cur=0,bs=0; i64 bk=0;
                for(size_t i=0;i<ev.size();i++){ cur+=ev[i].second; if(i+1==ev.size()||ev[i+1].first!=ev[i].first){ if(cur>bs){bs=cur;bk=ev[i].first;} } }
                if(bs>lbest){ lbest=bs; lbc=c; lbc.k1=k1; lbc.k2=bk; }
            }
        }
        #pragma omp critical
        { if(lbest>best){best=lbest;bc=lbc;} }
    }
    printf("[stage2] BEST full jwin18 score among landmark hits: %d / 1024  at w=%d s1=%d s2=%d KC=%lld K1=%lld K2=%lld\n",
        best,bc.w,bc.s1,bc.s2,(long long)bc.kc,(long long)bc.k1,(long long)bc.k2);
    return 0;
}
