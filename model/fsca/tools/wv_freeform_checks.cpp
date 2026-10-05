// wv_freeform_checks.cpp -- independent checks of the wf_* agent's SUPPORTING structural claims.
// Integer arithmetic throughout.
#include "base2.h"
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <map>

static Fsca F;
static const i64 C0TAB[17]={0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static const int U2TAB[17]={29,33,32,31,31,31,30,30,30,30,30,30,30,30,30,30,30};
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static i64 JL[1024],JH[1024]; static int OKT[1024];
static bool load_jwin(const char*p){FILE*f=fopen(p,"r");if(!f)return false;
    for(int i=0;i<1024;i++){int t,ok;long long a,b;if(fscanf(f,"%d %lld %lld %d",&t,&a,&b,&ok)!=4){fclose(f);return false;}
        JL[t+512]=a;JH[t+512]=b;OKT[t+512]=ok;}fclose(f);return true;}

struct Cons{ i64 lo,hi,S; int u,node; };
static std::vector<Cons> CON[1024];                 // T2 window, u = U2TAB
struct Cons2{ i64 base,wlo,whi,S,C1f,c3; int e1,u,node; i64 s; };  // for cubic-offset control
static std::vector<Cons2> CON2[1024];

static void build(int du){
    for(int t=-512;t<=511;t++){ CON[t+512].clear(); CON2[t+512].clear(); i64 dx=(i64)t<<13;
        for(int node=0;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; const Tab&T=TB[n];
            if(!T.S) continue; i64 s=flip?-1:1;
            i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
            i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
            i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
            i64 base=C0TAB[n]+s*T1-s*T3;
            int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
            CON[t+512].push_back({base-hi,base-lo,T.S,U2TAB[n]+du,node});
            CON2[t+512].push_back({C0TAB[n]+s*T1,lo,hi,T.S,T.C1f,T.c3,T.e1,U2TAB[n],node,s}); } }
}
int main(){ if(!load_fsca(F)){puts("no dump");return 1;} build_tables(); if(!load_jwin("jwin.txt")){puts("no jwin");return 1;}
    build(0);

    // ================= A. the claimed EXACT ALGEBRAIC IDENTITY for dev(t) =================
    {   int bad=0, badmul=0; std::map<i64,int> e1h; i64 e1min=1e18,e1max=-1e18; std::vector<i64> e2s;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13;
            i64 A1=arr(KC,dx); if(A1 % (1ll<<16)) badmul++;
            i64 P1=A1+K; i64 I1=P1>>23; i64 r1=P1-(I1<<23);
            i64 A2=arr(I1,dx); if(A2 % (1ll<<16)) badmul++;
            i64 P2=A2+K; i64 J=P2>>20; i64 r2=P2-(J<<20);
            i64 e1=KC*dx-A1, e2=I1*dx-A2;
            if(e1 % (1ll<<13) || e2 % (1ll<<13) || r1 % (1ll<<16) || r2 % (1ll<<16)) { bad++; continue; }
            i64 E1=e1>>13, R1=r1>>16, E2=e2>>13, R2=r2>>16;
            i64 dev = J*(1ll<<17) - KC*(i64)t*(i64)t;
            i64 rhs = 64*(i64)t + (1ll<<16) - (i64)t*(E1+8*R1) - 1024*E2 - 8192*R2;
            if(dev!=rhs) bad++;
            e1h[E1]++; if(E1<e1min)e1min=E1; if(E1>e1max)e1max=E1; e2s.push_back(E2); }
        std::sort(e2s.begin(),e2s.end()); e2s.erase(std::unique(e2s.begin(),e2s.end()),e2s.end());
        printf("[A] identity dev = 64t + 2^16 - t(E1+8R1) - 1024E2 - 8192R2 : mismatches %d/1024\n",bad);
        printf("    arr outputs not a multiple of 2^16: %d ; distinct E1 %zu in [%lld,%lld] ; distinct E2 %zu\n",
            badmul,e1h.size(),(long long)e1min,(long long)e1max,e2s.size());
        printf("    E1 histogram:"); for(auto&kv:e1h) printf(" %lld:%d",(long long)kv.first,kv.second); puts("");
    }

    // ================= B. width histogram / pinned values (claim item 1) =================
    {   int w[11]={0}, w11_20=0,w21_50=0,w51_100=0,w101_200=0,wbig=0, pinned=0;
        for(int i=0;i<1024;i++){ if(!OKT[i])continue; i64 ww=JH[i]-JL[i];
            if(ww<=10) w[ww]++; else if(ww<=20)w11_20++; else if(ww<=50)w21_50++; else if(ww<=100)w51_100++;
            else if(ww<=200)w101_200++; else wbig++; if(ww==0)pinned++; }
        printf("\n[B] width histogram: "); for(int i=0;i<=10;i++)printf("w=%d:%d ",i,w[i]);
        printf("[11,20]:%d [21,50]:%d [51,100]:%d [101,200]:%d >200:%d  pinned(w=0)=%d\n",
            w11_20,w21_50,w51_100,w101_200,wbig,pinned);
        int chk[]={-376,-342,-40,40,258,342,376};
        for(int t : chk){ int i=t+512; i64 ideal=(KC*(i64)t*(i64)t)>>17;
            printf("    t=%+4d  J in [%lld,%lld]  floor(KC t^2/2^17)=%lld  d=[%+lld,%+lld]  dev(lo)=%lld\n",
                t,(long long)JL[i],(long long)JH[i],(long long)ideal,
                (long long)(JL[i]-ideal),(long long)(JH[i]-ideal),
                (long long)(JL[i]*(1ll<<17)-KC*(i64)t*(i64)t)); }
    }

    // ================= C. the |t|=37 vs |t|=59 certificate (claim item 8) =================
    {   for(int a : {37,59}) for(int sg : {1,-1}){ int t=sg*a,i=t+512;
            printf("[C] t=%+4d  J in [%lld,%lld]   KC t^2 / 2^17 exact = %lld + %lld/2^17\n",t,
                (long long)JL[i],(long long)JH[i],(long long)((KC*(i64)t*(i64)t)>>17),
                (long long)(KC*(i64)t*(i64)t - (((KC*(i64)t*(i64)t)>>17)<<17))); }
    }

    // ================= D. SYMCAP: best possible score for any EVEN rule (claim 977) =================
    {   int cap=0, disj=0, dir_pos=0, dir_neg=0;
        for(int a=0;a<=511;a++){ int ip=a+512, im=-a+512;
            if(a==0){ cap += OKT[ip]?1:0; continue; }
            bool p=OKT[ip], m=OKT[im];
            if(p&&m){ i64 lo=std::max(JL[ip],JL[im]), hi=std::min(JH[ip],JH[im]);
                if(lo<=hi) cap+=2; else { cap+=1; disj++; if(JL[im]>JH[ip]) dir_pos++; else dir_neg++; } }
            else cap += (p||m)?1:0; }
        printf("\n[D] SYMCAP (max score of any rule with J(t)=J(-t)) = %d / 1022\n",cap);
        printf("    |t| with DISJOINT +t/-t windows: %d   of which J(-t)>J(t): %d ; J(-t)<J(t): %d\n",disj,dir_pos,dir_neg);
    }

    // ================= E. reachability by ANY first latch at s2=20 (claim item 4) =================
    {   for(int K2i=0;K2i<2;K2i++){ i64 K2 = K2i? K : 0;
        for(int role=0;role<2;role++){
            int reach=0; std::vector<int> un;
            for(int i=0;i<1024;i++){ if(!OKT[i])continue; int t=i-512; i64 dx=(i64)t<<13;
                bool ok=false;
                if(t==0){ i64 J=(arr(0,0)+K2)>>20; ok = J>=JL[i]&&J<=JH[i]; }
                else { i64 c = (JL[i]*(1ll<<20))/ (dx? dx:1); i64 lo=c-3000, hi=c+3000; if(t<0){ lo=-c-3000; hi=-c+3000; }
                    // brute bracket around both possible signs; widen generously
                    i64 est = ((JL[i]+JH[i])/2) * (1ll<<20) / (dx>0?dx:-dx); if(t<0) est=-est;
                    lo=est-4000; hi=est+4000;
                    for(i64 I1=lo;I1<=hi&&!ok;I1++){ i64 P = role? arr(dx,I1) : arr(I1,dx); i64 J=(P+K2)>>20;
                        if(J>=JL[i]&&J<=JH[i]) ok=true; } }
                if(ok)reach++; else un.push_back(t); }
            printf("[E] s2=20 K2=%lld role=%s : reachable by SOME integer first latch = %d/1022 ; unreachable t =",
                (long long)K2, role?"arr(dx,I1)":"arr(I1,dx)", reach);
            for(int t:un)printf(" %d",t); puts(""); } }
    }

    // ================= F. attribution controls (claim item 2) =================
    {   // free per-t offset on the SQUARE latch, |delta| <= 60
        int tot=0;
        #pragma omp parallel for reduction(+:tot) schedule(dynamic)
        for(int i=0;i<1024;i++){ int t=i-512; i64 dx=(i64)t<<13;
            i64 I1=(arr(KC,dx)+K)>>23, J0=(arr(I1,dx)+K)>>20;
            int best=1<<30;
            for(int d=-60;d<=60;d++){ i64 J=J0+d; if(J<0)continue; int v=0;
                for(const Cons&c:CON[i]){ i64 T2=sm(arr(J,c.S)+K,c.u); if(T2<c.lo||T2>c.hi)v++; }
                if(v<best)best=v; }
            tot+=best; }
        printf("\n[F] free per-t offset on SQUARE latch, |d|<=60 : %d violations\n",tot);
        // free per-t offset on the CUBIC latch A3, |delta| <= 6000
        int tot2=0;
        #pragma omp parallel for reduction(+:tot2) schedule(dynamic)
        for(int i=0;i<1024;i++){ int t=i-512; i64 dx=(i64)t<<13;
            i64 A30=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
            i64 I1=(arr(KC,dx)+K)>>23, J=(arr(I1,dx)+K)>>20;
            int best=1<<30;
            for(int d=-6000;d<=6000;d++){ i64 A3=A30+d; int v=0;
                for(const Cons2&c:CON2[i]){ i64 T3=c.c3? sm(arr(c.c3<<5,A3)+K,26):0;
                    i64 T2=c.S? sm(arr(J,c.S)+K,c.u):0; i64 V=c.base-T2-c.s*T3;
                    if(V<c.wlo||V>c.whi)v++; }
                if(v<best)best=v; }
            tot2+=best; }
        printf("    free per-t offset on CUBIC latch A3, |d|<=6000 : %d violations (node 0 excluded: no square term)\n",tot2);
    }

    // ================= G. how FITTED is the constant 20694294? =================
    {   printf("\n[G] sweep of Kq in  I1=(arr(dx,Kq)+2^19)>>25 ; J=arr(I1,dx)>>20   (claimed best Kq=20694294=4*KC+2)\n");
        int best=-1; i64 bestK=0; int cnt930=0,cnt935=0; std::vector<std::pair<int,i64>> top;
        #pragma omp parallel
        { int lb=-1; i64 lk=0; std::vector<std::pair<int,i64>> lt; int l930=0,l935=0;
          #pragma omp for schedule(dynamic)
          for(i64 Kq=20694292-4000; Kq<=20694292+4000; Kq++){
            int sc=0; for(int i=0;i<1024;i++){ if(!OKT[i])continue; int t=i-512; i64 dx=(i64)t<<13;
                i64 I1=(arr(dx,Kq)+K)>>25; i64 J=arr(I1,dx)>>20; if(J>=JL[i]&&J<=JH[i])sc++; }
            if(sc>=930)l930++; if(sc>=935)l935++;
            if(sc>lb){lb=sc;lk=Kq;} if(sc>=934) lt.push_back({sc,Kq}); }
          #pragma omp critical
          { if(lb>best){best=lb;bestK=lk;} cnt930+=l930; cnt935+=l935; for(auto&p:lt)top.push_back(p); } }
        std::sort(top.begin(),top.end(),[](auto&a,auto&b){return a.first>b.first;});
        printf("    over Kq in [%lld,%lld] (8001 values): best %d at Kq=%lld ; #Kq scoring >=930: %d ; >=935: %d\n",
            (long long)(20694292-4000),(long long)(20694292+4000),best,(long long)bestK,cnt930,cnt935);
        printf("    top ties (score>=934):"); for(size_t i=0;i<top.size()&&i<14;i++)printf(" %d@%lld",top[i].first,(long long)top[i].second); puts("");
    }
    return 0; }
