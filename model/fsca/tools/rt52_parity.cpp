// rt52_parity.cpp -- could FSCA's ROM lookup depend on a "parity" bit the way FSRRA's does ({exponent parity, node} -> 34 words,
// with the two x = 2 half-words sharing C0 / REC1 but carrying INDEPENDENTLY ROUNDED C2F / C3F)?  FSCA's analogue of the parity
// is the quadrant: the model FOLDS node m and node 32-m onto one word (and m+32 onto m with a sign).  If the silicon addresses
// the ROM with more node bits, mathematically equal words may differ in an LSB.
//  A. MODEL-FREE identities on the dump: cos(a) == sin(a + 0x4000);  sin(a + 0x8000) == -sin(a)   (both modes)
//  B. MODEL-FREE: the t = 0 windows (the C0 field) of the mirror nodes m and 32-m -- do they intersect?
//  C. DIAGNOSTIC FIT: all four fields (C0, C1F, c3, S) free by -2..+2 / -3..+3 LSB, jointly, per node m = 1..31 (16 excluded):
//        folded (the model) | one COMMON refit per pair (n, 32-n) | INDEPENDENT refits of n and of 32-n
//     CONTROL for the extra freedom: the pair's 2048 samples split at RANDOM into two halves, refit independently (mean of 40).
//     Same again with the split by the SIGN OF t (two half-node words per node) instead of the quadrant parity.
#include "base2.h"
#include <algorithm>
#include <random>
#include <omp.h>
static Fsca F;
static const i64 C0TAB[17] = {0,6577819,13092290,19480674,25681450,31634899,37283686,42573411,47453131,51875851,55798982,59184735,62000504,64219180,65819382,66785715,67108863};
static inline void win_of(int a, i64& lo, i64& hi){ Win w=decode(F.srz[a&0xffff],F.srn[a&0xffff]); if(w.sign>0){lo=w.lo;hi=w.hi;} else {lo=-w.hi;hi=-w.lo;} }
static const int D0=5,D1=5,D3=5,DS=7, NC=D0*D1*D3*DS;   // combos: C0 -2..2, C1F -2..2, c3 -2..2, S -3..3
static inline int combo(int a,int b,int c,int d){ return ((a*D1+b)*D3+c)*DS+d; }
static const int CZ = ((2*D1+2)*D3+2)*DS+3;
int main(){ if(!load_fsca(F))return 1; build_tables();
    // ---- A
    { long b1[2]={0,0},b2[2]={0,0},z=0; for(int a=0;a<65536;a++){ b1[0]+=F.crn[a]!=F.srn[(a+0x4000)&0xffff]; b1[1]+=F.crz[a]!=F.srz[(a+0x4000)&0xffff];
          uint32_t s0=F.srn[a],s1=F.srn[a^0x8000]; if((s0&0x7fffffff)==0){ z++; continue; } b2[0]+=s1!=(s0^0x80000000u); b2[1]+=F.srz[a^0x8000]!=(F.srz[a]^0x80000000u); }
      printf("A. cos(a) != sin(a+0x4000): RN %ld RZ %ld of 65536      sin(a+0x8000) != -sin(a): RN %ld RZ %ld  (%ld zero results skipped)\n",b1[0],b1[1],b2[0],b2[1],z); }
    // ---- B
    { int empty=0; printf("B. t = 0 windows of mirror nodes (value units):"); for(int n=1;n<=15;n++){ i64 l1,h1,l2,h2; win_of(n*1024,l1,h1); win_of((32-n)*1024,l2,h2); bool e=std::max(l1,l2)>std::min(h1,h2); empty+=e; printf(" %d:[%lld,%lld]/[%lld,%lld]%s",n,(long long)(l1-C0TAB[n]),(long long)(h1-C0TAB[n]),(long long)(l2-C0TAB[n]),(long long)(h2-C0TAB[n]),e?"!":""); } printf("\n   -> %d of 15 pairs have DISJOINT t = 0 windows (offsets shown relative to the model's C0)\n",empty); }
    // ---- C: per-sample violation indicator for every combo
    static std::vector<uint8_t> BAD[32]; 
    #pragma omp parallel for schedule(dynamic,1)
    for(int m=1;m<=31;m++){ if(m==16)continue; int n=m<=16?m:32-m; i64 s=m>16?-1:1; const Tab&T=TB[n]; BAD[m].assign((size_t)1024*NC,0);
        for(int t=-512;t<=511;t++){ i64 dx=(i64)t<<13; i64 lo,hi; win_of(m*1024+t,lo,hi); i64 A3=((arr(((i64)t*t)<<3,dx)+K)>>20)>>3; i64 I1=(arr(KC,dx)+K)>>23; i64 J=(arr(I1,dx)+K)>>20;
            i64 T1[D1],T3[D3],T2[DS]; for(int b=0;b<D1;b++)T1[b]=sm(arr(T.C1f+b-2,dx)+K,23-T.e1); for(int c=0;c<D3;c++)T3[c]=sm(arr((T.c3+c-2)<<5,A3)+K,26); for(int d=0;d<DS;d++)T2[d]=sm(arr(J,T.S+d-3)+K,T.u2);
            uint8_t*o=&BAD[m][(size_t)(t+512)*NC]; for(int a=0;a<D0;a++)for(int b=0;b<D1;b++)for(int c=0;c<D3;c++)for(int d=0;d<DS;d++){ i64 V=C0TAB[n]+a-2+s*T1[b]-T2[d]-s*T3[c]; o[combo(a,b,c,d)]=(V<lo||V>hi); } } }
    auto countsel=[&](int m,const std::vector<uint8_t>&sel,int which,std::vector<int>&cnt){ for(int i=0;i<1024;i++) if(sel[i]==which){ const uint8_t*o=&BAD[m][(size_t)i*NC]; for(int c=0;c<NC;c++)cnt[c]+=o[c]; } };
    auto show=[&](int c,char*b){ int d=c%DS, cc=(c/DS)%D3, bb=(c/DS/D3)%D1, aa=c/DS/D3/D1; sprintf(b,"%+d%+d%+d%+d",aa-2,bb-2,cc-2,d-3); };
    for(int mode=0;mode<2;mode++){ printf("\nC%d. split by %s.   deltas shown as dC0 dC1F dc3 dS\n word | folded | common refit (deltas) | INDEPENDENT refit: total   %s | random-split control (mean of 40)\n",mode+1,mode==0?"QUADRANT PARITY (node n vs node 32-n)":"SIGN OF t (t >= 0 vs t < 0, both mirror nodes)",mode==0?"node n (deltas) / node 32-n (deltas)":"t >= 0 (deltas)  /  t < 0 (deltas)  ");
        long tf=0,tc=0,ti=0; double tr=0; std::mt19937 rng(12345); std::vector<double> reptot(40,0.0);
        for(int n=1;n<=15;n++){ int m2=32-n; std::vector<uint8_t> all(1024,1),none(1024,0); std::vector<int> ca(NC,0),cb(NC,0);
            if(mode==0){ countsel(n,all,1,ca); countsel(m2,all,1,cb); } else { std::vector<uint8_t> pos(1024); for(int i=0;i<1024;i++)pos[i]=(i>=512); countsel(n,pos,1,ca); countsel(m2,pos,1,ca); countsel(n,pos,0,cb); countsel(m2,pos,0,cb); }
            int folded=ca[CZ]+cb[CZ]; int bc=0; for(int c=0;c<NC;c++) if(ca[c]+cb[c]<ca[bc]+cb[bc])bc=c; int ia=std::min_element(ca.begin(),ca.end())-ca.begin(), ib=std::min_element(cb.begin(),cb.end())-cb.begin();
            double rsum=0; for(int rep=0;rep<40;rep++){ std::vector<uint8_t> s1(1024),s2(1024); std::vector<int> idx(2048); for(int i=0;i<2048;i++)idx[i]=i; std::shuffle(idx.begin(),idx.end(),rng); for(int i=0;i<2048;i++){ int id=idx[i]; (id<1024?s1[id]:s2[id-1024])=(i<1024); }
                std::vector<int> ra(NC,0),rb(NC,0); countsel(n,s1,1,ra); countsel(m2,s2,1,ra); countsel(n,s1,0,rb); countsel(m2,s2,0,rb); int rv=*std::min_element(ra.begin(),ra.end())+*std::min_element(rb.begin(),rb.end()); rsum+=rv; reptot[rep]+=rv; }
            char b0[32],b1[32],b2[32]; show(bc,b0); show(ia,b1); show(ib,b2);
            printf("  %2d  |  %3d   |   %3d (%s)     |   %3d   =  %3d (%s) / %3d (%s)      |  %.1f\n",n,folded,ca[bc]+cb[bc],b0,ca[ia]+cb[ib],ca[ia],b1,cb[ib],b2,rsum/40); tf+=folded; tc+=ca[bc]+cb[bc]; ti+=ca[ia]+cb[ib]; tr+=rsum/40; }
        printf(" TOTAL|  %3ld   |   %3ld                  |   %3ld                                                 |  %.1f\n",tf,tc,ti,tr);
        { double mn=1e9,mx=0,sd=0; for(double v:reptot){ mn=std::min(mn,v); mx=std::max(mx,v); sd+=(v-tr)*(v-tr); } printf("   control total over the 40 random splits: mean %.1f, sd %.1f, min %.0f, max %.0f\n",tr,sqrt(sd/40),mn,mx); } }
    return 0; }
