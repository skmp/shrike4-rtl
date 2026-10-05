// wb_merge.cpp -- the patent's FRACTION-FEEDBACK form for the even path, scanned over its rounding
// constant.  E = floor((arr(rec,mul) + Kr) / 2^w) with the pair {26-bit table sine, 2^w - J}.
// Reports for each Kr: infeasible t, +/- disjoint pairs, reference-rule scores, guard-node violations.
#include "wb_even.h"
static Fsca F;
static inline void win_of(int a,i64&lo,i64&hi,bool&ex){Win w=decode(F.srz[a],F.srn[a]);
    if(w.sign>0){lo=w.lo;hi=w.hi;}else{lo=-w.hi;hi=-w.lo;}ex=w.exact;}
static int ROLE=0; static i64 KR=0; static int WW=33;
static inline i64 Eof(i64 J,int n){ const Tab&T=TB[n]; i64 C0=C0FIT[n]; if(!T.S) return C0;
    i64 Fr=(1ll<<WW)-J;
    if(ROLE==0) return (arrN(C0,Fr,14)+KR)>>WW;          // rec = table sine (26 bits, 13 digits), mul = fraction
    else        return (arrN(Fr,C0,18)+KR)>>WW; }        // rec = fraction (needs 18 digits: unphysical on FIPR)
int main(int argc,char**argv){ if(!load_fsca(F))return 1; build_tables();
    std::vector<i64> KRS; for(int j=0;j<=33;j++) KRS.push_back(1ll<<j);
    KRS.push_back(0); KRS.push_back((1ll<<33)-1); KRS.push_back((1ll<<33)-1-(1ll<<19));
    KRS.push_back((1ll<<33)-(1ll<<19)); KRS.push_back((1ll<<33)-1-(1ll<<32));
    std::sort(KRS.begin(),KRS.end()); KRS.erase(std::unique(KRS.begin(),KRS.end()),KRS.end());
    puts("role  Kr            infeas disj symceil  sc_floor sc_chain guard");
    for(ROLE=0;ROLE<2;ROLE++) for(i64 kr:KRS){ KR=kr;
        std::vector<i64> JL(1024),JH(1024); std::vector<char> ok(1024,0); i64 JMAX=12000000;
        int gviol=0;
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 gl=0,gh=JMAX; i64 ggl=0,ggh=JMAX; bool gbad=false;
            for(int node=1;node<=31;node++){ int flip=node>=17,n=flip?32-node:node; if(!TB[n].S)continue;
                const Tab&T=TB[n]; i64 s=flip?-1:1;
                i64 T1=T.C1f? sm(arr(T.C1f,dx)+K,23-T.e1):0;
                i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 T3=T.c3? sm(arr(T.c3<<5,A3)+K,26):0;
                int a=(node*1024+t)&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
                i64 elo=lo-s*T1+s*T3, ehi=hi-s*T1+s*T3;
                i64 a1=-1,b1=JMAX; while(a1<b1){i64 m=(a1+b1+1)/2; if(Eof(m,n)>=elo)a1=m;else b1=m-1;}
                i64 a2=0,b2=JMAX+1; while(a2<b2){i64 m=(a2+b2)/2; if(Eof(m,n)<=ehi)b2=m;else a2=m+1;}
                if(a2>gl)gl=a2; if(a1<gh)gh=a1;
                if(node==1||node==2||node==16){ if(a2>ggl)ggl=a2; if(a1<ggh)ggh=a1; } }
            { int n=0; int a=t&0xffff; i64 lo,hi;bool ex; win_of(a,lo,hi,ex);
              i64 T1=sm(arr(TB[0].C1f,dx)+K,23-TB[0].e1); i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3;
              i64 T3=sm(arr(TB[0].c3<<5,A3)+K,26); i64 V=C0FIT[0]+T1-T3; if(V<lo||V>hi) gbad=true; }
            JL[t+512]=gl;JH[t+512]=gh;ok[t+512]=gl<=gh; if(gbad||ggl>ggh)gviol++; }
        int infeas=0,disj=0; for(int i=0;i<1024;i++) if(!ok[i])infeas++;
        for(int aa=1;aa<=511;aa++){ int ip=aa+512,im=-aa+512; if(!ok[ip]||!ok[im])continue;
            if(JH[ip]<JL[im]||JH[im]<JL[ip])disj++; }
        int s1=0,s2=0; for(int t=-512;t<=511;t++){ if(!ok[t+512])continue;
            i64 Ja=(i64)KC*t*t>>17; if(Ja>=JL[t+512]&&Ja<=JH[t+512])s1++;
            i64 dxx=(i64)t<<13; i64 I1=(arr(KC,dxx)+K)>>23; i64 Jb=(arr(I1,dxx)+K)>>20;
            if(Jb>=JL[t+512]&&Jb<=JH[t+512])s2++; }
        printf("%d  %12lld  %5d %4d %6d   %5d %5d %5d\n",ROLE,(long long)kr,infeas,disj,1022-2*disj,s1,s2,gviol);
    }
    return 0; }
